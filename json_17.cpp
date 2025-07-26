#include <cctype>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace json {

class parse_error : public std::exception {
public:
    explicit parse_error(std::string message)
        : message_("JSON parse error: " + std::move(message))
    {
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return message_.c_str();
    }

private:
    std::string message_;
};

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
        END_OF_INPUT,
    };

    using token_value = std::variant<int64_t, double, std::string>;

    struct token {
        token_type type;
        std::optional<token_value> value;
    };

    explicit lexer(std::istreambuf_iterator<char> input)
        : input_(std::move(input))
    {
    }

    [[nodiscard]] token_type peek_type()
    {
        // skip whitespace
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
            throw parse_error("Unexpected character: " + std::string(1, *input_));
        }
    }

    token next_token()
    {
        token_type type = peek_type();

        switch (type) {
        case token_type::OBJECT_BEGIN:
        case token_type::OBJECT_END:
        case token_type::ARRAY_BEGIN:
        case token_type::ARRAY_END:
        case token_type::COMMA:
        case token_type::COLON:
        case token_type::END_OF_INPUT:
            if (type != token_type::END_OF_INPUT) {
                ++input_;
            }
            return { type, std::nullopt };
        case token_type::STRING:
            return parse_string();
        case token_type::NUMBER:
            return parse_number();
        }
        throw parse_error("Unexpected token type");
    }

    std::optional<token> try_consume_token(token_type type)
    {
        if (peek_type() != type) {
            return std::nullopt;
        }
        return next_token();
    }

private:
    [[nodiscard]] token parse_string()
    {
        ++input_; // skip opening quote
        std::string result;

        while (input_ != end_ && *input_ != '"') {
            result.push_back(*input_);
            ++input_;
        }

        if (input_ == end_) {
            throw parse_error("Unterminated string");
        }

        ++input_; // skip closing quote
        return { token_type::STRING, std::move(result) };
    }

    [[nodiscard]] token parse_number()
    {
        std::string number_str;
        bool has_decimal = false;

        if (*input_ == '-') {
            number_str.push_back(*input_);
            ++input_;
        }

        while (input_ != end_ && (std::isdigit(*input_) || *input_ == '.')) {
            if (*input_ == '.') {
                if (has_decimal) {
                    throw parse_error("Multiple decimal points in number");
                }
                has_decimal = true;
            }
            number_str.push_back(*input_);
            ++input_;
        }

        return { token_type::NUMBER, has_decimal ? std::stod(number_str) : std::stoll(number_str) };
    }

    std::istreambuf_iterator<char> input_;
    const std::istreambuf_iterator<char> end_ {};
};

class json_object;
class json_array;
using json_variant = std::variant<int64_t, double, std::string, json_object, json_array>;
template <typename T, typename Parser>
class iterator;

// Streaming JSON object parser
class json_object {
public:
    using value_type = std::pair<std::string, json_variant>;
    using iterator = ::json::iterator<value_type, json_object>;

    explicit json_object(std::shared_ptr<lexer>&& lex)
        : lexer_(std::move(lex))
    {
        // Consume opening brace
        if (lexer_->next_token().type != lexer::token_type::OBJECT_BEGIN) {
            throw parse_error("Expected '{'");
        }
    }
    json_object(const json_object&) = delete;
    json_object& operator=(const json_object&) = delete;
    json_object(json_object&&) = default;
    json_object& operator=(json_object&&) = default;

    [[nodiscard]] iterator begin();
    [[nodiscard]] iterator end();

private:
    friend iterator;
    [[nodiscard]] std::optional<value_type> next_value();

    std::shared_ptr<lexer> lexer_;
    bool first_pair_ = true;
};

// Streaming JSON array parser
class json_array {
public:
    using value_type = json_variant;
    using iterator = ::json::iterator<value_type, json_array>;

    explicit json_array(std::shared_ptr<lexer>&& lex)
        : lexer_(std::move(lex))
    {
        // Consume opening bracket
        if (lexer_->next_token().type != lexer::token_type::ARRAY_BEGIN) {
            throw parse_error("Expected '['");
        }
    }
    json_array(const json_array&) = delete;
    json_array& operator=(const json_array&) = delete;
    json_array(json_array&&) = default;
    json_array& operator=(json_array&&) = default;

    [[nodiscard]] iterator begin();
    [[nodiscard]] iterator end();

private:
    friend iterator;
    [[nodiscard]] std::optional<value_type> next_value();

