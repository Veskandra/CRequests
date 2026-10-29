#include "internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_hex_digit(int c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c - 'A' + 10;
}

static int is_unreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

static int is_subdelim(unsigned char c)
{
    switch (c) {
    case '!': case '$': case '&': case '\'': case '(': case ')':
    case '*': case '+': case ',': case ';': case '=':
        return 1;
    default:
        return 0;
    }
}

static int is_path_char(unsigned char c)
{
    return is_unreserved(c) || is_subdelim(c) || c == ':' || c == '@' || c == '/';
}

static int is_query_char(unsigned char c)
{
    return is_path_char(c) || c == '?';
}

static char hex_digit(unsigned int value)
{
    static const char digits[] = "0123456789ABCDEF";
    return digits[value & 0x0f];
}

/* kind: 0 = path, 1 = query, 2 = component (only unreserved characters). */
static char *encode_url(const char *text, int kind, int preserve_percent)
{
    cr_buf out;
    const unsigned char *p;

    if (text == NULL) {
        return NULL;
    }
    cr_buf_init(&out);
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        int allowed;
        if (kind == 2) {
            allowed = is_unreserved(*p);
        } else if (kind == 1) {
            allowed = is_query_char(*p);
        } else {
            allowed = is_path_char(*p);
        }
        if (preserve_percent && *p == '%' && is_hex_digit(p[1]) && is_hex_digit(p[2])) {
            if (!cr_buf_append(&out, p, 3)) {
                cr_buf_free(&out);
                return NULL;
            }
            p += 2;
        } else if (allowed) {
            if (!cr_buf_append_byte(&out, *p)) {
                cr_buf_free(&out);
                return NULL;
            }
        } else {
            char esc[3];
            esc[0] = '%';
            esc[1] = hex_digit(*p >> 4);
            esc[2] = hex_digit(*p);
            if (!cr_buf_append(&out, esc, 3)) {
                cr_buf_free(&out);
                return NULL;
            }
        }
    }
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

char *cr_url_encode_component(const char *text)
{
    return encode_url(text, 2, 0);
}

char *cr_url_encode_path(const char *text)
{
    return encode_url(text, 0, 1);
}

char *cr_url_encode_query(const char *text)
{
    return encode_url(text, 1, 1);
}

char *cr_url_decode_component(const char *text)
{
    size_t len;
    char *out;
    size_t i;
    size_t j;

    if (text == NULL) {
        return NULL;
    }
    len = strlen(text);
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    j = 0;
    for (i = 0; i < len; i++) {
        if (text[i] == '%' && i + 2 < len &&
            is_hex_digit((unsigned char)text[i + 1]) &&
            is_hex_digit((unsigned char)text[i + 2])) {
            out[j++] = (char)((hex_value((unsigned char)text[i + 1]) << 4) |
                              hex_value((unsigned char)text[i + 2]));
            i += 2;
        } else {
            out[j++] = text[i];
        }
    }
    out[j] = '\0';
    return out;
}

