#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <functional>
#include <generator>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace json {

class parse_error : public std::exception {
public:
    explicit parse_error(std::string message)
        : message_ { std::format("JSON parse error: {}", message) }
    {
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return message_.c_str();
    }

private:
    const std::string message_;
};

// Lexer outputs a sequence of tokens - language's basic primitives, without verifying any grammatics.
class lexer {
public:
    enum class token_type {
        STRING,
        NUMBER,
        OBJECT_BEGIN, // {
        OBJECT_END, // }
        ARRAY_BEGIN, // [
        ARRAY_END, // ]
        COMMA, // ,
        COLON, // :
        NOOP, // special value to skip parsing step
        END_OF_INPUT,
    };

    using token_value = std::variant<int64_t, double, std::string>;

    struct token {
        token_type type;
        std::optional<token_value> value;
    };

    explicit lexer(std::istreambuf_iterator<char>&& input)
        : input_ { std::move(input) }
    {
    }

    // Get current token type without consuming it
    [[nodiscard]] token_type peek_type()
    {
        // Skip whitespace characters
        while (input_ != end_ && std::isspace(*input_)) {
            ++input_;
        }

        if (input_ == end_) {
            return token_type::END_OF_INPUT;
        }

        switch (*input_) {
        case '{':
            return token_type::OBJECT_BEGIN;
        case '}':
            return token_type::OBJECT_END;
        case '[':
            return token_type::ARRAY_BEGIN;
        case ']':
            return token_type::ARRAY_END;
        case ',':
            return token_type::COMMA;
        case ':':
            return token_type::COLON;
        case '"':
            return token_type::STRING;
        default:
            if (std::isdigit(*input_) || *input_ == '-') {
                return token_type::NUMBER;
            }
            throw parse_error { std::format("Unexpected character: {}", *input_) };
        }
    }

    // Consume current token
    [[nodiscard]] token next_token()
    {
        token_type type = peek_type();

        switch (type) {
        case token_type::OBJECT_BEGIN:
        case token_type::OBJECT_END:
        case token_type::ARRAY_BEGIN:
        case token_type::ARRAY_END:
        case token_type::COMMA:
        case token_type::COLON:
        case token_type::NOOP:
        case token_type::END_OF_INPUT:
            if (type != token_type::END_OF_INPUT) {
                ++input_;
            }
            return { type, std::nullopt };
        case token_type::STRING:
            return { type, parse_string() };
        case token_type::NUMBER:
            return { type, parse_number() };
        }
        throw parse_error { "Unexpected token type" };
    }

    // Consume current token only if it is of expected type
    [[nodiscard]] std::expected<token, token_type> try_consume_token(token_type type)
    {
        if (token_type::NOOP == type) {
            return token { token_type::NOOP, std::nullopt };
        }
        if (auto nextType = peek_type(); nextType != type) {
            return std::unexpected(nextType);
        }
        return next_token();
    }

private:
    [[nodiscard]] token_value parse_string()
    {
        ++input_; // Skip opening quote

        std::string result;
        for (; input_ != end_ && *input_ != '"'; ++input_) {
            result.push_back(*input_);
        }
        if (input_ == end_) {
            throw parse_error { "Unterminated string" };
        }

        ++input_; // Skip closing quote
        return result;
    }

    [[nodiscard]] token_value parse_number()
    {
        std::string number_str;
        if (*input_ == '-') {
            number_str.push_back(*input_);
            ++input_;
        }

        bool has_decimal = false;
        for (; input_ != end_ && (std::isdigit(*input_) || *input_ == '.'); ++input_) {
            if (*input_ == '.') {
                if (has_decimal) {
                    throw parse_error { "Multiple decimal points in number" };
                }
                has_decimal = true;
            }
            number_str.push_back(*input_);
        }

        return has_decimal ? std::stod(number_str) : std::stoll(number_str);
    }

    std::istreambuf_iterator<char> input_;
    const std::istreambuf_iterator<char> end_ {};
};

// Forward declarations of parsing support types
class object_stream;
class array_stream;
using json = std::variant<int64_t, double, std::string, object_stream, array_stream>;
template <typename Value, typename Parser>
class iterator;

// Special type for the end of range indication
template <typename Value, typename Parser>
class sentinel { };

// Streaming JSON object parser
class object_stream {
public:
    using value_type = std::pair<std::string, json>;
    using iterator = ::json::iterator<value_type, object_stream>;
    using sentinel = ::json::sentinel<value_type, object_stream>;

