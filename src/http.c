#include "http.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct cr_reader {
    cr_conn *conn;
    unsigned char buffer[16384];
    size_t pos;
    size_t len;
    int eof;
    cr_error error;
    char message[512];
} cr_reader;

static cr_error reader_fail(cr_reader *reader, cr_error error,
                            const char *fmt, ...)
{
    va_list ap;

    reader->error = error;
    va_start(ap, fmt);
    vsnprintf(reader->message, sizeof(reader->message), fmt, ap);
    va_end(ap);
    reader->message[sizeof(reader->message) - 1] = '\0';
    return error;
}

static int reader_fill(cr_reader *reader)
{
    size_t got = 0;
    int eof = 0;
    char err[512];
    cr_error error;

    if (reader->pos < reader->len) {
        return 1;
    }
    err[0] = '\0';
    error = cr_conn_read(reader->conn, reader->buffer, sizeof(reader->buffer),
                         &got, &eof, err, sizeof(err));
    if (error == CR_OK && got > 0) {
        reader->pos = 0;
        reader->len = got;
        return 1;
    }
    if (error == CR_ERR_CLOSED && eof) {
        reader->eof = 1;
        return 0;
    }
    if (error == CR_OK) {
        reader->eof = 1;
        return 0;
    }
    reader->error = error;
    snprintf(reader->message, sizeof(reader->message), "%s",
             err[0] != '\0' ? err : cr_strerror(error));
    return -1;
}

/* Returns 1 for a line, 0 for clean EOF before any byte, -1 for error. */
static int reader_line(cr_reader *reader, cr_buf *line)
{
    line->len = 0;
    for (;;) {
        unsigned char c;

        if (reader->pos >= reader->len) {
            int filled = reader_fill(reader);
            if (filled == 0) {
                if (line->len == 0) {
                    return 0;
                }
                reader_fail(reader, CR_ERR_PROTOCOL,
                            "unexpected end of stream while reading a line");
                return -1;
            }
            if (filled < 0) {
                return -1;
            }
        }
        c = reader->buffer[reader->pos++];
        if (c == '\n') {
            if (line->len > 0 && line->data[line->len - 1] == '\r') {
                line->len--;
            }
            if (line->len > 0 && memchr(line->data, '\r', line->len) != NULL) {
                reader_fail(reader, CR_ERR_PROTOCOL, "bare CR in header line");
                return -1;
            }
            if (!cr_buf_append_byte(line, '\0')) {
                reader_fail(reader, CR_ERR_NOMEM, "out of memory");
                return -1;
            }
            line->len--;
            return 1;
        }
        if (c == '\0') {
            reader_fail(reader, CR_ERR_PROTOCOL, "NUL byte in HTTP header");
            return -1;
        }
        if (line->len >= CR_MAX_LINE_BYTES) {
            reader_fail(reader, CR_ERR_PROTOCOL, "HTTP header line too long");
            return -1;
        }
        if (!cr_buf_append_byte(line, c)) {
            reader_fail(reader, CR_ERR_NOMEM, "out of memory");
            return -1;
        }
    }
}

