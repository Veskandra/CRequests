#include <stdio.h>

#include "crequests.h"

int main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : "http://httpbin.org/post";
    const char *json = "{\"name\":\"crequests\",\"language\":\"C\"}";
    cr_session *session = cr_session_new();
    cr_request *request;
    cr_response *response;

    if (session == NULL) {
        return 1;
    }
    request = cr_request_new("POST", url);
    if (request == NULL) {
        fprintf(stderr, "invalid URL: %s\n", url);
        cr_session_free(session);
        return 1;
    }
    cr_request_set_json(request, json);
    cr_request_set_header(request, "Accept", "application/json");
    cr_request_set_timeout(request, 10000, 30000);

    response = cr_session_send(session, request);
    if (response == NULL) {
        fprintf(stderr, "out of memory\n");
    } else if (cr_response_error(response) != CR_OK) {
        fprintf(stderr, "request failed: %s\n", cr_response_error_message(response));
    } else {
        printf("status: %d\n", cr_response_status(response));
        printf("%s\n", cr_response_text(response));
    }

    cr_response_free(response);
    cr_request_free(request);
    cr_session_free(session);
    return 0;
}