    std::shared_ptr<lexer> lexer_;
    bool first_element_ = true;
};

template <typename T, typename Parser>
class iterator {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;

    iterator() = default;
    explicit iterator(Parser& parser)
        : parser_(&parser)
        , current_value_(parser_->next_value())
    {
    }

    [[nodiscard]] bool operator!=(const iterator& other) const
    {
        return current_value_.has_value() != other.current_value_.has_value();
    }

    iterator& operator++()
    {
        current_value_ = parser_->next_value();
        return *this;
    }

    [[nodiscard]] value_type& operator*() const
    {
        return *current_value_;
    }

private:
    Parser* parser_ = nullptr;
    mutable std::optional<value_type> current_value_;
};

json_variant parse_value(std::shared_ptr<lexer> lexer)
{
    lexer::token_type type = lexer->peek_type();

    switch (type) {
    case lexer::token_type::STRING:
    case lexer::token_type::NUMBER: {
        auto token = lexer->next_token();
        if (!token.value.has_value()) {
            throw parse_error("Expected value token to have a value");
        }
        return std::visit([](const auto& v) -> json_variant {
            return v;
        },
            *token.value);
    }
    case lexer::token_type::OBJECT_BEGIN:
        return json_object { std::move(lexer) };
    case lexer::token_type::ARRAY_BEGIN:
        return json_array { std::move(lexer) };
    default:
        throw parse_error("Expected value");
    }
}

auto json_object::begin() -> iterator { return iterator(*this); }
auto json_object::end() -> iterator { return iterator(); }

std::optional<json_object::value_type> json_object::next_value()
{
    if (lexer_->try_consume_token(lexer::token_type::OBJECT_END)) {
        return std::nullopt;
    }

    if (!first_pair_ && lexer_->next_token().type != lexer::token_type::COMMA) {
        throw parse_error("Expected ',' between object pairs");
    }
    first_pair_ = false;

    auto key_token = lexer_->next_token();
    if (key_token.type != lexer::token_type::STRING) {
        throw parse_error("Expected string key");
    }
    auto key = std::get<std::string>(*key_token.value);

    if (lexer_->next_token().type != lexer::token_type::COLON) {
        throw parse_error("Expected ':' after key");
    }

    return std::make_pair(std::move(key), parse_value(lexer_));
}

auto json_array::begin() -> iterator { return iterator(*this); }
auto json_array::end() -> iterator { return iterator(); }

std::optional<json_variant> json_array::next_value()
{
    if (lexer_->try_consume_token(lexer::token_type::ARRAY_END)) {
        return std::nullopt;
    }

    if (!first_element_ && lexer_->next_token().type != lexer::token_type::COMMA) {
        throw parse_error("Expected ',' between array elements");
    }
    first_element_ = false;

    return parse_value(lexer_);
}

// Main parsing function
json_variant parse(std::istreambuf_iterator<char> input)
{
    return parse_value(std::make_shared<lexer>(std::move(input)));
}

} // namespace json

std::string indent(uint16_t base, uint16_t level)
{
    return base ? "\n" + std::string(base * level, ' ') : "";
}

void serialize(std::ostream& out, uint16_t indentBase, uint16_t level, json::json_variant& value)
{
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            out << std::quoted(v);
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, double>) {
            out << v;
        } else if constexpr (std::is_same_v<T, json::json_object>) {
            out << "{";
            bool first = true;
            for (auto& pair : v) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << indent(indentBase, level + 1) << std::quoted(pair.first) << ":";
                serialize(out, indentBase, level + 1, pair.second);
            }
            out << indent(indentBase, level) << "}";
        } else if constexpr (std::is_same_v<T, json::json_array>) {
            out << "[";
            bool first = true;
            for (auto& val : v) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << indent(indentBase, level + 1);
                serialize(out, indentBase, level + 1, val);
            }
            out << indent(indentBase, level) << "]";
        }
    },
        value);
}

int main(int argc, char** argv)
{
    try {
        std::istreambuf_iterator<char> input;
        std::istringstream iss;
        const uint16_t indentBase = (argc >= 2) ? std::stoul(argv[1]) : 0;
        if (argc == 3) {
            iss.str(argv[2]);
            input = std::istreambuf_iterator<char>(iss);
        } else {
            input = std::istreambuf_iterator<char>(std::cin);
        }
        auto json_value = json::parse(std::move(input));
        serialize(std::cout, indentBase, 0, json_value);
        std::cout << "\n";
    } catch (const json::parse_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}