/*
 * http.h - HTTP/1.1 request/response exchange.
 */
#ifndef CR_HTTP_H
#define CR_HTTP_H

#include "internal.h"
#include "transport.h"

/* Send one HTTP/1.1 request over conn and read one response into resp.
 * resp must be zeroed. max_body == 0 means no limit. head_request suppresses
 * response body reading for HEAD requests. */
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
                          size_t errlen);

#endif /* CR_HTTP_H */
