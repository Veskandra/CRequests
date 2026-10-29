#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crequests.h"

static const char *g_base;
static int g_checks;
static int g_failures;

static void fail_at(const char *expr, const char *file, int line,
                    const char *detail)
{
    g_failures++;
    fprintf(stderr, "FAIL %s:%d: %s", file, line, expr);
    if (detail != NULL && detail[0] != '\0') {
        fprintf(stderr, " (%s)", detail);
    }
    fprintf(stderr, "\n");
}

#define CHECK(expr) \
    do { \
        g_checks++; \
        if (!(expr)) { \
            fail_at(#expr, __FILE__, __LINE__, NULL); \
        } \
    } while (0)

#define CHECK_DETAIL(expr, detail) \
    do { \
        g_checks++; \
        if (!(expr)) { \
            fail_at(#expr, __FILE__, __LINE__, detail); \
        } \
    } while (0)

#define CHECK_OK(response) \
    do { \
        g_checks++; \
        if ((response) == NULL || cr_response_error(response) != CR_OK) { \
            fail_at("response ok", __FILE__, __LINE__, \
                    (response) != NULL ? cr_response_error_message(response) : "NULL"); \
        } \
    } while (0)

static char *make_url(const char *path)
{
    size_t len = strlen(g_base) + strlen(path) + 1;
    char *url = (char *)malloc(len);
    if (url != NULL) {
        snprintf(url, len, "%s%s", g_base, path);
    }
    return url;
}

static int body_contains(const cr_response *response, const char *needle)
{
    const char *text = cr_response_text(response);
    return text != NULL && strstr(text, needle) != NULL;
}

static void check_body(const cr_response *response, const char *needle, int line)
{
    g_checks++;
    if (!body_contains(response, needle)) {
        char detail[512];
        snprintf(detail, sizeof(detail), "missing '%s' in body: %.300s",
                 needle, cr_response_text(response));
        fail_at("body contains", __FILE__, line, detail);
    }
}

#define CHECK_BODY(response, needle) check_body((response), (needle), __LINE__)

static cr_response *get_path(cr_session *session, const char *path)
{
    char *url = make_url(path);
    cr_response *response;
    if (url == NULL) {
        return NULL;
    }
    response = cr_get(session, url);
    free(url);
    return response;
}

static void test_unit(void)
{
    cr_session *session;
    cr_request *request;

    CHECK(strcmp(cr_version(), CR_VERSION_STRING) == 0);
    CHECK(strcmp(cr_strerror(CR_ERR_TIMEOUT), "operation timed out") == 0);
    CHECK(cr_request_new("GET", "not a url") == NULL);
    CHECK(cr_request_new("GET", "ftp://example.com/file") == NULL);
    CHECK(cr_request_new("", "http://example.com/") == NULL);
    CHECK(cr_request_new(NULL, "http://example.com/") == NULL);

    session = cr_session_new();
    CHECK(session != NULL);
    if (session != NULL) {
        CHECK(cr_session_cookie_count(session) == 0);
        CHECK(cr_session_set_header(session, "Bad Header", "x") == CR_ERR_ARGUMENT);
        CHECK(cr_session_set_header(session, "X-Test", "a\r\nInjected: yes") ==
              CR_ERR_ARGUMENT);
        CHECK(cr_session_set_timeout(session, -1, 100) == CR_ERR_ARGUMENT);
        CHECK(cr_session_set_max_redirects(session, -1) == CR_ERR_ARGUMENT);
        CHECK(cr_session_set_verify(session, 2) == CR_ERR_ARGUMENT);
        cr_session_free(session);
    }

    request = cr_request_new("get", "http://example.com/a/../b?x=1");
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_header(request, "X-Test", "ok") == CR_OK);
        CHECK(cr_request_set_header(request, "X-Test", "bad\nvalue") ==
              CR_ERR_ARGUMENT);
        CHECK(cr_request_add_query(request, "a", "b c") == CR_OK);
        CHECK(cr_request_set_verify(request, 2) == CR_ERR_ARGUMENT);
        CHECK(cr_request_set_follow_redirects(request, 2) == CR_ERR_ARGUMENT);
        CHECK(cr_request_set_body(request, NULL, 1) == CR_ERR_ARGUMENT);
        cr_request_free(request);
    }

    {
        cr_response *response = cr_get(NULL, NULL);
        CHECK(response != NULL);
        if (response != NULL) {
            CHECK(cr_response_error(response) == CR_ERR_ARGUMENT);
            CHECK(cr_response_status(response) == 0);
            CHECK(cr_response_ok(response) == 0);
            cr_response_free(response);
        }
    }
}

