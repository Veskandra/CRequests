#include "internal.h"
#include "http.h"
#include "transport.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define CR_DEFAULT_USER_AGENT "crequests/" CR_VERSION_STRING

/* ------------------------------------------------------------------------- */
/* Misc                                                                      */
/* ------------------------------------------------------------------------- */

const char *cr_version(void)
{
    return CR_VERSION_STRING;
}

const char *cr_strerror(cr_error error)
{
    switch (error) {
    case CR_OK: return "ok";
    case CR_ERR_ARGUMENT: return "invalid argument";
    case CR_ERR_NOMEM: return "out of memory";
    case CR_ERR_URL: return "invalid URL";
    case CR_ERR_UNSUPPORTED: return "unsupported operation";
    case CR_ERR_TLS: return "TLS error";
    case CR_ERR_TLS_VERIFY: return "TLS certificate verification failed";
    case CR_ERR_DNS: return "DNS resolution failed";
    case CR_ERR_CONNECT: return "connection failed";
    case CR_ERR_TIMEOUT: return "operation timed out";
    case CR_ERR_SEND: return "send failed";
    case CR_ERR_RECV: return "receive failed";
    case CR_ERR_PROTOCOL: return "HTTP protocol error";
    case CR_ERR_TOO_MANY_REDIRECTS: return "too many redirects";
    case CR_ERR_BODY_TOO_LARGE: return "response body too large";
    case CR_ERR_FILE: return "file error";
    case CR_ERR_CLOSED: return "connection closed";
    default: return "unknown error";
    }
}

static void set_string(char **target, const char *value)
{
    free(*target);
    *target = NULL;
    if (value != NULL) {
        *target = cr_strdup(value);
    }
}

/* ------------------------------------------------------------------------- */
/* Session                                                                   */
/* ------------------------------------------------------------------------- */

cr_session *cr_session_new(void)
{
    cr_session *session = (cr_session *)calloc(1, sizeof(*session));

    if (session == NULL) {
        return NULL;
    }
    cr_kv_list_init(&session->headers);
    cr_cookie_list_init(&session->cookies);
    session->connect_timeout_ms = CR_DEFAULT_CONNECT_TIMEOUT_MS;
    session->read_timeout_ms = CR_DEFAULT_READ_TIMEOUT_MS;
    session->verify = 1;
    session->follow_redirects = 1;
    session->max_redirects = CR_DEFAULT_MAX_REDIRECTS;
    session->max_body_size = CR_DEFAULT_MAX_BODY;
    session->user_agent = cr_strdup(CR_DEFAULT_USER_AGENT);
    if (session->user_agent == NULL) {
        cr_session_free(session);
        return NULL;
    }
    return session;
}

void cr_session_free(cr_session *session)
{
    if (session == NULL) {
        return;
    }
    cr_kv_list_free(&session->headers);
    cr_cookie_list_free(&session->cookies);
    free(session->user_agent);
    free(session);
}

cr_error cr_session_set_header(cr_session *session,
                               const char *name,
                               const char *value)
{
    if (session == NULL || name == NULL || !cr_header_name_valid(name) ||
        !cr_header_value_valid(value)) {
        return CR_ERR_ARGUMENT;
    }
    return cr_kv_set(&session->headers, name, value) ? CR_OK : CR_ERR_NOMEM;
}

cr_error cr_session_set_timeout(cr_session *session,
                                int connect_timeout_ms,
                                int read_timeout_ms)
{
    if (session == NULL || connect_timeout_ms < 0 || read_timeout_ms < 0) {
        return CR_ERR_ARGUMENT;
    }
    session->connect_timeout_ms = connect_timeout_ms;
    session->read_timeout_ms = read_timeout_ms;
    return CR_OK;
}

cr_error cr_session_set_verify(cr_session *session, int verify)
{
    if (session == NULL || (verify != 0 && verify != 1)) {
        return CR_ERR_ARGUMENT;
    }
    session->verify = verify;
    return CR_OK;
}

cr_error cr_session_set_follow_redirects(cr_session *session, int follow)
{
    if (session == NULL || (follow != 0 && follow != 1)) {
        return CR_ERR_ARGUMENT;
    }
    session->follow_redirects = follow;
    return CR_OK;
}

cr_error cr_session_set_max_redirects(cr_session *session, int max_redirects)
{
    if (session == NULL || max_redirects < 0) {
        return CR_ERR_ARGUMENT;
    }
    session->max_redirects = max_redirects;
    return CR_OK;
}

cr_error cr_session_set_user_agent(cr_session *session, const char *user_agent)
{
    if (session == NULL || !cr_header_value_valid(user_agent)) {
        return CR_ERR_ARGUMENT;
    }
    set_string(&session->user_agent, user_agent);
    if (user_agent != NULL && session->user_agent == NULL) {
        return CR_ERR_NOMEM;
    }
    return CR_OK;
}

cr_error cr_session_set_max_body_size(cr_session *session, size_t max_bytes)
{
    if (session == NULL) {
        return CR_ERR_ARGUMENT;
    }
    session->max_body_size = max_bytes;
    return CR_OK;
}

void cr_session_clear_cookies(cr_session *session)
{
    if (session != NULL) {
        cr_cookie_list_clear(&session->cookies);
    }
}

size_t cr_session_cookie_count(const cr_session *session)
{
    return session != NULL ? session->cookies.len : 0;
}

/* ------------------------------------------------------------------------- */
/* Request                                                                   */
/* ------------------------------------------------------------------------- */

static void request_clear_auth(cr_request *request)
{
    free(request->auth_user);
    free(request->auth_pass);
    free(request->auth_token);
    request->auth_user = NULL;
    request->auth_pass = NULL;
    request->auth_token = NULL;
    request->auth_type = 0;
}

