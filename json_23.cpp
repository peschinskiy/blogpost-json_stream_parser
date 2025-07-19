#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <generator>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
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

    explicit lexer(std::istream_iterator<char> input)
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

    std::istream_iterator<char> input_;
    const std::istream_iterator<char> end_ {};
};

class json_object;
class json_array;
using json_variant = std::variant<int64_t, double, std::string, std::unique_ptr<json_object>, std::unique_ptr<json_array>>;
template <typename T, typename Parser>
class iterator;

// special type for end of range indication
template <typename T, typename Parser>
class sentinel { };

// Streaming parser for JSON values
class json_parser {
public:
    explicit json_parser(std::shared_ptr<lexer> lex)
        : lexer_(std::move(lex))
    {
    }

    [[nodiscard]] json_variant parse_value();

private:
    std::shared_ptr<lexer> lexer_;
};

// Streaming JSON object parser
class json_object {
public:
    using value_type = std::pair<std::string, json_variant>;
    using iterator = ::json::iterator<value_type, json_object>;
    using sentinel = ::json::sentinel<value_type, json_object>;

    explicit json_object(std::shared_ptr<lexer> lex)
        : lexer_(std::move(lex))
        , parser_(std::make_unique<json_parser>(lexer_))
    {
        // Consume opening brace
        if (lexer_->next_token().type != lexer::token_type::OBJECT_BEGIN) {
            throw parse_error("Expected '{'");
        }
    }

    [[nodiscard]] iterator begin();
    [[nodiscard]] sentinel end();

private:
    friend iterator;
    [[nodiscard]] std::optional<value_type> next_value();

    std::shared_ptr<lexer> lexer_;
    std::unique_ptr<json_parser> parser_;
    bool first_pair_ = true;
};

// Streaming JSON array parser
class json_array {
public:
    using value_type = json_variant;
    using iterator = ::json::iterator<value_type, json_array>;
    using sentinel = ::json::sentinel<value_type, json_array>;

    explicit json_array(std::shared_ptr<lexer> lex)
        : lexer_(std::move(lex))
        , parser_(std::make_unique<json_parser>(lexer_))
    {
        // Consume opening bracket
        if (lexer_->next_token().type != lexer::token_type::ARRAY_BEGIN) {
            throw parse_error("Expected '['");
        }
    }

    [[nodiscard]] iterator begin();
    [[nodiscard]] sentinel end();

private:
    friend iterator;

    [[nodiscard]] std::optional<value_type> next_value();

    std::shared_ptr<lexer> lexer_;
    std::unique_ptr<json_parser> parser_;
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

    [[nodiscard]] bool operator==(const sentinel<T, Parser>& other) const
    {
        return !current_value_.has_value();
    }

    iterator& operator++()
    {
        current_value_ = parser_->next_value();
        return *this;
    }

    // post-increment to conform input_iterator requirements
    void operator++(int)
    {
        ++(*this);
    }

    [[nodiscard]] const value_type& operator*() const
    {
        return *current_value_;
    }

private:
    Parser* parser_ = nullptr;
    std::optional<value_type> current_value_;
};

json_variant json_parser::parse_value()
{
    lexer::token_type type = lexer_->peek_type();

    switch (type) {
    case lexer::token_type::STRING:
    case lexer::token_type::NUMBER: {
        auto token = lexer_->next_token();
        if (!token.value.has_value()) {
            throw parse_error("Expected value token to have a value");
        }
        return std::visit([](const auto& v) -> json_variant {
            return v;
        }, *token.value);
    }
    case lexer::token_type::OBJECT_BEGIN:
        return std::make_unique<json_object>(lexer_);
    case lexer::token_type::ARRAY_BEGIN:
        return std::make_unique<json_array>(lexer_);
    default:
        throw parse_error("Expected value");
    }
}

auto json_object::begin() -> iterator { return iterator(*this); }
auto json_object::end() -> sentinel { return sentinel(); }

std::optional<json_object::value_type> json_object::next_value()
{
    // Check for end of object
    if (lexer_->peek_type() == lexer::token_type::OBJECT_END) {
        lexer_->next_token(); // consume '}'
        return std::nullopt;
    }

    // Handle comma separator (skip if not first pair)
    if (!first_pair_) {
        if (lexer_->next_token().type != lexer::token_type::COMMA) {
            throw parse_error("Expected ',' between object pairs");
        }
    }
    first_pair_ = false;

    // Parse key
    auto key_token = lexer_->next_token();
    if (key_token.type != lexer::token_type::STRING) {
        throw parse_error("Expected string key");
    }
    std::string key = std::get<std::string>(*key_token.value);

    // Parse colon
    if (lexer_->next_token().type != lexer::token_type::COLON) {
        throw parse_error("Expected ':' after key");
    }

    // Parse value
    json_variant value = parser_->parse_value();

    return std::make_pair(std::move(key), std::move(value));
}

auto json_array::begin() -> iterator { return iterator(*this); }
auto json_array::end() -> sentinel { return sentinel(); }

std::optional<json_variant> json_array::next_value()
{
    // Check for end of array
    if (lexer_->peek_type() == lexer::token_type::ARRAY_END) {
        lexer_->next_token(); // consume ']'
        return std::nullopt;
    }

    // Handle comma separator (skip if not first element)
    if (!first_element_) {
        if (lexer_->next_token().type != lexer::token_type::COMMA) {
            throw parse_error("Expected ',' between array elements");
        }
    }
    first_element_ = false;

    return parser_->parse_value();
}

static_assert(std::input_iterator<iterator<json_object::value_type, json_parser>>);
static_assert(std::input_iterator<iterator<json_array::value_type, json_parser>>);
static_assert(std::ranges::input_range<json_object>);
static_assert(std::ranges::input_range<json_array>);

// Main parsing function
json_variant parse(std::istream_iterator<char> input)
{
    return json_parser { std::make_shared<lexer>(std::move(input)) }.parse_value();
}

} // namespace json

std::generator<char> serialize(const std::string& str)
{
    for (char c : '"' + str + '"')
        co_yield c;
}

std::generator<char> serialize(const json::json_variant& value)
{
    return std::visit([](const auto& v) -> std::generator<char> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            for (char c : serialize(v)) {
                co_yield c;
            }
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, double>) {
            for (char c : std::format("{}", v)) {
                co_yield c;
            }
        } else if constexpr (std::is_same_v<T, std::unique_ptr<json::json_object>>) {
            co_yield '{';
            for (char c : *v
                    | std::views::transform([](auto& p) {
                          return std::array { serialize(p.first), serialize(p.second) } | std::views::join_with(':');
                      })
                    | std::views::join_with(',')) {
                co_yield c;
            }
            co_yield '}';
        } else if constexpr (std::is_same_v<T, std::unique_ptr<json::json_array>>) {
            co_yield '[';
            for (char c : *v
                    | std::views::transform([](auto& s) { return serialize(s); })
                    | std::views::join_with(',')) {
                co_yield c;
            }
            co_yield ']';
        }
    },
        value);
}

int main(int argc, char** argv)
{
    try {
        std::istream_iterator<char> input;
        std::istringstream iss;
        if (argc > 1) {
            iss.str(argv[1]);
            input = std::istream_iterator<char>(iss);
        } else {
            input = std::istream_iterator<char>(std::cin);
        }
        auto json_value = json::parse(std::move(input));
        std::ranges::copy(serialize(json_value), std::ostream_iterator<char> { std::cout });
        std::cout << "\n";
    } catch (const json::parse_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}