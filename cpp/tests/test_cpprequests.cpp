#include "cpprequests.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace cpprequests;

static const char *g_base = nullptr;
static int g_checks = 0;
static int g_failures = 0;

static void fail_at(const char *expression, const char *file, int line,
                    const std::string &detail = {})
{
    ++g_failures;
    std::fprintf(stderr, "FAIL %s:%d: %s", file, line, expression);
    if (!detail.empty()) {
        std::fprintf(stderr, " (%s)", detail.c_str());
    }
    std::fprintf(stderr, "\n");
}

#define CHECK(expr)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(expr)) {                                                         \
            fail_at(#expr, __FILE__, __LINE__);                                \
        }                                                                      \
    } while (0)

#define CHECK_THROWS_AS(expr, type)                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        bool caught = false;                                                   \
        try {                                                                  \
            (void)(expr);                                                      \
        } catch (const type &) {                                               \
            caught = true;                                                     \
        } catch (...) {                                                        \
        }                                                                      \
        if (!caught) {                                                         \
            fail_at(#expr " throws " #type, __FILE__, __LINE__);               \
        }                                                                      \
    } while (0)

static std::string make_url(const std::string &path)
{
    return std::string(g_base) + path;
}

static void check_body(const Response &response, const std::string &needle,
                       int line)
{
    ++g_checks;
    if (response.content.find(needle) == std::string::npos) {
        std::string detail = "missing '" + needle + "' in body: ";
        detail += response.content.substr(0, 300);
        fail_at("body contains", __FILE__, line, detail);
    }
}

static void check_no_body(const Response &response, const std::string &needle,
                          int line)
{
    ++g_checks;
    if (response.content.find(needle) != std::string::npos) {
        fail_at("body does not contain", __FILE__, line, needle);
    }
}

#define CHECK_BODY(response, needle) check_body((response), (needle), __LINE__)
#define CHECK_NO_BODY(response, needle)                                        \
    check_no_body((response), (needle), __LINE__)

