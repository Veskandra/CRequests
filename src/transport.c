#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>

#include "transport.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifndef SP_PROT_TLS1_3_CLIENT
#define SP_PROT_TLS1_3_CLIENT 0x00002000
#endif

typedef struct cr_tls {
    CredHandle cred;
    CtxtHandle ctx;
    int have_cred;
    int have_ctx;
    SecPkgContext_StreamSizes sizes;
    unsigned char *in;
    size_t in_len;
    size_t in_cap;
    unsigned char *plain;
    size_t plain_len;
    size_t plain_off;
    size_t plain_cap;
    int eof;
} cr_tls;

struct cr_conn {
    SOCKET sock;
    int is_tls;
    int read_timeout_ms;
    cr_tls *tls;
};

static INIT_ONCE g_ws_once = INIT_ONCE_STATIC_INIT;
static int g_ws_ok = 0;
static DWORD g_ws_error = 0;

static BOOL CALLBACK ws_init_callback(PINIT_ONCE once, PVOID param, PVOID *context)
{
    WSADATA data;
    int rc;

    (void)once;
    (void)param;
    (void)context;
    rc = WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) {
        g_ws_error = (DWORD)rc;
        return FALSE;
    }
    g_ws_ok = 1;
    return TRUE;
}

static int net_init(char *err, size_t errlen)
{
    if (g_ws_ok) {
        return 1;
    }
    if (!InitOnceExecuteOnce(&g_ws_once, ws_init_callback, NULL, NULL)) {
        snprintf(err, errlen, "WSAStartup failed (%lu)", (unsigned long)g_ws_error);
        return 0;
    }
    return g_ws_ok;
}

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || errlen == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
    err[errlen - 1] = '\0';
}

static void trim_crlf(char *s)
{
    size_t len;

    if (s == NULL) {
        return;
    }
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static void format_win_error(DWORD code, char *out, size_t outlen)
{
    char message[256] = {0};
    DWORD n;
    const char *prefix = "unknown error";

    n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, 0, message, (DWORD)sizeof(message), NULL);
    if (n != 0) {
        trim_crlf(message);
        snprintf(out, outlen, "%s (0x%08lX)", message, (unsigned long)code);
    } else {
        snprintf(out, outlen, "%s (0x%08lX)", prefix, (unsigned long)code);
    }
}

static void format_socket_error(int code, char *err, size_t errlen, const char *where)
{
    char message[256];
    format_win_error((DWORD)code, message, sizeof(message));
    set_err(err, errlen, "%s: %s", where, message);
}

static int timeout_to_timeval(int timeout_ms, struct timeval *tv)
{
    if (timeout_ms <= 0) {
        return 0;
    }
    tv->tv_sec = timeout_ms / 1000;
    tv->tv_usec = (timeout_ms % 1000) * 1000;
    return 1;
}

static cr_error wait_socket(SOCKET sock, int for_write, int timeout_ms,
                            char *err, size_t errlen)
{
    fd_set set;
    struct timeval tv;
    struct timeval *tvp;
    int rc;

    FD_ZERO(&set);
    FD_SET(sock, &set);
    tvp = timeout_to_timeval(timeout_ms, &tv) ? &tv : NULL;
    rc = select(0, for_write ? NULL : &set, for_write ? &set : NULL, NULL, tvp);
    if (rc == 0) {
        set_err(err, errlen, "operation timed out after %d ms", timeout_ms);
        return CR_ERR_TIMEOUT;
    }
    if (rc == SOCKET_ERROR) {
        format_socket_error(WSAGetLastError(), err, errlen, "select");
        return CR_ERR_CONNECT;
    }
    return CR_OK;
}

static cr_error raw_send(cr_conn *conn, const void *data, size_t len,
                         int timeout_ms, char *err, size_t errlen)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t sent = 0;

    while (sent < len) {
        size_t remaining = len - sent;
        int chunk = (remaining > (size_t)INT_MAX) ? INT_MAX : (int)remaining;
        int n = send(conn->sock, (const char *)p + sent, chunk, 0);
        if (n == SOCKET_ERROR) {
            int code = WSAGetLastError();
            cr_error wait_error;
            if (code == WSAEWOULDBLOCK) {
                wait_error = wait_socket(conn->sock, 1, timeout_ms, err, errlen);
                if (wait_error != CR_OK) {
                    return wait_error;
                }
                continue;
            }
            format_socket_error(code, err, errlen, "send");
            return CR_ERR_SEND;
        }
        if (n == 0) {
            set_err(err, errlen, "send returned 0");
            return CR_ERR_SEND;
        }
        sent += (size_t)n;
    }
    return CR_OK;
}

