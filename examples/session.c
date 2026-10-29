#include <stdio.h>

#include "crequests.h"

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "http://httpbin.org";
    char url[1024];
    cr_session *session = cr_session_new();
    cr_response *response;

    if (session == NULL) {
        return 1;
    }

    /* Session defaults are applied to every request. */
    cr_session_set_header(session, "Accept", "text/plain");
    cr_session_set_user_agent(session, "crequests-session-demo/1.0");
    cr_session_set_timeout(session, 10000, 30000);

    /* Visit an endpoint that sets a cookie and redirects.  The cookie is
     * stored in the session and sent automatically on the next request. */
    snprintf(url, sizeof(url), "%s/cookies/set?demo=hello", base);
    response = cr_get(session, url);
    if (response != NULL) {
        printf("first: %d, cookies in jar: %zu\n",
               cr_response_status(response), cr_session_cookie_count(session));
        cr_response_free(response);
    }

    snprintf(url, sizeof(url), "%s/cookies", base);
    response = cr_get(session, url);
    if (response == NULL) {
        fprintf(stderr, "out of memory\n");
    } else if (cr_response_error(response) != CR_OK) {
        fprintf(stderr, "request failed: %s\n", cr_response_error_message(response));
    } else {
        printf("second: %d\n%s\n", cr_response_status(response),
               cr_response_text(response));
    }

    cr_response_free(response);
    cr_session_free(session);
    return 0;
}
