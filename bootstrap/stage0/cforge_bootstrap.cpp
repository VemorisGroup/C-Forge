// C-Forge Stage 0 Bootstrap
// Compilador mínimo alojado exclusivamente en C++17. No carga Python, JVM,
// .NET, Node ni ningún runtime extranjero.

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class TokenKind { identifier, number, string, symbol, end };

struct Token {
    TokenKind kind;
    std::string text;
    std::size_t line;
    std::size_t column;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!at_end()) {
            const char current = peek();
            if (current == ' ' || current == '\t' || current == '\r') {
                advance();
            } else if (current == '\n') {
                advance();
                ++line_;
                column_ = 1;
            } else if (current == '/' && peek_next() == '/') {
                while (!at_end() && peek() != '\n') advance();
            } else if (is_identifier_start(current)) {
                tokens.push_back(identifier());
            } else if (std::isdigit(static_cast<unsigned char>(current))) {
                tokens.push_back(number());
            } else if (current == '"') {
                tokens.push_back(string());
            } else {
                const std::size_t token_line = line_;
                const std::size_t token_column = column_;
                std::string symbol(1, advance());
                if (!at_end()) {
                    const std::string pair = symbol + peek();
                    if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") {
                        symbol.push_back(advance());
                    }
                }
                tokens.push_back({TokenKind::symbol, symbol, token_line, token_column});
            }
        }
        tokens.push_back({TokenKind::end, "", line_, column_});
        return tokens;
    }

private:
    std::string source_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    bool at_end() const { return position_ >= source_.size(); }
    char peek() const { return at_end() ? '\0' : source_[position_]; }
    char peek_next() const {
        return position_ + 1 >= source_.size() ? '\0' : source_[position_ + 1];
    }
    char advance() {
        const char value = source_[position_++];
        ++column_;
        return value;
    }
    static bool is_identifier_start(char value) {
        const auto byte = static_cast<unsigned char>(value);
        return std::isalpha(byte) || value == '_';
    }
    static bool is_identifier_continue(char value) {
        const auto byte = static_cast<unsigned char>(value);
        return std::isalnum(byte) || value == '_';
    }

    Token identifier() {
        const std::size_t start = position_;
        const std::size_t token_line = line_;
        const std::size_t token_column = column_;
        while (!at_end() && is_identifier_continue(peek())) advance();
        return {TokenKind::identifier, source_.substr(start, position_ - start),
                token_line, token_column};
    }

    Token number() {
        const std::size_t start = position_;
        const std::size_t token_line = line_;
        const std::size_t token_column = column_;
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        if (!at_end() && peek() == '.' &&
            std::isdigit(static_cast<unsigned char>(peek_next()))) {
            advance();
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
        return {TokenKind::number, source_.substr(start, position_ - start),
                token_line, token_column};
    }

    Token string() {
        const std::size_t token_line = line_;
        const std::size_t token_column = column_;
        advance();
        std::string value;
        while (!at_end() && peek() != '"') {
            char current = advance();
            if (current == '\\') {
                if (at_end()) fail(token_line, token_column, "escape incompleto");
                const char escaped = advance();
                if (escaped == 'n') value.push_back('\n');
                else if (escaped == 't') value.push_back('\t');
                else if (escaped == 'r') value.push_back('\r');
                else if (escaped == '"' || escaped == '\\') value.push_back(escaped);
                else fail(token_line, token_column, "escape desconocido");
            } else {
                if (current == '\n') {
                    ++line_;
                    column_ = 1;
                }
                value.push_back(current);
            }
        }
        if (at_end()) fail(token_line, token_column, "texto sin cerrar");
        advance();
        return {TokenKind::string, value, token_line, token_column};
    }

    [[noreturn]] static void fail(std::size_t line, std::size_t column,
                                  const std::string& message) {
        throw CompileError("Línea " + std::to_string(line) + ", columna " +
                           std::to_string(column) + ": " + message);
    }
};