static cr_error raw_recv(cr_conn *conn, void *data, size_t cap,
                         size_t *got, int *eof, int timeout_ms,
                         char *err, size_t errlen)
{
    int chunk;
    int n;
    int code;
    cr_error wait_error;

    *got = 0;
    *eof = 0;
    if (cap == 0) {
        return CR_OK;
    }
    chunk = (cap > (size_t)INT_MAX) ? INT_MAX : (int)cap;
    for (;;) {
        n = recv(conn->sock, (char *)data, chunk, 0);
        if (n > 0) {
            *got = (size_t)n;
            return CR_OK;
        }
        if (n == 0) {
            *eof = 1;
            return CR_ERR_CLOSED;
        }
        code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK) {
            wait_error = wait_socket(conn->sock, 0, timeout_ms, err, errlen);
            if (wait_error != CR_OK) {
                return wait_error;
            }
            continue;
        }
        format_socket_error(code, err, errlen, "recv");
        return CR_ERR_RECV;
    }
}

static int set_nonblocking(SOCKET sock)
{
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}

static cr_error tcp_connect(const cr_url *url, int connect_timeout_ms,
                            SOCKET *out_socket, char *err, size_t errlen)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *ai;
    char port[16];
    int rc;
    long long start;
    char last_error[256] = {0};

    if (!net_init(err, errlen)) {
        return CR_ERR_CONNECT;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(port, sizeof(port), "%u", (unsigned int)url->port);
    rc = getaddrinfo(url->host, port, &hints, &results);
    if (rc != 0) {
        set_err(err, errlen, "cannot resolve '%s': getaddrinfo error %d",
                url->host, rc);
        return CR_ERR_DNS;
    }

    start = cr_now_ms();
    *out_socket = INVALID_SOCKET;
    for (ai = results; ai != NULL; ai = ai->ai_next) {
        SOCKET sock;
        int remaining = 0;
        int attempt_timeout = 0;

        if (connect_timeout_ms > 0) {
            long long elapsed = cr_now_ms() - start;
            if (elapsed >= connect_timeout_ms) {
                set_err(err, errlen, "connect timed out after %d ms", connect_timeout_ms);
                freeaddrinfo(results);
                return CR_ERR_TIMEOUT;
            }
            remaining = (int)(connect_timeout_ms - elapsed);
            if (remaining <= 0) {
                remaining = 1;
            }
        }
        /* Do not spend the whole budget on one address (for example a
         * blackholed IPv6 localhost entry).  Leave time to try the next. */
        attempt_timeout = remaining;
        if (ai->ai_next != NULL && attempt_timeout > 1500) {
            attempt_timeout = 1500;
        }

        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET) {
            format_socket_error(WSAGetLastError(), last_error, sizeof(last_error),
                                "socket");
            continue;
        }
        if (!set_nonblocking(sock)) {
            format_socket_error(WSAGetLastError(), last_error, sizeof(last_error),
                                "ioctlsocket");
            closesocket(sock);
            continue;
        }

        rc = connect(sock, ai->ai_addr, (int)ai->ai_addrlen);
        if (rc == SOCKET_ERROR) {
            int code = WSAGetLastError();
            if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
                format_socket_error(code, last_error, sizeof(last_error), "connect");
                closesocket(sock);
                continue;
            }
            {
                cr_error wait_error = wait_socket(sock, 1, attempt_timeout, err, errlen);
                if (wait_error != CR_OK) {
                    closesocket(sock);
                    if (wait_error == CR_ERR_TIMEOUT && ai->ai_next == NULL) {
                        freeaddrinfo(results);
                        return wait_error;
                    }
                    continue;
                }
            }
            {
                int so_error = 0;
                int so_len = (int)sizeof(so_error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
                               (char *)&so_error, &so_len) == SOCKET_ERROR) {
                    format_socket_error(WSAGetLastError(), last_error,
                                        sizeof(last_error), "getsockopt");
                    closesocket(sock);
                    continue;
                }
                if (so_error != 0) {
                    format_socket_error(so_error, last_error, sizeof(last_error),
                                        "connect");
                    closesocket(sock);
                    continue;
                }
            }
        }

        {
            BOOL nodelay = TRUE;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                       (const char *)&nodelay, sizeof(nodelay));
        }
        *out_socket = sock;
        freeaddrinfo(results);
        return CR_OK;
    }

    freeaddrinfo(results);
    if (connect_timeout_ms > 0 && cr_now_ms() - start >= connect_timeout_ms) {
        set_err(err, errlen, "connect timed out after %d ms", connect_timeout_ms);
        return CR_ERR_TIMEOUT;
    }
    set_err(err, errlen, "cannot connect to %s:%u%s%s",
            url->host, (unsigned int)url->port,
            last_error[0] != '\0' ? " - " : "",
            last_error);
    return CR_ERR_CONNECT;
}