static void test_query_and_headers(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }
    CHECK(cr_session_set_header(session, "X-Session", "from-session") == CR_OK);
    url = make_url("/echo?base=1");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_add_query(request, "hello", "world") == CR_OK);
        CHECK(cr_request_add_query(request, "space", "a b") == CR_OK);
        CHECK(cr_request_add_query(request, "sym", "a&b") == CR_OK);
        CHECK(cr_request_set_header(request, "X-Test", "request-value") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 200);
            CHECK(cr_response_ok(response) == 1);
            CHECK(cr_response_http_version(response) == 11);
            CHECK(cr_response_header(response, "Content-Type") != NULL);
            CHECK_BODY(response, "method=GET");
            CHECK_BODY(response, "path=/echo");
            CHECK_BODY(response, "query=base=1&hello=world&space=a%20b&sym=a%26b");
            CHECK_BODY(response, "header:x-session=from-session");
            CHECK_BODY(response, "header:x-test=request-value");
            CHECK_BODY(response, "header:accept-encoding=identity");
            cr_response_free(response);
        }
        cr_request_free(request);
    }
    cr_session_free(session);
}

static void test_post_bodies(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;
    const char *form_names[] = { "a", "b" };
    const char *form_values[] = { "hello world", "x&y" };

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }

    url = make_url("/echo");
    request = cr_request_new("POST", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_json(request, "{\"a\":1}") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK_BODY(response, "method=POST");
            CHECK_BODY(response, "header:content-type=application/json");
            CHECK_BODY(response, "body={\"a\":1}");
            CHECK_BODY(response, "header:content-length=7");
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    url = make_url("/echo");
    request = cr_request_new("POST", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_add_form(request, "a", "hello world") == CR_OK);
        CHECK(cr_request_add_form(request, "b", "x&y") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK_BODY(response, "header:content-type=application/x-www-form-urlencoded");
            CHECK_BODY(response, "body=a=hello%20world&b=x%26y");
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    url = make_url("/echo");
    response = cr_post_form(session, url, form_names, form_values, 2);
    free(url);
    CHECK_OK(response);
    if (response != NULL) {
        CHECK_BODY(response, "body=a=hello%20world&b=x%26y");
        cr_response_free(response);
    }

    url = make_url("/echo");
    response = cr_post_json(session, url, "{\"ok\":true}");
    free(url);
    CHECK_OK(response);
    if (response != NULL) {
        CHECK_BODY(response, "body={\"ok\":true}");
        cr_response_free(response);
    }

    cr_session_free(session);
}

static void test_auth(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }

    url = make_url("/basic-auth/alice/secret");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_basic_auth(request, "alice", "secret") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 200);
            CHECK_BODY(response, "authenticated");
            cr_response_free(response);
        }
        CHECK(cr_request_set_basic_auth(request, "alice", "wrong") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 401);
            CHECK(cr_response_ok(response) == 0);
            CHECK(cr_response_header(response, "WWW-Authenticate") != NULL);
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    url = make_url("/bearer");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_bearer_token(request, "secret-token") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 200);
            cr_response_free(response);
        }
        CHECK(cr_request_set_bearer_token(request, "wrong-token") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 401);
            cr_response_free(response);
        }
        cr_request_free(request);
    }
    cr_session_free(session);
}

