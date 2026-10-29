#include "cpprequests.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    const std::string base = argc > 1 ? argv[1] : "http://example.com";

    cpprequests::Session session;
    session.set_header("User-Agent", "cpprequests-example/1.0");
    session.set_timeout(5000, 20000);

    cpprequests::Response first = session.get(base + "/");
    std::printf("first: %d %s\n", first.status_code, first.reason.c_str());

    cpprequests::Response second = session.get(base + "/");
    std::printf("second: %d, cookies stored: %zu\n", second.status_code,
                session.cookie_count());

    session.clear_cookies();
    std::printf("after clear: %zu cookies\n", session.cookie_count());
    return 0;
}