/* ------------------------------------------------------------------------- */
/* SChannel TLS                                                              */
/* ------------------------------------------------------------------------- */

static int tls_reserve(unsigned char **buffer, size_t *cap, size_t needed)
{
    unsigned char *next;
    size_t new_cap;

    if (needed <= *cap) {
        return 1;
    }
    new_cap = (*cap == 0) ? 4096 : *cap;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = needed;
            break;
        }
        new_cap *= 2;
    }
    next = (unsigned char *)realloc(*buffer, new_cap);
    if (next == NULL) {
        return 0;
    }
    *buffer = next;
    *cap = new_cap;
    return 1;
}

static int tls_append_input(cr_tls *tls, const void *data, size_t len)
{
    if (!tls_reserve(&tls->in, &tls->in_cap, tls->in_len + len)) {
        return 0;
    }
    memcpy(tls->in + tls->in_len, data, len);
    tls->in_len += len;
    return 1;
}

static int acquire_client_credentials(cr_tls *tls, char *err, size_t errlen)
{
    TimeStamp expiry;
    SECURITY_STATUS status;
    SCHANNEL_CRED schannel;
    int rc;

    memset(&schannel, 0, sizeof(schannel));
    schannel.dwVersion = SCHANNEL_CRED_VERSION;
    schannel.grbitEnabledProtocols = 0;
    schannel.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION;

    status = AcquireCredentialsHandleA(NULL, UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
                                       NULL, &schannel, NULL, NULL,
                                       &tls->cred, &expiry);
    if (status != SEC_E_OK) {
        /* Some Windows versions reject SCHANNEL_CRED in restricted contexts,
         * but accept the system default credentials. */
        status = AcquireCredentialsHandleA(NULL, UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
                                           NULL, NULL, NULL, NULL,
                                           &tls->cred, &expiry);
    }
    if (status != SEC_E_OK) {
        char message[256];
        format_win_error((DWORD)status, message, sizeof(message));
        set_err(err, errlen, "AcquireCredentialsHandle failed: %s", message);
        return 0;
    }
    tls->have_cred = 1;
    rc = 1;
    return rc;
}

static int is_ip_literal(const char *host)
{
    unsigned char buffer[16];
    if (host == NULL) {
        return 0;
    }
    if (InetPtonA(AF_INET, host, buffer) == 1) {
        return 1;
    }
    if (InetPtonA(AF_INET6, host, buffer) == 1) {
        return 1;
    }
    return 0;
}

