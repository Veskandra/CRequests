#include "cpprequests.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    if (std::string(cpprequests::version()) != "1.0.0") {
        std::fprintf(stderr, "unexpected version\n");
        return 1;
    }
    cpprequests::Json value = cpprequests::Json::parse("{\"ok\":true}");
    if (!value["ok"].get<bool>()) {
        std::fprintf(stderr, "json failure\n");
        return 1;
    }

    if (argc > 1) {
        cpprequests::Session session;
        cpprequests::Response response =
            session.get(std::string(argv[1]) + "/echo");
        if (response.has_error() || response.status_code != 200) {
            std::fprintf(stderr, "HTTP failure: %s\n",
                         response.error_message.c_str());
            return 1;
        }
        std::printf("dll smoke: %d %s\n", response.status_code,
                    response.reason.c_str());
    } else {
        std::printf("dll smoke: version/json ok\n");
    }
    return 0;
}