    explicit object_stream(std::shared_ptr<lexer>&& lex)
        : lexer_ { std::move(lex) }
    {
        // Consume opening brace
        if (!lexer_->try_consume_token(lexer::token_type::OBJECT_BEGIN)) {
            throw parse_error { "Expected '{'" };
        }
    }
    // Copying would make two parsers consuming the same stream
    object_stream(const object_stream&) = delete;
    object_stream& operator=(const object_stream&) = delete;
    object_stream(object_stream&&) = default;
    object_stream& operator=(object_stream&&) = default;

    [[nodiscard]] iterator begin();
    [[nodiscard]] sentinel end();

private:
    friend iterator;
    // Get next value checking languages grammatics.
    // Returns std::nullopt after last value.
    [[nodiscard]] std::optional<value_type> next_value();

    std::shared_ptr<lexer> lexer_;
    bool first_pair_ = true;
};

// Streaming JSON array parser
class array_stream {
public:
    using value_type = json;
    using iterator = ::json::iterator<value_type, array_stream>;
    using sentinel = ::json::sentinel<value_type, array_stream>;

    explicit array_stream(std::shared_ptr<lexer>&& lex)
        : lexer_ { std::move(lex) }
    {
        // Consume opening bracket
        if (!lexer_->try_consume_token(lexer::token_type::ARRAY_BEGIN)) {
            throw parse_error { "Expected '['" };
        }
    }
    // Copying would make two parsers consuming the same stream
    array_stream(const array_stream&) = delete;
    array_stream& operator=(const array_stream&) = delete;
    array_stream(array_stream&&) = default;
    array_stream& operator=(array_stream&&) = default;

    [[nodiscard]] iterator begin();
    [[nodiscard]] sentinel end();

private:
    friend iterator;
    // Get next value checking languages grammatics.
    // Returns std::nullopt after last value.
    [[nodiscard]] std::optional<value_type> next_value();

    std::shared_ptr<lexer> lexer_;
    bool first_element_ = true;
};

// Implements std::input_iterator concept
// https://en.cppreference.com/w/cpp/iterator/input_iterator.html
template <typename Value, typename Parser>
class iterator {
public:
    using value_type = Value;
    using difference_type = std::ptrdiff_t;

    iterator() = default;
    explicit iterator(Parser* parser)
        : parser_ { parser }
        , current_value_ { parser_->next_value() }
    {
    }

    [[nodiscard]] bool operator==(const sentinel<Value, Parser>&) const
    {
        return !current_value_.has_value();
    }

    iterator& operator++()
    {
        current_value_ = parser_->next_value();
        return *this;
    }

    // post-increment to conform std::input_iterator requirements
    void operator++(int)
    {
        ++(*this);
    }

    [[nodiscard]] value_type& operator*() const
    {
        return *current_value_;
    }

private:
    Parser* parser_ = nullptr;
    // std::input_iterator must have const dereference operator
    mutable std::optional<value_type> current_value_;
};

json parse_value(std::shared_ptr<lexer> lexer)
{
    lexer::token_type type = lexer->peek_type();

    switch (type) {
    case lexer::token_type::STRING:
    case lexer::token_type::NUMBER: {
        auto token = lexer->next_token();
        if (!token.value) {
            throw parse_error { "Expected value token to have a value" };
        }
        return std::visit([](auto& v) {
            return json { std::move(v) };
        },
            *token.value);
    }
    case lexer::token_type::OBJECT_BEGIN:
        return object_stream { std::move(lexer) };
    case lexer::token_type::ARRAY_BEGIN:
        return array_stream { std::move(lexer) };
    default:
        throw parse_error { "Expected value" };
    }
}

auto object_stream::begin() -> iterator { return iterator { this }; }
auto object_stream::end() -> sentinel { return {}; }

std::optional<object_stream::value_type> object_stream::next_value()
{
    if (lexer_->try_consume_token(lexer::token_type::OBJECT_END)) {
        return std::nullopt;
    }
    auto val = lexer_->try_consume_token(std::exchange(first_pair_, false) ? lexer::token_type::NOOP : lexer::token_type::COMMA)
                   .transform_error([](auto) { return "Expected ',' between object pairs"; })
                   .and_then([&](auto) {
                       return lexer_->try_consume_token(lexer::token_type::STRING)
                           .transform([&](auto tok) { return std::get<std::string>(*tok.value); })
                           .transform_error([](auto) { return "Expected string key"; });
                   })
                   .and_then([&](auto key) {
                       return lexer_->try_consume_token(lexer::token_type::COLON)
                           .transform([&](auto) { return key; })
                           .transform_error([](auto) { return "Expected ':' after key"; });
                   })
                   .transform([&](auto key) { return object_stream::value_type(std::move(key), parse_value(lexer_)); });
    if (!val) {
        throw parse_error { val.error() };
    }
    return std::move(*val);
}