static int verify_server_certificate(cr_tls *tls, const char *host,
                                     char *err, size_t errlen)
{
    PCCERT_CONTEXT cert = NULL;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    CERT_CHAIN_PARA chain_para;
    HTTPSPolicyCallbackData ssl_policy;
    CERT_CHAIN_POLICY_PARA policy;
    CERT_CHAIN_POLICY_STATUS status;
    SECURITY_STATUS query_status;
    wchar_t *wide_host = NULL;
    int wide_len;
    BOOL ok;
    int result = 0;

    query_status = QueryContextAttributesA(&tls->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                           (PVOID)&cert);
    if (query_status != SEC_E_OK || cert == NULL) {
        set_err(err, errlen, "cannot read server certificate (0x%08lX)",
                (unsigned long)query_status);
        return 0;
    }

    memset(&chain_para, 0, sizeof(chain_para));
    chain_para.cbSize = sizeof(chain_para);
    ok = CertGetCertificateChain(NULL, cert, NULL, cert->hCertStore,
                                 &chain_para, 0, NULL, &chain);
    if (!ok || chain == NULL) {
        char message[256];
        format_win_error(GetLastError(), message, sizeof(message));
        set_err(err, errlen, "cannot build certificate chain: %s", message);
        CertFreeCertificateContext(cert);
        return 0;
    }

    wide_len = MultiByteToWideChar(CP_UTF8, 0, host, -1, NULL, 0);
    if (wide_len <= 0) {
        set_err(err, errlen, "invalid server name for certificate validation");
        goto cleanup;
    }
    wide_host = (wchar_t *)malloc((size_t)wide_len * sizeof(wchar_t));
    if (wide_host == NULL) {
        set_err(err, errlen, "out of memory");
        goto cleanup;
    }
    MultiByteToWideChar(CP_UTF8, 0, host, -1, wide_host, wide_len);

    memset(&ssl_policy, 0, sizeof(ssl_policy));
    ssl_policy.cbSize = sizeof(ssl_policy);
    ssl_policy.dwAuthType = AUTHTYPE_SERVER;
    ssl_policy.fdwChecks = 0;
    ssl_policy.pwszServerName = wide_host;

    memset(&policy, 0, sizeof(policy));
    policy.cbSize = sizeof(policy);
    policy.pvExtraPolicyPara = &ssl_policy;

    memset(&status, 0, sizeof(status));
    status.cbSize = sizeof(status);

    ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain,
                                          &policy, &status);
    if (!ok || status.dwError != 0) {
        char message[256];
        DWORD code = ok ? status.dwError : GetLastError();
        format_win_error(code, message, sizeof(message));
        set_err(err, errlen, "TLS certificate verification failed: %s", message);
        goto cleanup;
    }
    result = 1;

cleanup:
    free(wide_host);
    if (chain != NULL) {
        CertFreeCertificateChain(chain);
    }
    CertFreeCertificateContext(cert);
    return result;
}

