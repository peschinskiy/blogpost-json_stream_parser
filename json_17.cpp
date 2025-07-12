#include <cassert>
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

class bad_syntax : public std::exception {
public:
    bad_syntax(std::string err)
        : err_("json syntax error: " + err)
    {
    }

    const char* what() const noexcept override { return err_.data(); }

private:
    std::string err_;
};

class lexer {
public:
    enum class token_type {
        VALUE_STRING,
        VALUE_NUMERIC,
        DICT_BEGIN,
        DICT_END,
        LIST_BEGIN,
        LIST_END,
        COMMA,
        COLON,
        END_STREAM,
    };

    using value = std::variant<int64_t, double, std::string>;
    using token = std::pair<token_type, std::optional<value>>;

    explicit lexer(std::istream_iterator<char> it)
        : it_(std::move(it))
    {
    }

    token_type peek()
    {
        while (std::isspace(*it_)) {
            ++it_;
        }

        if (it_ == e_) {
            return token_type::END_STREAM;
        } else if (*it_ == ',') {
            return token_type::COMMA;
        } else if (*it_ == ':') {
            return token_type::COLON;
        } else if (*it_ == '{') {
            return token_type::DICT_BEGIN;
        } else if (*it_ == '}') {
            return token_type::DICT_END;
        } else if (*it_ == '[') {
            return token_type::LIST_BEGIN;
        } else if (*it_ == ']') {
            return token_type::LIST_END;
        } else if (*it_ == '"') {
            return token_type::VALUE_STRING;
        } else if (std::isdigit(*it_) || *it_ == '-') {
            return token_type::VALUE_NUMERIC;
        }
        throw bad_syntax { "unrecognized token" };
    }

    token next()
    {
        const auto type = peek();
        switch (type) {
        case token_type::END_STREAM:
        case token_type::COMMA:
        case token_type::COLON:
        case token_type::DICT_BEGIN:
        case token_type::DICT_END:
        case token_type::LIST_BEGIN:
        case token_type::LIST_END:
            it_++;
            return { type, std::nullopt };
        case token_type::VALUE_STRING: {
            it_++;
            std::string value;
            for (; it_ != e_; ++it_) {
                if (*it_ == '"') {
                    it_++;
                    return { type, std::move(value) };
                }
                value.push_back(*it_);
            }
            throw bad_syntax { "string value is not completed" };
        }
        case token_type::VALUE_NUMERIC: {
            std::string value;
            bool isFloating = false;
            if (*it_ == '-') {
                value.push_back(*(it_++));
            }
            for (; it_ != e_; ++it_) {
                if (*it_ == '.') {
                    if (isFloating) {
                        throw bad_syntax { "duplicate point in numeric value" };
                    } else {
                        isFloating = true;
                    }
                } else if (!std::isdigit(*it_)) {
                    break;
                }
                value.push_back(*it_);
            }
            return { type, isFloating ? std::stod(value) : std::stol(value) };
        }
        }
        throw bad_syntax { "unexpected token type" };
    }

private:
    std::istream_iterator<char> it_;
    const std::istream_iterator<char> e_;
};

struct value_expr;
struct dict_expr;
struct list_expr;

template <typename T>
class stream_parser {
public:
    class iterator {
    public:
        iterator() = default;
        explicit iterator(stream_parser<T>& parser)
            : parser_(&parser)
            , value_(parser_->next())
        {
        }

        bool operator!=(const iterator& other) const
        {
            return value_.has_value() != other.value_.has_value();
        }

        iterator& operator++()
        {
            value_ = parser_->next();
            return *this;
        }

        typename T::node operator*() const
        {
            return value_.value();
        }

    private:
        stream_parser<T>* parser_ = nullptr;
        std::optional<typename T::node> value_;
    };

    explicit stream_parser(std::shared_ptr<lexer> lex)
        : lexer_(lex)
    {
    }

    iterator begin() { return iterator { *this }; }
    iterator end() { return iterator {}; }

private:
    std::optional<typename T::node> next();

    std::shared_ptr<lexer> lexer_;
    bool firstValue = true;
};

struct json_expr {
    using node = std::variant<value_expr, dict_expr, list_expr>;

    stream_parser<json_expr> parser_;
};
struct value_expr {
    using node = lexer::value;

    stream_parser<value_expr> parser_;
};
struct list_expr {
    using node = json_expr;

    stream_parser<list_expr> parser_;
};
struct dict_expr {
    using node = std::pair<std::string, json_expr>;