static void test_redirects(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }

    response = get_path(session, "/redirect/3");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 200);
        CHECK(cr_response_redirect_count(response) == 4);
        CHECK(strstr(cr_response_url(response), "/echo") != NULL);
        CHECK_BODY(response, "method=GET");
        cr_response_free(response);
    }

    response = get_path(session, "/redirect-relative");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 200);
        CHECK(cr_response_redirect_count(response) == 1);
        CHECK_BODY(response, "query=from=relative");
        cr_response_free(response);
    }

    url = make_url("/redirect/1");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_follow_redirects(request, 0) == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 302);
            CHECK(cr_response_redirect_count(response) == 0);
            CHECK(strcmp(cr_response_header(response, "Location"), "/redirect/0") == 0);
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    url = make_url("/redirect/100");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_max_redirects(request, 3) == CR_OK);
        response = cr_session_send(session, request);
        CHECK(response != NULL);
        if (response != NULL) {
            CHECK(cr_response_error(response) == CR_ERR_TOO_MANY_REDIRECTS);
            CHECK(cr_response_redirect_count(response) == 3);
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    cr_session_free(session);
}

static void test_redirect_methods(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }

    url = make_url("/redirect-post");
    request = cr_request_new("POST", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_body_text(request, "payload", "text/plain") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK_BODY(response, "method=GET");
            CHECK(!body_contains(response, "body=payload"));
            CHECK(!body_contains(response, "header:content-type=text/plain"));
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    url = make_url("/redirect307");
    request = cr_request_new("POST", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_body_text(request, "payload", "text/plain") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK_BODY(response, "method=POST");
            CHECK_BODY(response, "body=payload");
            CHECK_BODY(response, "header:content-type=text/plain");
            cr_response_free(response);
        }
        cr_request_free(request);
    }
    cr_session_free(session);
}

static void test_cookies(void)
{
    cr_session *session = cr_session_new();
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }
    response = get_path(session, "/cookies/set?a=1&b=2");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_session_cookie_count(session) == 2);
        cr_response_free(response);
    }
    response = get_path(session, "/cookies");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK_BODY(response, "cookie=a=1; b=2");
        cr_response_free(response);
    }

    cr_session_clear_cookies(session);
    CHECK(cr_session_cookie_count(session) == 0);
    response = get_path(session, "/cookies/attributes");
    CHECK_OK(response);
    if (response != NULL) {
        cr_response_free(response);
    }
    response = get_path(session, "/cookies");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK_BODY(response, "a=1");
        CHECK(!body_contains(response, "b=2"));
        CHECK(!body_contains(response, "secure=1"));
        CHECK(!body_contains(response, "gone=1"));
        cr_response_free(response);
    }

    cr_session_clear_cookies(session);
    response = get_path(session, "/cookies/case");
    if (response != NULL) {
        CHECK_OK(response);
        CHECK(cr_session_cookie_count(session) == 2);
        cr_response_free(response);
    }
    cr_session_free(session);
}

static void test_bodies_and_headers(void)
{
    cr_session *session = cr_session_new();
    cr_response *response;
    size_t length = 123;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }

    response = get_path(session, "/chunked");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(strcmp(cr_response_text(response), "Wikipedia!") == 0);
        CHECK(cr_response_header(response, "X-Chunked") != NULL);
        CHECK(strcmp(cr_response_header(response, "X-Trailer"), "done") == 0);
        cr_response_free(response);
    }

    response = get_path(session, "/no-content");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 204);
        cr_response_body(response, &length);
        CHECK(length == 0);
        cr_response_free(response);
    }

    {
        char *head_url = make_url("/head");
        response = cr_head(session, head_url);
        free(head_url);
    }
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 200);
        cr_response_body(response, &length);
        CHECK(length == 0);
        CHECK(strcmp(cr_response_header(response, "Content-Length"), "9") == 0);
        cr_response_free(response);
    }

    response = get_path(session, "/status/404");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 404);
        CHECK(cr_response_ok(response) == 0);
        cr_response_free(response);
    }

    response = get_path(session, "/reason");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 418);
        CHECK(strcmp(cr_response_reason(response), "I am a teapot") == 0);
        cr_response_free(response);
    }

    response = get_path(session, "/continue");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(cr_response_status(response) == 200);
        CHECK(strcmp(cr_response_text(response), "after-continue") == 0);
        cr_response_free(response);
    }

    response = get_path(session, "/multi-header");
    CHECK_OK(response);
    if (response != NULL) {
        size_t i;
        int x_multi = 0;
        for (i = 0; i < cr_response_header_count(response); i++) {
            if (strcmp(cr_response_header_name(response, i), "X-Multi") == 0) {
                x_multi++;
            }
        }
        CHECK(x_multi == 2);
        CHECK(strcmp(cr_response_header(response, "X-Multi"), "one") == 0);
        CHECK(cr_session_cookie_count(session) == 2);
        cr_response_free(response);
    }

    response = get_path(session, "/utf8");
    CHECK_OK(response);
    if (response != NULL) {
        CHECK(strcmp(cr_response_text(response), "你好, crequests") == 0);
        cr_response_body(response, &length);
        CHECK(length == strlen("你好, crequests"));
        cr_response_free(response);
    }

    response = get_path(session, "/bad-length");
    CHECK(response != NULL);
    if (response != NULL) {
        CHECK(cr_response_error(response) != CR_OK);
        cr_response_free(response);
    }
    cr_session_free(session);
}

