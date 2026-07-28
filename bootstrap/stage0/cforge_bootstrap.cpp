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
    bool check_text(const std::string& text) const {
        return peek().kind != TokenKind::string && peek().text == text;
    }
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Compilador del subconjunto Core 0.4 requerido por B1/B2/B3. Conserva Stage 0 como
// una herramienta pequeña, pero añade funciones, control de flujo, listas y
// objetos suficientes para compilar el lexer, el AST y el parser escritos en
// C-Forge. La semántica continúa alojada únicamente en este binario C++17.
class CoreB1Compiler {
public:
    explicit CoreB1Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
        discover_symbols();
    }

    std::string compile_to_cpp() {
        std::ostringstream declarations;
        std::ostringstream body;
        current_ = 0;
        scopes_.push_back({});
        while (!check(TokenKind::end)) {
            if (take(";")) continue;
            if (check_text("estructura")) {
                parse_structure();
            } else if (check_text("clase")) {
                parse_class(declarations);
            } else if (check_text("funcion")) {
                parse_function(declarations, false, "");
            } else {
                statement(body);
            }
        }

        std::ostringstream prototypes;
        for (const auto& name : functions_) {
            prototypes << "static Value cfv_fn_" << name
                       << "(const std::vector<Value>&);\n";
        }
        for (const auto& name : methods_) {
            prototypes << "static Value cfv_method_" << name
                       << "(Value, const std::vector<Value>&);\n";
        }
        return runtime() + prototypes.str() + declarations.str() +
               "int main(int argc, char** argv) {\n"
               "    try {\n"
               "        cfv_process_args.clear();\n"
               "        for (int index = 0; index < argc; ++index) {\n"
               "            cfv_process_args.emplace_back(std::string(argv[index]));\n"
               "        }\n" + body.str() +
               "        return 0;\n"
               "    } catch (const std::exception& error) {\n"
               "        std::cerr << \"[C-Forge Core Runtime Error] \""
               " << error.what() << '\\n';\n"
               "        return 1;\n"
               "    }\n}\n";
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::map<std::string, std::vector<std::string>> records_;
    std::vector<std::string> functions_;
    std::vector<std::string> methods_;
    std::vector<std::map<std::string, bool>> scopes_;

    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    const Token& look(std::size_t offset) const {
        const std::size_t index = current_ + offset;
        return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
    }
    bool check(TokenKind kind) const { return peek().kind == kind; }
    bool check_text(const std::string& text) const {
        return peek().kind != TokenKind::string && peek().text == text;
    }
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
    Token require_name(const std::string& message) {
        return require(TokenKind::identifier, message);
    }
    void require_text(const std::string& text, const std::string& message) {
        if (!take(text)) fail(peek(), message);
    }
    static std::string safe(const std::string& name) { return "cfv_" + name; }
    static bool contains(const std::vector<std::string>& values,
                         const std::string& value) {
        for (const auto& item : values) if (item == value) return true;
        return false;
    }

    void discover_symbols() {
        for (std::size_t index = 0; index + 1 < tokens_.size(); ++index) {
            if (tokens_[index].text == "funcion" &&
                tokens_[index + 1].kind == TokenKind::identifier) {
                const std::string name = tokens_[index + 1].text;
                if (!contains(functions_, name)) functions_.push_back(name);
            }
            if (tokens_[index].text == "metodo" &&
                tokens_[index + 1].kind == TokenKind::identifier) {
                const std::string name = tokens_[index + 1].text;
                if (!contains(methods_, name)) methods_.push_back(name);
            }
        }
    }

    void skip_type() {
        if (!check(TokenKind::identifier)) fail(peek(), "se esperaba un tipo");
        advance();
        if (take("<")) {
            int depth = 1;
            while (depth > 0 && !check(TokenKind::end)) {
                if (take("<")) ++depth;
                else if (take(">")) --depth;
                else advance();
            }
        }
    }

    void parse_structure() {
        require_text("estructura", "se esperaba 'estructura'");
        const Token name = require_name("se esperaba el nombre de la estructura");
        require_text("{", "se esperaba '{'");
        std::vector<std::string> fields;
        while (!take("}")) {
            const Token field = require_name("se esperaba un campo");
            require_text(":", "se esperaba ':'");
            skip_type();
            take(";");
            fields.push_back(field.text);
        }
        take(";");
        records_[name.text] = fields;
    }

    std::vector<std::string> parameters() {
        std::vector<std::string> result;
        require_text("(", "se esperaba '('");
        if (!check_text(")")) {
            do {
                const Token parameter = require_name("se esperaba un parámetro");
                result.push_back(parameter.text);
                if (take(":")) skip_type();
            } while (take(","));
        }
        require_text(")", "se esperaba ')'");
        if (take(":")) skip_type();
        return result;
    }

    void parse_class(std::ostringstream& output) {
        require_text("clase", "se esperaba 'clase'");
        const Token class_name = require_name("se esperaba el nombre de la clase");
        require_text("{", "se esperaba '{'");
        std::vector<std::string> fields;
        while (!check_text("}")) {
            if (take("campo")) {
                const Token field = require_name("se esperaba el campo");
                require_text(":", "se esperaba ':'");
                skip_type();
                take(";");
                fields.push_back(field.text);
            } else if (check_text("metodo")) {
                advance();
                const Token method = require_name("se esperaba el método");
                parse_function_after_name(output, true, class_name.text, method);
            } else {
                fail(peek(), "miembro de clase no admitido por Core 0.4");
            }
        }
        advance();
        take(";");
        records_[class_name.text] = fields;
    }

    void parse_function(std::ostringstream& output, bool method,
                        const std::string& owner) {
        require_text("funcion", "se esperaba 'funcion'");
        const Token name = require_name("se esperaba el nombre de la función");
        parse_function_after_name(output, method, owner, name);
    }

    void parse_function_after_name(std::ostringstream& output, bool method,
                                   const std::string&, const Token& name) {
        const auto params = parameters();
        output << "static Value "
               << (method ? "cfv_method_" : "cfv_fn_") << name.text << "(";
        if (method) output << "Value cfv_este, ";
        output << "const std::vector<Value>& cfv_args) {\n";
        scopes_.push_back({});
        if (method) scopes_.back()["este"] = true;
        for (std::size_t index = 0; index < params.size(); ++index) {
            scopes_.back()[params[index]] = true;
            output << "    Value " << safe(params[index]) << " = cfv_arg(cfv_args, "
                   << index << ", " << cpp_string(name.text) << ");\n";
        }
        require_text("{", "se esperaba el cuerpo de la función");
        while (!check_text("}")) statement(output);
        advance();
        output << "    return Value();\n}\n";
        scopes_.pop_back();
    }

    void statement(std::ostringstream& output) {
        if (take(";")) return;
        if (take("sea")) {
            const Token name = require_name("se esperaba el nombre de la variable");
            if (take(":")) skip_type();
            require_text("=", "se esperaba '='");
            const std::string value = expression();
            scopes_.back()[name.text] = true;
            output << "    Value " << safe(name.text) << " = " << value << ";\n";
            take(";");
            return;
        }
        if (take("si")) {
            const bool grouped = take("(");
            const std::string condition = expression();
            if (grouped) require_text(")", "se esperaba ')'");
            output << "    if (cfv_truth(" << condition << ")) ";
            block(output);
            if (take("sino")) {
                output << "    else ";
                block(output);
            }
            return;
        }
        if (take("mientras")) {
            const bool grouped = take("(");
            const std::string condition = expression();
            if (grouped) require_text(")", "se esperaba ')'");
            output << "    while (cfv_truth(" << condition << ")) ";
            block(output);
            return;
        }
        if (take("retornar")) {
            output << "    return " << expression() << ";\n";
            take(";");
            return;
        }
        if (take("mostrar") || take("print")) {
            require_text("(", "se esperaba '('");
            const std::string value = expression();
            require_text(")", "se esperaba ')'");
            output << "    cfv_print(" << value << ");\n";
            take(";");
            return;
        }
        if (check(TokenKind::identifier) && look(1).text == "=") {
            const Token name = advance();
            advance();
            output << "    " << variable(name) << " = " << expression() << ";\n";
            take(";");
            return;
        }
        if (check(TokenKind::identifier) && look(1).text == "." &&
            look(2).kind == TokenKind::identifier && look(3).text == "=") {
            const Token owner = advance();
            advance();
            const Token field = advance();
            advance();
            output << "    cfv_member_ref(" << variable(owner) << ", "
                   << cpp_string(field.text) << ") = " << expression() << ";\n";
            take(";");
            return;
        }
        output << "    (void)(" << expression() << ");\n";
        take(";");
    }

    void block(std::ostringstream& output) {
        require_text("{", "se esperaba '{'");
        output << "{\n";
        scopes_.push_back(scopes_.back());
        while (!check_text("}")) statement(output);
        advance();
        scopes_.pop_back();
        output << "    }\n";
    }

    std::string expression() { return logic_or(); }
    std::string logic_or() {
        std::string left = logic_and();
        while (take("o")) left = "cfv_bool(cfv_truth(" + left + ") || cfv_truth(" +
                                  logic_and() + "))";
        return left;
    }
    std::string logic_and() {
        std::string left = equality();
        while (take("y")) left = "cfv_bool(cfv_truth(" + left + ") && cfv_truth(" +
                                  equality() + "))";
        return left;
    }
    std::string equality() {
        std::string left = comparison();
        while (check_text("==") || check_text("!=")) {
            const std::string op = advance().text;
            const std::string right = comparison();
            left = "cfv_bool(cfv_equal(" + left + ", " + right + ")" +
                   (op == "!=" ? " == false" : "") + ")";
        }
        return left;
    }
    std::string comparison() {
        std::string left = sum();
        while (check_text("<") || check_text("<=") || check_text(">") ||
               check_text(">=")) {
            const std::string op = advance().text;
            left = "cfv_compare(" + left + ", " + sum() + ", " + cpp_string(op) + ")";
        }
        return left;
    }
    std::string sum() {
        std::string left = product();
        while (check_text("+") || check_text("-")) {
            const std::string op = advance().text;
            left = (op == "+" ? "cfv_add(" : "cfv_sub(") + left + ", " +
                   product() + ")";
        }
        return left;
    }
    std::string product() {
        std::string left = unary();
        while (check_text("*") || check_text("/") || check_text("%")) {
            const std::string op = advance().text;
            const std::string right = unary();
            if (op == "*") left = "cfv_mul(" + left + ", " + right + ")";
            else if (op == "/") left = "cfv_div(" + left + ", " + right + ")";
            else left = "cfv_mod(" + left + ", " + right + ")";
        }
        return left;
    }
    std::string unary() {
        if (take("no")) return "cfv_bool(!cfv_truth(" + unary() + "))";
        if (take("-")) return "cfv_neg(" + unary() + ")";
        return postfix();
    }

    std::string postfix() {
        std::string value = primary();
        while (true) {
            if (take("[")) {
                const std::string index = expression();
                require_text("]", "se esperaba ']'");
                value = "cfv_index(" + value + ", " + index + ")";
            } else if (take(".")) {
                const Token member = require_name("se esperaba el miembro");
                if (take("(")) {
                    const auto args = arguments_after_open();
                    value = "cfv_method_" + member.text + "(" + value + ", " +
                            vector_expression(args) + ")";
                } else {
                    value = "cfv_member(" + value + ", " +
                            cpp_string(member.text) + ")";
                }
            } else {
                break;
            }
        }
        return value;
    }

    std::string primary() {
        if (check(TokenKind::number)) return "cfv_number(" + advance().text + ")";
        if (check(TokenKind::string)) return "cfv_text(" + cpp_string(advance().text) + ")";
        if (take("verdadero")) return "cfv_bool(true)";
        if (take("falso")) return "cfv_bool(false)";
        if (take("[")) return "cfv_list(" + vector_expression(arguments_after("[", "]")) + ")";
        if (take("(")) {
            const std::string value = expression();
            require_text(")", "se esperaba ')'");
            return value;
        }
        if (check(TokenKind::identifier)) {
            const Token name = advance();
            if (take("(")) {
                const auto args = arguments_after_open();
                const auto record = records_.find(name.text);
                if (record != records_.end()) {
                    return "cfv_object(" + cpp_string(name.text) + ", " +
                           string_vector(record->second) + ", " +
                           vector_expression(args) + ")";
                }
                return call(name, args);
            }
            return variable(name);
        }
        fail(peek(), "expresión Core 0.4 inválida");
    }

    std::vector<std::string> arguments_after_open() {
        std::vector<std::string> args;
        if (!check_text(")")) {
            do args.push_back(expression()); while (take(","));
        }
        require_text(")", "se esperaba ')' después de los argumentos");
        return args;
    }
    std::vector<std::string> arguments_after(const std::string&,
                                             const std::string& close) {
        std::vector<std::string> args;
        if (!check_text(close)) {
            do args.push_back(expression()); while (take(","));
        }
        require_text(close, "se esperaba el cierre de la lista");
        return args;
    }

    std::string call(const Token& name, const std::vector<std::string>& args) {
        if (name.text == "longitud") return "cfv_length(" + one(name, args) + ")";
        if (name.text == "a_texto") return "cfv_text(cfv_format(" + one(name, args) + "))";
        if (name.text == "agregar") {
            if (args.size() != 2) fail(name, "agregar requiere dos argumentos");
            return "cfv_append(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "afirmar") {
            if (args.size() != 2) fail(name, "afirmar requiere dos argumentos");
            return "cfv_assert(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "argumentos_programa") {
            if (!args.empty()) fail(name, "argumentos_programa no recibe argumentos");
            return "cfv_arguments()";
        }
        if (name.text == "leer_archivo") {
            return "cfv_read_file(" + one(name, args) + ")";
        }
        if (name.text == "escribir_archivo") {
            if (args.size() != 2) fail(name, "escribir_archivo requiere dos argumentos");
            return "cfv_write_file(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "eliminar_archivo") {
            return "cfv_remove_file(" + one(name, args) + ")";
        }
        if (name.text == "bytes_texto") {
            return "cfv_text_bytes(" + one(name, args) + ")";
        }
        if (name.text == "escribir_bytes") {
            if (args.size() != 2) fail(name, "escribir_bytes requiere dos argumentos");
            return "cfv_write_bytes(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "hacer_ejecutable") {
            return "cfv_make_executable(" + one(name, args) + ")";
        }
        if (name.text == "sha256_rango") {
            if (args.size() != 3) fail(name, "sha256_rango requiere tres argumentos");
            return "cfv_sha256_range(" + args[0] + ", " + args[1] + ", " + args[2] + ")";
        }
        if (name.text == "compilar_cpp_nativo") {
            if (args.size() != 2) fail(name, "compilar_cpp_nativo requiere dos argumentos");
            return "cfv_compile_cpp(" + args[0] + ", " + args[1] + ")";
        }
        if (!contains(functions_, name.text)) {
            fail(name, "función desconocida '" + name.text + "'");
        }
        return "cfv_fn_" + name.text + "(" + vector_expression(args) + ")";
    }
    std::string one(const Token& name, const std::vector<std::string>& args) {
        if (args.size() != 1) fail(name, name.text + " requiere un argumento");
        return args[0];
    }
    std::string variable(const Token& name) const {
        if (name.text == "este") return "cfv_este";
        return safe(name.text);
    }
    static std::string vector_expression(const std::vector<std::string>& values) {
        std::ostringstream output;
        output << "std::vector<Value>{";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index) output << ", ";
            output << values[index];
        }
        return output.str() + "}";
    }
    static std::string string_vector(const std::vector<std::string>& values) {
        std::ostringstream output;
        output << "std::vector<std::string>{";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index) output << ", ";
            output << cpp_string(values[index]);
        }
        return output.str() + "}";
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

    static std::string runtime() {
        return R"CPP(#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <variant>
#include <vector>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif

struct Value;
using List = std::vector<Value>;
using Object = std::map<std::string, Value>;
struct Value {
    using Data = std::variant<std::monostate, double, bool, std::string,
                              std::shared_ptr<List>, std::shared_ptr<Object>>;
    Data data;
    Value() = default;
    explicit Value(double value) : data(value) {}
    explicit Value(bool value) : data(value) {}
    explicit Value(std::string value) : data(std::move(value)) {}
    explicit Value(std::shared_ptr<List> value) : data(std::move(value)) {}
    explicit Value(std::shared_ptr<Object> value) : data(std::move(value)) {}
};
static std::vector<Value> cfv_process_args;
static Value cfv_number(double value) { return Value(value); }
static Value cfv_text(std::string value) { return Value(std::move(value)); }
static Value cfv_bool(bool value) { return Value(value); }
static double cfv_num(const Value& value) {
    if (const auto* found = std::get_if<double>(&value.data)) return *found;
    throw std::runtime_error("se esperaba numero");
}
static bool cfv_truth(const Value& value) {
    if (const auto* found = std::get_if<bool>(&value.data)) return *found;
    if (const auto* found = std::get_if<double>(&value.data)) return *found != 0;
    if (const auto* found = std::get_if<std::string>(&value.data)) return !found->empty();
    if (const auto* found = std::get_if<std::shared_ptr<List>>(&value.data))
        return !(*found)->empty();
    return !std::holds_alternative<std::monostate>(value.data);
}
static std::string cfv_format(const Value& value) {
    if (std::holds_alternative<std::monostate>(value.data)) return "nulo";
    if (const auto* found = std::get_if<bool>(&value.data))
        return *found ? "verdadero" : "falso";
    if (const auto* found = std::get_if<std::string>(&value.data)) return *found;
    if (const auto* found = std::get_if<double>(&value.data)) {
        if (std::floor(*found) == *found) return std::to_string(static_cast<long long>(*found));
        std::ostringstream output; output << std::setprecision(15) << *found;
        return output.str();
    }
    return "<objeto>";
}
static bool cfv_equal(const Value& left, const Value& right) {
    if (left.data.index() != right.data.index()) return false;
    if (const auto* a = std::get_if<double>(&left.data))
        return *a == std::get<double>(right.data);
    if (const auto* a = std::get_if<bool>(&left.data))
        return *a == std::get<bool>(right.data);
    if (const auto* a = std::get_if<std::string>(&left.data))
        return *a == std::get<std::string>(right.data);
    return left.data == right.data;
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
static Value cfv_sub(const Value& a, const Value& b) { return Value(cfv_num(a) - cfv_num(b)); }
static Value cfv_mul(const Value& a, const Value& b) { return Value(cfv_num(a) * cfv_num(b)); }
static Value cfv_div(const Value& a, const Value& b) {
    const double divisor = cfv_num(b);
    if (divisor == 0) throw std::runtime_error("división por cero");
    return Value(cfv_num(a) / divisor);
}
static Value cfv_mod(const Value& a, const Value& b) {
    return Value(std::fmod(cfv_num(a), cfv_num(b)));
}
static Value cfv_neg(const Value& value) { return Value(-cfv_num(value)); }
static Value cfv_compare(const Value& a, const Value& b, const std::string& op) {
    if (const auto* left = std::get_if<double>(&a.data)) {
        const double right = cfv_num(b);
        return Value(op == "<" ? *left < right : op == "<=" ? *left <= right :
                     op == ">" ? *left > right : *left >= right);
    }
    const auto* left = std::get_if<std::string>(&a.data);
    const auto* right = std::get_if<std::string>(&b.data);
    if (!left || !right) throw std::runtime_error("comparación incompatible");
    return Value(op == "<" ? *left < *right : op == "<=" ? *left <= *right :
                 op == ">" ? *left > *right : *left >= *right);
}
static Value cfv_list(const std::vector<Value>& values) {
    return Value(std::make_shared<List>(values));
}
static Value cfv_object(const std::string& type,
                        const std::vector<std::string>& fields,
                        const std::vector<Value>& values) {
    if (fields.size() != values.size())
        throw std::runtime_error(type + " recibió una cantidad de campos inválida");
    auto object = std::make_shared<Object>();
    (*object)["__tipo"] = Value(type);
    for (std::size_t i = 0; i < fields.size(); ++i) (*object)[fields[i]] = values[i];
    return Value(object);
}
static Value cfv_index(const Value& value, const Value& index) {
    const auto position = static_cast<std::size_t>(cfv_num(index));
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value.data)) {
        if (position >= (*list)->size()) throw std::runtime_error("índice fuera de rango");
        return (**list)[position];
    }
    if (const auto* text = std::get_if<std::string>(&value.data)) {
        if (position >= text->size()) throw std::runtime_error("índice fuera de rango");
        return Value(std::string(1, (*text)[position]));
    }
    throw std::runtime_error("el valor no admite índices");
}
static Value cfv_member(const Value& value, const std::string& field) {
    const auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);
    if (!object) throw std::runtime_error("se esperaba un objeto");
    const auto found = (*object)->find(field);
    if (found == (*object)->end()) throw std::runtime_error("campo desconocido " + field);
    return found->second;
}
static Value& cfv_member_ref(Value& value, const std::string& field) {
    auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);
    if (!object) throw std::runtime_error("se esperaba un objeto mutable");
    const auto found = (*object)->find(field);
    if (found == (*object)->end()) throw std::runtime_error("campo desconocido " + field);
    return found->second;
}
static Value cfv_length(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value.data))
        return Value(static_cast<double>(text->size()));
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value.data))
        return Value(static_cast<double>((*list)->size()));
    throw std::runtime_error("longitud requiere texto o lista");
}
static Value cfv_append(Value value, const Value& item) {
    auto* list = std::get_if<std::shared_ptr<List>>(&value.data);
    if (!list) throw std::runtime_error("agregar requiere una lista");
    (*list)->push_back(item);
    return Value();
}
static Value cfv_assert(const Value& condition, const Value& message) {
    if (!cfv_truth(condition)) throw std::runtime_error(cfv_format(message));
    return Value();
}
static std::string cfv_required_text(const Value& value,
                                     const std::string& function) {
    if (const auto* text = std::get_if<std::string>(&value.data)) return *text;
    throw std::runtime_error(function + " requiere texto");
}
static Value cfv_arguments() {
    return cfv_list(cfv_process_args);
}
static Value cfv_read_file(const Value& path_value) {
    const std::string path = cfv_required_text(path_value, "leer_archivo");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("no se pudo abrir " + path);
    return Value(std::string(std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()));
}
static Value cfv_write_file(const Value& path_value, const Value& content_value) {
    const std::string path = cfv_required_text(path_value, "escribir_archivo");
    const std::string content =
        cfv_required_text(content_value, "escribir_archivo");
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("no se pudo escribir " + path);
    stream << content;
    if (!stream) throw std::runtime_error("escritura incompleta en " + path);
    return Value(true);
}
static Value cfv_remove_file(const Value& path_value) {
    const std::string path = cfv_required_text(path_value, "eliminar_archivo");
    return Value(std::remove(path.c_str()) == 0);
}
static Value cfv_text_bytes(const Value& text_value) {
    const std::string text = cfv_required_text(text_value, "bytes_texto");
    std::vector<Value> bytes;
    bytes.reserve(text.size());
    for (const unsigned char byte : text)
        bytes.emplace_back(static_cast<double>(byte));
    return cfv_list(bytes);
}
static Value cfv_write_bytes(const Value& path_value, const Value& bytes_value) {
    const std::string path = cfv_required_text(path_value, "escribir_bytes");
    const auto* bytes = std::get_if<std::shared_ptr<List>>(&bytes_value.data);
    if (!bytes) throw std::runtime_error("escribir_bytes requiere una lista");
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("no se pudo escribir " + path);
    for (const Value& value : **bytes) {
        const double number = cfv_num(value);
        if (number < 0 || number > 255 || std::floor(number) != number)
            throw std::runtime_error("byte fuera de rango");
        stream.put(static_cast<char>(static_cast<unsigned char>(number)));
    }
    return Value(static_cast<bool>(stream));
}
static Value cfv_make_executable(const Value& path_value) {
    const std::string path = cfv_required_text(path_value, "hacer_ejecutable");
    return Value(::chmod(path.c_str(), 0755) == 0);
}
static Value cfv_sha256_range(const Value& bytes_value,
                              const Value& start_value,
                              const Value& count_value) {
    const auto* bytes = std::get_if<std::shared_ptr<List>>(&bytes_value.data);
    if (!bytes) throw std::runtime_error("sha256_rango requiere una lista");
    const auto start = static_cast<std::size_t>(cfv_num(start_value));
    const auto count = static_cast<std::size_t>(cfv_num(count_value));
    if (start > (*bytes)->size() || count > (*bytes)->size() - start)
        throw std::runtime_error("sha256_rango fuera de límites");
    std::vector<unsigned char> input;
    input.reserve(count);
    for (std::size_t index = start; index < start + count; ++index) {
        const double number = cfv_num((**bytes)[index]);
        if (number < 0 || number > 255 || std::floor(number) != number)
            throw std::runtime_error("byte fuera de rango");
        input.push_back(static_cast<unsigned char>(number));
    }
#if defined(__APPLE__)
    unsigned char digest[CC_SHA256_DIGEST_LENGTH] = {};
    CC_SHA256(input.data(), static_cast<CC_LONG>(input.size()), digest);
    std::vector<Value> result;
    result.reserve(CC_SHA256_DIGEST_LENGTH);
    for (const unsigned char byte : digest)
        result.emplace_back(static_cast<double>(byte));
    return cfv_list(result);
#else
    throw std::runtime_error(
        "sha256_rango todavía requiere el backend criptográfico del objetivo");
#endif
}
static std::string cfv_shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char byte : value) {
        if (byte == '\'') quoted += "'\\''";
        else quoted.push_back(byte);
    }
    return quoted + "'";
}
#if defined(__APPLE__)
static bool cfv_normalize_macho_uuid(const std::string& path) {
    std::fstream stream(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!stream) return false;
    std::uint32_t magic = 0;
    std::uint32_t commands = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != 0xfeedfacf && magic != 0xfeedface) return false;
    stream.seekg(16);
    stream.read(reinterpret_cast<char*>(&commands), sizeof(commands));
    std::streamoff offset = magic == 0xfeedfacf ? 32 : 28;
    for (std::uint32_t index = 0; index < commands; ++index) {
        std::uint32_t command = 0;
        std::uint32_t size = 0;
        stream.seekg(offset);
        stream.read(reinterpret_cast<char*>(&command), sizeof(command));
        stream.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (!stream || size < 8) return false;
        if (command == 0x1b && size >= 24) {
            const char zero_uuid[16] = {};
            stream.seekp(offset + 8);
            stream.write(zero_uuid, sizeof(zero_uuid));
            stream.flush();
            return static_cast<bool>(stream);
        }
        offset += size;
    }
    return false;
}
static bool cfv_sign_reproducible_macos(const std::string& path) {
    if (!cfv_normalize_macho_uuid(path)) return false;
    const std::string command =
        "codesign --force --sign - --identifier "
        "org.vemoris.cforge.bootstrap " +
        cfv_shell_quote(path) + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}
