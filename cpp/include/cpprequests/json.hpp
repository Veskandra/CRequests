/*
 * json.hpp - a compact JSON value type for cpprequests.
 *
 * Copyright (c) 2026 cpprequests contributors
 * SPDX-License-Identifier: MIT
 *
 * The type is self-contained: no third-party JSON dependency is required.
 * Objects preserve insertion order, matching the usual Python dict/jq output
 * order, while object comparison is order-insensitive as required by JSON.
 */
#ifndef CPPREQUESTS_JSON_HPP
#define CPPREQUESTS_JSON_HPP

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cpprequests {

/* Thrown by Json::parse(), get<T>() and the mutating accessors. */
class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string &message, std::size_t offset = 0)
        : std::runtime_error(offset == 0
                                 ? message
                                 : message + " (at byte " +
                                       std::to_string(offset) + ")"),
          offset_(offset)
    {
    }

    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

class Json {
public:
    using array_type = std::vector<Json>;
    using object_item = std::pair<std::string, Json>;
    using object_type = std::vector<object_item>;

    enum class Type {
        Null,
        Boolean,
        Integer,
        Number,
        String,
        Array,
        Object
    };

    Json() noexcept : value_(nullptr) {}
    Json(std::nullptr_t) noexcept : value_(nullptr) {}
    Json(bool value) : value_(value) {}

    template <typename T,
              typename std::enable_if<std::is_integral<T>::value &&
                                          !std::is_same<T, bool>::value,
                                      int>::type = 0>
    Json(T value) : value_(static_cast<std::int64_t>(value))
    {
    }

    template <typename T,
              typename std::enable_if<std::is_floating_point<T>::value,
                                      int>::type = 0>
    Json(T value) : value_(static_cast<double>(value))
    {
    }

    Json(const char *value)
        : value_(value != nullptr ? storage_type(std::string(value))
                                  : storage_type(nullptr))
    {
    }

    Json(std::string value) : value_(std::move(value)) {}
    Json(std::string_view value) : value_(std::string(value)) {}
    Json(array_type value) : value_(std::move(value)) {}
    Json(object_type value) : value_(std::move(value)) {}
    Json(std::initializer_list<object_item> value) : value_(object_type(value)) {}

    static Json array() { return Json(array_type{}); }
    static Json array(std::initializer_list<Json> values)
    {
        return Json(array_type(values));
    }

    static Json object() { return Json(object_type{}); }
    static Json object(std::initializer_list<object_item> values)
    {
        return Json(object_type(values));
    }

    /* Parse a complete JSON document.  Throws JsonError on malformed input. */
    static Json parse(std::string_view text);

    /* Serialize.  indent < 0 means compact output; indent >= 0 pretty-prints
     * with that many spaces per nesting level. */
    std::string dump(int indent = -1) const;

    Type type() const noexcept
    {
        switch (value_.index()) {
        case 1: return Type::Boolean;
        case 2: return Type::Integer;
        case 3: return Type::Number;
        case 4: return Type::String;
        case 5: return Type::Array;
        case 6: return Type::Object;
        default: return Type::Null;
        }
    }

    const char *type_name() const noexcept
    {
        switch (type()) {
        case Type::Null: return "null";
        case Type::Boolean: return "boolean";
        case Type::Integer: return "integer";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Object: return "object";
        }
        return "unknown";
    }

    bool is_null() const noexcept { return value_.index() == 0; }
    bool is_boolean() const noexcept { return value_.index() == 1; }
    bool is_integer() const noexcept { return value_.index() == 2; }
    bool is_number() const noexcept
    {
        return value_.index() == 2 || value_.index() == 3;
    }
    bool is_string() const noexcept { return value_.index() == 4; }
    bool is_array() const noexcept { return value_.index() == 5; }
    bool is_object() const noexcept { return value_.index() == 6; }

    /* Array/object/string length.  Scalars have size() == 0. */
    std::size_t size() const noexcept
    {
        switch (value_.index()) {
        case 4: return std::get<4>(value_).size();
        case 5: return std::get<5>(value_).size();
        case 6: return std::get<6>(value_).size();
        default: return 0;
        }
    }

    bool empty() const noexcept { return size() == 0; }