static void test_errors_and_limits(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }

    url = make_url("/slow?ms=1000");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_timeout(request, 1000, 250) == CR_OK);
        response = cr_session_send(session, request);
        CHECK(response != NULL);
        if (response != NULL) {
            CHECK(cr_response_error(response) == CR_ERR_TIMEOUT);
            cr_response_free(response);
        }
        cr_request_free(request);
    }

    CHECK(cr_session_set_max_body_size(session, 100) == CR_OK);
    response = get_path(session, "/large?n=1000");
    CHECK(response != NULL);
    if (response != NULL) {
        CHECK(cr_response_error(response) == CR_ERR_BODY_TOO_LARGE);
        cr_response_free(response);
    }

    CHECK(cr_session_set_max_body_size(session, 64 * 1024 * 1024) == CR_OK);
    response = get_path(session, "/large?n=1000");
    CHECK_OK(response);
    if (response != NULL) {
        size_t length = 0;
        cr_response_body(response, &length);
        CHECK(length == 1000);
        cr_response_free(response);
    }

    cr_session_free(session);
}

static void test_cross_origin_auth(void)
{
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }
    url = make_url("/redirect-localhost");
    request = cr_request_new("GET", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_set_basic_auth(request, "alice", "secret") == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK(cr_response_status(response) == 200);
            CHECK(cr_response_redirect_count(response) == 1);
            CHECK(strstr(cr_response_url(response), "localhost") != NULL);
            CHECK(!body_contains(response, "header:authorization="));
            cr_response_free(response);
        }
        cr_request_free(request);
    }
    cr_session_free(session);
}

static void test_multipart(void)
{
    const char *path = "build/test_upload.txt";
    FILE *file = fopen(path, "wb");
    cr_session *session = cr_session_new();
    cr_request *request;
    char *url;
    cr_response *response;

    CHECK(file != NULL);
    if (file != NULL) {
        fwrite("hello file", 1, 10, file);
        fclose(file);
    }
    CHECK(session != NULL);
    if (session == NULL) {
        return;
    }
    url = make_url("/upload");
    request = cr_request_new("POST", url);
    free(url);
    CHECK(request != NULL);
    if (request != NULL) {
        CHECK(cr_request_add_form(request, "field", "value") == CR_OK);
        CHECK(cr_request_add_file(request, "file", path, NULL) == CR_OK);
        response = cr_session_send(session, request);
        CHECK_OK(response);
        if (response != NULL) {
            CHECK_BODY(response, "type=multipart/form-data; boundary=");
            CHECK_BODY(response, "name=\"field\"");
            CHECK_BODY(response, "name=\"file\"; filename=\"test_upload.txt\"");
            CHECK_BODY(response, "Content-Type: text/plain");
            CHECK_BODY(response, "hello file");
            cr_response_free(response);
        }
        cr_request_free(request);
    }
    cr_session_free(session);
    remove(path);
}

int main(int argc, char **argv)
{
    test_unit();
    if (argc > 1) {
        g_base = argv[1];
        test_query_and_headers();
        test_post_bodies();
        test_auth();
        test_redirects();
        test_redirect_methods();
        test_cookies();
        test_bodies_and_headers();
        test_errors_and_limits();
        test_cross_origin_auth();
        test_multipart();
    } else {
        printf("No base URL supplied; network tests skipped.\n");
    }

    printf("%d checks, %d failure%s\n", g_checks, g_failures,
           g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