enum class ValueType { number, text };

struct Expression {
    ValueType type;
    std::string generated;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::string compile_to_cpp() {
        std::ostringstream body;
        while (!check(TokenKind::end)) statement(body);
        return prologue() + body.str() + "    return 0;\n}\n";
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::map<std::string, ValueType> variables_;

    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    bool check(TokenKind kind) const { return peek().kind == kind; }
    bool check_text(const std::string& text) const { return peek().text == text; }
    const Token& advance() {
        if (!check(TokenKind::end)) ++current_;
        return previous();
    }
    bool take(const std::string& text) {
        if (!check_text(text)) return false;
        advance();
        return true;
    }
    const Token& require(TokenKind kind, const std::string& message) {
        if (!check(kind)) fail(peek(), message);
        return advance();
    }
    void require_text(const std::string& text, const std::string& message) {
        if (!take(text)) fail(peek(), message);
    }
    static std::string safe_name(const std::string& name) { return "cfv_" + name; }

    void statement(std::ostringstream& body) {
        if (take(";")) return;
        if (take("sea")) {
            const Token name = require(TokenKind::identifier,
                                       "se esperaba el nombre de la variable");
            if (take(":")) {
                const Token declared = require(TokenKind::identifier,
                                               "se esperaba el tipo declarado");
                if (declared.text != "numero" && declared.text != "texto") {
                    fail(declared, "Stage 0 solo admite numero y texto");
                }
            }
            require_text("=", "se esperaba '=' en la declaración");
            const Expression value = expression();
            if (variables_.count(name.text)) fail(name, "variable duplicada");
            variables_[name.text] = value.type;
            body << "    Value " << safe_name(name.text) << " = " << value.generated << ";\n";
            take(";");
            return;
        }
        if (take("mostrar") || take("print")) {
            require_text("(", "se esperaba '(' después de mostrar");
            const Expression value = expression();
            require_text(")", "se esperaba ')' después del valor");
            body << "    cfv_print(" << value.generated << ");\n";
            take(";");
            return;
        }
        fail(peek(), "Stage 0 esperaba 'sea' o 'mostrar'");
    }

    Expression expression() {
        Expression left = term();
        while (check_text("+") || check_text("-")) {
            const Token operation = advance();
            Expression right = term();
            if (operation.text == "+") {
                if (left.type != right.type) fail(operation, "tipos incompatibles para '+'");
                left.generated = "cfv_add(" + left.generated + ", " + right.generated + ")";
            } else {
                require_numbers(operation, left, right);
                left.generated = "cfv_sub(" + left.generated + ", " + right.generated + ")";
            }
        }
        return left;
    }

    Expression term() {
        Expression left = primary();
        while (check_text("*") || check_text("/")) {
            const Token operation = advance();
            Expression right = primary();
            require_numbers(operation, left, right);
            left.generated = (operation.text == "*" ? "cfv_mul(" : "cfv_div(") +
                             left.generated + ", " + right.generated + ")";
        }
        return left;
    }

    Expression primary() {
        if (check(TokenKind::number)) {
            const Token value = advance();
            return {ValueType::number, "Value(" + value.text + ")"};
        }
        if (check(TokenKind::string)) {
            const Token value = advance();
            return {ValueType::text, "Value(std::string(" + cpp_string(value.text) + "))"};
        }
        if (check(TokenKind::identifier)) {
            const Token name = advance();
            const auto found = variables_.find(name.text);
            if (found == variables_.end()) fail(name, "variable desconocida '" + name.text + "'");
            return {found->second, safe_name(name.text)};
        }
        if (take("(")) {
            Expression value = expression();
            require_text(")", "se esperaba ')'");
            return value;
        }
        fail(peek(), "expresión inválida");
    }

    static void require_numbers(const Token& operation, const Expression& left,
                                const Expression& right) {
        if (left.type != ValueType::number || right.type != ValueType::number) {
            fail(operation, "el operador '" + operation.text + "' requiere números");
        }
    }

    static std::string cpp_string(const std::string& value) {
        std::ostringstream escaped;
        escaped << '"';
        for (const unsigned char byte : value) {
            if (byte == '\\') escaped << "\\\\";
            else if (byte == '"') escaped << "\\\"";
            else if (byte == '\n') escaped << "\\n";
            else if (byte == '\r') escaped << "\\r";
            else if (byte == '\t') escaped << "\\t";
            else escaped << static_cast<char>(byte);
        }
        escaped << '"';
        return escaped.str();
    }

    [[noreturn]] static void fail(const Token& token, const std::string& message) {
        throw CompileError("Línea " + std::to_string(token.line) + ", columna " +
                           std::to_string(token.column) + ": " + message);
    }

    static std::string prologue() {
        return R"CPP(#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

struct Value {
    std::variant<double, std::string> data;
    explicit Value(double value) : data(value) {}
    explicit Value(std::string value) : data(std::move(value)) {}
};
static double number(const Value& value) {
    if (const auto* found = std::get_if<double>(&value.data)) return *found;
    throw std::runtime_error("se esperaba numero");
}
static Value cfv_add(const Value& left, const Value& right) {
    if (const auto* a = std::get_if<double>(&left.data)) {
        if (const auto* b = std::get_if<double>(&right.data)) return Value(*a + *b);
    }
    if (const auto* a = std::get_if<std::string>(&left.data)) {
        if (const auto* b = std::get_if<std::string>(&right.data)) return Value(*a + *b);
    }
    throw std::runtime_error("tipos incompatibles para '+'");
}
static Value cfv_sub(const Value& a, const Value& b) { return Value(number(a) - number(b)); }
static Value cfv_mul(const Value& a, const Value& b) { return Value(number(a) * number(b)); }
static Value cfv_div(const Value& a, const Value& b) {
    const double divisor = number(b);
    if (divisor == 0) throw std::runtime_error("no se puede dividir por cero");
    return Value(number(a) / divisor);
}
static void cfv_print(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value.data)) {
        std::cout << *text << '\n';
        return;
    }
    const double number_value = number(value);
    if (std::floor(number_value) == number_value) {
        std::cout << static_cast<long long>(number_value) << '\n';
    } else {
        std::ostringstream output;
        output << std::setprecision(15) << number_value;
        std::cout << output.str() << '\n';
    }
}
int main() {
)CPP";
    }
};

