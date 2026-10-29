#include "cpprequests.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    const std::string url = argc > 1 ? argv[1] : "http://example.com/";

    cpprequests::Session session;
    session.set_header("Accept", "text/html");
    session.set_timeout(10000, 30000);

    cpprequests::Response response = session.get(url);
    if (response.has_error()) {
        std::fprintf(stderr, "%s\n", response.error_message.c_str());
        return 1;
    }

    std::printf("%d %s\n", response.status_code, response.reason.c_str());
    std::printf("final URL: %s\n", response.url.c_str());
    std::printf("elapsed: %lld ms\n", response.elapsed_ms);
    std::printf("%s\n", response.text().c_str());
    return response.ok() ? 0 : 2;
}
