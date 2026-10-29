/*
 * crequests.h - a small, requests-like HTTP client library for C.
 *
 * Copyright (c) 2026 crequests contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CREQUESTS_H
#define CREQUESTS_H

#include <stddef.h>

#if defined(_WIN32) && defined(CREQUESTS_SHARED)
#  if defined(CREQUESTS_BUILD)
#    define CR_API __declspec(dllexport)
#  else
#    define CR_API __declspec(dllimport)
#  endif
#else
#  define CR_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CR_VERSION_MAJOR 1
#define CR_VERSION_MINOR 0
#define CR_VERSION_PATCH 0
#define CR_VERSION_STRING "1.0.0"

/* Error codes.  Most library functions return one of these values directly;
 * a few return allocated objects and report transport/protocol failures on
 * the resulting cr_response object. */
typedef enum cr_error {
    CR_OK = 0,
    CR_ERR_ARGUMENT,
    CR_ERR_NOMEM,
    CR_ERR_URL,
    CR_ERR_UNSUPPORTED,
    CR_ERR_TLS,
    CR_ERR_TLS_VERIFY,
    CR_ERR_DNS,
    CR_ERR_CONNECT,
    CR_ERR_TIMEOUT,
    CR_ERR_SEND,
    CR_ERR_RECV,
    CR_ERR_PROTOCOL,
    CR_ERR_TOO_MANY_REDIRECTS,
    CR_ERR_BODY_TOO_LARGE,
    CR_ERR_FILE,
    CR_ERR_CLOSED
} cr_error;

/* Human readable name of an error code. Never returns NULL. */
CR_API const char *cr_strerror(cr_error error);

/* Library version, e.g. "1.0.0". */
CR_API const char *cr_version(void);

/* Opaque objects. */
typedef struct cr_session cr_session;
typedef struct cr_request cr_request;
typedef struct cr_response cr_response;

/* ------------------------------------------------------------------------- */
/* Session                                                                   */
/* ------------------------------------------------------------------------- */

/* Create a session.  A session owns default headers, a cookie jar and default
 * options.  Sessions are not thread safe; use one session per thread or
 * synchronize access. */
CR_API cr_session *cr_session_new(void);
CR_API void cr_session_free(cr_session *session);

/* Set/replace a default header.  Passing value == NULL removes the header.
 * Header names must not contain ':', CR or LF; values must not contain CR/LF. */
CR_API cr_error cr_session_set_header(cr_session *session,
                                      const char *name,
                                      const char *value);

/* Default timeouts in milliseconds.  0 means "wait forever". */
CR_API cr_error cr_session_set_timeout(cr_session *session,
                                       int connect_timeout_ms,
                                       int read_timeout_ms);

/* Enable/disable TLS certificate verification.  Enabled by default. */
CR_API cr_error cr_session_set_verify(cr_session *session, int verify);

/* Follow HTTP redirects.  Enabled by default. */
CR_API cr_error cr_session_set_follow_redirects(cr_session *session, int follow);
CR_API cr_error cr_session_set_max_redirects(cr_session *session, int max_redirects);

/* Default User-Agent.  Passing NULL restores the built-in value. */
CR_API cr_error cr_session_set_user_agent(cr_session *session, const char *user_agent);

/* Reject response bodies larger than max_bytes.  0 disables the limit.
 * The default is 64 MiB. */
CR_API cr_error cr_session_set_max_body_size(cr_session *session, size_t max_bytes);

/* Cookie jar helpers. */
CR_API void cr_session_clear_cookies(cr_session *session);
CR_API size_t cr_session_cookie_count(const cr_session *session);

/* ------------------------------------------------------------------------- */
/* Request                                                                   */
/* ------------------------------------------------------------------------- */

/* Create a request.  method is usually "GET", "POST", ... (case insensitive).
 * url must be an absolute http:// or https:// URL.  Returns NULL on invalid
 * arguments or allocation failure. */
CR_API cr_request *cr_request_new(const char *method, const char *url);
CR_API void cr_request_free(cr_request *request);

CR_API cr_error cr_request_set_method(cr_request *request, const char *method);
CR_API cr_error cr_request_set_url(cr_request *request, const char *url);

/* Per-request header.  Overrides a session header of the same name.
 * value == NULL removes the header from this request. */
CR_API cr_error cr_request_set_header(cr_request *request,
                                      const char *name,
                                      const char *value);

/* Add a query-string parameter.  Values are percent-encoded; a NULL value
 * produces a bare key ("?flag").  Existing URL query parameters are kept. */
CR_API cr_error cr_request_add_query(cr_request *request,
                                     const char *name,
                                     const char *value);

/* Add an application/x-www-form-urlencoded field.  Switches the body to form
 * mode.  It is an error to mix this with cr_request_set_body(). */