static cr_error read_status_line(cr_reader *reader, cr_response *resp,
                                 size_t *header_bytes)
{
    cr_buf line;
    int rc;
    int skipped = 0;
    const char *reason;
    int i;

    cr_buf_init(&line);
    for (;;) {
        rc = reader_line(reader, &line);
        if (rc <= 0) {
            if (rc == 0) {
                reader_fail(reader, CR_ERR_PROTOCOL, "empty HTTP response");
            }
            cr_buf_free(&line);
            return reader->error;
        }
        if (line.len != 0) {
            break;
        }
        if (++skipped > 4) {
            cr_buf_free(&line);
            return reader_fail(reader, CR_ERR_PROTOCOL,
                               "too many blank lines before HTTP status");
        }
    }
    *header_bytes += line.len + 2;
    if (line.len < 12 || strncmp((char *)line.data, "HTTP/1.", 7) != 0) {
        cr_buf_free(&line);
        return reader_fail(reader, CR_ERR_PROTOCOL, "invalid HTTP status line");
    }
    if (line.data[7] != '0' && line.data[7] != '1') {
        cr_buf_free(&line);
        return reader_fail(reader, CR_ERR_PROTOCOL,
                           "unsupported HTTP version");
    }
    if (line.data[8] != ' ') {
        cr_buf_free(&line);
        return reader_fail(reader, CR_ERR_PROTOCOL, "invalid HTTP status line");
    }
    for (i = 9; i < 12; i++) {
        if (!isdigit(line.data[i])) {
            cr_buf_free(&line);
            return reader_fail(reader, CR_ERR_PROTOCOL,
                               "invalid HTTP status code");
        }
    }
    if (line.len > 12 && line.data[12] != ' ') {
        cr_buf_free(&line);
        return reader_fail(reader, CR_ERR_PROTOCOL, "invalid HTTP status line");
    }

    resp->http_minor = line.data[7] - '0';
    resp->status = (line.data[9] - '0') * 100 +
                   (line.data[10] - '0') * 10 +
                   (line.data[11] - '0');
    reason = (line.len > 13) ? (const char *)line.data + 13 : "";
    while (*reason == ' ' || *reason == '\t') {
        reason++;
    }
    resp->reason = cr_strdup(reason);
    if (resp->reason == NULL) {
        cr_buf_free(&line);
        return reader_fail(reader, CR_ERR_NOMEM, "out of memory");
    }
    cr_buf_free(&line);
    return CR_OK;
}

static int add_header(cr_response *resp, const char *name, const char *value,
                      size_t *count, char *message, size_t message_len)
{
    if (*count >= CR_MAX_HEADER_COUNT) {
        snprintf(message, message_len, "too many response headers");
        return 0;
    }
    if (!cr_kv_add(&resp->headers, name, value)) {
        snprintf(message, message_len, "out of memory");
        return 0;
    }
    (*count)++;
    return 1;
}

static int append_folded(cr_response *resp, const char *continuation,
                         size_t add_len)
{
    cr_kv *last;
    char *combined;
    size_t old_len;

    if (resp->headers.len == 0) {
        return 0;
    }
    last = &resp->headers.items[resp->headers.len - 1];
    old_len = last->value != NULL ? strlen(last->value) : 0;
    if (old_len > SIZE_MAX - add_len - 2) {
        return 0;
    }
    combined = (char *)malloc(old_len + add_len + 2);
    if (combined == NULL) {
        return 0;
    }
    if (old_len > 0) {
        memcpy(combined, last->value, old_len);
    }
    combined[old_len] = ' ';
    memcpy(combined + old_len + 1, continuation, add_len + 1);
    free(last->value);
    last->value = combined;
    return 1;
}

