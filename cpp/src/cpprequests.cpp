/*
 * cpprequests.cpp - C++17 facade over the crequests C core.
 *
 * Copyright (c) 2026 cpprequests contributors
 * SPDX-License-Identifier: MIT
 */
#include "cpprequests.hpp"

#include "crequests.h"

#include <cctype>
#include <memory>
#include <new>
#include <utility>

namespace cpprequests {
namespace {

std::string upper_ascii(std::string text)
{
    for (char &ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= 'a' && byte <= 'z') {
            ch = static_cast<char>(byte - 'a' + 'A');
        }
    }
    return text;
}

void check(cr_error error, const char *action, std::string detail = {})
{
    if (error == CR_OK) {
        return;
    }
    std::string message(action);
    message += ": ";
    message += cr_strerror(error);
    if (!detail.empty()) {
        message += " (";
        message += detail;
        message += ")";
    }
    throw Error(static_cast<int>(error), message);
}

} // namespace

Error::Error(int code, const std::string &message)
    : std::runtime_error(message), code_(code)
{
}

Error::~Error() = default;

HTTPError::HTTPError(int status_code, const std::string &url)
    : Error(-1, "HTTP " + std::to_string(status_code) +
                    (url.empty() ? std::string() : " for " + url)),
      status_code_(status_code),
      url_(url)
{
}

HTTPError::~HTTPError() = default;

const char *version() noexcept
{
    return CPPREQUESTS_VERSION_STRING;
}

const char *strerror(int code) noexcept
{
    return cr_strerror(static_cast<cr_error>(code));
}

/* ------------------------------------------------------------------------- */
/* Request                                                                   */
/* ------------------------------------------------------------------------- */

struct Request::Impl {
    cr_request *handle = nullptr;
    std::string method;
    std::string url;

    ~Impl()
    {
        if (handle != nullptr) {
            cr_request_free(handle);
        }
    }
};

Request::Request(std::string method, std::string url) : impl_(new Impl)
{
    impl_->method = upper_ascii(std::move(method));
    impl_->url = std::move(url);
    impl_->handle =
        cr_request_new(impl_->method.c_str(), impl_->url.c_str());
    if (impl_->handle == nullptr) {
        const std::string url_copy = impl_->url;
        delete impl_;
        impl_ = nullptr;
        throw Error(CR_ERR_ARGUMENT,
                    "invalid method or URL: " + url_copy);
    }
}

Request::~Request()
{
    delete impl_;
}

Request::Request(Request &&other) noexcept : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

