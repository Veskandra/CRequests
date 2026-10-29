/*
 * internal.h - private data structures and helpers for crequests.
 */
#ifndef CR_INTERNAL_H
#define CR_INTERNAL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "crequests.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CR_DEFAULT_CONNECT_TIMEOUT_MS 10000
#define CR_DEFAULT_READ_TIMEOUT_MS 30000
#define CR_DEFAULT_MAX_REDIRECTS 20
#define CR_DEFAULT_MAX_BODY ((size_t)64 * 1024 * 1024)
#define CR_MAX_HEADER_BYTES (1024 * 1024)
#define CR_MAX_HEADER_COUNT 512
#define CR_MAX_LINE_BYTES (64 * 1024)
#define CR_MAX_COOKIES 256
#define CR_MAX_COOKIE_BYTES 8192

typedef struct cr_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
} cr_buf;

typedef struct cr_kv {
    char *name;
    char *value;
} cr_kv;

typedef struct cr_kv_list {
    cr_kv *items;
    size_t len;
    size_t cap;
} cr_kv_list;

typedef struct cr_url {
    char *scheme;
    char *username;
    char *password;
    char *host;
    unsigned short port;
    char *path;
    char *query;
    int tls;
} cr_url;

typedef struct cr_cookie {
    char *name;
    char *value;
    char *domain;
    char *path;
    int host_only;
    int secure;
    int http_only;
    time_t expires; /* 0 == session cookie */
    unsigned long seq;
} cr_cookie;

typedef struct cr_cookie_list {
    cr_cookie *items;
    size_t len;
    size_t cap;
} cr_cookie_list;

typedef struct cr_file_part {
    char *field;
    char *path;
    char *content_type;
} cr_file_part;

typedef enum cr_body_mode {
    CR_BODY_NONE = 0,
    CR_BODY_RAW,
    CR_BODY_FORM,
    CR_BODY_MULTIPART
} cr_body_mode;

struct cr_session {
    cr_kv_list headers;
    cr_cookie_list cookies;
    int connect_timeout_ms;
    int read_timeout_ms;
    int verify;
    int follow_redirects;
    int max_redirects;
    size_t max_body_size;
    char *user_agent;
    unsigned long cookie_seq;
};

struct cr_request {
    char *method;
    char *url;
    cr_kv_list headers;
    cr_kv_list query;
    cr_kv_list form;
    cr_file_part *files;
    size_t file_count;
    size_t file_cap;
    cr_buf body;
    cr_body_mode body_mode;
    int auth_type; /* 0 none, 1 basic, 2 bearer */
    char *auth_user;
    char *auth_pass;
    char *auth_token;
    int connect_timeout_ms; /* -1 inherit */
    int read_timeout_ms;
    int verify;             /* -1 inherit */
    int follow_redirects;   /* -1 inherit */
    int max_redirects;      /* -1 inherit */
    char *user_agent;
};

struct cr_response {
    int status;
    char *reason;
    int http_minor;
    cr_kv_list headers;
    unsigned char *body;
    size_t body_len;
    char *url;
    int redirects;
    long long elapsed_ms;
    cr_error error;
    char error_message[512];
};

/* ------------------------------------------------------------------------- */
/* Utilities                                                                 */
/* ------------------------------------------------------------------------- */

void *cr_calloc(size_t count, size_t size);
char *cr_strdup(const char *s);
char *cr_strndup(const char *s, size_t n);
int cr_ascii_casecmp(const char *a, const char *b);
int cr_ascii_ncasecmp(const char *a, const char *b, size_t n);
char *cr_ascii_lower(char *s);
void cr_trim(const char **start, size_t *len);
long long cr_now_ms(void);
int cr_parse_ll(const char *s, long long *out);
int cr_is_ascii_token(const char *s);
int cr_header_name_valid(const char *name);
int cr_header_value_valid(const char *value);
char *cr_fopen_read_all_utf8(const char *path, size_t *len, cr_error *error);
char *cr_base64_encode(const unsigned char *data, size_t len);
time_t cr_parse_http_date(const char *text);

void cr_buf_init(cr_buf *buf);
void cr_buf_free(cr_buf *buf);
int cr_buf_reserve(cr_buf *buf, size_t extra);
int cr_buf_append(cr_buf *buf, const void *data, size_t len);
int cr_buf_append_byte(cr_buf *buf, unsigned char byte);
int cr_buf_append_str(cr_buf *buf, const char *s);
int cr_buf_appendf(cr_buf *buf, const char *fmt, ...);
int cr_buf_set(cr_buf *buf, const void *data, size_t len);

void cr_kv_list_init(cr_kv_list *list);
void cr_kv_list_free(cr_kv_list *list);
void cr_kv_list_clear(cr_kv_list *list);
int cr_kv_add(cr_kv_list *list, const char *name, const char *value);
int cr_kv_set(cr_kv_list *list, const char *name, const char *value);
const char *cr_kv_get(const cr_kv_list *list, const char *name);
int cr_kv_has(const cr_kv_list *list, const char *name);
int cr_kv_remove(cr_kv_list *list, const char *name);
int cr_kv_copy(cr_kv_list *dst, const cr_kv_list *src);

/* ------------------------------------------------------------------------- */
/* URL helpers                                                               */
/* ------------------------------------------------------------------------- */

void cr_url_init(cr_url *url);
void cr_url_free(cr_url *url);
cr_error cr_url_parse(const char *text, cr_url *out);
cr_error cr_url_resolve(const cr_url *base, const char *location, cr_url *out);
int cr_url_origin_equal(const cr_url *a, const cr_url *b);
char *cr_url_to_string(const cr_url *url);
char *cr_url_host_header(const cr_url *url);
char *cr_url_request_target(const cr_url *url, const cr_kv_list *extra_query);
char *cr_url_encode_component(const char *text);
char *cr_url_encode_path(const char *text);
char *cr_url_encode_query(const char *text);
char *cr_url_decode_component(const char *text);
char *cr_url_join_query(const char *base_query, const cr_kv_list *extra);

/* ------------------------------------------------------------------------- */
/* Cookie helpers                                                            */
/* ------------------------------------------------------------------------- */

void cr_cookie_list_init(cr_cookie_list *list);
void cr_cookie_list_free(cr_cookie_list *list);
void cr_cookie_list_clear(cr_cookie_list *list);
cr_error cr_cookie_store_headers(cr_session *session,
                                 const cr_url *url,
                                 const cr_kv_list *headers);
char *cr_cookie_header_for(cr_session *session, const cr_url *url);
void cr_cookie_purge_expired(cr_session *session);

#endif /* CR_INTERNAL_H */