static cr_error read_headers(cr_reader *reader, cr_response *resp,
                             size_t *header_bytes, size_t *count)
{
    cr_buf line;
    int rc;

    cr_buf_init(&line);
    for (;;) {
        const char *text;
        size_t text_len;

        rc = reader_line(reader, &line);
        if (rc <= 0) {
            if (rc == 0) {
                reader_fail(reader, CR_ERR_PROTOCOL,
                            "unexpected end of stream in HTTP headers");
            }
            cr_buf_free(&line);
            return reader->error;
        }
        *header_bytes += line.len + 2;
        if (*header_bytes > CR_MAX_HEADER_BYTES) {
            cr_buf_free(&line);
            return reader_fail(reader, CR_ERR_PROTOCOL,
                               "HTTP response headers too large");
        }
        if (line.len == 0) {
            break;
        }

        text = (const char *)line.data;
        text_len = line.len;
        if (text[0] == ' ' || text[0] == '\t') {
            const char *start = text;
            size_t len = text_len;
            cr_trim(&start, &len);
            if (*count == 0 || len == 0 || !append_folded(resp, start, len)) {
                cr_buf_free(&line);
                return reader_fail(reader, CR_ERR_PROTOCOL,
                                   "invalid folded HTTP header");
            }
        } else {
            const char *colon = memchr(text, ':', text_len);
            const char *name_start = text;
            size_t name_len;
            const char *value_start;
            size_t value_len;
            char *name;
            char *value;
            int ok;

            if (colon == NULL) {
                cr_buf_free(&line);
                return reader_fail(reader, CR_ERR_PROTOCOL,
                                   "malformed HTTP header line");
            }
            name_len = (size_t)(colon - text);
            value_start = colon + 1;
            value_len = text_len - name_len - 1;
            cr_trim(&name_start, &name_len);
            cr_trim(&value_start, &value_len);
            name = cr_strndup(name_start, name_len);
            value = cr_strndup(value_start, value_len);
            if (name == NULL || value == NULL) {
                free(name);
                free(value);
                cr_buf_free(&line);
                return reader_fail(reader, CR_ERR_NOMEM, "out of memory");
            }
            if (!cr_header_name_valid(name)) {
                free(name);
                free(value);
                cr_buf_free(&line);
                return reader_fail(reader, CR_ERR_PROTOCOL,
                                   "invalid HTTP header name");
            }
            ok = add_header(resp, name, value, count,
                            reader->message, sizeof(reader->message));
            free(name);
            free(value);
            if (!ok) {
                cr_buf_free(&line);
                reader->error = CR_ERR_PROTOCOL;
                return reader->error;
            }
        }
    }
    cr_buf_free(&line);
    return CR_OK;
}

static int parse_content_length(const char *value, unsigned long long *out)
{
    const char *start = value;
    const char *end;
    size_t len;
    unsigned long long result = 0;
    size_t i;

    while (*start == ' ' || *start == '\t') {
        start++;
    }
    len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned int digit;
        if (!isdigit((unsigned char)start[i])) {
            return 0;
        }
        digit = (unsigned int)(start[i] - '0');
        if (result > (ULLONG_MAX - digit) / 10ULL) {
            return 0;
        }
        result = result * 10ULL + digit;
    }
    end = start + len;
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *out = result;
    return 1;
}

static int parse_chunk_size(const char *line, unsigned long long *out)
{
    const char *semi;
    const char *start = line;
    size_t len;
    size_t i;
    unsigned long long result = 0;

    semi = strchr(line, ';');
    if (semi != NULL) {
        len = (size_t)(semi - line);
    } else {
        len = strlen(line);
    }
    while (len > 0 && (start[0] == ' ' || start[0] == '\t')) {
        start++;
        len--;
    }
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned int digit;
        unsigned char c = (unsigned char)start[i];
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return 0;
        }
        if (result > (ULLONG_MAX - digit) / 16ULL) {
            return 0;
        }
        result = result * 16ULL + digit;
    }
    *out = result;
    return 1;
}

static cr_error read_exact_body(cr_reader *reader, cr_buf *body, size_t length,
                                size_t max_body)
{
    size_t remaining = length;

    if (max_body > 0 && length > max_body) {
        return reader_fail(reader, CR_ERR_BODY_TOO_LARGE,
                           "response body exceeds the configured limit");
    }
    if (length == SIZE_MAX || !cr_buf_reserve(body, length + 1)) {
        return reader_fail(reader, CR_ERR_NOMEM, "out of memory");
    }
    while (remaining > 0) {
        size_t available;
        size_t take;

        if (reader->pos >= reader->len) {
            int filled = reader_fill(reader);
            if (filled <= 0) {
                if (filled == 0) {
                    reader_fail(reader, CR_ERR_PROTOCOL,
                                "connection closed before the response body was complete");
                }
                return reader->error;
            }
        }
        available = reader->len - reader->pos;
        take = (available < remaining) ? available : remaining;
        memcpy(body->data + body->len, reader->buffer + reader->pos, take);
        body->len += take;
        reader->pos += take;
        remaining -= take;
    }
    return CR_OK;
}