    /* Object lookup.  Returns nullptr when absent or when this is not an
     * object. */
    Json *find(std::string_view key) noexcept
    {
        if (!is_object()) {
            return nullptr;
        }
        for (auto &item : std::get<object_type>(value_)) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }

    const Json *find(std::string_view key) const noexcept
    {
        if (!is_object()) {
            return nullptr;
        }
        for (const auto &item : std::get<object_type>(value_)) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }

    bool contains(std::string_view key) const noexcept
    {
        return find(key) != nullptr;
    }

    Json &operator[](std::size_t index)
    {
        if (is_null()) {
            value_ = array_type{};
        }
        array_type &items = get_array();
        if (index >= items.size()) {
            throw JsonError("array index out of range");
        }
        return items[index];
    }

    const Json &operator[](std::size_t index) const { return at(index); }

    Json &operator[](std::string_view key)
    {
        if (is_null()) {
            value_ = object_type{};
        }
        object_type &items = get_object();
        for (auto &item : items) {
            if (item.first == key) {
                return item.second;
            }
        }
        items.emplace_back(std::string(key), Json());
        return items.back().second;
    }

    const Json &operator[](std::string_view key) const { return at(key); }

    Json &at(std::size_t index)
    {
        array_type &items = get_array();
        if (index >= items.size()) {
            throw JsonError("array index out of range");
        }
        return items[index];
    }

    const Json &at(std::size_t index) const
    {
        const array_type &items = get_array();
        if (index >= items.size()) {
            throw JsonError("array index out of range");
        }
        return items[index];
    }

    Json &at(std::string_view key)
    {
        Json *value = find(key);
        if (value == nullptr) {
            throw JsonError("object key not found: " + std::string(key));
        }
        return *value;
    }

    const Json &at(std::string_view key) const
    {
        const Json *value = find(key);
        if (value == nullptr) {
            throw JsonError("object key not found: " + std::string(key));
        }
        return *value;
    }

    bool get_boolean() const
    {
        require(Type::Boolean);
        return std::get<bool>(value_);
    }

    std::int64_t get_integer() const
    {
        if (is_integer()) {
            return std::get<std::int64_t>(value_);
        }
        if (is_number()) {
            const double number = std::get<double>(value_);
            if (std::isfinite(number) && std::trunc(number) == number &&
                number >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
                number <= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                return static_cast<std::int64_t>(number);
            }
        }
        throw_type_error("integer");
    }

    double get_number() const
    {
        if (is_integer()) {
            return static_cast<double>(std::get<std::int64_t>(value_));
        }
        if (is_number()) {
            return std::get<double>(value_);
        }
        throw_type_error("number");
    }

    const std::string &get_string() const
    {
        require(Type::String);
        return std::get<std::string>(value_);
    }

    std::string &get_string()
    {
        require(Type::String);
        return std::get<std::string>(value_);
    }

    const array_type &get_array() const
    {
        require(Type::Array);
        return std::get<array_type>(value_);
    }

    array_type &get_array()
    {
        require(Type::Array);
        return std::get<array_type>(value_);
    }

    const object_type &get_object() const
    {
        require(Type::Object);
        return std::get<object_type>(value_);
    }

    object_type &get_object()
    {
        require(Type::Object);
        return std::get<object_type>(value_);
    }

    template <typename T>
    T get() const
    {
        using U = typename std::decay<T>::type;

        if constexpr (std::is_same<U, Json>::value) {
            return *this;
        } else if constexpr (std::is_same<U, std::string>::value) {
            return get_string();
        } else if constexpr (std::is_same<U, bool>::value) {
            return get_boolean();
        } else if constexpr (std::is_integral<U>::value) {
            const std::int64_t value = get_integer();
            if constexpr (std::is_unsigned<U>::value) {
                if (value < 0 ||
                    static_cast<std::uint64_t>(value) >
                        static_cast<std::uint64_t>(std::numeric_limits<U>::max())) {
                    throw JsonError("integer value is out of range");
                }
            } else {
                if (value < static_cast<std::int64_t>(std::numeric_limits<U>::min()) ||
                    value > static_cast<std::int64_t>(std::numeric_limits<U>::max())) {
                    throw JsonError("integer value is out of range");
                }
            }
            return static_cast<U>(value);
        } else if constexpr (std::is_floating_point<U>::value) {
            return static_cast<U>(get_number());
        } else {
            static_assert(dependent_false<U>::value,
                          "Json::get<T>() supports bool, integer types, "
                          "floating-point types, std::string and Json");
        }
    }