static void request_clear_files(cr_request *request)
{
    size_t i;

    for (i = 0; i < request->file_count; i++) {
        free(request->files[i].field);
        free(request->files[i].path);
        free(request->files[i].content_type);
    }
    free(request->files);
    request->files = NULL;
    request->file_count = 0;
    request->file_cap = 0;
}

void cr_request_free(cr_request *request)
{
    if (request == NULL) {
        return;
    }
    free(request->method);
    free(request->url);
    cr_kv_list_free(&request->headers);
    cr_kv_list_free(&request->query);
    cr_kv_list_free(&request->form);
    request_clear_files(request);
    cr_buf_free(&request->body);
    request_clear_auth(request);
    free(request->user_agent);
    free(request);
}

static int valid_method(const char *method)
{
    return cr_is_ascii_token(method);
}

cr_request *cr_request_new(const char *method, const char *url)
{
    cr_request *request;
    cr_url parsed;
    cr_error error;

    if (method == NULL || url == NULL || !valid_method(method)) {
        return NULL;
    }
    error = cr_url_parse(url, &parsed);
    if (error != CR_OK) {
        return NULL;
    }
    cr_url_free(&parsed);

    request = (cr_request *)calloc(1, sizeof(*request));
    if (request == NULL) {
        return NULL;
    }
    cr_kv_list_init(&request->headers);
    cr_kv_list_init(&request->query);
    cr_kv_list_init(&request->form);
    cr_buf_init(&request->body);
    request->method = cr_strdup(method);
    request->url = cr_strdup(url);
    request->connect_timeout_ms = -1;
    request->read_timeout_ms = -1;
    request->verify = -1;
    request->follow_redirects = -1;
    request->max_redirects = -1;
    if (request->method == NULL || request->url == NULL) {
        cr_request_free(request);
        return NULL;
    }
    {
        char *p;
        for (p = request->method; *p != '\0'; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
    }
    return request;
}

cr_error cr_request_set_method(cr_request *request, const char *method)
{
    char *copy;
    char *p;

    if (request == NULL || method == NULL || !valid_method(method)) {
        return CR_ERR_ARGUMENT;
    }
    copy = cr_strdup(method);
    if (copy == NULL) {
        return CR_ERR_NOMEM;
    }
    for (p = copy; *p != '\0'; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    free(request->method);
    request->method = copy;
    return CR_OK;
}

cr_error cr_request_set_url(cr_request *request, const char *url)
{
    cr_url parsed;
    cr_error error;
    char *copy;

    if (request == NULL || url == NULL) {
        return CR_ERR_ARGUMENT;
    }
    error = cr_url_parse(url, &parsed);
    if (error != CR_OK) {
        return error;
    }
    cr_url_free(&parsed);
    copy = cr_strdup(url);
    if (copy == NULL) {
        return CR_ERR_NOMEM;
    }
    free(request->url);
    request->url = copy;
    return CR_OK;
}

cr_error cr_request_set_header(cr_request *request,
                               const char *name,
                               const char *value)
{
    if (request == NULL || name == NULL || !cr_header_name_valid(name) ||
        !cr_header_value_valid(value)) {
        return CR_ERR_ARGUMENT;
    }
    return cr_kv_set(&request->headers, name, value) ? CR_OK : CR_ERR_NOMEM;
}

cr_error cr_request_add_query(cr_request *request,
                              const char *name,
                              const char *value)
{
    if (request == NULL || name == NULL || *name == '\0') {
        return CR_ERR_ARGUMENT;
    }
    return cr_kv_add(&request->query, name, value) ? CR_OK : CR_ERR_NOMEM;
}

cr_error cr_request_add_form(cr_request *request,
                             const char *name,
                             const char *value)
{
    if (request == NULL || name == NULL || *name == '\0') {
        return CR_ERR_ARGUMENT;
    }
    if (request->body_mode == CR_BODY_RAW) {
        return CR_ERR_ARGUMENT;
    }
    if (!cr_kv_add(&request->form, name, value)) {
        return CR_ERR_NOMEM;
    }
    if (request->body_mode == CR_BODY_NONE) {
        request->body_mode = CR_BODY_FORM;
    }
    return CR_OK;
}

static char *basename_copy(const char *path)
{
    const char *base = path;
    const char *p;

    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (*base == '\0') {
        base = "upload.bin";
    }
    return cr_strdup(base);
}

cr_error cr_request_add_file(cr_request *request,
                             const char *field_name,
                             const char *path,
                             const char *content_type)
{
    char *field;
    char *path_copy;
    char *type_copy = NULL;
    size_t cap;
    cr_file_part *items;

    if (request == NULL || field_name == NULL || *field_name == '\0' ||
        path == NULL || *path == '\0' || !cr_header_value_valid(content_type)) {
        return CR_ERR_ARGUMENT;
    }
    if (request->body_mode == CR_BODY_RAW) {
        return CR_ERR_ARGUMENT;
    }
    field = cr_strdup(field_name);
    path_copy = cr_strdup(path);
    if (content_type != NULL) {
        type_copy = cr_strdup(content_type);
    }
    if (field == NULL || path_copy == NULL ||
        (content_type != NULL && type_copy == NULL)) {
        free(field);
        free(path_copy);
        free(type_copy);
        return CR_ERR_NOMEM;
    }
    if (request->file_count == request->file_cap) {
        cap = (request->file_cap == 0) ? 4 : request->file_cap * 2;
        items = (cr_file_part *)realloc(request->files, cap * sizeof(cr_file_part));
        if (items == NULL) {
            free(field);
            free(path_copy);
            free(type_copy);
            return CR_ERR_NOMEM;
        }
        request->files = items;
        request->file_cap = cap;
    }
    request->files[request->file_count].field = field;
    request->files[request->file_count].path = path_copy;
    request->files[request->file_count].content_type = type_copy;
    request->file_count++;
    request->body_mode = CR_BODY_MULTIPART;
    return CR_OK;
}

cr_error cr_request_set_body(cr_request *request, const void *data, size_t len)
{
    if (request == NULL || (data == NULL && len != 0)) {
        return CR_ERR_ARGUMENT;
    }
    if (data == NULL && len == 0) {
        cr_buf_free(&request->body);
        cr_buf_init(&request->body);
        cr_kv_list_clear(&request->form);
        request_clear_files(request);
        request->body_mode = CR_BODY_NONE;
        return CR_OK;
    }
    if (!cr_buf_set(&request->body, data, len)) {
        return CR_ERR_NOMEM;
    }
    cr_kv_list_clear(&request->form);
    request_clear_files(request);
    request->body_mode = CR_BODY_RAW;
    return CR_OK;
}

cr_error cr_request_set_body_text(cr_request *request,
                                  const char *text,
                                  const char *content_type)
{
    cr_error error;

    if (request == NULL || text == NULL || !cr_header_value_valid(content_type)) {
        return CR_ERR_ARGUMENT;
    }
    error = cr_request_set_body(request, text, strlen(text));
    if (error != CR_OK) {
        return error;
    }
    if (content_type != NULL) {
        return cr_request_set_header(request, "Content-Type", content_type);
    }
    return CR_OK;
}

cr_error cr_request_set_json(cr_request *request, const char *json)
{
    if (json == NULL) {
        return CR_ERR_ARGUMENT;
    }
    return cr_request_set_body_text(request, json, "application/json");
}

cr_error cr_request_set_basic_auth(cr_request *request,
                                   const char *username,
                                   const char *password)
{
    char *user;
    char *pass;

    if (request == NULL || username == NULL) {
        return CR_ERR_ARGUMENT;
    }
    user = cr_strdup(username);
    pass = cr_strdup(password != NULL ? password : "");
    if (user == NULL || pass == NULL) {
        free(user);
        free(pass);
        return CR_ERR_NOMEM;
    }
    request_clear_auth(request);
    request->auth_type = 1;
    request->auth_user = user;
    request->auth_pass = pass;
    return CR_OK;
}

cr_error cr_request_set_bearer_token(cr_request *request, const char *token)
{
    char *copy;

    if (request == NULL || token == NULL || !cr_header_value_valid(token)) {
        return CR_ERR_ARGUMENT;
    }
    copy = cr_strdup(token);
    if (copy == NULL) {
        return CR_ERR_NOMEM;
    }
    request_clear_auth(request);
    request->auth_type = 2;
    request->auth_token = copy;
    return CR_OK;
}

cr_error cr_request_set_timeout(cr_request *request,
                                int connect_timeout_ms,
                                int read_timeout_ms)
{
    if (request == NULL || connect_timeout_ms < -1 || read_timeout_ms < -1) {
        return CR_ERR_ARGUMENT;
    }
    request->connect_timeout_ms = connect_timeout_ms;
    request->read_timeout_ms = read_timeout_ms;
    return CR_OK;
}

cr_error cr_request_set_verify(cr_request *request, int verify)
{
    if (request == NULL || (verify != -1 && verify != 0 && verify != 1)) {
        return CR_ERR_ARGUMENT;
    }
    request->verify = verify;
    return CR_OK;
}

cr_error cr_request_set_follow_redirects(cr_request *request, int follow)
{
    if (request == NULL || (follow != -1 && follow != 0 && follow != 1)) {
        return CR_ERR_ARGUMENT;
    }
    request->follow_redirects = follow;
    return CR_OK;
}

cr_error cr_request_set_max_redirects(cr_request *request, int max_redirects)
{
    if (request == NULL || max_redirects < -1) {
        return CR_ERR_ARGUMENT;
    }
    request->max_redirects = max_redirects;
    return CR_OK;
}

cr_error cr_request_set_user_agent(cr_request *request, const char *user_agent)
{
    if (request == NULL || !cr_header_value_valid(user_agent)) {
        return CR_ERR_ARGUMENT;
    }
    set_string(&request->user_agent, user_agent);
    if (user_agent != NULL && request->user_agent == NULL) {
        return CR_ERR_NOMEM;
    }
    return CR_OK;
}

/* ------------------------------------------------------------------------- */
/* Response                                                                  */
/* ------------------------------------------------------------------------- */

static cr_response *response_new(void)
{
    cr_response *response = (cr_response *)calloc(1, sizeof(*response));
    if (response == NULL) {
        return NULL;
    }
    cr_kv_list_init(&response->headers);
    response->error = CR_OK;
    return response;
}

static cr_response *response_error_new(cr_error error,
                                       const char *url,
                                       const char *message)
{
    cr_response *response = response_new();
    if (response == NULL) {
        return NULL;
    }
    response->error = error;
    if (url != NULL) {
        response->url = cr_strdup(url);
    }
    snprintf(response->error_message, sizeof(response->error_message), "%s",
             message != NULL ? message : cr_strerror(error));
    return response;
}

void cr_response_free(cr_response *response)
{
    if (response == NULL) {
        return;
    }
    free(response->reason);
    cr_kv_list_free(&response->headers);
    free(response->body);
    free(response->url);
    free(response);
}

static void response_set_error(cr_response *response, cr_error error,
                               const char *fmt, ...)
{
    va_list ap;

    if (response == NULL) {
        return;
    }
    response->error = error;
    va_start(ap, fmt);
    vsnprintf(response->error_message, sizeof(response->error_message), fmt, ap);
    va_end(ap);
    response->error_message[sizeof(response->error_message) - 1] = '\0';
}

static void response_clear_payload(cr_response *response)
{
    free(response->reason);
    response->reason = NULL;
    cr_kv_list_free(&response->headers);
    cr_kv_list_init(&response->headers);
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
    response->status = 0;
    response->http_minor = 0;
    response->error = CR_OK;
    response->error_message[0] = '\0';
}

int cr_response_ok(const cr_response *response)
{
    return response != NULL && response->error == CR_OK &&
           response->status >= 200 && response->status < 400;
}

int cr_response_status(const cr_response *response)
{
    return response != NULL ? response->status : 0;
}

const char *cr_response_reason(const cr_response *response)
{
    if (response == NULL || response->reason == NULL) {
        return "";
    }
    return response->reason;
}

int cr_response_http_version(const cr_response *response)
{
    if (response == NULL || response->status == 0) {
        return 0;
    }
    return response->http_minor == 0 ? 10 : 11;
}

const void *cr_response_body(const cr_response *response, size_t *length)
{
    static const unsigned char empty = 0;
    if (length != NULL) {
        *length = (response != NULL) ? response->body_len : 0;
    }
    if (response == NULL || response->body == NULL) {
        return &empty;
    }
    return response->body;
}

const char *cr_response_text(const cr_response *response)
{
    if (response == NULL || response->body == NULL) {
        return "";
    }
    return (const char *)response->body;
}

const char *cr_response_header(const cr_response *response, const char *name)
{
    if (response == NULL) {
        return NULL;
    }
    return cr_kv_get(&response->headers, name);
}

size_t cr_response_header_count(const cr_response *response)
{
    return response != NULL ? response->headers.len : 0;
}

const char *cr_response_header_name(const cr_response *response, size_t index)
{
    if (response == NULL || index >= response->headers.len) {
        return NULL;
    }
    return response->headers.items[index].name;
}

const char *cr_response_header_value(const cr_response *response, size_t index)
{
    if (response == NULL || index >= response->headers.len) {
        return NULL;
    }
    return response->headers.items[index].value;
}

const char *cr_response_url(const cr_response *response)
{
    if (response == NULL || response->url == NULL) {
        return "";
    }
    return response->url;
}

int cr_response_redirect_count(const cr_response *response)
{
    return response != NULL ? response->redirects : 0;
}

long long cr_response_elapsed_ms(const cr_response *response)
{
    return response != NULL ? response->elapsed_ms : 0;
}

cr_error cr_response_error(const cr_response *response)
{
    return response != NULL ? response->error : CR_ERR_ARGUMENT;
}

const char *cr_response_error_message(const cr_response *response)
{
    if (response == NULL || response->error == CR_OK) {
        return "";
    }
    return response->error_message;
}

/* ------------------------------------------------------------------------- */
/* Request payloads                                                          */
/* ------------------------------------------------------------------------- */

typedef struct cr_payload {
    cr_buf body;
    char *content_type;
    int has_body;
} cr_payload;

static void payload_init(cr_payload *payload)
{
    cr_buf_init(&payload->body);
    payload->content_type = NULL;
    payload->has_body = 0;
}

static void payload_free(cr_payload *payload)
{
    cr_buf_free(&payload->body);
    free(payload->content_type);
    payload->content_type = NULL;
    payload->has_body = 0;
}

static char *sanitize_disposition(const char *text)
{
    cr_buf out;
    const unsigned char *p;

    cr_buf_init(&out);
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        const char *replacement = NULL;
        if (*p == '"') {
            replacement = "%22";
        } else if (*p == '\r') {
            replacement = "%0D";
        } else if (*p == '\n') {
            replacement = "%0A";
        }
        if (replacement != NULL) {
            if (!cr_buf_append_str(&out, replacement)) {
                cr_buf_free(&out);
                return NULL;
            }
        } else if (!cr_buf_append_byte(&out, *p)) {
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

static const char *infer_content_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');

    if (dot == NULL) {
        return "application/octet-stream";
    }
    dot++;
    if (cr_ascii_casecmp(dot, "txt") == 0 || cr_ascii_casecmp(dot, "log") == 0) {
        return "text/plain";
    }
    if (cr_ascii_casecmp(dot, "html") == 0 || cr_ascii_casecmp(dot, "htm") == 0) {
        return "text/html";
    }
    if (cr_ascii_casecmp(dot, "css") == 0) {
        return "text/css";
    }
    if (cr_ascii_casecmp(dot, "csv") == 0) {
        return "text/csv";
    }
    if (cr_ascii_casecmp(dot, "json") == 0) {
        return "application/json";
    }
    if (cr_ascii_casecmp(dot, "xml") == 0) {
        return "application/xml";
    }
    if (cr_ascii_casecmp(dot, "pdf") == 0) {
        return "application/pdf";
    }
    if (cr_ascii_casecmp(dot, "zip") == 0) {
        return "application/zip";
    }
    if (cr_ascii_casecmp(dot, "png") == 0) {
        return "image/png";
    }
    if (cr_ascii_casecmp(dot, "jpg") == 0 || cr_ascii_casecmp(dot, "jpeg") == 0) {
        return "image/jpeg";
    }
    if (cr_ascii_casecmp(dot, "gif") == 0) {
        return "image/gif";
    }
    if (cr_ascii_casecmp(dot, "svg") == 0) {
        return "image/svg+xml";
    }
    if (cr_ascii_casecmp(dot, "webp") == 0) {
        return "image/webp";
    }
    return "application/octet-stream";
}

static cr_error build_form_body(const cr_request *request, cr_buf *body)
{
    size_t i;

    for (i = 0; i < request->form.len; i++) {
        char *name = cr_url_encode_component(request->form.items[i].name);
        char *value = NULL;
        if (name == NULL) {
            return CR_ERR_NOMEM;
        }
        if (request->form.items[i].value != NULL) {
            value = cr_url_encode_component(request->form.items[i].value);
            if (value == NULL) {
                free(name);
                return CR_ERR_NOMEM;
            }
        }
        if ((body->len > 0 && !cr_buf_append_byte(body, '&')) ||
            !cr_buf_append_str(body, name) ||
            !cr_buf_append_byte(body, '=') ||
            (value != NULL && !cr_buf_append_str(body, value))) {
            free(name);
            free(value);
            return CR_ERR_NOMEM;
        }
        free(name);
        free(value);
    }
    return CR_OK;
}

static cr_error build_multipart_body(const cr_request *request,
                                     cr_payload *payload,
                                     char *err,
                                     size_t errlen)
{
    static volatile LONG sequence = 0;
    char boundary[96];
    cr_buf *body = &payload->body;
    size_t i;
    LONG seq;

    seq = InterlockedIncrement(&sequence);
    snprintf(boundary, sizeof(boundary), "----crequests-%llX-%08lX",
             (unsigned long long)cr_now_ms(), (unsigned long)seq);

    for (i = 0; i < request->form.len; i++) {
        char *name = sanitize_disposition(request->form.items[i].name);
        if (name == NULL ||
            !cr_buf_appendf(body, "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n",
                            boundary, name) ||
            !cr_buf_append_str(body, request->form.items[i].value != NULL ?
                               request->form.items[i].value : "") ||
            !cr_buf_append_str(body, "\r\n")) {
            free(name);
            return CR_ERR_NOMEM;
        }
        free(name);
    }

    for (i = 0; i < request->file_count; i++) {
        const cr_file_part *file = &request->files[i];
        char *named = sanitize_disposition(file->field);
        char *filename = basename_copy(file->path);
        char *safe_filename = filename != NULL ? sanitize_disposition(filename) : NULL;
        char *data;
        size_t data_len = 0;
        cr_error file_error = CR_OK;
        const char *type;

        free(filename);
        if (named == NULL || safe_filename == NULL) {
            free(named);
            free(safe_filename);
            return CR_ERR_NOMEM;
        }
        data = cr_fopen_read_all_utf8(file->path, &data_len, &file_error);
        if (data == NULL) {
            snprintf(err, errlen, "cannot read upload file '%s'", file->path);
            free(named);
            free(safe_filename);
            return file_error != CR_OK ? file_error : CR_ERR_FILE;
        }
        type = (file->content_type != NULL && file->content_type[0] != '\0') ?
               file->content_type : infer_content_type(safe_filename);
        if (!cr_buf_appendf(body,
                            "--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
                            boundary, named, safe_filename, type) ||
            !cr_buf_append(body, data, data_len) ||
            !cr_buf_append_str(body, "\r\n")) {
            free(data);
            free(named);
            free(safe_filename);
            return CR_ERR_NOMEM;
        }
        free(data);
        free(named);
        free(safe_filename);
    }

    if (!cr_buf_appendf(body, "--%s--\r\n", boundary)) {
        return CR_ERR_NOMEM;
    }
    {
        char content_type[160];
        snprintf(content_type, sizeof(content_type),
                 "multipart/form-data; boundary=%s", boundary);
        payload->content_type = cr_strdup(content_type);
        if (payload->content_type == NULL) {
            return CR_ERR_NOMEM;
        }
    }
    payload->has_body = 1;
    return CR_OK;
}

static cr_error build_payload(const cr_request *request, cr_payload *payload,
                              char *err, size_t errlen)
{
    payload_init(payload);
    switch (request->body_mode) {
    case CR_BODY_NONE:
        return CR_OK;
    case CR_BODY_RAW:
        if (!cr_buf_append(&payload->body, request->body.data, request->body.len)) {
            return CR_ERR_NOMEM;
        }
        payload->has_body = 1;
        return CR_OK;
    case CR_BODY_FORM: {
        cr_error error = build_form_body(request, &payload->body);
        if (error != CR_OK) {
            return error;
        }
        payload->content_type = cr_strdup("application/x-www-form-urlencoded");
        if (payload->content_type == NULL) {
            return CR_ERR_NOMEM;
        }
        payload->has_body = 1;
        return CR_OK;
    }
    case CR_BODY_MULTIPART:
        return build_multipart_body(request, payload, err, errlen);
    default:
        return CR_ERR_ARGUMENT;
    }
}

/* ------------------------------------------------------------------------- */
/* Header preparation                                                        */
/* ------------------------------------------------------------------------- */

typedef struct cr_auth {
    int type;
    char *user;
    char *pass;
    char *token;
} cr_auth;

static void auth_init(cr_auth *auth)
{
    memset(auth, 0, sizeof(*auth));
}

static void auth_free(cr_auth *auth)
{
    free(auth->user);
    free(auth->pass);
    free(auth->token);
    memset(auth, 0, sizeof(*auth));
}

static cr_error auth_prepare(const cr_request *request, const cr_url *url,
                             cr_auth *auth)
{
    if (request->auth_type == 1) {
        auth->type = 1;
        auth->user = cr_strdup(request->auth_user != NULL ? request->auth_user : "");
        auth->pass = cr_strdup(request->auth_pass != NULL ? request->auth_pass : "");
        if (auth->user == NULL || auth->pass == NULL) {
            return CR_ERR_NOMEM;
        }
        return CR_OK;
    }
    if (request->auth_type == 2) {
        auth->type = 2;
        auth->token = cr_strdup(request->auth_token != NULL ? request->auth_token : "");
        return auth->token != NULL ? CR_OK : CR_ERR_NOMEM;
    }
    if (url->username != NULL) {
        auth->type = 1;
        auth->user = cr_strdup(url->username);
        auth->pass = cr_strdup(url->password != NULL ? url->password : "");
        if (auth->user == NULL || auth->pass == NULL) {
            return CR_ERR_NOMEM;
        }
    }
    return CR_OK;
}

static char *basic_authorization(const cr_auth *auth)
{
    size_t user_len;
    size_t pass_len;
    char *plain;
    char *encoded;
    char *header;

    if (auth->user == NULL || auth->pass == NULL) {
        return NULL;
    }
    user_len = strlen(auth->user);
    pass_len = strlen(auth->pass);
    if (user_len > SIZE_MAX - pass_len - 2) {
        return NULL;
    }
    plain = (char *)malloc(user_len + pass_len + 2);
    if (plain == NULL) {
        return NULL;
    }
    memcpy(plain, auth->user, user_len);
    plain[user_len] = ':';
    memcpy(plain + user_len + 1, auth->pass, pass_len + 1);
    encoded = cr_base64_encode((const unsigned char *)plain, user_len + pass_len + 1);
    free(plain);
    if (encoded == NULL) {
        return NULL;
    }
    header = (char *)malloc(strlen(encoded) + 7);
    if (header == NULL) {
        free(encoded);
        return NULL;
    }
    sprintf(header, "Basic %s", encoded);
    free(encoded);
    return header;
}

static cr_error prepare_headers(cr_session *session,
                                const cr_request *request,
                                const cr_url *url,
                                const char *method,
                                const cr_payload *payload,
                                const cr_auth *auth,
                                int include_auth,
                                int preserve_content_headers,
                                cr_kv_list *headers,
                                char *err,
                                size_t errlen)
{
    size_t i;
    char *jar_cookie = NULL;
    const char *explicit_cookie;
    char *host_header = NULL;
    const char *user_agent = NULL;
    int need_auth_header;

    cr_kv_list_init(headers);
    for (i = 0; i < session->headers.len; i++) {
        if (!cr_kv_add(headers, session->headers.items[i].name,
                       session->headers.items[i].value)) {
            goto nomem;
        }
    }
    for (i = 0; i < request->headers.len; i++) {
        if (!cr_kv_set(headers, request->headers.items[i].name,
                       request->headers.items[i].value)) {
            goto nomem;
        }
    }

    host_header = cr_url_host_header(url);
    if (host_header == NULL) {
        goto nomem;
    }
    if (cr_kv_get(&request->headers, "Host") == NULL) {
        if (!cr_kv_set(headers, "Host", host_header)) {
            goto nomem;
        }
    }

    if (!cr_kv_has(headers, "User-Agent")) {
        if (request->user_agent != NULL) {
            user_agent = request->user_agent;
        } else if (session->user_agent != NULL) {
            user_agent = session->user_agent;
        } else {
            user_agent = CR_DEFAULT_USER_AGENT;
        }
        if (!cr_kv_set(headers, "User-Agent", user_agent)) {
            goto nomem;
        }
    }
    if (!cr_kv_has(headers, "Accept") &&
        !cr_kv_set(headers, "Accept", "*/*")) {
        goto nomem;
    }
    if (!cr_kv_has(headers, "Accept-Encoding") &&
        !cr_kv_set(headers, "Accept-Encoding", "identity")) {
        goto nomem;
    }
    if (!cr_kv_has(headers, "Connection") &&
        !cr_kv_set(headers, "Connection", "close")) {
        goto nomem;
    }

    jar_cookie = cr_cookie_header_for(session, url);
    explicit_cookie = cr_kv_get(headers, "Cookie");
    if (jar_cookie != NULL) {
        char *combined;
        if (explicit_cookie != NULL && explicit_cookie[0] != '\0') {
            combined = (char *)malloc(strlen(explicit_cookie) + strlen(jar_cookie) + 3);
            if (combined == NULL) {
                goto nomem;
            }
            sprintf(combined, "%s; %s", explicit_cookie, jar_cookie);
        } else {
            combined = cr_strdup(jar_cookie);
            if (combined == NULL) {
                goto nomem;
            }
        }
        if (!cr_kv_set(headers, "Cookie", combined)) {
            free(combined);
            goto nomem;
        }
        free(combined);
    }

    if (!include_auth) {
        if (!cr_kv_remove(headers, "Authorization")) {
            goto nomem;
        }
    } else {
        need_auth_header = !cr_kv_has(headers, "Authorization");
        if (need_auth_header && auth->type == 1) {
            char *authorization = basic_authorization(auth);
            if (authorization == NULL) {
                goto nomem;
            }
            if (!cr_kv_set(headers, "Authorization", authorization)) {
                free(authorization);
                goto nomem;
            }
            free(authorization);
        } else if (need_auth_header && auth->type == 2) {
            char *authorization;
            size_t token_len = strlen(auth->token != NULL ? auth->token : "");
            authorization = (char *)malloc(token_len + 8);
            if (authorization == NULL) {
                goto nomem;
            }
            sprintf(authorization, "Bearer %s", auth->token != NULL ? auth->token : "");
            if (!cr_kv_set(headers, "Authorization", authorization)) {
                free(authorization);
                goto nomem;
            }
            free(authorization);
        }
    }

    if (!cr_kv_remove(headers, "Content-Length") ||
        !cr_kv_remove(headers, "Transfer-Encoding")) {
        goto nomem;
    }
    if (payload->content_type != NULL) {
        int multipart = strncmp(payload->content_type, "multipart/", 10) == 0;
        if (multipart || !cr_kv_has(headers, "Content-Type")) {
            if (!cr_kv_set(headers, "Content-Type", payload->content_type)) {
                goto nomem;
            }
        }
    }
    if (!preserve_content_headers) {
        if (!cr_kv_remove(headers, "Content-Type")) {
            goto nomem;
        }
    }

    if (preserve_content_headers &&
        (payload->has_body || strcmp(method, "POST") == 0 ||
         strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0)) {
        char length[32];
        snprintf(length, sizeof(length), "%llu",
                 (unsigned long long)payload->body.len);
        if (!cr_kv_set(headers, "Content-Length", length)) {
            goto nomem;
        }
    }

    free(jar_cookie);
    free(host_header);
    return CR_OK;

nomem:
    free(jar_cookie);
    free(host_header);
    cr_kv_list_free(headers);
    if (err != NULL && errlen > 0) {
        snprintf(err, errlen, "out of memory");
    }
    return CR_ERR_NOMEM;
}

/* ------------------------------------------------------------------------- */
/* Sending                                                                   */
/* ------------------------------------------------------------------------- */

static void response_clear_for_redirect(cr_response *response)
{
    response_clear_payload(response);
}

static cr_error send_with_session(cr_session *session,
                                  cr_request *request,
                                  cr_response *response,
                                  char *err,
                                  size_t errlen)
{
    cr_url url;
    cr_payload payload;
    cr_auth auth;
    char *method = NULL;
    int connect_timeout;
    int read_timeout;
    int verify;
    int follow_redirects;
    int max_redirects;
    int redirects = 0;
    int is_initial = 1;
    int include_auth = 1;
    int preserve_content_headers = 1;
    cr_error result = CR_OK;
    char *url_text = NULL;

    cr_url_init(&url);
    payload_init(&payload);
    auth_init(&auth);
    result = cr_url_parse(request->url, &url);
    if (result != CR_OK) {
        response_set_error(response, result, "invalid URL: %s", request->url);
        if (request->url != NULL) {
            response->url = cr_strdup(request->url);
        }
        return result;
    }
    url_text = cr_url_to_string(&url);
    if (url_text == NULL) {
        result = CR_ERR_NOMEM;
        response_set_error(response, result, "out of memory");
        goto cleanup;
    }
    response->url = url_text;
    url_text = NULL;

    result = build_payload(request, &payload, err, errlen);
    if (result != CR_OK) {
        response_set_error(response, result, "%s",
                           err[0] != '\0' ? err : cr_strerror(result));
        goto cleanup;
    }
    result = auth_prepare(request, &url, &auth);
    if (result != CR_OK) {
        response_set_error(response, CR_ERR_NOMEM, "out of memory");
        goto cleanup;
    }
    method = cr_strdup(request->method);
    if (method == NULL) {
        result = CR_ERR_NOMEM;
        response_set_error(response, result, "out of memory");
        goto cleanup;
    }

    connect_timeout = request->connect_timeout_ms >= 0 ?
                      request->connect_timeout_ms : session->connect_timeout_ms;
    read_timeout = request->read_timeout_ms >= 0 ?
                   request->read_timeout_ms : session->read_timeout_ms;
    verify = request->verify >= 0 ? request->verify : session->verify;
    follow_redirects = request->follow_redirects >= 0 ?
                       request->follow_redirects : session->follow_redirects;
    max_redirects = request->max_redirects >= 0 ?
                    request->max_redirects : session->max_redirects;

    for (;;) {
        cr_conn *conn = NULL;
        cr_kv_list headers;
        char *target = NULL;
        cr_error open_error;
        cr_error exchange_error;

        cr_kv_list_init(&headers);
        target = cr_url_request_target(&url, is_initial ? &request->query : NULL);
        if (target == NULL) {
            result = CR_ERR_NOMEM;
            response_set_error(response, result, "cannot build request target");
            break;
        }
        result = prepare_headers(session, request, &url, method, &payload,
                                 &auth, include_auth, preserve_content_headers,
                                 &headers, err, errlen);
        if (result != CR_OK) {
            response_set_error(response, result, "%s",
                               err[0] != '\0' ? err : cr_strerror(result));
            free(target);
            break;
        }

        err[0] = '\0';
        open_error = cr_conn_open(&conn, &url, verify, connect_timeout,
                                  read_timeout, err, errlen);
        if (open_error != CR_OK) {
            result = open_error;
            response_set_error(response, result, "%s",
                               err[0] != '\0' ? err : cr_strerror(result));
            cr_kv_list_free(&headers);
            free(target);
            break;
        }

        exchange_error = cr_http_exchange(conn, method, target, &headers,
                                         payload.body.data, payload.body.len,
                                         strcmp(method, "HEAD") == 0,
                                         session->max_body_size,
                                         response, err, errlen);
        cr_conn_close(conn);
        conn = NULL;
        cr_kv_list_free(&headers);
        free(target);

        if (exchange_error != CR_OK) {
            result = exchange_error;
            response_set_error(response, result, "%s",
                               err[0] != '\0' ? err : cr_strerror(result));
            break;
        }

        cr_cookie_store_headers(session, &url, &response->headers);

        if (!follow_redirects ||
            !(response->status == 301 || response->status == 302 ||
              response->status == 303 || response->status == 307 ||
              response->status == 308)) {
            result = CR_OK;
            break;
        }

        {
            const char *location = cr_response_header(response, "Location");
            if (location == NULL || *location == '\0') {
                result = CR_OK;
                break;
            }
            if (redirects >= max_redirects) {
                result = CR_ERR_TOO_MANY_REDIRECTS;
                response_set_error(response, result,
                                   "exceeded maximum number of redirects (%d)",
                                   max_redirects);
                break;
            }
            {
                cr_url next_url;
                cr_error resolve_error;
                int switch_to_get = 0;

                cr_url_init(&next_url);
                resolve_error = cr_url_resolve(&url, location, &next_url);
                if (resolve_error != CR_OK) {
                    result = resolve_error;
                    response_set_error(response, result,
                                       "invalid redirect URL '%s'", location);
                    break;
                }

                if (response->status == 303 && strcmp(method, "HEAD") != 0) {
                    switch_to_get = 1;
                } else if ((response->status == 301 || response->status == 302) &&
                           strcmp(method, "POST") == 0) {
                    switch_to_get = 1;
                }

                if (switch_to_get) {
                    char *get = cr_strdup("GET");
                    if (get == NULL) {
                        cr_url_free(&next_url);
                        result = CR_ERR_NOMEM;
                        response_set_error(response, result, "out of memory");
                        break;
                    }
                    free(method);
                    method = get;
                    payload_free(&payload);
                    payload_init(&payload);
                    preserve_content_headers = 0;
                }

                if (!cr_url_origin_equal(&url, &next_url)) {
                    include_auth = 0;
                }
                cr_url_free(&url);
                url = next_url;
                redirects++;
                is_initial = 0;

                {
                    char *next_text = cr_url_to_string(&url);
                    if (next_text == NULL) {
                        result = CR_ERR_NOMEM;
                        response_set_error(response, result, "out of memory");
                        break;
                    }
                    free(response->url);
                    response->url = next_text;
                }
                response_clear_for_redirect(response);
                response->redirects = redirects;
            }
        }
    }

cleanup:
    if (result == CR_OK && response->error != CR_OK) {
        result = response->error;
    }
    response->redirects = redirects;
    if (response->url == NULL) {
        response->url = cr_strdup(request->url);
    }
    cr_url_free(&url);
    payload_free(&payload);
    auth_free(&auth);
    free(method);
    return result;
}

cr_response *cr_session_send(cr_session *session, cr_request *request)
{
    cr_response *response;
    char err[512];
    long long start;
    cr_error result;

    if (session == NULL || request == NULL) {
        return response_error_new(CR_ERR_ARGUMENT, NULL,
                                  "session and request are required");
    }
    response = response_new();
    if (response == NULL) {
        return NULL;
    }
    err[0] = '\0';
    start = cr_now_ms();
    result = send_with_session(session, request, response, err, sizeof(err));
    if (result != CR_OK && response->error == CR_OK) {
        response_set_error(response, result, "%s",
                           err[0] != '\0' ? err : cr_strerror(result));
    }
    response->elapsed_ms = cr_now_ms() - start;
    return response;
}

/* ------------------------------------------------------------------------- */
/* Convenience API                                                           */
/* ------------------------------------------------------------------------- */

static cr_response *one_shot(cr_session *session, cr_request *request)
{
    cr_session *temporary = NULL;
    cr_response *response;

    if (request == NULL) {
        return response_error_new(CR_ERR_ARGUMENT, NULL,
                                  "invalid method or URL");
    }
    if (session == NULL) {
        temporary = cr_session_new();
        if (temporary == NULL) {
            cr_request_free(request);
            return response_error_new(CR_ERR_NOMEM, NULL, "out of memory");
        }
        session = temporary;
    }
    response = cr_session_send(session, request);
    if (temporary != NULL) {
        cr_session_free(temporary);
    }
    cr_request_free(request);
    return response;
}

cr_response *cr_get(cr_session *session, const char *url)
{
    return one_shot(session, cr_request_new("GET", url));
}

cr_response *cr_head(cr_session *session, const char *url)
{
    return one_shot(session, cr_request_new("HEAD", url));
}

cr_response *cr_delete(cr_session *session, const char *url)
{
    return one_shot(session, cr_request_new("DELETE", url));
}

static cr_response *post_body(cr_session *session, const char *url,
                              const void *body, size_t len,
                              const char *content_type)
{
    cr_request *request;
    cr_error error;

    if (url == NULL || (body == NULL && len != 0) ||
        !cr_header_value_valid(content_type)) {
        return response_error_new(CR_ERR_ARGUMENT, url, "invalid arguments");
    }
    request = cr_request_new("POST", url);
    if (request == NULL) {
        return response_error_new(CR_ERR_URL, url, "invalid method or URL");
    }
    error = cr_request_set_body(request, body, len);
    if (error == CR_OK && content_type != NULL) {
        error = cr_request_set_header(request, "Content-Type", content_type);
    }
    if (error != CR_OK) {
        cr_request_free(request);
        return response_error_new(error, url, cr_strerror(error));
    }
    return one_shot(session, request);
}

cr_response *cr_post(cr_session *session,
                     const char *url,
                     const void *body,
                     size_t len,
                     const char *content_type)
{
    return post_body(session, url, body, len, content_type);
}

cr_response *cr_post_json(cr_session *session, const char *url, const char *json)
{
    if (json == NULL) {
        return response_error_new(CR_ERR_ARGUMENT, url, "json must not be NULL");
    }
    return post_body(session, url, json, strlen(json), "application/json");
}

cr_response *cr_post_form(cr_session *session,
                          const char *url,
                          const char *const *names,
                          const char *const *values,
                          size_t count)
{
    cr_request *request;
    size_t i;
    cr_error error = CR_OK;

    if (url == NULL || (count > 0 && (names == NULL || values == NULL))) {
        return response_error_new(CR_ERR_ARGUMENT, url, "invalid arguments");
    }
    request = cr_request_new("POST", url);
    if (request == NULL) {
        return response_error_new(CR_ERR_URL, url, "invalid method or URL");
    }
    for (i = 0; i < count; i++) {
        error = cr_request_add_form(request, names[i], values[i]);
        if (error != CR_OK) {
            break;
        }
    }
    if (error != CR_OK) {
        cr_request_free(request);
        return response_error_new(error, url, cr_strerror(error));
    }
    return one_shot(session, request);
}