static cr_error read_body_until_eof(cr_reader *reader, cr_buf *body,
                                    size_t max_body)
{
    for (;;) {
        size_t available;
        int filled;

        if (reader->pos < reader->len) {
            available = reader->len - reader->pos;
            if (max_body > 0 && (body->len > max_body ||
                                 available > max_body - body->len)) {
                return reader_fail(reader, CR_ERR_BODY_TOO_LARGE,
                                   "response body exceeds the configured limit");
            }
            if (!cr_buf_append(body, reader->buffer + reader->pos, available)) {
                return reader_fail(reader, CR_ERR_NOMEM, "out of memory");
            }
            reader->pos = reader->len;
            continue;
        }
        filled = reader_fill(reader);
        if (filled == 0) {
            break;
        }
        if (filled < 0) {
            return reader->error;
        }
    }
    return CR_OK;
}

static cr_error read_chunked_body(cr_reader *reader, cr_response *resp,
                                  cr_buf *body, size_t max_body,
                                  size_t *header_count, size_t *header_bytes)
{
    cr_buf line;
    cr_error result = CR_OK;

    cr_buf_init(&line);
    for (;;) {
        unsigned long long chunk_size;
        int rc;

        rc = reader_line(reader, &line);
        if (rc <= 0) {
            if (rc == 0) {
                reader_fail(reader, CR_ERR_PROTOCOL,
                            "unexpected end of chunked response");
            }
            result = reader->error;
            break;
        }
        if (!parse_chunk_size((const char *)line.data, &chunk_size)) {
            result = reader_fail(reader, CR_ERR_PROTOCOL,
                                 "invalid chunk size in response");
            break;
        }
        if (chunk_size == 0) {
            /* Trailer section. */
            for (;;) {
                rc = reader_line(reader, &line);
                if (rc <= 0) {
                    if (rc == 0) {
                        reader_fail(reader, CR_ERR_PROTOCOL,
                                    "unexpected end of chunked trailer");
                    }
                    result = reader->error;
                    break;
                }
                *header_bytes += line.len + 2;
                if (*header_bytes > CR_MAX_HEADER_BYTES) {
                    result = reader_fail(reader, CR_ERR_PROTOCOL,
                                         "chunked trailers too large");
                    break;
                }
                if (line.len == 0) {
                    break;
                }
                if (line.data[0] == ' ' || line.data[0] == '\t') {
                    continue;
                }
                {
                    const char *text = (const char *)line.data;
                    const char *colon = memchr(text, ':', line.len);
                    if (colon != NULL) {
                        const char *name = text;
                        size_t name_len = (size_t)(colon - text);
                        const char *value = colon + 1;
                        size_t value_len = line.len - name_len - 1;
                        cr_trim(&name, &name_len);
                        cr_trim(&value, &value_len);
                        if (*header_count < CR_MAX_HEADER_COUNT && name_len > 0) {
                            char *n = cr_strndup(name, name_len);
                            char *v = cr_strndup(value, value_len);
                            if (n != NULL && v != NULL && cr_header_name_valid(n)) {
                                if (add_header(resp, n, v, header_count,
                                               reader->message,
                                               sizeof(reader->message))) {
                                    /* trailer header added */
                                }
                            }
                            free(n);
                            free(v);
                        }
                    }
                }
            }
            break;
        }
        if (chunk_size > (unsigned long long)SIZE_MAX) {
            result = reader_fail(reader, CR_ERR_BODY_TOO_LARGE,
                                 "chunk size is too large");
            break;
        }
        if (max_body > 0 &&
            (body->len > max_body || (size_t)chunk_size > max_body - body->len)) {
            result = reader_fail(reader, CR_ERR_BODY_TOO_LARGE,
                                 "response body exceeds the configured limit");
            break;
        }
        result = read_exact_body(reader, body, (size_t)chunk_size, max_body);
        if (result != CR_OK) {
            break;
        }
        rc = reader_line(reader, &line);
        if (rc <= 0 || line.len != 0) {
            result = reader_fail(reader, CR_ERR_PROTOCOL,
                                 "missing CRLF after chunk data");
            break;
        }
    }
    cr_buf_free(&line);
    return result;
}

