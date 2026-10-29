#include "cpprequests.hpp"

#include <cstdio>
#include <string>

int main()
{
    const std::string text =
        "{\"name\":\"cpprequests\",\"stars\":5,\"tags\":[\"http\",\"json\"],"
        "\"unicode\":\"\\u4f60\\u597d\"}";

    cpprequests::Json value = cpprequests::Json::parse(text);
    std::printf("%s\n", value.dump(2).c_str());

    value["stars"] = 6;
    value["active"] = true;
    value["meta"] = cpprequests::Json::object(
        {{"author", "cpprequests contributors"}, {"year", 2026}});

    std::printf("compact: %s\n", value.dump().c_str());
    std::printf("name = %s, stars = %d, unicode = %s\n",
                value["name"].get<std::string>().c_str(),
                value["stars"].get<int>(),
                value["unicode"].get<std::string>().c_str());
    return 0;
}