auto array_stream::begin() -> iterator { return iterator { this }; }
auto array_stream::end() -> sentinel { return sentinel {}; }

std::optional<json> array_stream::next_value()
{
    if (lexer_->try_consume_token(lexer::token_type::ARRAY_END)) {
        return std::nullopt;
    }
    auto val = lexer_->try_consume_token(std::exchange(first_element_, false) ? lexer::token_type::NOOP : lexer::token_type::COMMA)
                   .transform_error([](auto) { return "Expected ',' between array elements"; })
                   .transform([&](auto) { return parse_value(lexer_); });
    if (!val) {
        throw parse_error { val.error() };
    }
    return std::move(*val);
}

// Using concepts to verify or parser implementation is ranges-compatible
static_assert(std::input_iterator<iterator<object_stream::value_type, object_stream>>);
static_assert(std::input_iterator<iterator<array_stream::value_type, array_stream>>);
static_assert(std::ranges::input_range<object_stream>);
static_assert(std::ranges::input_range<array_stream>);

// Main parsing function
json parse(std::istreambuf_iterator<char>&& input)
{
    return parse_value(std::make_shared<lexer>(std::move(input)));
}

} // namespace json

std::string indent(uint16_t base, uint16_t level)
{
    return base ? "\n" + std::string(base * level, ' ') : "";
}

// Converts range to generator
std::generator<char> stream(std::ranges::input_range auto streamable)
{
    for (auto c : streamable)
        co_yield c;
}

// Converts range to indented string representation
template <std::ranges::input_range R, typename Fn>
    requires std::ranges::input_range<std::invoke_result_t<Fn, std::ranges::range_reference_t<R>>>
std::generator<char> streamContainer(R& streamable, uint16_t indent_base, uint16_t level, std::pair<char, char> brackets, Fn serializeItem)
{
    auto serializeWithIndent = [indentString = indent(indent_base, level + 1), serializeItem = std::move(serializeItem)](auto& v) {
        return std::array { stream(indentString), stream(serializeItem(v)) } | std::views::join;
    };
    return stream(std::array {
                      stream(std::format("{}", brackets.first)),
                      stream(streamable | std::views::transform(std::move(serializeWithIndent)) | std::views::join_with(',')),
                      stream(std::format("{}{}", indent(indent_base, level), brackets.second)) }
        | std::views::join);
}

// Streaming serialization outputs consumed part of JSON stream with indentation
std::generator<char> serialize(uint16_t indent_base, uint16_t level, json::json& value)
{
    return std::visit([&](auto& v) -> std::generator<char> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return stream(std::format("\"{}\"", v));
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, double>) {
            return stream(std::format("{}", v));
        } else if constexpr (std::is_same_v<T, json::object_stream>) {
            return streamContainer(v, indent_base, level, std::pair { '{', '}' }, [indent_base, level](auto& p) {
                return std::array { stream(std::format("\"{}\": ", p.first)), serialize(indent_base, level + 1, p.second) } | std::views::join;
            });
        } else if constexpr (std::is_same_v<T, json::array_stream>) {
            return streamContainer(v, indent_base, level, std::pair { '[', ']' }, std::bind_front(serialize, indent_base, level + 1));
        }
    },
        value);
}

int main(int argc, char** argv)
try {
    std::istreambuf_iterator<char> input;
    std::istringstream iss;
    const uint16_t indent_base = (argc >= 2) ? std::stoul(argv[1]) : 0;
    if (argc < 3) {
        input = std::istreambuf_iterator<char> { std::cin };
    } else if (argc == 3) {
        iss.str(argv[2]);
        input = std::istreambuf_iterator<char> { iss };
    } else {
        std::println("Usage:");
        std::println("echo '{{\"key\": \"value\"}}' | ./json 2");
        std::println("./json 2 '{{\"key\": \"value\"}}'\n");
        return 0;
    }
    auto json_value = json::parse(std::move(input));
    std::ranges::copy(serialize(indent_base, 0, json_value), std::ostream_iterator<char> { std::cout });
    std::println();
    return 0;
} catch (const json::parse_error& e) {
    std::println(std::cerr, "{}", e.what());
    return 1;
}