static char *remove_dot_segments(const char *path)
{
    cr_buf out;
    const char *in;
    size_t in_len;
    size_t pos;

    if (path == NULL) {
        return NULL;
    }
    cr_buf_init(&out);
    in = path;
    in_len = strlen(path);
    pos = 0;

    while (pos < in_len) {
        const char *p = in + pos;
        size_t remaining = in_len - pos;

        if (remaining >= 3 && strncmp(p, "../", 3) == 0) {
            pos += 3;
        } else if (remaining >= 2 && strncmp(p, "./", 2) == 0) {
            pos += 2;
        } else if (remaining >= 3 && strncmp(p, "/./", 3) == 0) {
            pos += 2;
        } else if (remaining == 2 && strncmp(p, "/.", 2) == 0) {
            if (!cr_buf_append_byte(&out, '/')) {
                cr_buf_free(&out);
                return NULL;
            }
            pos += 2;
        } else if (remaining >= 4 && strncmp(p, "/../", 4) == 0) {
            size_t out_len = out.len;
            while (out_len > 0 && out.data[out_len - 1] != '/') {
                out_len--;
            }
            if (out_len > 0) {
                out_len--;
            }
            out.len = out_len;
            pos += 3;
        } else if (remaining == 3 && strncmp(p, "/..", 3) == 0) {
            size_t out_len = out.len;
            while (out_len > 0 && out.data[out_len - 1] != '/') {
                out_len--;
            }
            if (out_len > 0) {
                out_len--;
            }
            out.len = out_len;
            if (!cr_buf_append_byte(&out, '/')) {
                cr_buf_free(&out);
                return NULL;
            }
            pos += 3;
        } else if ((remaining == 1 && p[0] == '.') ||
                   (remaining == 2 && strncmp(p, "..", 2) == 0)) {
            pos = in_len;
        } else {
            size_t n = 1;
            while (pos + n < in_len && in[pos + n] != '/') {
                n++;
            }
            if (!cr_buf_append(&out, p, n)) {
                cr_buf_free(&out);
                return NULL;
            }
            pos += n;
        }
    }

    if (out.len == 0) {
        if (!cr_buf_append_byte(&out, '/')) {
            cr_buf_free(&out);
            return NULL;
        }
    }
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

void cr_url_init(cr_url *url)
{
    if (url != NULL) {
        memset(url, 0, sizeof(*url));
    }
}

void cr_url_free(cr_url *url)
{
    if (url == NULL) {
        return;
    }
    free(url->scheme);
    free(url->username);
    free(url->password);
    free(url->host);
    free(url->path);
    free(url->query);
    memset(url, 0, sizeof(*url));
}

static int has_control_chars(const char *s)
{
    const unsigned char *p;

    for (p = (const unsigned char *)s; *p != '\0'; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            return 1;
        }
    }
    return 0;
}

static int scheme_supported(const char *text, size_t *scheme_len)
{
    const char *sep;

    sep = strstr(text, "://");
    if (sep == NULL || sep == text) {
        return 0;
    }
    if (!((sep - text == 4 && cr_ascii_ncasecmp(text, "http", 4) == 0) ||
          (sep - text == 5 && cr_ascii_ncasecmp(text, "https", 5) == 0))) {
        return 0;
    }
    *scheme_len = (size_t)(sep - text);
    return 1;
}