static void reset_response_for_continue(cr_response *resp)
{
    free(resp->reason);
    resp->reason = NULL;
    cr_kv_list_free(&resp->headers);
    cr_kv_list_init(&resp->headers);
    resp->status = 0;
    resp->http_minor = 0;
}

cr_error cr_http_exchange(cr_conn *conn,
                          const char *method,
                          const char *target,
                          const cr_kv_list *headers,
                          const void *body,
                          size_t body_len,
                          int head_request,
                          size_t max_body,
                          cr_response *resp,
                          char *err,
                          size_t errlen)
{
    cr_buf request;
    cr_reader reader;
    cr_buf line;
    cr_buf response_body;
    size_t header_bytes = 0;
    size_t header_count = 0;
    cr_error result;
    size_t i;
    int final_response = 0;
    int has_transfer_encoding = 0;
    int has_content_length = 0;
    unsigned long long content_length = 0;
    int chunked = 0;

    if (conn == NULL || method == NULL || target == NULL ||
        headers == NULL || resp == NULL || (body == NULL && body_len > 0)) {
        return CR_ERR_ARGUMENT;
    }
    if (body_len > 0 && !cr_kv_has(headers, "Content-Length")) {
        if (err != NULL && errlen > 0) {
            snprintf(err, errlen, "request body has no Content-Length");
        }
        return CR_ERR_ARGUMENT;
    }
    memset(&reader, 0, sizeof(reader));
    reader.conn = conn;
    reader.error = CR_OK;
    cr_buf_init(&request);
    cr_buf_init(&line);
    cr_buf_init(&response_body);

    if (!cr_buf_append_str(&request, method) ||
        !cr_buf_append_byte(&request, ' ') ||
        !cr_buf_append_str(&request, target) ||
        !cr_buf_append_str(&request, " HTTP/1.1\r\n")) {
        result = CR_ERR_NOMEM;
        goto done;
    }
    for (i = 0; i < headers->len; i++) {
        if (headers->items[i].name == NULL || headers->items[i].value == NULL) {
            continue;
        }
        if (!cr_header_name_valid(headers->items[i].name) ||
            !cr_header_value_valid(headers->items[i].value)) {
            result = CR_ERR_ARGUMENT;
            snprintf(err, errlen, "invalid request header");
            goto done;
        }
        if (!cr_buf_append_str(&request, headers->items[i].name) ||
            !cr_buf_append_str(&request, ": ") ||
            !cr_buf_append_str(&request, headers->items[i].value) ||
            !cr_buf_append_str(&request, "\r\n")) {
            result = CR_ERR_NOMEM;
            goto done;
        }
    }
    if (!cr_buf_append_str(&request, "\r\n") ||
        !cr_buf_append(&request, body, body_len)) {
        result = CR_ERR_NOMEM;
        goto done;
    }

    result = cr_conn_write(conn, request.data, request.len, err, errlen);
    if (result != CR_OK) {
        goto done;
    }

    for (;;) {
        size_t count_before = 0;
        result = read_status_line(&reader, resp, &header_bytes);
        if (result != CR_OK) {
            goto done;
        }
        count_before = header_count;
        result = read_headers(&reader, resp, &header_bytes, &header_count);
        if (result != CR_OK) {
            goto done;
        }
        (void)count_before;
        if (resp->status >= 100 && resp->status < 200 && resp->status != 101) {
            reset_response_for_continue(resp);
            header_count = 0;
            if (header_bytes > CR_MAX_HEADER_BYTES) {
                result = CR_ERR_PROTOCOL;
                snprintf(err, errlen, "HTTP response headers too large");
                goto done;
            }
            continue;
        }
        final_response = 1;
        break;
    }
    if (!final_response) {
        result = CR_ERR_PROTOCOL;
        snprintf(err, errlen, "no final HTTP response");
        goto done;
    }

    for (i = 0; i < resp->headers.len; i++) {
        if (cr_ascii_casecmp(resp->headers.items[i].name, "Transfer-Encoding") == 0) {
            const char *value = resp->headers.items[i].value;
            const char *start = value;
            size_t len = value != NULL ? strlen(value) : 0;
            cr_trim(&start, &len);
            if (len == 0) {
                continue;
            }
            if (len == 8 && cr_ascii_ncasecmp(start, "identity", 8) == 0) {
                continue;
            }
            if (len != 7 || cr_ascii_ncasecmp(start, "chunked", 7) != 0) {
                result = CR_ERR_UNSUPPORTED;
                snprintf(err, errlen, "unsupported Transfer-Encoding: %.*s",
                         (int)(len > 200 ? 200 : len), start);
                goto done;
            }
            has_transfer_encoding = 1;
        } else if (cr_ascii_casecmp(resp->headers.items[i].name, "Content-Length") == 0) {
            unsigned long long parsed;
            if (!parse_content_length(resp->headers.items[i].value, &parsed)) {
                result = CR_ERR_PROTOCOL;
                snprintf(err, errlen, "invalid Content-Length header");
                goto done;
            }
            if (has_content_length && content_length != parsed) {
                result = CR_ERR_PROTOCOL;
                snprintf(err, errlen, "conflicting Content-Length headers");
                goto done;
            }
            has_content_length = 1;
            content_length = parsed;
        }
    }
    chunked = has_transfer_encoding;

    if (head_request || resp->status == 204 || resp->status == 205 ||
        resp->status == 304 || (resp->status >= 100 && resp->status < 200)) {
        /* No response body. */
    } else if (chunked) {
        result = read_chunked_body(&reader, resp, &response_body, max_body,
                                   &header_count, &header_bytes);
        if (result != CR_OK) {
            goto done;
        }
    } else if (has_content_length) {
        if (content_length > (unsigned long long)SIZE_MAX) {
            result = CR_ERR_BODY_TOO_LARGE;
            snprintf(err, errlen, "Content-Length is too large");
            goto done;
        }
        result = read_exact_body(&reader, &response_body,
                                 (size_t)content_length, max_body);
        if (result != CR_OK) {
            goto done;
        }
    } else {
        result = read_body_until_eof(&reader, &response_body, max_body);
        if (result != CR_OK) {
            goto done;
        }
    }

    if (!cr_buf_reserve(&response_body, 1)) {
        result = CR_ERR_NOMEM;
        goto done;
    }
    response_body.data[response_body.len] = '\0';
    resp->body = response_body.data;
    resp->body_len = response_body.len;
    response_body.data = NULL;
    result = CR_OK;

done:
    if (result != CR_OK) {
        char final_message[512];
        if (reader.message[0] != '\0') {
            snprintf(final_message, sizeof(final_message), "%s", reader.message);
        } else if (err != NULL && errlen > 0 && err[0] != '\0') {
            snprintf(final_message, sizeof(final_message), "%s", err);
        } else {
            snprintf(final_message, sizeof(final_message), "%s", cr_strerror(result));
        }
        if (err != NULL && errlen > 0) {
            snprintf(err, errlen, "%s", final_message);
            err[errlen - 1] = '\0';
        }
        resp->error = result;
    }
    cr_buf_free(&request);
    cr_buf_free(&line);
    cr_buf_free(&response_body);
    return result;
}