static void test_json(void)
{
    Json null;
    CHECK(null.is_null());
    CHECK(null.dump() == "null");
    CHECK(Json(true).is_boolean());
    CHECK(Json(true).get<bool>());
    CHECK(Json(42).is_integer());
    CHECK(Json(42).get<int>() == 42);
    CHECK(Json(3.14).is_number());
    CHECK(Json(3.14).dump() == "3.14");
    CHECK(Json(1.0).dump() == "1.0");
    CHECK(Json(std::string("a\nb")).dump() == "\"a\\nb\"");
    CHECK(Json::array({1, 2, 3}).dump() == "[1,2,3]");
    CHECK(Json::object({{"a", 1}, {"b", "x"}}).dump() ==
          "{\"a\":1,\"b\":\"x\"}");

    Json object = {{"first", 1}, {"second", 2}};
    CHECK(object.is_object());
    CHECK(object.size() == 2);
    CHECK(object["first"].get<int>() == 1);
    object["third"] = "three";
    CHECK(object.size() == 3);
    CHECK(object.contains("third"));
    CHECK(!object.contains("missing"));
    CHECK(object.find("missing") == nullptr);
    CHECK(object.at("second").get<int>() == 2);
    CHECK_THROWS_AS(object.at("missing"), JsonError);
    CHECK(object.erase("third"));
    CHECK(!object.contains("third"));
    CHECK(!object.erase("third"));

    Json array = Json::array();
    array.push_back(1);
    array.push_back("two");
    array.push_back(true);
    CHECK(array.size() == 3);
    CHECK(array[1].get<std::string>() == "two");
    array[0] = 9;
    CHECK(array[0].get<int>() == 9);
    array.erase(1);
    CHECK(array.size() == 2);
    CHECK_THROWS_AS(array.at(8), JsonError);

    Json copy = object;
    CHECK(copy == object);
    copy["first"] = 100;
    CHECK(copy != object);
    CHECK(copy["first"].get<int>() == 100);

    Json reordered = {{"second", 2}, {"first", 1}};
    CHECK(reordered == object);

    Json parsed = Json::parse(
        "{\"a\":1,\"b\":[true,null,\"x\"],\"u\":\"\\u4f60\\u597d\"}");
    CHECK(parsed["a"].get<int>() == 1);
    CHECK(parsed["b"][0].get<bool>());
    CHECK(parsed["b"][1].is_null());
    CHECK(parsed["b"][2].get<std::string>() == "x");
    CHECK(parsed["u"].get<std::string>() ==
          "\xE4\xBD\xA0\xE5\xA5\xBD");
    CHECK(parsed.dump() ==
          "{\"a\":1,\"b\":[true,null,\"x\"],\"u\":\"\xE4\xBD\xA0\xE5\xA5\xBD\"}");

    Json escaped = Json::parse("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"");
    CHECK(escaped.get<std::string>() == "a\"b\\c/d\b\f\n\r\t");
    CHECK(Json("a\"b\\c/d\b\f\n\r\t").dump() ==
          "\"a\\\"b\\\\c/d\\b\\f\\n\\r\\t\"");

    Json emoji = Json::parse("\"\\uD83D\\uDE00\"");
    CHECK(emoji.get<std::string>() == "\xF0\x9F\x98\x80");
    CHECK(Json::parse(" \n {\"ok\": true} \t")["ok"].get<bool>());
    CHECK(Json::parse("{\"a\":1,\"a\":2}")["a"].get<int>() == 2);
    CHECK(Json::parse("{\"a\":1,\"a\":2}").size() == 1);

    CHECK(Json::parse("9223372036854775807").get<long long>() ==
          9223372036854775807LL);
    CHECK(Json::parse("9223372036854775808").is_number());
    CHECK(Json::parse("-0.0").dump() == "-0.0");
    CHECK(Json::parse("1e3").dump() == "1000.0");
    CHECK(Json::parse("[1,2.5,true,null]").size() == 4);

    CHECK_THROWS_AS(Json::parse(""), JsonError);
    CHECK_THROWS_AS(Json::parse("{"), JsonError);
    CHECK_THROWS_AS(Json::parse("[1,]"), JsonError);
    CHECK_THROWS_AS(Json::parse("{\"a\":}"), JsonError);
    CHECK_THROWS_AS(Json::parse("01"), JsonError);
    CHECK_THROWS_AS(Json::parse("1."), JsonError);
    CHECK_THROWS_AS(Json::parse("truex"), JsonError);
    CHECK_THROWS_AS(Json::parse("\"\\uD800\""), JsonError);
    CHECK_THROWS_AS(Json::parse("\"\\q\""), JsonError);
    CHECK_THROWS_AS(Json::parse("1 2"), JsonError);
    CHECK_THROWS_AS(Json::parse("1e309"), JsonError);

    const std::string deep = std::string(600, '[') + std::string(600, ']');
    CHECK_THROWS_AS(Json::parse(deep), JsonError);

    CHECK(Json(300).get<int>() == 300);
    CHECK(Json(3.0).get<int>() == 3);
    CHECK_THROWS_AS(Json(300).get<unsigned char>(), JsonError);
    CHECK_THROWS_AS(Json("x").get<int>(), JsonError);
    CHECK_THROWS_AS(Json(1).get<std::string>(), JsonError);

    CHECK(Json(1).value("missing", 7) == 7);
    CHECK(Json("x").value("missing", std::string("fallback")) == "fallback");
    CHECK(Json::object({{"port", 8080}}).value("port", 80) == 8080);

    const Json pretty = Json::parse("{\"a\":[1,2]}");
    const std::string pretty_text = pretty.dump(2);
    CHECK(pretty_text.find("\n  \"a\": [") != std::string::npos);
    CHECK(pretty_text.find("\n    1,") != std::string::npos);

    CHECK_THROWS_AS(Json(std::numeric_limits<double>::infinity()).dump(),
                    JsonError);
    CHECK_THROWS_AS(Json(std::nan("")).dump(), JsonError);

    Response response;
    response.content = "{\"x\":1}";
    CHECK(response.json_or(Json(nullptr)).is_object());
    response.content = "bad";
    CHECK(response.json_or(Json(5)).get<int>() == 5);
}

static void test_unit(void)
{
    CHECK(std::string(version()) == CPPREQUESTS_VERSION_STRING);
    CHECK(std::string(strerror(1)) == "invalid argument");
    CHECK(std::string(strerror(9)) == "operation timed out");

    Headers headers;
    headers.set("Content-Type", "text/plain");
    headers.set("content-type", "application/json");
    CHECK(headers.size() == 1);
    CHECK(headers.get("CONTENT-TYPE") == "application/json");
    headers.add("X-Multi", "one").add("x-multi", "two");
    CHECK(headers.count("X-MULTI") == 2);
    CHECK(headers.get_all("x-multi").size() == 2);
    CHECK(headers.contains("x-multi"));
    CHECK(headers.remove("X-Multi"));
    CHECK(!headers.contains("x-multi"));
    CHECK(headers.get("missing", "fallback") == "fallback");
    headers.clear();
    CHECK(headers.empty());

    Headers initializer{{"Accept", "text/html"}, {"X-Test", "1"}};
    CHECK(initializer.size() == 2);
    CHECK(initializer["accept"] == "text/html");

    Response response;
    response.status_code = 204;
    CHECK(response.ok());
    response.status_code = 404;
    CHECK(!response.ok());
    response.error_code = 1;
    CHECK(!response.ok());
    CHECK(response.has_error());
    CHECK_THROWS_AS(response.raise_for_status(), Error);

    CHECK_THROWS_AS(Request("GET", "not a url"), Error);
    CHECK_THROWS_AS(Request("GET", "ftp://example.com/x"), Error);
    CHECK_THROWS_AS(Request("", "http://example.com/"), Error);

    Request request("get", "http://example.com/");
    CHECK(request.method() == "GET");
    CHECK(request.url() == "http://example.com/");
    request.set_method("post");
    CHECK(request.method() == "POST");
    CHECK_THROWS_AS(request.set_method(""), Error);
    CHECK_THROWS_AS(request.set_url("not a url"), Error);
    CHECK_THROWS_AS(request.set_header("Bad Header", "x"), Error);
    CHECK_THROWS_AS(request.set_header("X-Test", "a\r\nInjected: yes"), Error);
    CHECK_THROWS_AS(request.add_query("", "x"), Error);
    CHECK_THROWS_AS(request.set_body(nullptr, 1), Error);
    CHECK_THROWS_AS(request.set_timeout(-2, 100), Error);
    CHECK_THROWS_AS(request.set_max_redirects(-2), Error);

    Request body_request("POST", "http://example.com/");
    body_request.set_body("raw");
    CHECK_THROWS_AS(body_request.add_form("a", "b"), Error);
    CHECK_THROWS_AS(body_request.add_file("f", "x.txt"), Error);
    body_request.set_headers({{"X-One", "1"}, {"X-Two", "2"}});
    body_request.remove_header("x-one");

    Session session;
    CHECK_THROWS_AS(session.set_header("Bad Header", "x"), Error);
    CHECK_THROWS_AS(session.set_header("X-Test", "a\nb"), Error);
    CHECK_THROWS_AS(session.set_timeout(-1, 100), Error);
    CHECK_THROWS_AS(session.set_max_redirects(-1), Error);
}

static void test_get(void)
{
    Session session;
    session.set_header("X-Session", "from-session");

    Request request("GET", make_url("/echo?base=1"));
    request.add_query("hello", "world")
        .add_query("space", "a b")
        .add_query("sym", "a&b")
        .add_query("flag")
        .set_header("X-Test", "request-value")
        .set_user_agent("cpprequests-test/1.0");

    Response response = session.send(request);
    CHECK(!response.has_error());
    CHECK(response.status_code == 200);
    CHECK(response.ok());
    CHECK(response.http_version == 11);
    CHECK(response.elapsed_ms >= 0);
    CHECK(response.headers.get("content-type").find("text/plain") !=
          std::string::npos);
    CHECK_BODY(response, "method=GET");
    CHECK_BODY(response, "path=/echo");
    CHECK_BODY(response,
               "query=base=1&hello=world&space=a%20b&sym=a%26b&flag");
    CHECK_BODY(response, "header:x-session=from-session");
    CHECK_BODY(response, "header:x-test=request-value");
    CHECK_BODY(response, "header:user-agent=cpprequests-test/1.0");
    CHECK_BODY(response, "header:accept-encoding=identity");
    CHECK_BODY(response, "body=\n");

    Query query{{"q", "hello world"}, {"empty", ""}};
    Response with_params = session.get(make_url("/echo"), query);
    CHECK(with_params.status_code == 200);
    CHECK_BODY(with_params, "query=q=hello%20world&empty=");

    Headers extra{{"X-Test", "extra"}};
    Response with_headers = session.get(make_url("/echo"), {}, extra);
    CHECK_BODY(with_headers, "header:x-test=extra");

    Response free_response = cpprequests::get(make_url("/echo"));
    CHECK(free_response.status_code == 200);
    CHECK_BODY(free_response, "method=GET");

    Response deleted = session.del(make_url("/echo"));
    CHECK_BODY(deleted, "method=DELETE");
}

static void test_post(void)
{
    Session session;

    Response raw = session.post(make_url("/echo"), "hello body",
                                "text/plain");
    CHECK(raw.status_code == 200);
    CHECK_BODY(raw, "method=POST");
    CHECK_BODY(raw, "body=hello body");
    CHECK_BODY(raw, "header:content-type=text/plain");

    Json payload = {{"name", "cpprequests"}, {"n", 2}, {"ok", true}};
    Response json_response = session.post_json(make_url("/echo"), payload);
    CHECK(json_response.status_code == 200);
    CHECK_BODY(json_response, "method=POST");
    CHECK_BODY(json_response, "body={\"name\":\"cpprequests\",\"n\":2,\"ok\":true}");
    CHECK_BODY(json_response, "header:content-type=application/json");

    Form form{{"hello", "a b"}, {"sym", "a&b"}};
    Response form_response = session.post_form(make_url("/echo"), form);
    CHECK(form_response.status_code == 200);
    CHECK_BODY(form_response, "body=hello=a%20b&sym=a%26b");
    CHECK_BODY(form_response, "header:content-type=application/x-www-form-urlencoded");

    Request request("POST", make_url("/upload"));
    request.add_form("field", "value")
        .add_file("upload", "cpp_upload_test.txt", "text/plain");
    {
        std::ofstream file("cpp_upload_test.txt", std::ios::binary);
        file << "upload-payload";
    }
    Response upload = session.send(request);
    std::remove("cpp_upload_test.txt");
    CHECK(upload.status_code == 200);
    CHECK_BODY(upload, "type=multipart/form-data; boundary=");
    CHECK_BODY(upload, "name=\"field\"");
    CHECK_BODY(upload, "value");
    CHECK_BODY(upload, "filename=\"cpp_upload_test.txt\"");
    CHECK_BODY(upload, "upload-payload");

    Response free_post = cpprequests::post_json(make_url("/echo"), payload);
    CHECK(free_post.status_code == 200);
    CHECK_BODY(free_post, "method=POST");

    Response free_form = cpprequests::post_form(make_url("/echo"), form);
    CHECK(free_form.status_code == 200);
    CHECK_BODY(free_form, "method=POST");
}

static void test_auth_and_redirects(void)
{
    Session session;

    Request basic("GET", make_url("/basic-auth/alice/secret"));
    basic.basic_auth("alice", "secret");
    Response basic_response = session.send(basic);
    CHECK(basic_response.status_code == 200);
    CHECK_BODY(basic_response, "authenticated");

    Request bearer("GET", make_url("/bearer"));
    bearer.bearer_token("secret-token");
    Response bearer_response = session.send(bearer);
    CHECK(bearer_response.status_code == 200);
    CHECK_BODY(bearer_response, "authenticated");

    std::string url_with_auth = make_url("/basic-auth/alice/secret");
    const std::string scheme = "http://";
    url_with_auth.replace(0, scheme.size(), scheme + "alice:secret@");
    Request embedded("GET", url_with_auth);
    Response embedded_response = session.send(embedded);
    CHECK(embedded_response.status_code == 200);
    CHECK(embedded_response.url.find("127.0.0.1") != std::string::npos);

    Request cross("GET", make_url("/redirect-localhost"));
    cross.bearer_token("secret-token");
    Response cross_response = session.send(cross);
    CHECK(cross_response.status_code == 200);
    CHECK_NO_BODY(cross_response, "header:authorization=");

    Request post_redirect("POST", make_url("/redirect-post"));
    post_redirect.set_body_text("should be discarded");
    Response post_redirect_response = session.send(post_redirect);
    CHECK(post_redirect_response.status_code == 200);
    CHECK(post_redirect_response.redirect_count == 1);
    CHECK_BODY(post_redirect_response, "method=GET");
    CHECK_BODY(post_redirect_response, "body=\n");

    Request post_307("POST", make_url("/redirect307"));
    post_307.set_body_text("kept body");
    Response post_307_response = session.send(post_307);
    CHECK(post_307_response.status_code == 200);
    CHECK_BODY(post_307_response, "method=POST");
    CHECK_BODY(post_307_response, "body=kept body");

    Response relative = session.get(make_url("/redirect-relative"));
    CHECK(relative.status_code == 200);
    CHECK(relative.url.find("echo?from=relative") != std::string::npos);
    CHECK_BODY(relative, "query=from=relative");

    Session no_follow;
    no_follow.set_follow_redirects(false);
    Response raw_redirect = no_follow.get(make_url("/redirect/1"));
    CHECK(raw_redirect.status_code == 302);
    CHECK(raw_redirect.redirect_count == 0);

    Session limited;
    limited.set_max_redirects(3);
    Response loop = limited.get(make_url("/redirect-loop"));
    CHECK(loop.has_error());
    CHECK(loop.error_code != 0);
    CHECK(loop.error_message.find("redirect") != std::string::npos);
}

static void test_cookies(void)
{
    Session session;
    Response set = session.get(make_url("/cookies/set?a=1&b=2"));
    CHECK(set.status_code == 200);
    CHECK(session.cookie_count() == 2);
    CHECK_BODY(set, "cookie=a=1; b=2");

    Response again = session.get(make_url("/cookies"));
    CHECK_BODY(again, "cookie=a=1; b=2");
    session.clear_cookies();
    CHECK(session.cookie_count() == 0);
    Response cleared = session.get(make_url("/cookies"));
    CHECK(cleared.content == "cookie=");

    Session attributes;
    Response attributes_set = attributes.get(make_url("/cookies/attributes"));
    CHECK(attributes_set.status_code == 200);
    CHECK(attributes.cookie_count() == 3);
    CHECK_BODY(attributes_set, "cookie=a=1");
    CHECK_NO_BODY(attributes_set, "b=2");
    CHECK_NO_BODY(attributes_set, "secure=1");
    CHECK_NO_BODY(attributes_set, "gone=1");

    Session case_cookies;
    Response case_set = case_cookies.get(make_url("/cookies/case"));
    CHECK(case_set.status_code == 200);
    CHECK(case_cookies.cookie_count() == 2);
    CHECK_BODY(case_set, "Case=1");
    CHECK_BODY(case_set, "case=2");
}

static void test_responses(void)
{
    Session session;

    Response chunked = session.get(make_url("/chunked"));
    CHECK(chunked.status_code == 200);
    CHECK(chunked.content == "Wikipedia!");
    CHECK(chunked.headers.get("x-chunked") == "yes");

    Response empty = session.get(make_url("/empty"));
    CHECK(empty.status_code == 200);
    CHECK(empty.content.empty());

    Response no_content = session.get(make_url("/no-content"));
    CHECK(no_content.status_code == 204);
    CHECK(no_content.content.empty());

    Response head = session.head(make_url("/head"));
    CHECK(head.status_code == 200);
    CHECK(head.content.empty());

    Response utf8 = session.get(make_url("/utf8"));
    CHECK(utf8.status_code == 200);
    CHECK(utf8.content.find("cpprequests") != std::string::npos);

    Response reason = session.get(make_url("/reason"));
    CHECK(reason.status_code == 418);
    CHECK(reason.reason == "I am a teapot");
    CHECK_THROWS_AS(reason.raise_for_status(), HTTPError);

    Response missing = session.get(make_url("/status/404"));
    CHECK(missing.status_code == 404);
    CHECK(!missing.ok());
    CHECK_THROWS_AS(missing.raise_for_status(), HTTPError);

    Response multi = session.get(make_url("/multi-header"));
    CHECK(multi.headers.get("x-multi") == "one");
    CHECK(multi.headers.count("X-Multi") == 2);
    CHECK(multi.header_values("x-multi").size() == 2);
    CHECK(multi.header_values("x-multi")[1] == "two");
    CHECK(multi.headers.get_all("set-cookie").size() == 2);

    Response continued = session.get(make_url("/continue"));
    CHECK(continued.status_code == 200);
    CHECK(continued.content == "after-continue");

    Response malformed = session.get(make_url("/bad-length"));
    CHECK(malformed.has_error());

    Session limited;
    limited.set_max_body_size(10);
    Response too_large = limited.get(make_url("/large?n=1000"));
    CHECK(too_large.has_error());
    CHECK(too_large.error_code != 0);

    Request slow("GET", make_url("/slow?ms=400"));
    slow.set_timeout(100, 100);
    Response timed_out = session.send(slow);
    CHECK(timed_out.has_error());
    CHECK(timed_out.error_code != 0);

    Request bad_verify("GET", make_url("/echo"));
    CHECK_THROWS_AS(bad_verify.set_timeout(-2, 1), Error);
    CHECK_THROWS_AS(bad_verify.set_max_redirects(-2), Error);
}

static void test_json_response(void)
{
    Session session;
    Response response = session.get(make_url("/json"));
    CHECK(response.status_code == 200);
    Json parsed = response.json();
    CHECK(parsed["ok"].get<bool>());
    CHECK(parsed["number"].get<int>() == 7);
    CHECK(parsed["items"].size() == 3);

    Json fallback = response.json_or(Json::object({{"fallback", true}}));
    CHECK(fallback == parsed);

    Response not_json = session.get(make_url("/echo"));
    CHECK(not_json.json_or(Json(nullptr)).is_null());
    CHECK_THROWS_AS(not_json.json(), JsonError);
}

static void test_reuse_and_free_functions(void)
{
    Session session;
    Request request("GET", make_url("/echo"));
    request.add_query("round", "1");
    Response first = session.send(request);
    CHECK(first.status_code == 200);
    request.add_query("round", "2");
    Response second = session.send(request);
    CHECK(second.status_code == 200);
    CHECK_BODY(second, "query=round=1&round=2");

    Response one = cpprequests::get(make_url("/echo"), {{"x", "1"}});
    CHECK(one.status_code == 200);
    CHECK_BODY(one, "query=x=1");

    Headers headers{{"X-Test", "one-shot"}};
    Response with_headers =
        cpprequests::get(make_url("/echo"), {{"y", "2"}}, headers);
    CHECK_BODY(with_headers, "header:x-test=one-shot");

    Response head = cpprequests::head(make_url("/head"));
    CHECK(head.status_code == 200);
    Response deleted = cpprequests::del(make_url("/echo"));
    CHECK_BODY(deleted, "method=DELETE");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s BASE_URL\n", argv[0]);
        return 2;
    }
    g_base = argv[1];

    test_json();
    test_unit();
    test_get();
    test_post();
    test_auth_and_redirects();
    test_cookies();
    test_responses();
    test_json_response();
    test_reuse_and_free_functions();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