cr_error cr_url_parse(const char *text, cr_url *out)
{
    char *copy = NULL;
    char *authority = NULL;
    char *authority_end = NULL;
    char *at = NULL;
    char *host_start = NULL;
    char *host_end = NULL;
    char *port_start = NULL;
    char *path_start = NULL;
    char *query_start = NULL;
    char *fragment = NULL;
    char authority_delim = '\0';
    int bracketed_host = 0;
    size_t scheme_len = 0;
    size_t len;
    char *path_encoded = NULL;
    char *query_encoded = NULL;
    char *normalized_path = NULL;

    if (out == NULL) {
        return CR_ERR_ARGUMENT;
    }
    cr_url_init(out);
    if (text == NULL) {
        return CR_ERR_ARGUMENT;
    }

    copy = cr_strdup(text);
    if (copy == NULL) {
        return CR_ERR_NOMEM;
    }
    len = strlen(copy);
    while (len > 0 && (copy[len - 1] == ' ' || copy[len - 1] == '\t')) {
        copy[--len] = '\0';
    }
    {
        char *start = copy;
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        if (start != copy) {
            memmove(copy, start, strlen(start) + 1);
        }
    }
    if (*copy == '\0' || has_control_chars(copy)) {
        free(copy);
        return CR_ERR_URL;
    }
    if (!scheme_supported(copy, &scheme_len)) {
        free(copy);
        return CR_ERR_UNSUPPORTED;
    }

    fragment = strchr(copy, '#');
    if (fragment != NULL) {
        *fragment = '\0';
    }

    authority = copy + scheme_len + 3;
    authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/' && *authority_end != '?') {
        authority_end++;
    }
    authority_delim = *authority_end;
    *authority_end = '\0';
    if (*authority == '\0') {
        free(copy);
        return CR_ERR_URL;
    }

    at = strrchr(authority, '@');
    if (at != NULL) {
        char *colon;
        *at = '\0';
        colon = strchr(authority, ':');
        if (colon != NULL) {
            *colon = '\0';
            out->password = cr_url_decode_component(colon + 1);
            if (out->password == NULL) {
                goto nomem;
            }
        }
        if (*authority != '\0') {
            out->username = cr_url_decode_component(authority);
            if (out->username == NULL) {
                goto nomem;
            }
        }
        authority = at + 1;
    }

    if (*authority == '[') {
        bracketed_host = 1;
        host_start = authority + 1;
        host_end = strchr(host_start, ']');
        if (host_end == NULL || host_end == host_start) {
            goto bad_url;
        }
        host_start = cr_strndup(host_start, (size_t)(host_end - host_start));
        if (host_start == NULL) {
            goto nomem;
        }
        if (host_end[1] != '\0') {
            if (host_end[1] != ':') {
                free(host_start);
                goto bad_url;
            }
            port_start = host_end + 2;
        }
        out->host = host_start;
        host_start = NULL;
    } else {
        char *colon = strchr(authority, ':');
        if (colon != NULL) {
            *colon = '\0';
            port_start = colon + 1;
        }
        if (*authority == '\0') {
            goto bad_url;
        }
        out->host = cr_strdup(authority);
        if (out->host == NULL) {
            goto nomem;
        }
    }
    if (!bracketed_host && strchr(out->host, ':') != NULL) {
        /* IPv6 literals must be bracketed. */
        goto bad_url;
    }
    if (strpbrk(out->host, " /?#@[]") != NULL) {
        goto bad_url;
    }
    cr_ascii_lower(out->host);

    out->scheme = cr_strndup(copy, scheme_len);
    if (out->scheme == NULL) {
        goto nomem;
    }
    cr_ascii_lower(out->scheme);
    out->tls = (strcmp(out->scheme, "https") == 0);
    out->port = (unsigned short)(out->tls ? 443 : 80);

    if (port_start != NULL) {
        long long port;
        if (*port_start == '\0' || !cr_parse_ll(port_start, &port) ||
            port < 1 || port > 65535) {
            goto bad_url;
        }
        out->port = (unsigned short)port;
    }

    *authority_end = authority_delim;
    path_start = authority_end;
    if (*path_start == '?') {
        out->path = cr_strdup("/");
        if (out->path == NULL) {
            goto nomem;
        }
        query_start = path_start + 1;
    } else if (*path_start == '/') {
        query_start = strchr(path_start, '?');
        if (query_start != NULL) {
            *query_start = '\0';
            query_start++;
        }
        path_encoded = cr_url_encode_path(path_start);
        if (path_encoded == NULL) {
            goto nomem;
        }
        normalized_path = remove_dot_segments(path_encoded);
        free(path_encoded);
        path_encoded = NULL;
        if (normalized_path == NULL) {
            goto nomem;
        }
        out->path = normalized_path;
        normalized_path = NULL;
    } else {
        out->path = cr_strdup("/");
        if (out->path == NULL) {
            goto nomem;
        }
    }

    if (query_start != NULL) {
        query_encoded = cr_url_encode_query(query_start);
        if (query_encoded == NULL) {
            goto nomem;
        }
        out->query = query_encoded;
        query_encoded = NULL;
    }

    free(copy);
    return CR_OK;

bad_url:
    free(host_start);
    cr_url_free(out);
    free(copy);
    return CR_ERR_URL;

nomem:
    free(host_start);
    free(path_encoded);
    free(query_encoded);
    free(normalized_path);
    cr_url_free(out);
    free(copy);
    return CR_ERR_NOMEM;
}