#endif
static Value cfv_compile_cpp(const Value& source_value, const Value& output_value) {
    const std::string source =
        cfv_required_text(source_value, "compilar_cpp_nativo");
    const std::string output =
        cfv_required_text(output_value, "compilar_cpp_nativo");
    std::string command =
        "clang++ -std=c++17 -O2 " + cfv_shell_quote(source) +
        " -o " + cfv_shell_quote(output);
#if defined(__linux__)
    command += " -Wl,--build-id=none";
#endif
    if (std::system(command.c_str()) != 0) return Value(false);
#if defined(__APPLE__)
    return Value(cfv_sign_reproducible_macos(output));
#else
    return Value(true);
#endif
}
static Value cfv_arg(const std::vector<Value>& args, std::size_t index,
                     const std::string& function) {
    if (index >= args.size()) throw std::runtime_error(function + ": faltan argumentos");
    return args[index];
}
static void cfv_print(const Value& value) { std::cout << cfv_format(value) << '\n'; }
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
        std::vector<Token> tokens = Lexer(read_file(input)).scan();
        bool requires_b1 = false;
        for (const auto& token : tokens) {
            if (token.text == "funcion" || token.text == "estructura" ||
                token.text == "clase" || token.text == "mientras" ||
                token.text == "si") {
                requires_b1 = true;
                break;
            }
        }
        const std::string generated = requires_b1
            ? CoreB1Compiler(std::move(tokens)).compile_to_cpp()
            : Parser(std::move(tokens)).compile_to_cpp();
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
