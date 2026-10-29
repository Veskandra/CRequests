/*
 * cpprequests.hpp - a requests-like HTTP/HTTPS client for C++17.
 *
 * Copyright (c) 2026 cpprequests contributors
 * SPDX-License-Identifier: MIT
 *
 * The public API is C++-native.  The transport/HTTP core is the mature C
 * implementation in ../src, so behaviour is intentionally identical to
 * crequests; the only added layer is C++ ownership and JSON handling.
 */
#ifndef CPPREQUESTS_HPP
#define CPPREQUESTS_HPP

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cpprequests/json.hpp"

#if defined(_WIN32) && defined(CPPR_SHARED)
#  if defined(CPPR_BUILD)
#    define CPPR_API __declspec(dllexport)
#  else
#    define CPPR_API __declspec(dllimport)
#  endif
#else
#  define CPPR_API
#endif

#define CPPREQUESTS_VERSION_MAJOR 1
#define CPPREQUESTS_VERSION_MINOR 0
#define CPPREQUESTS_VERSION_PATCH 0
#define CPPREQUESTS_VERSION_STRING "1.0.0"

namespace cpprequests {

using Query = std::vector<std::pair<std::string, std::string>>;
using Form = std::vector<std::pair<std::string, std::string>>;

/* Transport/protocol error.  code matches crequests' cr_error values. */
class CPPR_API Error : public std::runtime_error {
public:
    Error(int code, const std::string &message);
    ~Error() override;

    int code() const noexcept { return code_; }

private:
    int code_;
};

/* Thrown by Response::raise_for_status() for 4xx/5xx responses. */
class CPPR_API HTTPError : public Error {
public:
    HTTPError(int status_code, const std::string &url);
    ~HTTPError() override;

    int status_code() const noexcept { return status_code_; }
    const std::string &url() const noexcept { return url_; }

private:
    int status_code_;
    std::string url_;
};

/* Case-insensitive, order-preserving header collection. */
class Headers {
public:
    struct Entry {
        std::string name;
        std::string value;
    };

    Headers() = default;

    Headers(std::initializer_list<std::pair<std::string, std::string>> values)
    {
        for (const auto &item : values) {
            add(item.first, item.second);
        }
    }

    Headers &add(std::string name, std::string value)
    {
        entries_.push_back(Entry{std::move(name), std::move(value)});
        return *this;
    }

    /* Replace every existing header with this name (case-insensitive). */
    Headers &set(std::string name, std::string value)
    {
        remove(name);
        return add(std::move(name), std::move(value));
    }

    bool remove(std::string_view name)
    {
        const auto old_size = entries_.size();
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                           [name](const Entry &entry) {
                               return iequals(entry.name, name);
                           }),
            entries_.end());
        return entries_.size() != old_size;
    }

    void clear() noexcept { entries_.clear(); }

    bool contains(std::string_view name) const noexcept
    {
        return find(name) != nullptr;
    }

    std::size_t count(std::string_view name) const noexcept
    {
        std::size_t result = 0;
        for (const auto &entry : entries_) {
            if (iequals(entry.name, name)) {
                ++result;
            }
        }
        return result;
    }

    const std::string *find(std::string_view name) const noexcept
    {
        for (const auto &entry : entries_) {
            if (iequals(entry.name, name)) {
                return &entry.value;
            }
        }
        return nullptr;
    }

    std::string get(std::string_view name, std::string fallback = {}) const
    {
        const std::string *value = find(name);
        return value != nullptr ? *value : fallback;
    }

    std::string operator[](std::string_view name) const { return get(name); }

    std::vector<std::string> get_all(std::string_view name) const
    {
        std::vector<std::string> result;
        for (const auto &entry : entries_) {
            if (iequals(entry.name, name)) {
                result.push_back(entry.value);
            }
        }
        return result;
    }

    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

    const std::vector<Entry> &entries() const noexcept { return entries_; }
    std::vector<Entry>::const_iterator begin() const noexcept
    {
        return entries_.begin();
    }
    std::vector<Entry>::const_iterator end() const noexcept
    {
        return entries_.end();
    }

private:
    static bool iequals(std::string_view left, std::string_view right) noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left.size(); ++i) {
            unsigned char a = static_cast<unsigned char>(left[i]);
            unsigned char b = static_cast<unsigned char>(right[i]);
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<unsigned char>(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = static_cast<unsigned char>(b - 'A' + 'a');
            }
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    std::vector<Entry> entries_;
};

struct File {
    std::string field;
    std::string path;
    std::string content_type;

    File() = default;
    File(std::string field_name, std::string file_path,
         std::string type = {})
        : field(std::move(field_name)),
          path(std::move(file_path)),
          content_type(std::move(type))
    {
    }
};

/* A received response.  This is a plain value type; it owns its bytes and can
 * be copied or stored after the Session/Request that produced it is gone. */
class Response {
public:
    int status_code = 0;
    int http_version = 0; /* 10, 11, or 0 when no response was received */
    int redirect_count = 0;
    long long elapsed_ms = 0;
    int error_code = 0;
    std::string reason;
    std::string url;
    std::string content;
    std::string error_message;
    Headers headers;