static char *url_origin_string(const cr_url *url)
{
    cr_buf out;

    cr_buf_init(&out);
    if (!cr_buf_append_str(&out, url->scheme) || !cr_buf_append_str(&out, "://")) {
        cr_buf_free(&out);
        return NULL;
    }
    if (strchr(url->host, ':') != NULL) {
        if (!cr_buf_append_byte(&out, '[') ||
            !cr_buf_append_str(&out, url->host) ||
            !cr_buf_append_byte(&out, ']')) {
            cr_buf_free(&out);
            return NULL;
        }
    } else if (!cr_buf_append_str(&out, url->host)) {
        cr_buf_free(&out);
        return NULL;
    }
    if (url->port != (unsigned short)(url->tls ? 443 : 80)) {
        if (!cr_buf_appendf(&out, ":%u", (unsigned int)url->port)) {
            cr_buf_free(&out);
            return NULL;
        }
    }
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

static char *merge_relative_path(const char *base_path, const char *location_path)
{
    cr_buf merged;
    const char *slash;

    if (location_path == NULL) {
        location_path = "";
    }
    if (location_path[0] == '/') {
        return remove_dot_segments(location_path);
    }
    if (base_path == NULL || base_path[0] == '\0') {
        base_path = "/";
    }
    slash = strrchr(base_path, '/');
    cr_buf_init(&merged);
    if (slash != NULL) {
        if (!cr_buf_append(&merged, base_path, (size_t)(slash - base_path + 1))) {
            cr_buf_free(&merged);
            return NULL;
        }
    } else if (!cr_buf_append_byte(&merged, '/')) {
        cr_buf_free(&merged);
        return NULL;
    }
    if (!cr_buf_append_str(&merged, location_path) ||
        !cr_buf_append_byte(&merged, '\0')) {
        cr_buf_free(&merged);
        return NULL;
    }
    {
        char *raw = (char *)merged.data;
        char *normalized = remove_dot_segments(raw);
        free(raw);
        return normalized;
    }
}

cr_error cr_url_resolve(const cr_url *base, const char *location, cr_url *out)
{
    char *loc = NULL;
    char *fragment;
    char *origin = NULL;
    char *path_part = NULL;
    char *query_part = NULL;
    char *query_encoded = NULL;
    char *new_path = NULL;
    cr_buf url;
    cr_error err;

    if (base == NULL || location == NULL || out == NULL) {
        return CR_ERR_ARGUMENT;
    }
    loc = cr_strdup(location);
    if (loc == NULL) {
        return CR_ERR_NOMEM;
    }
    while (*loc == ' ' || *loc == '\t') {
        memmove(loc, loc + 1, strlen(loc));
    }
    {
        size_t len = strlen(loc);
        while (len > 0 && (loc[len - 1] == ' ' || loc[len - 1] == '\t')) {
            loc[--len] = '\0';
        }
    }
    if (has_control_chars(loc)) {
        free(loc);
        return CR_ERR_URL;
    }

    fragment = strchr(loc, '#');
    if (fragment != NULL) {
        *fragment = '\0';
    }

    if (*loc == '\0') {
        char *base_text = cr_url_to_string(base);
        if (base_text == NULL) {
            free(loc);
            return CR_ERR_NOMEM;
        }
        err = cr_url_parse(base_text, out);
        free(base_text);
        free(loc);
        return err;
    }
    if ((strncmp(loc, "http://", 7) == 0) || (strncmp(loc, "https://", 8) == 0) ||
        (cr_ascii_ncasecmp(loc, "http://", 7) == 0) ||
        (cr_ascii_ncasecmp(loc, "https://", 8) == 0)) {
        err = cr_url_parse(loc, out);
        free(loc);
        return err;
    }
    if (strstr(loc, "://") != NULL) {
        free(loc);
        return CR_ERR_UNSUPPORTED;
    }
    {
        char *colon = strchr(loc, ':');
        char *separator = strpbrk(loc, "/?#");
        if (colon != NULL && (separator == NULL || colon < separator)) {
            free(loc);
            return CR_ERR_UNSUPPORTED;
        }
    }
    if (loc[0] == '/' && loc[1] == '/') {
        origin = url_origin_string(base);
        if (origin == NULL) {
            free(loc);
            return CR_ERR_NOMEM;
        }
        cr_buf_init(&url);
        if (!cr_buf_append_str(&url, base->scheme) ||
            !cr_buf_append_str(&url, ":") ||
            !cr_buf_append_str(&url, loc) ||
            !cr_buf_append_byte(&url, '\0')) {
            cr_buf_free(&url);
            free(origin);
            free(loc);
            return CR_ERR_NOMEM;
        }
        err = cr_url_parse((char *)url.data, out);
        cr_buf_free(&url);
        free(origin);
        free(loc);
        return err;
    }

    {
        char *q = strchr(loc, '?');
        if (q != NULL) {
            *q = '\0';
            query_part = q + 1;
        }
        path_part = loc;
    }

    cr_buf_init(&url);
    origin = url_origin_string(base);
    if (origin == NULL) {
        cr_buf_free(&url);
        free(loc);
        return CR_ERR_NOMEM;
    }
    if (!cr_buf_append_str(&url, origin)) {
        cr_buf_free(&url);
        free(origin);
        free(loc);
        return CR_ERR_NOMEM;
    }

    if (path_part[0] == '\0') {
        new_path = cr_strdup(base->path);
        if (new_path == NULL) {
            cr_buf_free(&url);
            free(origin);
            free(loc);
            return CR_ERR_NOMEM;
        }
        if (query_part == NULL) {
            query_part = base->query;
        }
    } else {
        new_path = merge_relative_path(base->path, path_part);
        if (new_path == NULL) {
            cr_buf_free(&url);
            free(origin);
            free(loc);
            return CR_ERR_NOMEM;
        }
    }
    if (!cr_buf_append_str(&url, new_path)) {
        cr_buf_free(&url);
        free(origin);
        free(loc);
        free(new_path);
        return CR_ERR_NOMEM;
    }
    if (query_part != NULL) {
        if (!cr_buf_append_byte(&url, '?')) {
            cr_buf_free(&url);
            free(origin);
            free(loc);
            free(new_path);
            return CR_ERR_NOMEM;
        }
        query_encoded = cr_url_encode_query(query_part);
        if (query_encoded == NULL) {
            cr_buf_free(&url);
            free(origin);
            free(loc);
            free(new_path);
            return CR_ERR_NOMEM;
        }
        if (!cr_buf_append_str(&url, query_encoded)) {
            cr_buf_free(&url);
            free(origin);
            free(loc);
            free(new_path);
            free(query_encoded);
            return CR_ERR_NOMEM;
        }
    }
    if (!cr_buf_append_byte(&url, '\0')) {
        cr_buf_free(&url);
        free(origin);
        free(loc);
        free(new_path);
        free(query_encoded);
        return CR_ERR_NOMEM;
    }

    err = cr_url_parse((char *)url.data, out);
    cr_buf_free(&url);
    free(origin);
    free(loc);
    free(new_path);
    free(query_encoded);
    return err;
}

int cr_url_origin_equal(const cr_url *a, const cr_url *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return a->port == b->port &&
           cr_ascii_casecmp(a->scheme, b->scheme) == 0 &&
           cr_ascii_casecmp(a->host, b->host) == 0;
}

char *cr_url_to_string(const cr_url *url)
{
    cr_buf out;

    if (url == NULL) {
        return NULL;
    }
    cr_buf_init(&out);
    if (!cr_buf_append_str(&out, url->scheme) ||
        !cr_buf_append_str(&out, "://")) {
        cr_buf_free(&out);
        return NULL;
    }
    if (strchr(url->host, ':') != NULL) {
        if (!cr_buf_append_byte(&out, '[') ||
            !cr_buf_append_str(&out, url->host) ||
            !cr_buf_append_byte(&out, ']')) {
            cr_buf_free(&out);
            return NULL;
        }
    } else if (!cr_buf_append_str(&out, url->host)) {
        cr_buf_free(&out);
        return NULL;
    }
    if (url->port != (unsigned short)(url->tls ? 443 : 80)) {
        if (!cr_buf_appendf(&out, ":%u", (unsigned int)url->port)) {
            cr_buf_free(&out);
            return NULL;
        }
    }
    if (!cr_buf_append_str(&out, url->path)) {
        cr_buf_free(&out);
        return NULL;
    }
    if (url->query != NULL) {
        if (!cr_buf_append_byte(&out, '?') ||
            !cr_buf_append_str(&out, url->query)) {
            cr_buf_free(&out);
            return NULL;
        }
    }
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

char *cr_url_host_header(const cr_url *url)
{
    cr_buf out;

    if (url == NULL) {
        return NULL;
    }
    cr_buf_init(&out);
    if (strchr(url->host, ':') != NULL) {
        if (!cr_buf_append_byte(&out, '[') ||
            !cr_buf_append_str(&out, url->host) ||
            !cr_buf_append_byte(&out, ']')) {
            cr_buf_free(&out);
            return NULL;
        }
    } else if (!cr_buf_append_str(&out, url->host)) {
        cr_buf_free(&out);
        return NULL;
    }
    if (url->port != (unsigned short)(url->tls ? 443 : 80)) {
        if (!cr_buf_appendf(&out, ":%u", (unsigned int)url->port)) {
            cr_buf_free(&out);
            return NULL;
        }
    }
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

char *cr_url_join_query(const char *base_query, const cr_kv_list *extra)
{
    cr_buf out;
    size_t i;

    cr_buf_init(&out);
    if (base_query != NULL && !cr_buf_append_str(&out, base_query)) {
        cr_buf_free(&out);
        return NULL;
    }
    if (extra != NULL) {
        for (i = 0; i < extra->len; i++) {
            char *name = cr_url_encode_component(extra->items[i].name);
            char *value = NULL;
            if (name == NULL) {
                cr_buf_free(&out);
                return NULL;
            }
            if (extra->items[i].value != NULL) {
                value = cr_url_encode_component(extra->items[i].value);
                if (value == NULL) {
                    free(name);
                    cr_buf_free(&out);
                    return NULL;
                }
            }
            if (out.len > 0 && !cr_buf_append_byte(&out, '&')) {
                free(name);
                free(value);
                cr_buf_free(&out);
                return NULL;
            }
            if (!cr_buf_append_str(&out, name)) {
                free(name);
                free(value);
                cr_buf_free(&out);
                return NULL;
            }
            if (value != NULL) {
                if (!cr_buf_append_byte(&out, '=') ||
                    !cr_buf_append_str(&out, value)) {
                    free(name);
                    free(value);
                    cr_buf_free(&out);
                    return NULL;
                }
            }
            free(name);
            free(value);
        }
    }
    if (base_query == NULL && (extra == NULL || extra->len == 0)) {
        return NULL;
    }
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

char *cr_url_request_target(const cr_url *url, const cr_kv_list *extra_query)
{
    cr_buf out;
    char *query = NULL;

    if (url == NULL) {
        return NULL;
    }
    query = cr_url_join_query(url->query, extra_query);
    if (url->query != NULL && query == NULL) {
        return NULL;
    }
    cr_buf_init(&out);
    if (!cr_buf_append_str(&out, url->path)) {
        cr_buf_free(&out);
        free(query);
        return NULL;
    }
    if (query != NULL) {
        if (!cr_buf_append_byte(&out, '?') ||
            !cr_buf_append_str(&out, query)) {
            cr_buf_free(&out);
            free(query);
            return NULL;
        }
    }
    free(query);
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}
