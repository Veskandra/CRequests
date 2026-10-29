/* Small consumer used to verify the crequests DLL import library and exports. */
#include <stdio.h>
#include <string.h>

#include "crequests.h"

int main(void)
{
    cr_session *session;
    cr_response *response;

    printf("crequests DLL version: %s\n", cr_version());
    printf("CR_ERR_TIMEOUT => %s\n", cr_strerror(CR_ERR_TIMEOUT));

    session = cr_session_new();
    if (session == NULL) {
        fprintf(stderr, "cr_session_new failed\n");
        return 1;
    }
    response = cr_get(session, "http://example.com/");
    if (response == NULL) {
        fprintf(stderr, "cr_get failed\n");
        cr_session_free(session);
        return 1;
    }
    printf("HTTP status: %d (%s)\n",
           cr_response_status(response), cr_response_error_message(response));
    if (cr_response_error(response) != CR_OK ||
        cr_response_status(response) != 200) {
        cr_response_free(response);
        cr_session_free(session);
        return 1;
    }
    cr_response_free(response);
    cr_session_free(session);
    return 0;
}
