#include <stdio.h>
#include <stdlib.h>

#include "crequests.h"

int main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : "http://example.com/";
    cr_session *session = cr_session_new();
    cr_response *response;

    if (session == NULL) {
        fprintf(stderr, "cannot create session\n");
        return 1;
    }
    response = cr_get(session, url);
    if (response == NULL) {
        fprintf(stderr, "out of memory\n");
        cr_session_free(session);
        return 1;
    }

    if (cr_response_error(response) != CR_OK) {
        fprintf(stderr, "request failed: %s\n", cr_response_error_message(response));
        cr_response_free(response);
        cr_session_free(session);
        return 1;
    }

    printf("status: %d %s\n", cr_response_status(response), cr_response_reason(response));
    printf("url: %s\n", cr_response_url(response));
    printf("elapsed: %lld ms\n", cr_response_elapsed_ms(response));
    printf("content-type: %s\n",
           cr_response_header(response, "Content-Type") != NULL ?
           cr_response_header(response, "Content-Type") : "(none)");
    printf("--- body ---\n%s\n", cr_response_text(response));

    cr_response_free(response);
    cr_session_free(session);
    return 0;
}
