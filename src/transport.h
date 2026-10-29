/*
 * transport.h - TCP/TLS transport used by crequests.
 */
#ifndef CR_TRANSPORT_H
#define CR_TRANSPORT_H

#include "internal.h"

typedef struct cr_conn cr_conn;

/* Connect to url->host:url->port, performing TLS when url->tls is set.
 * Returns NULL in *out on failure and fills err. */
cr_error cr_conn_open(cr_conn **out,
                      const cr_url *url,
                      int verify,
                      int connect_timeout_ms,
                      int read_timeout_ms,
                      char *err,
                      size_t errlen);

cr_error cr_conn_write(cr_conn *conn,
                       const void *data,
                       size_t len,
                       char *err,
                       size_t errlen);

/* Returns CR_OK with *got bytes, or CR_ERR_CLOSED with *eof = 1. */
cr_error cr_conn_read(cr_conn *conn,
                      void *data,
                      size_t cap,
                      size_t *got,
                      int *eof,
                      char *err,
                      size_t errlen);

void cr_conn_close(cr_conn *conn);

#endif /* CR_TRANSPORT_H */
