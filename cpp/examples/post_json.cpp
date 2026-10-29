#include "cpprequests.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    const std::string url =
        argc > 1 ? argv[1] : "https://httpbin.org/post";

    cpprequests::Json payload = {
        {"name", "cpprequests"},
        {"language", "C++17"},
        {"features", cpprequests::Json::array(
                         {"session", "cookies", "redirects", "json"})},
        {"version", 1.0}};

    cpprequests::Session session;
    cpprequests::Response response = session.post_json(url, payload);
    if (response.has_error()) {
        std::fprintf(stderr, "%s\n", response.error_message.c_str());
        return 1;
    }

    std::printf("%d %s\n", response.status_code, response.reason.c_str());
    std::printf("%s\n", response.text().c_str());

    try {
        cpprequests::Json echoed = response.json();
        std::printf("server parsed our JSON object: %s\n",
                    echoed.contains("json") ? "yes" : "no");
    } catch (const cpprequests::JsonError &error) {
        std::fprintf(stderr, "response is not JSON: %s\n", error.what());
    }
    return response.ok() ? 0 : 2;
}