static int tls_handshake(cr_conn *conn, cr_tls *tls, const char *host,
                         int verify, int timeout_ms, char *err, size_t errlen)
{
    DWORD flags;
    SECURITY_STATUS status;
    int first = 1;
    int result = 0;

    flags = ISC_REQ_STREAM | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
            ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_MANUAL_CRED_VALIDATION;

    if (!acquire_client_credentials(tls, err, errlen)) {
        return 0;
    }

    for (;;) {
        SecBuffer in_buffers[2];
        SecBuffer out_buffer;
        SecBufferDesc in_desc;
        SecBufferDesc out_desc;
        PCtxtHandle old_context = first ? NULL : &tls->ctx;
        const char *target = is_ip_literal(host) ? NULL : host;
        size_t extra = 0;

        memset(in_buffers, 0, sizeof(in_buffers));
        in_buffers[0].BufferType = SECBUFFER_TOKEN;
        in_buffers[0].cbBuffer = (unsigned long)tls->in_len;
        in_buffers[0].pvBuffer = tls->in_len > 0 ? tls->in : NULL;
        in_buffers[1].BufferType = SECBUFFER_EMPTY;
        in_desc.ulVersion = SECBUFFER_VERSION;
        in_desc.cBuffers = (tls->in_len > 0) ? 2 : 0;
        in_desc.pBuffers = in_buffers;

        memset(&out_buffer, 0, sizeof(out_buffer));
        out_buffer.BufferType = SECBUFFER_TOKEN;
        out_desc.ulVersion = SECBUFFER_VERSION;
        out_desc.cBuffers = 1;
        out_desc.pBuffers = &out_buffer;

        status = InitializeSecurityContextA(
            &tls->cred,
            old_context,
            (SEC_CHAR *)target,
            flags,
            0,
            SECURITY_NATIVE_DREP,
            tls->in_len > 0 ? &in_desc : NULL,
            0,
            &tls->ctx,
            &out_desc,
            &flags,
            NULL);

        if (out_buffer.cbBuffer > 0 && out_buffer.pvBuffer != NULL) {
            cr_error send_error = raw_send(conn, out_buffer.pvBuffer,
                                           out_buffer.cbBuffer, timeout_ms,
                                           err, errlen);
            FreeContextBuffer(out_buffer.pvBuffer);
            out_buffer.pvBuffer = NULL;
            if (send_error != CR_OK) {
                return 0;
            }
        } else if (out_buffer.pvBuffer != NULL) {
            FreeContextBuffer(out_buffer.pvBuffer);
            out_buffer.pvBuffer = NULL;
        }

        if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED ||
            status == SEC_E_INCOMPLETE_MESSAGE) {
            if (status != SEC_E_INCOMPLETE_MESSAGE && tls->in_len > 0) {
                if (in_buffers[1].BufferType == SECBUFFER_EXTRA) {
                    extra = in_buffers[1].cbBuffer;
                }
                if (extra > tls->in_len) {
                    extra = 0;
                }
                if (extra > 0) {
                    memmove(tls->in, tls->in + tls->in_len - extra, extra);
                }
                tls->in_len = extra;
            }
            first = 0;
            tls->have_ctx = 1;

            if (status == SEC_E_OK) {
                result = 1;
                break;
            }
            if (status == SEC_I_CONTINUE_NEEDED && tls->in_len > 0) {
                continue;
            }

            {
                unsigned char block[16384];
                size_t got = 0;
                int eof = 0;
                cr_error read_error = raw_recv(conn, block, sizeof(block),
                                               &got, &eof, timeout_ms,
                                               err, errlen);
                if (read_error != CR_OK) {
                    if (eof) {
                        set_err(err, errlen, "connection closed during TLS handshake");
                    }
                    return 0;
                }
                if (!tls_append_input(tls, block, got)) {
                    set_err(err, errlen, "out of memory");
                    return 0;
                }
            }
            continue;
        }

        if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
            set_err(err, errlen, "server requested a client certificate, which is not supported");
        } else {
            char message[256];
            format_win_error((DWORD)status, message, sizeof(message));
            set_err(err, errlen, "TLS handshake failed: %s", message);
        }
        return 0;
    }

    status = QueryContextAttributesA(&tls->ctx, SECPKG_ATTR_STREAM_SIZES, &tls->sizes);
    if (status != SEC_E_OK) {
        char message[256];
        format_win_error((DWORD)status, message, sizeof(message));
        set_err(err, errlen, "cannot query TLS stream sizes: %s", message);
        return 0;
    }

    if (verify && !verify_server_certificate(tls, host, err, errlen)) {
        return 0;
    }
    return result;
}

static void tls_free(cr_tls *tls)
{
    if (tls == NULL) {
        return;
    }
    if (tls->have_ctx) {
        DeleteSecurityContext(&tls->ctx);
    }
    if (tls->have_cred) {
        FreeCredentialsHandle(&tls->cred);
    }
    free(tls->in);
    free(tls->plain);
    free(tls);
}

static cr_error tls_write(cr_conn *conn, const void *data, size_t len,
                          int timeout_ms, char *err, size_t errlen)
{
    cr_tls *tls = conn->tls;
    const unsigned char *p = (const unsigned char *)data;
    size_t offset = 0;

    if (tls->sizes.cbMaximumMessage == 0) {
        set_err(err, errlen, "invalid TLS stream size");
        return CR_ERR_TLS;
    }
    while (offset < len) {
        size_t chunk = len - offset;
        size_t total;
        unsigned char *message;
        SecBuffer buffers[4];
        SecBufferDesc desc;
        SECURITY_STATUS status;
        int i;

        if (chunk > tls->sizes.cbMaximumMessage) {
            chunk = tls->sizes.cbMaximumMessage;
        }
        total = tls->sizes.cbHeader + chunk + tls->sizes.cbTrailer;
        message = (unsigned char *)malloc(total);
        if (message == NULL) {
            set_err(err, errlen, "out of memory");
            return CR_ERR_NOMEM;
        }
        memcpy(message + tls->sizes.cbHeader, p + offset, chunk);

        memset(buffers, 0, sizeof(buffers));
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].cbBuffer = tls->sizes.cbHeader;
        buffers[0].pvBuffer = message;
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].cbBuffer = (unsigned long)chunk;
        buffers[1].pvBuffer = message + tls->sizes.cbHeader;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].cbBuffer = tls->sizes.cbTrailer;
        buffers[2].pvBuffer = message + tls->sizes.cbHeader + chunk;
        buffers[3].BufferType = SECBUFFER_EMPTY;

        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        status = EncryptMessage(&tls->ctx, 0, &desc, 0);
        if (status != SEC_E_OK) {
            free(message);
            set_err(err, errlen, "TLS encryption failed (0x%08lX)",
                    (unsigned long)status);
            return CR_ERR_TLS;
        }
        for (i = 0; i < 3; i++) {
            if (buffers[i].cbBuffer > 0) {
                cr_error send_error = raw_send(conn, buffers[i].pvBuffer,
                                               buffers[i].cbBuffer, timeout_ms,
                                               err, errlen);
                if (send_error != CR_OK) {
                    free(message);
                    return send_error;
                }
            }
        }
        free(message);
        offset += chunk;
    }
    return CR_OK;
}