CR_API cr_error cr_request_add_form(cr_request *request,
                                    const char *name,
                                    const char *value);

/* Add a file part to a multipart/form-data body.  The body switches to
 * multipart mode; previously added form fields are kept.  The file is read
 * when the request is sent.  content_type may be NULL (auto-detected). */
CR_API cr_error cr_request_add_file(cr_request *request,
                                    const char *field_name,
                                    const char *path,
                                    const char *content_type);

/* Replace the raw request body.  data == NULL with len == 0 clears the body.
 * The bytes are copied.  Returns to raw-body mode. */
CR_API cr_error cr_request_set_body(cr_request *request,
                                    const void *data,
                                    size_t len);

/* Convenience for a raw text body.  content_type == NULL leaves the current
 * Content-Type header alone. */
CR_API cr_error cr_request_set_body_text(cr_request *request,
                                         const char *text,
                                         const char *content_type);

/* Set a raw JSON body and Content-Type: application/json.  The string is sent
 * verbatim (the library does not validate or generate JSON). */
CR_API cr_error cr_request_set_json(cr_request *request, const char *json);

/* Authentication.  Calling one removes the other. */
CR_API cr_error cr_request_set_basic_auth(cr_request *request,
                                          const char *username,
                                          const char *password);
CR_API cr_error cr_request_set_bearer_token(cr_request *request, const char *token);

/* Per-request overrides.  A negative value means "inherit from the session"
 * (that is also the initial state).  verify/follow take 0 or 1. */
CR_API cr_error cr_request_set_timeout(cr_request *request,
                                       int connect_timeout_ms,
                                       int read_timeout_ms);
CR_API cr_error cr_request_set_verify(cr_request *request, int verify);
CR_API cr_error cr_request_set_follow_redirects(cr_request *request, int follow);
CR_API cr_error cr_request_set_max_redirects(cr_request *request, int max_redirects);
CR_API cr_error cr_request_set_user_agent(cr_request *request, const char *user_agent);

/* ------------------------------------------------------------------------- */
/* Response                                                                  */
/* ------------------------------------------------------------------------- */

/* Send request through session.  A response object is normally returned even
 * for network errors; inspect cr_response_error().  session must not be NULL.
 * The request is not consumed and may be reused. */
CR_API cr_response *cr_session_send(cr_session *session, cr_request *request);

/* Convenience one-shot helpers.  session may be NULL, in which case a
 * temporary session (without persistent cookies) is used.  These always
 * return a response unless allocation fails. */
CR_API cr_response *cr_get(cr_session *session, const char *url);
CR_API cr_response *cr_head(cr_session *session, const char *url);
CR_API cr_response *cr_delete(cr_session *session, const char *url);
CR_API cr_response *cr_post(cr_session *session,
                            const char *url,
                            const void *body,
                            size_t len,
                            const char *content_type);
CR_API cr_response *cr_post_json(cr_session *session,
                                 const char *url,
                                 const char *json);
CR_API cr_response *cr_post_form(cr_session *session,
                                 const char *url,
                                 const char *const *names,
                                 const char *const *values,
                                 size_t count);

CR_API void cr_response_free(cr_response *response);

/* 2xx-3xx responses (and no transport error) are considered successful,
 * mirroring requests.Response.ok. */
CR_API int cr_response_ok(const cr_response *response);
CR_API int cr_response_status(const cr_response *response);
CR_API const char *cr_response_reason(const cr_response *response);

/* 10 or 11 for HTTP/1.0 and HTTP/1.1; 0 if no response was received. */
CR_API int cr_response_http_version(const cr_response *response);

/* Body bytes are always followed by a NUL byte, but may contain arbitrary
 * binary data.  The returned pointers are valid until cr_response_free(). */
CR_API const void *cr_response_body(const cr_response *response, size_t *length);
CR_API const char *cr_response_text(const cr_response *response);

/* Response header helpers.  Names are matched case-insensitively.
 * cr_response_header() returns the first matching value, or NULL.
 * Use the indexed accessors to inspect repeated headers such as Set-Cookie. */
CR_API const char *cr_response_header(const cr_response *response, const char *name);
CR_API size_t cr_response_header_count(const cr_response *response);
CR_API const char *cr_response_header_name(const cr_response *response, size_t index);
CR_API const char *cr_response_header_value(const cr_response *response, size_t index);

CR_API const char *cr_response_url(const cr_response *response);
CR_API int cr_response_redirect_count(const cr_response *response);
CR_API long long cr_response_elapsed_ms(const cr_response *response);
CR_API cr_error cr_response_error(const cr_response *response);
CR_API const char *cr_response_error_message(const cr_response *response);

#ifdef __cplusplus
}
#endif

#endif /* CREQUESTS_H */