    stream_parser<dict_expr> parser_;
};

template <typename T>
std::optional<typename T::node> stream_parser<T>::next()
{
    if constexpr (std::is_same_v<T, json_expr>) {
        if (!firstValue) {
            return std::nullopt;
        }
        firstValue = false;
        auto type = lexer_->peek();
        if (type == lexer::token_type::VALUE_NUMERIC || type == lexer::token_type::VALUE_STRING) {
            return value_expr { .parser_ = stream_parser<value_expr> { lexer_ } };
        } else if (type == lexer::token_type::DICT_BEGIN) {
            return dict_expr { .parser_ = stream_parser<dict_expr> { lexer_ } };
        } else if (type == lexer::token_type::LIST_BEGIN) {
            return list_expr { .parser_ = stream_parser<list_expr> { lexer_ } };
        } else if (type == lexer::token_type::END_STREAM) {
            return std::nullopt; // it is allowed to stop parsing in the middle of json structure
        } else {
            throw bad_syntax { "unexpected token" };
        }
    } else if constexpr (std::is_same_v<T, value_expr>) {
        if (!firstValue) {
            return std::nullopt;
        }
        auto [type, value] = lexer_->next();
        if (type != lexer::token_type::VALUE_NUMERIC && type != lexer::token_type::VALUE_STRING) {
            throw bad_syntax { "expected a value" };
        }
        firstValue = false;
        return value;
    } else if constexpr (std::is_same_v<T, dict_expr>) {
        if (lexer_->peek() == lexer::token_type::DICT_END) {
            return std::nullopt;
        }
        auto [type, value] = lexer_->next();
        if ((!firstValue && type == lexer::token_type::COMMA) || (firstValue && type == lexer::token_type::DICT_BEGIN)) {
            auto tok = lexer_->next();
            type = tok.first;
            value = tok.second;
        }
        if (type != lexer::token_type::VALUE_STRING) {
            throw bad_syntax { "expected key string" };
        }
        if (lexer_->next().first != lexer::token_type::COLON) {
            throw bad_syntax { "expected a colon after dict key" };
        }
        firstValue = false;
        return dict_expr::node { std::get<std::string>(*value), { .parser_ = stream_parser<json_expr>(lexer_) } };
    } else if constexpr (std::is_same_v<T, list_expr>) {
        auto type = lexer_->peek();
        if (type == lexer::token_type::LIST_END) {
            lexer_->next();
            return std::nullopt;
        } else if ((!firstValue && type == lexer::token_type::COMMA) || (firstValue && type == lexer::token_type::LIST_BEGIN)) {
            lexer_->next();
        }
        firstValue = false;
        return list_expr::node { .parser_ = stream_parser<json_expr> { lexer_ } };
    }
}

stream_parser<json_expr> parse(std::istream_iterator<char> it)
{
    return stream_parser<json_expr>(std::make_shared<lexer>(std::move(it)));
}

} // namespace json

void serialize(std::ostream& out, json::stream_parser<json::json_expr> parser)
{
    for (auto val : parser) {
        if (std::holds_alternative<json::value_expr>(val)) {
            auto valueExpr = std::get<json::value_expr>(val);
            for (auto v : valueExpr.parser_) {
                if (std::holds_alternative<std::string>(v)) {
                    out << std::quoted(std::get<std::string>(v));
                } else if (std::holds_alternative<double>(v)) {
                    out << std::get<double>(v);
                } else {
                    out << std::get<int64_t>(v);
                }
            }
        } else if (std::holds_alternative<json::list_expr>(val)) {
            auto& v = std::get<json::list_expr>(val);
            out << "[";
            bool first = true;
            for (auto valueExpr : v.parser_) {
                if (!first) {
                    out << ",";
                }
                serialize(out, valueExpr.parser_);
                first = false;
            }
            out << "]";
        } else if (std::holds_alternative<json::dict_expr>(val)) {
            auto& v = std::get<json::dict_expr>(val);
            out << "{";
            bool first = true;
            for (auto [key, valueExpr] : v.parser_) {
                if (!first) {
                    out << ",";
                }
                out << std::quoted(key) << ":";
                serialize(out, valueExpr.parser_);
                first = false;
            }
            out << "}";
        }
    }
}

int main()
{
    serialize(std::cout, json::parse(std::istream_iterator<char>(std::cin)));
    std::cout << "\n";
    return 0;
}