static cr_error tls_copy_plain(cr_tls *tls, void *data, size_t cap,
                               size_t *got)
{
    size_t available;
    size_t take;

    available = tls->plain_len - tls->plain_off;
    take = (available < cap) ? available : cap;
    if (take > 0) {
        memcpy(data, tls->plain + tls->plain_off, take);
        tls->plain_off += take;
        if (tls->plain_off == tls->plain_len) {
            tls->plain_off = 0;
            tls->plain_len = 0;
        }
    }
    *got = take;
    return CR_OK;
}

static cr_error tls_read(cr_conn *conn, void *data, size_t cap,
                         size_t *got, int *eof, char *err, size_t errlen)
{
    cr_tls *tls = conn->tls;

    *got = 0;
    *eof = 0;
    if (tls->plain_off < tls->plain_len) {
        return tls_copy_plain(tls, data, cap, got);
    }
    if (tls->eof) {
        *eof = 1;
        return CR_ERR_CLOSED;
    }

    for (;;) {
        SecBuffer buffers[2];
        SecBufferDesc desc;
        SECURITY_STATUS status;
        unsigned long qop = 0;
        size_t extra = 0;
        size_t plain_len;

        if (tls->in_len == 0) {
            unsigned char block[16384];
            size_t n = 0;
            int socket_eof = 0;
            cr_error read_error = raw_recv(conn, block, sizeof(block),
                                           &n, &socket_eof,
                                           conn->read_timeout_ms, err, errlen);
            if (read_error != CR_OK) {
                if (socket_eof) {
                    tls->eof = 1;
                    *eof = 1;
                    if (err != NULL && errlen > 0) {
                        err[0] = '\0';
                    }
                }
                return read_error;
            }
            if (!tls_append_input(tls, block, n)) {
                set_err(err, errlen, "out of memory");
                return CR_ERR_NOMEM;
            }
        }

        memset(buffers, 0, sizeof(buffers));
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].cbBuffer = (unsigned long)tls->in_len;
        buffers[0].pvBuffer = tls->in;
        buffers[1].BufferType = SECBUFFER_EMPTY;

        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 2;
        desc.pBuffers = buffers;

        status = DecryptMessage(&tls->ctx, &desc, 0, &qop);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            unsigned char block[16384];
            size_t n = 0;
            int socket_eof = 0;
            cr_error read_error = raw_recv(conn, block, sizeof(block),
                                           &n, &socket_eof,
                                           conn->read_timeout_ms, err, errlen);
            if (read_error != CR_OK) {
                if (socket_eof) {
                    set_err(err, errlen, "connection closed in the middle of a TLS record");
                    return CR_ERR_RECV;
                }
                return read_error;
            }
            if (!tls_append_input(tls, block, n)) {
                set_err(err, errlen, "out of memory");
                return CR_ERR_NOMEM;
            }
            continue;
        }
        if (status == SEC_I_CONTEXT_EXPIRED) {
            tls->eof = 1;
            *eof = 1;
            if (err != NULL && errlen > 0) {
                err[0] = '\0';
            }
            return CR_ERR_CLOSED;
        }
        if (status == SEC_I_RENEGOTIATE) {
            set_err(err, errlen, "TLS renegotiation is not supported");
            return CR_ERR_TLS;
        }
        if (status != SEC_E_OK) {
            set_err(err, errlen, "TLS decryption failed (0x%08lX)",
                    (unsigned long)status);
            return CR_ERR_TLS;
        }

        plain_len = buffers[0].cbBuffer;
        if (buffers[1].BufferType == SECBUFFER_EXTRA) {
            extra = buffers[1].cbBuffer;
        }
        if (extra > tls->in_len) {
            extra = 0;
        }

        if (plain_len > 0) {
            if (!tls_reserve(&tls->plain, &tls->plain_cap,
                             tls->plain_len + plain_len)) {
                set_err(err, errlen, "out of memory");
                return CR_ERR_NOMEM;
            }
            memcpy(tls->plain + tls->plain_len, buffers[0].pvBuffer, plain_len);
            tls->plain_len += plain_len;
        }
        if (extra > 0) {
            memmove(tls->in, tls->in + tls->in_len - extra, extra);
        }
        tls->in_len = extra;

        if (tls->plain_len > 0) {
            return tls_copy_plain(tls, data, cap, got);
        }
        /* A TLS record with no application data (for example a warning). */
    }
}