static std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw CompileError("no se pudo abrir " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

static std::string shell_quote(const fs::path& path) {
    std::string quoted = "'";
    for (const char byte : path.string()) {
        if (byte == '\'') quoted += "'\\''";
        else quoted.push_back(byte);
    }
    return quoted + "'";
}

int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string(argv[2]) != "-o") {
            std::cerr << "Uso: cforge-bootstrap archivo.cfv -o ejecutable\n";
            return 2;
        }
        const fs::path input = fs::absolute(argv[1]);
        const fs::path output = fs::absolute(argv[3]);
        const std::string generated = Parser(Lexer(read_file(input)).scan()).compile_to_cpp();
        const fs::path temporary = output.string() + ".stage0.cpp";
        {
            std::ofstream stream(temporary, std::ios::binary);
            if (!stream) throw CompileError("no se pudo crear fuente temporal");
            stream << generated;
        }
        const std::string command =
            "clang++ -std=c++17 -O2 " + shell_quote(temporary) + " -o " + shell_quote(output);
        const int status = std::system(command.c_str());
        std::error_code ignored;
        fs::remove(temporary, ignored);
        if (status != 0) throw CompileError("clang++ no pudo producir el ejecutable Stage 0");
        std::cout << "C-Forge Stage 0 creó: " << output << "\n";
        return 0;
    } catch (const CompileError& error) {
        std::cerr << "[C-Forge Bootstrap Error] " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "[C-Forge Bootstrap Internal Error] " << error.what() << "\n";
        return 1;
    }
}