    template <typename T>
    T value(std::string_view key, T fallback) const
    {
        const Json *item = find(key);
        if (item == nullptr || item->is_null()) {
            return fallback;
        }
        try {
            return item->template get<T>();
        } catch (const JsonError &) {
            return fallback;
        }
    }

    void push_back(Json value)
    {
        if (is_null()) {
            value_ = array_type{};
        }
        get_array().push_back(std::move(value));
    }

    bool erase(std::string_view key)
    {
        if (!is_object()) {
            return false;
        }
        auto &items = std::get<object_type>(value_);
        auto it = std::find_if(items.begin(), items.end(),
                               [key](const object_item &item) {
                                   return item.first == key;
                               });
        if (it == items.end()) {
            return false;
        }
        items.erase(it);
        return true;
    }

    void erase(std::size_t index)
    {
        auto &items = get_array();
        if (index >= items.size()) {
            throw JsonError("array index out of range");
        }
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void clear()
    {
        switch (value_.index()) {
        case 4: std::get<4>(value_).clear(); break;
        case 5: std::get<5>(value_).clear(); break;
        case 6: std::get<6>(value_).clear(); break;
        default: value_ = nullptr; break;
        }
    }

    bool operator==(const Json &other) const
    {
        if (is_number() && other.is_number()) {
            if (is_integer() && other.is_integer()) {
                return get_integer() == other.get_integer();
            }
            return get_number() == other.get_number();
        }
        if (value_.index() != other.value_.index()) {
            return false;
        }
        if (is_object()) {
            const object_type &left = get_object();
            const object_type &right = other.get_object();
            if (left.size() != right.size()) {
                return false;
            }
            for (const auto &item : left) {
                const Json *found = other.find(item.first);
                if (found == nullptr || !(item.second == *found)) {
                    return false;
                }
            }
            return true;
        }
        return value_ == other.value_;
    }

    bool operator!=(const Json &other) const { return !(*this == other); }

    void swap(Json &other) noexcept { value_.swap(other.value_); }

private:
    using storage_type =
        std::variant<std::nullptr_t, bool, std::int64_t, double, std::string,
                     array_type, object_type>;

    template <typename>
    struct dependent_false {
        static constexpr bool value = false;
    };

    storage_type value_;

    void require(Type expected) const
    {
        if (type() != expected) {
            throw_type_error(type_name_of(expected));
        }
    }

    static const char *type_name_of(Type type) noexcept
    {
        switch (type) {
        case Type::Null: return "null";
        case Type::Boolean: return "boolean";
        case Type::Integer: return "integer";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Object: return "object";
        }
        return "unknown";
    }

    [[noreturn]] void throw_type_error(const char *expected) const
    {
        throw JsonError(std::string("expected ") + expected + ", got " +
                        type_name());
    }

    void dump_to(std::string &out, int indent, int depth) const;
};

inline void swap(Json &left, Json &right) noexcept { left.swap(right); }

namespace json_detail {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text), position_(0) {}

    Json parse_document()
    {
        if (text_.size() >= 3 &&
            static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            position_ = 3;
        }
        skip_whitespace();
        if (position_ >= text_.size()) {
            fail("expected a JSON value");
        }
        Json result = parse_value();
        skip_whitespace();
        if (position_ != text_.size()) {
            fail("trailing characters after JSON value");
        }
        return result;
    }

private:
    static constexpr int kMaxDepth = 512;

    std::string_view text_;
    std::size_t position_;
    int depth_ = 0;

    [[noreturn]] void fail(const std::string &message) const
    {
        throw JsonError(message, position_);
    }

    bool at_end() const noexcept { return position_ >= text_.size(); }

    char peek() const
    {
        if (at_end()) {
            fail("unexpected end of input");
        }
        return text_[position_];
    }