    /* Same success rule as crequests and requests.Response.ok: a transport
     * error fails, 2xx/3xx succeed. */
    bool ok() const noexcept
    {
        return error_code == 0 && status_code >= 200 && status_code < 400;
    }

    bool has_error() const noexcept { return error_code != 0; }
    explicit operator bool() const noexcept { return ok(); }

    const std::string &body() const noexcept { return content; }
    std::string text() const { return content; }

    std::string header(std::string_view name,
                       std::string fallback = {}) const
    {
        return headers.get(name, std::move(fallback));
    }

    std::vector<std::string> header_values(std::string_view name) const
    {
        return headers.get_all(name);
    }

    /* Parse the response body as JSON.  Throws JsonError when invalid. */
    Json json() const { return Json::parse(content); }

    Json json_or(Json fallback) const
    {
        try {
            return Json::parse(content);
        } catch (const JsonError &) {
            return fallback;
        }
    }

    /* Throw Error for a transport/protocol failure, or HTTPError for 4xx/5xx. */
    void raise_for_status() const
    {
        if (error_code != 0) {
            throw Error(error_code,
                        error_message.empty()
                            ? "request failed with error code " +
                                  std::to_string(error_code)
                            : error_message);
        }
        if (status_code >= 400) {
            throw HTTPError(status_code, url);
        }
    }
};

class CPPR_API Request {
public:
    Request(std::string method, std::string url);
    ~Request();

    Request(Request &&other) noexcept;
    Request &operator=(Request &&other) noexcept;
    Request(const Request &) = delete;
    Request &operator=(const Request &) = delete;

    const std::string &method() const noexcept;
    const std::string &url() const noexcept;

    Request &set_method(std::string method);
    Request &set_url(std::string url);

    Request &set_header(std::string name, std::string value);
    Request &remove_header(std::string_view name);
    Request &set_headers(const Headers &headers);

    /* A value-less query parameter produces a bare key ("?flag"). */
    Request &add_query(std::string name, std::string value);
    Request &add_query(std::string name);

    Request &add_form(std::string name, std::string value);
    Request &add_form(const Form &form);
    Request &add_file(File file);
    Request &add_file(std::string field, std::string path,
                      std::string content_type = {});

    Request &set_body(std::string body);
    Request &set_body(const void *data, std::size_t size);
    Request &set_body_text(std::string text, std::string content_type = {});
    Request &set_json(const Json &body);
    Request &set_json_text(std::string json);

    Request &basic_auth(std::string username, std::string password = {});
    Request &bearer_token(std::string token);

    /* Negative timeout means "inherit from the session". */
    Request &set_timeout(int connect_timeout_ms, int read_timeout_ms);
    Request &set_verify(bool verify);
    Request &set_follow_redirects(bool follow);
    Request &set_max_redirects(int max_redirects);
    Request &set_user_agent(std::string user_agent);

private:
    struct Impl;
    Impl *impl_;

    friend class Session;
};

class CPPR_API Session {
public:
    Session();
    ~Session();

    Session(Session &&other) noexcept;
    Session &operator=(Session &&other) noexcept;
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    Session &set_header(std::string name, std::string value);
    Session &remove_header(std::string_view name);
    Session &set_headers(const Headers &headers);

    Session &set_timeout(int connect_timeout_ms, int read_timeout_ms);
    Session &set_verify(bool verify);
    Session &set_follow_redirects(bool follow);
    Session &set_max_redirects(int max_redirects);
    Session &set_user_agent(std::string user_agent);
    Session &set_max_body_size(std::size_t max_bytes);

    void clear_cookies();
    std::size_t cookie_count() const noexcept;

    /* Send a request without consuming it; the Request may be reused. */
    Response send(const Request &request);

    Response request(std::string method, std::string url);

    Response get(std::string url);
    Response get(std::string url, const Query &params);
    Response get(std::string url, const Query &params, const Headers &headers);

    Response head(std::string url);
    Response del(std::string url);

    Response post(std::string url, std::string body,
                  std::string content_type = {});
    Response post_json(std::string url, const Json &body);
    Response post_form(std::string url, const Form &form);

    Response put(std::string url, std::string body,
                 std::string content_type = {});
    Response patch(std::string url, std::string body,
                   std::string content_type = {});

private:
    struct Impl;
    Impl *impl_;

    Response send_with_params(std::string method, std::string url,
                              const Query &params, const Headers *headers);
};

CPPR_API const char *version() noexcept;
CPPR_API const char *strerror(int code) noexcept;

/* requests.get/post-style one-shot helpers.  They use a temporary session, so
 * cookies do not persist between calls. */
CPPR_API Response get(std::string url);
CPPR_API Response get(std::string url, const Query &params);
CPPR_API Response get(std::string url, const Query &params,
                      const Headers &headers);
CPPR_API Response head(std::string url);
CPPR_API Response del(std::string url);
CPPR_API Response post(std::string url, std::string body,
                       std::string content_type = {});
CPPR_API Response post_json(std::string url, const Json &body);
CPPR_API Response post_form(std::string url, const Form &form);

} // namespace cpprequests

#endif // CPPREQUESTS_HPP