/* ------------------------------------------------------------------------- */
/* Public connection API                                                     */
/* ------------------------------------------------------------------------- */

cr_error cr_conn_open(cr_conn **out,
                      const cr_url *url,
                      int verify,
                      int connect_timeout_ms,
                      int read_timeout_ms,
                      char *err,
                      size_t errlen)
{
    cr_conn *conn;
    cr_error result;
    SOCKET sock = INVALID_SOCKET;

    if (out == NULL || url == NULL) {
        return CR_ERR_ARGUMENT;
    }
    *out = NULL;
    if (err != NULL && errlen > 0) {
        err[0] = '\0';
    }
    if (!net_init(err, errlen)) {
        return CR_ERR_CONNECT;
    }

    result = tcp_connect(url, connect_timeout_ms, &sock, err, errlen);
    if (result != CR_OK) {
        return result;
    }

    conn = (cr_conn *)calloc(1, sizeof(*conn));
    if (conn == NULL) {
        closesocket(sock);
        set_err(err, errlen, "out of memory");
        return CR_ERR_NOMEM;
    }
    conn->sock = sock;
    conn->read_timeout_ms = read_timeout_ms;

    if (url->tls) {
        cr_tls *tls = (cr_tls *)calloc(1, sizeof(*tls));
        if (tls == NULL) {
            closesocket(sock);
            free(conn);
            set_err(err, errlen, "out of memory");
            return CR_ERR_NOMEM;
        }
        conn->tls = tls;
        conn->is_tls = 1;
        if (!tls_handshake(conn, tls, url->host, verify,
                           connect_timeout_ms > 0 ? connect_timeout_ms : read_timeout_ms,
                           err, errlen)) {
            tls_free(tls);
            closesocket(sock);
            free(conn);
            return CR_ERR_TLS;
        }
    }

    *out = conn;
    return CR_OK;
}

cr_error cr_conn_write(cr_conn *conn,
                       const void *data,
                       size_t len,
                       char *err,
                       size_t errlen)
{
    if (conn == NULL || (data == NULL && len != 0)) {
        return CR_ERR_ARGUMENT;
    }
    if (conn->tls) {
        return tls_write(conn, data, len, conn->read_timeout_ms, err, errlen);
    }
    return raw_send(conn, data, len, conn->read_timeout_ms, err, errlen);
}

cr_error cr_conn_read(cr_conn *conn,
                      void *data,
                      size_t cap,
                      size_t *got,
                      int *eof,
                      char *err,
                      size_t errlen)
{
    if (conn == NULL || data == NULL || got == NULL || eof == NULL) {
        return CR_ERR_ARGUMENT;
    }
    if (conn->tls) {
        return tls_read(conn, data, cap, got, eof, err, errlen);
    }
    return raw_recv(conn, data, cap, got, eof, conn->read_timeout_ms, err, errlen);
}

void cr_conn_close(cr_conn *conn)
{
    if (conn == NULL) {
        return;
    }
    if (conn->tls) {
        tls_free(conn->tls);
    }
    if (conn->sock != INVALID_SOCKET) {
        closesocket(conn->sock);
    }
    free(conn);
}