    bool consume(char expected) noexcept
    {
        if (!at_end() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected)
    {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    void skip_whitespace() noexcept
    {
        while (!at_end()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                break;
            }
            ++position_;
        }
    }

    Json parse_value()
    {
        if (at_end()) {
            fail("expected a JSON value");
        }
        switch (text_[position_]) {
        case 'n': return parse_literal("null", Json(nullptr));
        case 't': return parse_literal("true", Json(true));
        case 'f': return parse_literal("false", Json(false));
        case '"': return Json(parse_string());
        case '{': return parse_object();
        case '[': return parse_array();
        default:
            if (text_[position_] == '-' ||
                (text_[position_] >= '0' && text_[position_] <= '9')) {
                return parse_number();
            }
            fail("unexpected character while parsing a JSON value");
        }
    }

    Json parse_literal(std::string_view literal, Json value)
    {
        if (text_.substr(position_, literal.size()) != literal) {
            fail("invalid literal");
        }
        position_ += literal.size();
        return value;
    }

    void enter_container()
    {
        ++depth_;
        if (depth_ > kMaxDepth) {
            fail("JSON nesting is too deep");
        }
    }

    void leave_container() noexcept { --depth_; }

    Json parse_object()
    {
        enter_container();
        expect('{');
        skip_whitespace();

        Json::object_type object;
        if (consume('}')) {
            leave_container();
            return Json(std::move(object));
        }

        while (true) {
            skip_whitespace();
            if (at_end() || text_[position_] != '"') {
                fail("expected a string object key");
            }
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            skip_whitespace();
            Json value = parse_value();

            bool replaced = false;
            for (auto &item : object) {
                if (item.first == key) {
                    item.second = std::move(value);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                object.emplace_back(std::move(key), std::move(value));
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            expect(',');
        }

        leave_container();
        return Json(std::move(object));
    }

    Json parse_array()
    {
        enter_container();
        expect('[');
        skip_whitespace();

        Json::array_type array;
        if (consume(']')) {
            leave_container();
            return Json(std::move(array));
        }

        while (true) {
            skip_whitespace();
            array.push_back(parse_value());
            skip_whitespace();
            if (consume(']')) {
                break;
            }
            expect(',');
        }

        leave_container();
        return Json(std::move(array));
    }

    static int hex_value(char c) noexcept
    {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    std::uint32_t parse_hex4()
    {
        if (position_ + 4 > text_.size()) {
            fail("incomplete \\u escape");
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(text_[position_ + static_cast<std::size_t>(i)]);
            if (digit < 0) {
                fail("invalid \\u escape");
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        position_ += 4;
        return value;
    }

    static void append_utf8(std::string &out, std::uint32_t codepoint)
    {
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    std::string parse_string()
    {
        expect('"');
        std::string out;

        while (!at_end()) {
            const unsigned char c =
                static_cast<unsigned char>(text_[position_++]);

            if (c == '"') {
                return out;
            }
            if (c < 0x20) {
                fail("unescaped control character in string");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }

            if (at_end()) {
                fail("unterminated escape sequence");
            }
            const char escape = text_[position_++];
            switch (escape) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                std::uint32_t codepoint = parse_hex4();
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    if (position_ + 2 > text_.size() ||
                        text_[position_] != '\\' || text_[position_ + 1] != 'u') {
                        fail("high surrogate is not followed by a low surrogate");
                    }
                    position_ += 2;
                    const std::uint32_t low = parse_hex4();
                    if (low < 0xDC00 || low > 0xDFFF) {
                        fail("invalid low surrogate");
                    }
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                                (low - 0xDC00);
                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                    fail("unpaired low surrogate");
                }
                append_utf8(out, codepoint);
                break;
            }
            default:
                fail("invalid escape character");
            }
        }

        fail("unterminated string");
    }

    Json parse_number()
    {
        const std::size_t start = position_;
        bool has_fraction = false;
        bool has_exponent = false;

        if (consume('-')) {
            // sign consumed
        }
        if (at_end()) {
            fail("incomplete number");
        }
        if (text_[position_] == '0') {
            ++position_;
        } else if (text_[position_] >= '1' && text_[position_] <= '9') {
            while (!at_end() && text_[position_] >= '0' && text_[position_] <= '9') {
                ++position_;
            }
        } else {
            fail("invalid number");
        }

        if (!at_end() && text_[position_] == '.') {
            has_fraction = true;
            ++position_;
            if (at_end() || text_[position_] < '0' || text_[position_] > '9') {
                fail("expected a digit after the decimal point");
            }
            while (!at_end() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }

        if (!at_end() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            has_exponent = true;
            ++position_;
            if (!at_end() && (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            if (at_end() || text_[position_] < '0' || text_[position_] > '9') {
                fail("expected a digit in the exponent");
            }
            while (!at_end() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }

        const std::string_view token = text_.substr(start, position_ - start);
        if (!has_fraction && !has_exponent) {
            std::int64_t integer = 0;
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), integer);
            if (result.ec == std::errc() &&
                result.ptr == token.data() + token.size()) {
                return Json(integer);
            }
        }

        double number = 0.0;
        const auto result = std::from_chars(
            token.data(), token.data() + token.size(), number);
        if (result.ec != std::errc() || result.ptr != token.data() + token.size() ||
            !std::isfinite(number)) {
            fail("number is out of range");
        }
        return Json(number);
    }
};

inline void append_indent(std::string &out, int indent, int depth)
{
    if (indent > 0) {
        out.append(static_cast<std::size_t>(indent) *
                       static_cast<std::size_t>(depth),
                   ' ');
    }
}

inline void append_escaped(std::string &out, std::string_view value)
{
    static const char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out.push_back(hex[(c >> 4) & 0x0F]);
                out.push_back(hex[c & 0x0F]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

} // namespace json_detail

inline Json Json::parse(std::string_view text)
{
    json_detail::Parser parser(text);
    return parser.parse_document();
}

inline std::string Json::dump(int indent) const
{
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

inline void Json::dump_to(std::string &out, int indent, int depth) const
{
    switch (value_.index()) {
    case 1:
        out += std::get<bool>(value_) ? "true" : "false";
        return;
    case 2:
        out += std::to_string(std::get<std::int64_t>(value_));
        return;
    case 3: {
        const double number = std::get<double>(value_);
        if (!std::isfinite(number)) {
            throw JsonError("cannot serialize a non-finite number");
        }
        char buffer[64];
        const auto result =
            std::to_chars(buffer, buffer + sizeof(buffer), number);
        if (result.ec != std::errc()) {
            throw JsonError("cannot serialize number");
        }
        std::string text(buffer, result.ptr);
        if (text.find_first_of(".eE") == std::string::npos) {
            text += ".0";
        }
        out += text;
        return;
    }
    case 4:
        json_detail::append_escaped(out, std::get<std::string>(value_));
        return;
    case 5: {
        const array_type &items = std::get<array_type>(value_);
        out.push_back('[');
        if (items.empty()) {
            out.push_back(']');
            return;
        }
        const bool pretty = indent >= 0;
        if (pretty) {
            out.push_back('\n');
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (pretty) {
                json_detail::append_indent(out, indent, depth + 1);
            }
            items[i].dump_to(out, indent, depth + 1);
            if (i + 1 != items.size()) {
                out.push_back(',');
            }
            if (pretty) {
                out.push_back('\n');
            }
        }
        if (pretty) {
            json_detail::append_indent(out, indent, depth);
        }
        out.push_back(']');
        return;
    }
    case 6: {
        const object_type &items = std::get<object_type>(value_);
        out.push_back('{');
        if (items.empty()) {
            out.push_back('}');
            return;
        }
        const bool pretty = indent >= 0;
        if (pretty) {
            out.push_back('\n');
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (pretty) {
                json_detail::append_indent(out, indent, depth + 1);
            }
            json_detail::append_escaped(out, items[i].first);
            out.push_back(':');
            if (pretty) {
                out.push_back(' ');
            }
            items[i].second.dump_to(out, indent, depth + 1);
            if (i + 1 != items.size()) {
                out.push_back(',');
            }
            if (pretty) {
                out.push_back('\n');
            }
        }
        if (pretty) {
            json_detail::append_indent(out, indent, depth);
        }
        out.push_back('}');
        return;
    }
    default:
        out += "null";
        return;
    }
}

using json = Json;

} // namespace cpprequests

#endif // CPPREQUESTS_JSON_HPP