Request &Request::operator=(Request &&other) noexcept
{
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

const std::string &Request::method() const noexcept
{
    return impl_->method;
}

const std::string &Request::url() const noexcept
{
    return impl_->url;
}

Request &Request::set_method(std::string method)
{
    const std::string upper = upper_ascii(std::move(method));
    check(cr_request_set_method(impl_->handle, upper.c_str()),
          "set request method");
    impl_->method = upper;
    return *this;
}

Request &Request::set_url(std::string url)
{
    check(cr_request_set_url(impl_->handle, url.c_str()), "set request URL",
          url);
    impl_->url = std::move(url);
    return *this;
}

Request &Request::set_header(std::string name, std::string value)
{
    check(cr_request_set_header(impl_->handle, name.c_str(), value.c_str()),
          "set request header", name);
    return *this;
}

Request &Request::remove_header(std::string_view name)
{
    check(cr_request_set_header(impl_->handle,
                                std::string(name).c_str(), nullptr),
          "remove request header", std::string(name));
    return *this;
}

Request &Request::set_headers(const Headers &headers)
{
    for (const auto &entry : headers) {
        set_header(entry.name, entry.value);
    }
    return *this;
}

Request &Request::add_query(std::string name, std::string value)
{
    check(cr_request_add_query(impl_->handle, name.c_str(), value.c_str()),
          "add query parameter", name);
    return *this;
}

Request &Request::add_query(std::string name)
{
    check(cr_request_add_query(impl_->handle, name.c_str(), nullptr),
          "add query parameter", name);
    return *this;
}

Request &Request::add_form(std::string name, std::string value)
{
    check(cr_request_add_form(impl_->handle, name.c_str(), value.c_str()),
          "add form field", name);
    return *this;
}

Request &Request::add_form(const Form &form)
{
    for (const auto &item : form) {
        add_form(item.first, item.second);
    }
    return *this;
}

Request &Request::add_file(File file)
{
    check(cr_request_add_file(impl_->handle, file.field.c_str(),
                              file.path.c_str(),
                              file.content_type.empty()
                                  ? nullptr
                                  : file.content_type.c_str()),
          "add file", file.field);
    return *this;
}

Request &Request::add_file(std::string field, std::string path,
                           std::string content_type)
{
    return add_file(File(std::move(field), std::move(path),
                         std::move(content_type)));
}

Request &Request::set_body(std::string body)
{
    check(cr_request_set_body(impl_->handle, body.data(), body.size()),
          "set request body");
    return *this;
}

Request &Request::set_body(const void *data, std::size_t size)
{
    if (data == nullptr && size != 0) {
        throw Error(CR_ERR_ARGUMENT,
                    "set request body: data is NULL but size is non-zero");
    }
    check(cr_request_set_body(impl_->handle, data, size),
          "set request body");
    return *this;
}

Request &Request::set_body_text(std::string text, std::string content_type)
{
    check(cr_request_set_body_text(impl_->handle, text.c_str(),
                                   content_type.empty()
                                       ? nullptr
                                       : content_type.c_str()),
          "set request text body");
    return *this;
}

Request &Request::set_json(const Json &body)
{
    return set_json_text(body.dump());
}

Request &Request::set_json_text(std::string json)
{
    check(cr_request_set_json(impl_->handle, json.c_str()),
          "set JSON body");
    return *this;
}

Request &Request::basic_auth(std::string username, std::string password)
{
    check(cr_request_set_basic_auth(impl_->handle, username.c_str(),
                                    password.c_str()),
          "set basic authentication");
    return *this;
}

Request &Request::bearer_token(std::string token)
{
    check(cr_request_set_bearer_token(impl_->handle, token.c_str()),
          "set bearer token");
    return *this;
}

Request &Request::set_timeout(int connect_timeout_ms, int read_timeout_ms)
{
    check(cr_request_set_timeout(impl_->handle, connect_timeout_ms,
                                 read_timeout_ms),
          "set request timeout");
    return *this;
}

Request &Request::set_verify(bool verify)
{
    check(cr_request_set_verify(impl_->handle, verify ? 1 : 0),
          "set request verify");
    return *this;
}

Request &Request::set_follow_redirects(bool follow)
{
    check(cr_request_set_follow_redirects(impl_->handle, follow ? 1 : 0),
          "set request redirect policy");
    return *this;
}

Request &Request::set_max_redirects(int max_redirects)
{
    check(cr_request_set_max_redirects(impl_->handle, max_redirects),
          "set request max redirects");
    return *this;
}

Request &Request::set_user_agent(std::string user_agent)
{
    check(cr_request_set_user_agent(impl_->handle, user_agent.c_str()),
          "set request user agent");
    return *this;
}

/* ------------------------------------------------------------------------- */
/* Session                                                                   */
/* ------------------------------------------------------------------------- */

struct Session::Impl {
    cr_session *handle = nullptr;

    ~Impl()
    {
        if (handle != nullptr) {
            cr_session_free(handle);
        }
    }
};

Session::Session() : impl_(new Impl)
{
    impl_->handle = cr_session_new();
    if (impl_->handle == nullptr) {
        delete impl_;
        impl_ = nullptr;
        throw std::bad_alloc();
    }
}

Session::~Session()
{
    delete impl_;
}

Session::Session(Session &&other) noexcept : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

Session &Session::operator=(Session &&other) noexcept
{
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

Session &Session::set_header(std::string name, std::string value)
{
    check(cr_session_set_header(impl_->handle, name.c_str(), value.c_str()),
          "set session header", name);
    return *this;
}

Session &Session::remove_header(std::string_view name)
{
    check(cr_session_set_header(impl_->handle,
                                std::string(name).c_str(), nullptr),
          "remove session header", std::string(name));
    return *this;
}

Session &Session::set_headers(const Headers &headers)
{
    for (const auto &entry : headers) {
        set_header(entry.name, entry.value);
    }
    return *this;
}

Session &Session::set_timeout(int connect_timeout_ms, int read_timeout_ms)
{
    check(cr_session_set_timeout(impl_->handle, connect_timeout_ms,
                                 read_timeout_ms),
          "set session timeout");
    return *this;
}

Session &Session::set_verify(bool verify)
{
    check(cr_session_set_verify(impl_->handle, verify ? 1 : 0),
          "set session verify");
    return *this;
}

Session &Session::set_follow_redirects(bool follow)
{
    check(cr_session_set_follow_redirects(impl_->handle, follow ? 1 : 0),
          "set session redirect policy");
    return *this;
}

Session &Session::set_max_redirects(int max_redirects)
{
    check(cr_session_set_max_redirects(impl_->handle, max_redirects),
          "set session max redirects");
    return *this;
}

Session &Session::set_user_agent(std::string user_agent)
{
    check(cr_session_set_user_agent(impl_->handle, user_agent.c_str()),
          "set session user agent");
    return *this;
}

Session &Session::set_max_body_size(std::size_t max_bytes)
{
    check(cr_session_set_max_body_size(impl_->handle, max_bytes),
          "set session max body size");
    return *this;
}

void Session::clear_cookies()
{
    cr_session_clear_cookies(impl_->handle);
}

std::size_t Session::cookie_count() const noexcept
{
    return cr_session_cookie_count(impl_->handle);
}

Response Session::send(const Request &request)
{
    if (impl_ == nullptr || impl_->handle == nullptr ||
        request.impl_ == nullptr || request.impl_->handle == nullptr) {
        throw Error(CR_ERR_ARGUMENT, "send: session or request is invalid");
    }

    cr_response *raw =
        cr_session_send(impl_->handle, request.impl_->handle);
    if (raw == nullptr) {
        throw std::bad_alloc();
    }
    std::unique_ptr<cr_response, void (*)(cr_response *)> guard(
        raw, &cr_response_free);

    Response response;
    response.status_code = cr_response_status(raw);
    response.http_version = cr_response_http_version(raw);
    response.redirect_count = cr_response_redirect_count(raw);
    response.elapsed_ms = cr_response_elapsed_ms(raw);
    response.reason = cr_response_reason(raw);
    response.url = cr_response_url(raw);

    const std::size_t header_count = cr_response_header_count(raw);
    response.headers = Headers();
    for (std::size_t i = 0; i < header_count; ++i) {
        const char *name = cr_response_header_name(raw, i);
        const char *value = cr_response_header_value(raw, i);
        if (name != nullptr && value != nullptr) {
            response.headers.add(name, value);
        }
    }

    std::size_t body_size = 0;
    const void *body = cr_response_body(raw, &body_size);
    if (body != nullptr && body_size != 0) {
        response.content.assign(static_cast<const char *>(body), body_size);
    }

    response.error_code = static_cast<int>(cr_response_error(raw));
    response.error_message = cr_response_error_message(raw);
    if (response.error_code != 0 && response.error_message.empty()) {
        response.error_message = cr_strerror(
            static_cast<cr_error>(response.error_code));
    }
    return response;
}

Response Session::send_with_params(std::string method, std::string url,
                                   const Query &params,
                                   const Headers *headers)
{
    Request request(std::move(method), std::move(url));
    for (const auto &item : params) {
        request.add_query(item.first, item.second);
    }
    if (headers != nullptr) {
        request.set_headers(*headers);
    }
    return send(request);
}

Response Session::request(std::string method, std::string url)
{
    Request request(std::move(method), std::move(url));
    return send(request);
}

Response Session::get(std::string url)
{
    return request("GET", std::move(url));
}

Response Session::get(std::string url, const Query &params)
{
    return send_with_params("GET", std::move(url), params, nullptr);
}

Response Session::get(std::string url, const Query &params,
                      const Headers &headers)
{
    return send_with_params("GET", std::move(url), params, &headers);
}

Response Session::head(std::string url)
{
    return request("HEAD", std::move(url));
}

Response Session::del(std::string url)
{
    return request("DELETE", std::move(url));
}

Response Session::post(std::string url, std::string body,
                       std::string content_type)
{
    Request request("POST", std::move(url));
    request.set_body(std::move(body));
    if (!content_type.empty()) {
        request.set_header("Content-Type", std::move(content_type));
    }
    return send(request);
}

Response Session::post_json(std::string url, const Json &body)
{
    Request request("POST", std::move(url));
    request.set_json(body);
    return send(request);
}

Response Session::post_form(std::string url, const Form &form)
{
    Request request("POST", std::move(url));
    request.add_form(form);
    return send(request);
}

Response Session::put(std::string url, std::string body,
                      std::string content_type)
{
    Request request("PUT", std::move(url));
    request.set_body(std::move(body));
    if (!content_type.empty()) {
        request.set_header("Content-Type", std::move(content_type));
    }
    return send(request);
}

Response Session::patch(std::string url, std::string body,
                        std::string content_type)
{
    Request request("PATCH", std::move(url));
    request.set_body(std::move(body));
    if (!content_type.empty()) {
        request.set_header("Content-Type", std::move(content_type));
    }
    return send(request);
}

/* ------------------------------------------------------------------------- */
/* One-shot helpers                                                          */
/* ------------------------------------------------------------------------- */

Response get(std::string url)
{
    Session session;
    return session.get(std::move(url));
}

Response get(std::string url, const Query &params)
{
    Session session;
    return session.get(std::move(url), params);
}

Response get(std::string url, const Query &params, const Headers &headers)
{
    Session session;
    return session.get(std::move(url), params, headers);
}

Response head(std::string url)
{
    Session session;
    return session.head(std::move(url));
}

Response del(std::string url)
{
    Session session;
    return session.del(std::move(url));
}

Response post(std::string url, std::string body, std::string content_type)
{
    Session session;
    return session.post(std::move(url), std::move(body),
                        std::move(content_type));
}

Response post_json(std::string url, const Json &body)
{
    Session session;
    return session.post_json(std::move(url), body);
}

Response post_form(std::string url, const Form &form)
{
    Session session;
    return session.post_form(std::move(url), form);
}

} // namespace cpprequests
