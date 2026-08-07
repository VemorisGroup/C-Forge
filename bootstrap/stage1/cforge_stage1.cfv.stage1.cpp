#include <cmath>
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
#include <variant>
#include <vector>

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
    const char* cfv_cxx_env = std::getenv("CXX");
    std::string cfv_cxx =
        (cfv_cxx_env && *cfv_cxx_env) ? std::string(cfv_cxx_env) :
#if defined(__linux__)
        "g++";
#else
        "clang++";
#endif
    std::string command =
        cfv_cxx + " -std=c++17 -O2 " + cfv_shell_quote(source) +
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
static Value cfv_map(const std::vector<Value>& entries) {
    if (entries.size() % 2 != 0) throw std::runtime_error("mapa interno incompleto");
    auto object = std::make_shared<Object>();
    for (std::size_t i = 0; i < entries.size(); i += 2) {
        const auto* key = std::get_if<std::string>(&entries[i].data);
        if (!key) throw std::runtime_error("la clave del mapa debe ser texto");
        (*object)[*key] = entries[i + 1];
    }
    return Value(object);
}
static Value cfv_index_any(const Value& value, const Value& index) {
    if (const auto* object = std::get_if<std::shared_ptr<Object>>(&value.data)) {
        const auto* key = std::get_if<std::string>(&index.data);
        if (!key) throw std::runtime_error("la clave del mapa debe ser texto");
        const auto found = (*object)->find(*key);
        if (found == (*object)->end()) throw std::runtime_error("clave desconocida " + *key);
        return found->second;
    }
    return cfv_index(value, index);
}
static Value& cfv_index_ref(Value& value, const Value& index) {
    if (auto* object = std::get_if<std::shared_ptr<Object>>(&value.data)) {
        const auto* key = std::get_if<std::string>(&index.data);
        if (!key) throw std::runtime_error("la clave del mapa debe ser texto");
        return (**object)[*key];
    }
    auto* list = std::get_if<std::shared_ptr<List>>(&value.data);
    if (!list) throw std::runtime_error("el valor no admite asignación indexada");
    const auto position = static_cast<std::size_t>(cfv_num(index));
    if (position >= (*list)->size()) throw std::runtime_error("índice fuera de rango");
    return (**list)[position];
}
static Value cfv_fn_es_espacio(const std::vector<Value>&);
static Value cfv_fn_es_digito(const std::vector<Value>&);
static Value cfv_fn_es_letra(const std::vector<Value>&);
static Value cfv_fn_es_identificador(const std::vector<Value>&);
static Value cfv_fn_tokenizar_core(const std::vector<Value>&);
static Value cfv_fn_nodo_ast_core(const std::vector<Value>&);
static Value cfv_fn_nodo_ast_core_con_hijos(const std::vector<Value>&);
static Value cfv_fn_escapar_ast_core(const std::vector<Value>&);
static Value cfv_fn_ast_core_canonico(const std::vector<Value>&);
static Value cfv_fn_token_actual_core(const std::vector<Value>&);
static Value cfv_fn_token_anterior_core(const std::vector<Value>&);
static Value cfv_fn_esta_al_final_core(const std::vector<Value>&);
static Value cfv_fn_avanzar_parser_core(const std::vector<Value>&);
static Value cfv_fn_comprobar_lexema_core(const std::vector<Value>&);
static Value cfv_fn_tomar_lexema_core(const std::vector<Value>&);
static Value cfv_fn_requerir_lexema_core(const std::vector<Value>&);
static Value cfv_fn_requerir_tipo_core(const std::vector<Value>&);
static Value cfv_fn_tipo_parser_core(const std::vector<Value>&);
static Value cfv_fn_lista_argumentos_parser_core(const std::vector<Value>&);
static Value cfv_fn_primaria_parser_core(const std::vector<Value>&);
static Value cfv_fn_postfix_parser_core(const std::vector<Value>&);
static Value cfv_fn_unaria_parser_core(const std::vector<Value>&);
static Value cfv_fn_producto_parser_core(const std::vector<Value>&);
static Value cfv_fn_suma_parser_core(const std::vector<Value>&);
static Value cfv_fn_comparacion_parser_core(const std::vector<Value>&);
static Value cfv_fn_igualdad_parser_core(const std::vector<Value>&);
static Value cfv_fn_conjuncion_parser_core(const std::vector<Value>&);
static Value cfv_fn_expresion_parser_core(const std::vector<Value>&);
static Value cfv_fn_bloque_parser_core(const std::vector<Value>&);
static Value cfv_fn_declaracion_parser_core(const std::vector<Value>&);
static Value cfv_fn_impresion_parser_core(const std::vector<Value>&);
static Value cfv_fn_sentencia_si_parser_core(const std::vector<Value>&);
static Value cfv_fn_sentencia_mientras_parser_core(const std::vector<Value>&);
static Value cfv_fn_sentencia_retorno_parser_core(const std::vector<Value>&);
static Value cfv_fn_sentencia_lanzar_parser_core(const std::vector<Value>&);
static Value cfv_fn_sentencia_intentar_parser_core(const std::vector<Value>&);
static Value cfv_fn_sentencia_parser_core(const std::vector<Value>&);
static Value cfv_fn_parametros_parser_core(const std::vector<Value>&);
static Value cfv_fn_funcion_parser_core(const std::vector<Value>&);
static Value cfv_fn_estructura_parser_core(const std::vector<Value>&);
static Value cfv_fn_metodo_contrato_parser_core(const std::vector<Value>&);
static Value cfv_fn_interfaz_parser_core(const std::vector<Value>&);
static Value cfv_fn_clase_parser_core(const std::vector<Value>&);
static Value cfv_fn_declaracion_superior_parser_core(const std::vector<Value>&);
static Value cfv_fn_parsear_tokens_core(const std::vector<Value>&);
static Value cfv_fn_parsear_fuente_core(const std::vector<Value>&);
static Value cfv_fn_diagnostico_core(const std::vector<Value>&);
static Value cfv_fn_indice_dos_puntos_core(const std::vector<Value>&);
static Value cfv_fn_segmento_core(const std::vector<Value>&);
static Value cfv_fn_nombre_declaracion_core(const std::vector<Value>&);
static Value cfv_fn_tipo_declaracion_core(const std::vector<Value>&);
static Value cfv_fn_buscar_simbolo_core(const std::vector<Value>&);
static Value cfv_fn_tipo_identificador_core(const std::vector<Value>&);
static Value cfv_fn_tipo_expresion_core(const std::vector<Value>&);
static Value cfv_fn_analizar_sentencia_core(const std::vector<Value>&);
static Value cfv_fn_analizar_semantica_core(const std::vector<Value>&);
static Value cfv_fn_diagnosticos_semanticos_core(const std::vector<Value>&);
static Value cfv_fn_nombre_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_indice_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_error_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_propiedad_activa_core(const std::vector<Value>&);
static Value cfv_fn_objetivo_identificable_core(const std::vector<Value>&);
static Value cfv_fn_analizar_expresion_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_liberar_ambito_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_analizar_bloque_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_analizar_declaracion_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_analizar_sentencia_propiedad_core(const std::vector<Value>&);
static Value cfv_fn_verificar_ownership_core(const std::vector<Value>&);
static Value cfv_fn_si_error_ownership_core(const std::vector<Value>&);
static Value cfv_fn_bajar_expresion_ownership_core(const std::vector<Value>&);
static Value cfv_fn_bajar_ownership_core(const std::vector<Value>&);
static Value cfv_fn_runtime_cpp_core(const std::vector<Value>&);
static Value cfv_fn_runtime_mapas_cpp_core(const std::vector<Value>&);
static Value cfv_fn_nombre_cpp_core(const std::vector<Value>&);
static Value cfv_fn_contiene_texto_core(const std::vector<Value>&);
static Value cfv_fn_nombre_firma_core(const std::vector<Value>&);
static Value cfv_fn_campos_nodo_tipo_core(const std::vector<Value>&);
static Value cfv_fn_contexto_emision_core(const std::vector<Value>&);
static Value cfv_fn_indice_tipo_nativo_core(const std::vector<Value>&);
static Value cfv_fn_vector_textos_cpp_core(const std::vector<Value>&);
static Value cfv_fn_emitir_argumentos_core(const std::vector<Value>&);
static Value cfv_fn_emitir_llamada_core(const std::vector<Value>&);
static Value cfv_fn_emitir_binario_core(const std::vector<Value>&);
static Value cfv_fn_emitir_expresion_core(const std::vector<Value>&);
static Value cfv_fn_emitir_bloque_core(const std::vector<Value>&);
static Value cfv_fn_emitir_objetivo_asignacion_core(const std::vector<Value>&);
static Value cfv_fn_emitir_sentencia_core(const std::vector<Value>&);
static Value cfv_fn_emitir_parametros_funcion_core(const std::vector<Value>&);
static Value cfv_fn_emitir_funcion_core(const std::vector<Value>&);
static Value cfv_fn_prototipos_core(const std::vector<Value>&);
static Value cfv_fn_definiciones_core(const std::vector<Value>&);
static Value cfv_fn_cuerpo_principal_core(const std::vector<Value>&);
static Value cfv_fn_emitir_programa_core(const std::vector<Value>&);
static Value cfv_fn_compilar_fuente_stage1(const std::vector<Value>&);
static Value cfv_fn_diagnosticos_stage1(const std::vector<Value>&);
static Value cfv_method_actual(Value, const std::vector<Value>&);
static Value cfv_method_anterior(Value, const std::vector<Value>&);
static Value cfv_method_finalizado(Value, const std::vector<Value>&);
static Value cfv_method_avanzar(Value, const std::vector<Value>&);
static Value cfv_method_marcar_movido(Value, const std::vector<Value>&);

static Value cfv_fn_es_espacio(const std::vector<Value>& cfv_args) {
    Value cfv_caracter = cfv_arg(cfv_args, 0, "es_espacio");
    return cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text(" ")))) || cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("\t")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("\r")))));
    return Value();
}
static Value cfv_fn_es_digito(const std::vector<Value>& cfv_args) {
    Value cfv_caracter = cfv_arg(cfv_args, 0, "es_digito");
    return cfv_bool(cfv_truth(cfv_compare(cfv_caracter, cfv_text("0"), ">=")) && cfv_truth(cfv_compare(cfv_caracter, cfv_text("9"), "<=")));
    return Value();
}
static Value cfv_fn_es_letra(const std::vector<Value>& cfv_args) {
    Value cfv_caracter = cfv_arg(cfv_args, 0, "es_letra");
    return cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_caracter, cfv_text("a"), ">=")) && cfv_truth(cfv_compare(cfv_caracter, cfv_text("z"), "<=")))) || cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_caracter, cfv_text("A"), ">=")) && cfv_truth(cfv_compare(cfv_caracter, cfv_text("Z"), "<=")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("_")))));
    return Value();
}
static Value cfv_fn_es_identificador(const std::vector<Value>& cfv_args) {
    Value cfv_caracter = cfv_arg(cfv_args, 0, "es_identificador");
    return cfv_bool(cfv_truth(cfv_fn_es_letra(std::vector<Value>{cfv_caracter})) || cfv_truth(cfv_fn_es_digito(std::vector<Value>{cfv_caracter})));
    return Value();
}
static Value cfv_fn_tokenizar_core(const std::vector<Value>& cfv_args) {
    Value cfv_fuente = cfv_arg(cfv_args, 0, "tokenizar_core");
    Value cfv_tokens = cfv_list(std::vector<Value>{});
    Value cfv_posicion = cfv_number(0);
    Value cfv_linea = cfv_number(1);
    while (cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<"))) {
        Value cfv_actual = cfv_index_any(cfv_fuente, cfv_posicion);
        if (cfv_truth(cfv_bool(cfv_equal(cfv_actual, cfv_text("\n"))))) {
            cfv_linea = cfv_add(cfv_linea, cfv_number(1));
            cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
        } else {
            if (cfv_truth(cfv_fn_es_espacio(std::vector<Value>{cfv_actual}))) {
                cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
            } else {
                if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_actual, cfv_text("/")))) && cfv_truth(cfv_compare(cfv_add(cfv_posicion, cfv_number(1)), cfv_length(cfv_fuente), "<")))) && cfv_truth(cfv_bool(cfv_equal(cfv_index_any(cfv_fuente, cfv_add(cfv_posicion, cfv_number(1))), cfv_text("/"))))))) {
                    while (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<")) && cfv_truth(cfv_bool(!cfv_equal(cfv_index_any(cfv_fuente, cfv_posicion), cfv_text("\n"))))))) {
                        cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                    }
                } else {
                    if (cfv_truth(cfv_fn_es_letra(std::vector<Value>{cfv_actual}))) {
                        Value cfv_inicio = cfv_posicion;
                        while (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<")) && cfv_truth(cfv_fn_es_identificador(std::vector<Value>{cfv_index_any(cfv_fuente, cfv_posicion)}))))) {
                            cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                        }
                        Value cfv_lexema = cfv_text("");
                        Value cfv_cursor = cfv_inicio;
                        while (cfv_truth(cfv_compare(cfv_cursor, cfv_posicion, "<"))) {
                            cfv_lexema = cfv_add(cfv_lexema, cfv_index_any(cfv_fuente, cfv_cursor));
                            cfv_cursor = cfv_add(cfv_cursor, cfv_number(1));
                        }
                        (void)(cfv_append(cfv_tokens, cfv_object("TokenCore", std::vector<std::string>{"tipo", "lexema", "linea"}, std::vector<Value>{cfv_text("IDENT"), cfv_lexema, cfv_linea})));
                    } else {
                        if (cfv_truth(cfv_fn_es_digito(std::vector<Value>{cfv_actual}))) {
                            Value cfv_inicio_numero = cfv_posicion;
                            while (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<")) && cfv_truth(cfv_fn_es_digito(std::vector<Value>{cfv_index_any(cfv_fuente, cfv_posicion)}))))) {
                                cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                            }
                            Value cfv_numero_texto = cfv_text("");
                            Value cfv_cursor_numero = cfv_inicio_numero;
                            while (cfv_truth(cfv_compare(cfv_cursor_numero, cfv_posicion, "<"))) {
                                cfv_numero_texto = cfv_add(cfv_numero_texto, cfv_index_any(cfv_fuente, cfv_cursor_numero));
                                cfv_cursor_numero = cfv_add(cfv_cursor_numero, cfv_number(1));
                            }
                            (void)(cfv_append(cfv_tokens, cfv_object("TokenCore", std::vector<std::string>{"tipo", "lexema", "linea"}, std::vector<Value>{cfv_text("NUMBER"), cfv_numero_texto, cfv_linea})));
                        } else {
                            if (cfv_truth(cfv_bool(cfv_equal(cfv_actual, cfv_text("\""))))) {
                                Value cfv_linea_texto = cfv_linea;
                                Value cfv_literal = cfv_text("\"");
                                cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                                Value cfv_cerrado = cfv_bool(false);
                                while (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<")) && cfv_truth(cfv_bool(!cfv_truth(cfv_cerrado)))))) {
                                    Value cfv_parte = cfv_index_any(cfv_fuente, cfv_posicion);
                                    cfv_literal = cfv_add(cfv_literal, cfv_parte);
                                    cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                                    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_parte, cfv_text("\\")))) && cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<"))))) {
                                        cfv_literal = cfv_add(cfv_literal, cfv_index_any(cfv_fuente, cfv_posicion));
                                        cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                                    } else {
                                        if (cfv_truth(cfv_bool(cfv_equal(cfv_parte, cfv_text("\""))))) {
                                            cfv_cerrado = cfv_bool(true);
                                        } else {
                                            if (cfv_truth(cfv_bool(cfv_equal(cfv_parte, cfv_text("\n"))))) {
                                                cfv_linea = cfv_add(cfv_linea, cfv_number(1));
                                            }
                                        }
                                    }
                                }
                                (void)(cfv_assert(cfv_cerrado, cfv_add(cfv_text("texto sin cerrar en línea "), cfv_text(cfv_format(cfv_linea_texto)))));
                                (void)(cfv_append(cfv_tokens, cfv_object("TokenCore", std::vector<std::string>{"tipo", "lexema", "linea"}, std::vector<Value>{cfv_text("STRING"), cfv_literal, cfv_linea_texto})));
                            } else {
                                Value cfv_simbolo = cfv_actual;
                                cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                                if (cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_fuente), "<"))) {
                                    Value cfv_par = cfv_add(cfv_simbolo, cfv_index_any(cfv_fuente, cfv_posicion));
                                    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_par, cfv_text("==")))) || cfv_truth(cfv_bool(cfv_equal(cfv_par, cfv_text("!=")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_par, cfv_text(">=")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_par, cfv_text("<="))))))) {
                                        cfv_simbolo = cfv_par;
                                        cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
                                    }
                                }
                                (void)(cfv_append(cfv_tokens, cfv_object("TokenCore", std::vector<std::string>{"tipo", "lexema", "linea"}, std::vector<Value>{cfv_text("SYMBOL"), cfv_simbolo, cfv_linea})));
                            }
                        }
                    }
                }
            }
        }
    }
    (void)(cfv_append(cfv_tokens, cfv_object("TokenCore", std::vector<std::string>{"tipo", "lexema", "linea"}, std::vector<Value>{cfv_text("EOF"), cfv_text(""), cfv_linea})));
    return cfv_tokens;
    return Value();
}
static Value cfv_fn_nodo_ast_core(const std::vector<Value>& cfv_args) {
    Value cfv_tipo = cfv_arg(cfv_args, 0, "nodo_ast_core");
    Value cfv_valor = cfv_arg(cfv_args, 1, "nodo_ast_core");
    Value cfv_linea = cfv_arg(cfv_args, 2, "nodo_ast_core");
    return cfv_object("NodoASTCore", std::vector<std::string>{"tipo", "valor", "linea", "hijos"}, std::vector<Value>{cfv_tipo, cfv_valor, cfv_linea, cfv_list(std::vector<Value>{})});
    return Value();
}
static Value cfv_fn_nodo_ast_core_con_hijos(const std::vector<Value>& cfv_args) {
    Value cfv_tipo = cfv_arg(cfv_args, 0, "nodo_ast_core_con_hijos");
    Value cfv_valor = cfv_arg(cfv_args, 1, "nodo_ast_core_con_hijos");
    Value cfv_linea = cfv_arg(cfv_args, 2, "nodo_ast_core_con_hijos");
    Value cfv_hijos = cfv_arg(cfv_args, 3, "nodo_ast_core_con_hijos");
    return cfv_object("NodoASTCore", std::vector<std::string>{"tipo", "valor", "linea", "hijos"}, std::vector<Value>{cfv_tipo, cfv_valor, cfv_linea, cfv_hijos});
    return Value();
}
static Value cfv_fn_escapar_ast_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "escapar_ast_core");
    Value cfv_salida = cfv_text("");
    Value cfv_posicion = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_valor), "<"))) {
        Value cfv_caracter = cfv_index_any(cfv_valor, cfv_posicion);
        if (cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("\\"))))) {
            cfv_salida = cfv_add(cfv_salida, cfv_text("\\\\"));
        } else {
            if (cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("\n"))))) {
                cfv_salida = cfv_add(cfv_salida, cfv_text("\\n"));
            } else {
                if (cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("\r"))))) {
                    cfv_salida = cfv_add(cfv_salida, cfv_text("\\r"));
                } else {
                    if (cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("\t"))))) {
                        cfv_salida = cfv_add(cfv_salida, cfv_text("\\t"));
                    } else {
                        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("[")))) || cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("]")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text(":")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_caracter, cfv_text("@"))))))) {
                            cfv_salida = cfv_add(cfv_add(cfv_salida, cfv_text("\\")), cfv_caracter);
                        } else {
                            cfv_salida = cfv_add(cfv_salida, cfv_caracter);
                        }
                    }
                }
            }
        }
        cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_ast_core_canonico(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "ast_core_canonico");
    Value cfv_salida = cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_member(cfv_nodo, "tipo"), cfv_text(":")), cfv_text(cfv_format(cfv_length(cfv_member(cfv_nodo, "valor"))))), cfv_text(":")), cfv_fn_escapar_ast_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")})), cfv_text("@")), cfv_text(cfv_format(cfv_member(cfv_nodo, "linea")))), cfv_text("["));
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_hijos), "<"))) {
        if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">"))) {
            cfv_salida = cfv_add(cfv_salida, cfv_text(","));
        }
        cfv_salida = cfv_add(cfv_salida, cfv_fn_ast_core_canonico(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_indice)}));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_add(cfv_salida, cfv_text("]"));
    return Value();
}
static Value cfv_method_actual(Value cfv_este, const std::vector<Value>& cfv_args) {
    Value cfv_tokens_actuales = cfv_member(cfv_este, "tokens");
    Value cfv_posicion_actual = cfv_member(cfv_este, "posicion");
    return cfv_index_any(cfv_tokens_actuales, cfv_posicion_actual);
    return Value();
}
static Value cfv_method_anterior(Value cfv_este, const std::vector<Value>& cfv_args) {
    Value cfv_tokens_actuales = cfv_member(cfv_este, "tokens");
    Value cfv_posicion_anterior = cfv_sub(cfv_member(cfv_este, "posicion"), cfv_number(1));
    return cfv_index_any(cfv_tokens_actuales, cfv_posicion_anterior);
    return Value();
}
static Value cfv_method_finalizado(Value cfv_este, const std::vector<Value>& cfv_args) {
    Value cfv_token = cfv_method_actual(cfv_este, std::vector<Value>{});
    return cfv_bool(cfv_equal(cfv_member(cfv_token, "tipo"), cfv_text("EOF")));
    return Value();
}
static Value cfv_method_avanzar(Value cfv_este, const std::vector<Value>& cfv_args) {
    if (cfv_truth(cfv_bool(!cfv_truth(cfv_method_finalizado(cfv_este, std::vector<Value>{}))))) {
        Value cfv_posicion_actual = cfv_member(cfv_este, "posicion");
        cfv_member_ref(cfv_este, "posicion") = cfv_add(cfv_posicion_actual, cfv_number(1));
    }
    return cfv_method_anterior(cfv_este, std::vector<Value>{});
    return Value();
}
static Value cfv_fn_token_actual_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "token_actual_core");
    return cfv_method_actual(cfv_estado, std::vector<Value>{});
    return Value();
}
static Value cfv_fn_token_anterior_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "token_anterior_core");
    return cfv_method_anterior(cfv_estado, std::vector<Value>{});
    return Value();
}
static Value cfv_fn_esta_al_final_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "esta_al_final_core");
    Value cfv_token = cfv_fn_token_actual_core(std::vector<Value>{cfv_estado});
    return cfv_bool(cfv_equal(cfv_member(cfv_token, "tipo"), cfv_text("EOF")));
    return Value();
}
static Value cfv_fn_avanzar_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "avanzar_parser_core");
    return cfv_method_avanzar(cfv_estado, std::vector<Value>{});
    return Value();
}
static Value cfv_fn_comprobar_lexema_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "comprobar_lexema_core");
    Value cfv_lexema = cfv_arg(cfv_args, 1, "comprobar_lexema_core");
    Value cfv_token = cfv_fn_token_actual_core(std::vector<Value>{cfv_estado});
    return cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_lexema));
    return Value();
}
static Value cfv_fn_tomar_lexema_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "tomar_lexema_core");
    Value cfv_lexema = cfv_arg(cfv_args, 1, "tomar_lexema_core");
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_lexema}))) {
        (void)(cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado}));
        return cfv_bool(true);
    }
    return cfv_bool(false);
    return Value();
}
static Value cfv_fn_requerir_lexema_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "requerir_lexema_core");
    Value cfv_lexema = cfv_arg(cfv_args, 1, "requerir_lexema_core");
    Value cfv_mensaje = cfv_arg(cfv_args, 2, "requerir_lexema_core");
    Value cfv_token = cfv_fn_token_actual_core(std::vector<Value>{cfv_estado});
    (void)(cfv_assert(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_lexema)), cfv_add(cfv_add(cfv_mensaje, cfv_text(" en línea ")), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
    return cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
    return Value();
}
static Value cfv_fn_requerir_tipo_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "requerir_tipo_core");
    Value cfv_tipo = cfv_arg(cfv_args, 1, "requerir_tipo_core");
    Value cfv_mensaje = cfv_arg(cfv_args, 2, "requerir_tipo_core");
    Value cfv_token = cfv_fn_token_actual_core(std::vector<Value>{cfv_estado});
    (void)(cfv_assert(cfv_bool(cfv_equal(cfv_member(cfv_token, "tipo"), cfv_tipo)), cfv_add(cfv_add(cfv_mensaje, cfv_text(" en línea ")), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
    return cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
    return Value();
}
static Value cfv_fn_tipo_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "tipo_parser_core");
    Value cfv_token = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba un tipo")});
    return cfv_member(cfv_token, "lexema");
    return Value();
}
static Value cfv_fn_lista_argumentos_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "lista_argumentos_parser_core");
    Value cfv_cierre = cfv_arg(cfv_args, 1, "lista_argumentos_parser_core");
    Value cfv_argumentos = cfv_list(std::vector<Value>{});
    if (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_cierre}))))) {
        (void)(cfv_append(cfv_argumentos, cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado})));
        while (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(",")}))) {
            (void)(cfv_append(cfv_argumentos, cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado})));
        }
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_cierre, cfv_add(cfv_add(cfv_text("se esperaba '"), cfv_cierre), cfv_text("'"))}));
    return cfv_argumentos;
    return Value();
}
static Value cfv_fn_primaria_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "primaria_parser_core");
    Value cfv_token = cfv_fn_token_actual_core(std::vector<Value>{cfv_estado});
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "tipo"), cfv_text("NUMBER"))))) {
        (void)(cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado}));
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Numero"), cfv_member(cfv_token, "lexema"), cfv_member(cfv_token, "linea")});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "tipo"), cfv_text("STRING"))))) {
        (void)(cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado}));
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Texto"), cfv_member(cfv_token, "lexema"), cfv_member(cfv_token, "linea")});
    }
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_text("verdadero")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_text("falso"))))))) {
        (void)(cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado}));
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Booleano"), cfv_member(cfv_token, "lexema"), cfv_member(cfv_token, "linea")});
    }
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("[")}))) {
        return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Lista"), cfv_text("lista"), cfv_member(cfv_token, "linea"), cfv_fn_lista_argumentos_parser_core(std::vector<Value>{cfv_estado, cfv_text("]")})});
    }
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("{")}))) {
        Value cfv_entradas = cfv_list(std::vector<Value>{});
        if (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}")}))))) {
            Value cfv_continuar_mapa = cfv_bool(true);
            while (cfv_truth(cfv_continuar_mapa)) {
                Value cfv_clave = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
                (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":"), cfv_text("se esperaba ':' después de la clave del mapa")}));
                Value cfv_valor_mapa = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
                (void)(cfv_append(cfv_entradas, cfv_clave));
                (void)(cfv_append(cfv_entradas, cfv_valor_mapa));
                cfv_continuar_mapa = cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(",")});
            }
        }
        (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}"), cfv_text("se esperaba '}' en el mapa")}));
        return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Mapa"), cfv_text("mapa"), cfv_member(cfv_token, "linea"), cfv_entradas});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "tipo"), cfv_text("IDENT"))))) {
        (void)(cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado}));
        if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("(")}))) {
            Value cfv_argumentos = cfv_fn_lista_argumentos_parser_core(std::vector<Value>{cfv_estado, cfv_text(")")});
            if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_text("mover"))))) {
                (void)(cfv_assert(cfv_bool(cfv_equal(cfv_length(cfv_argumentos), cfv_number(1))), cfv_add(cfv_text("mover requiere un argumento en línea "), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
                return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Mover"), cfv_text("mover"), cfv_member(cfv_token, "linea"), cfv_argumentos});
            }
            if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_text("prestar"))))) {
                (void)(cfv_assert(cfv_bool(cfv_equal(cfv_length(cfv_argumentos), cfv_number(1))), cfv_add(cfv_text("prestar requiere un argumento en línea "), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
                return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("PrestamoCompartido"), cfv_text("prestar"), cfv_member(cfv_token, "linea"), cfv_argumentos});
            }
            if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_text("prestar_mut"))))) {
                (void)(cfv_assert(cfv_bool(cfv_equal(cfv_length(cfv_argumentos), cfv_number(1))), cfv_add(cfv_text("prestar_mut requiere un argumento en línea "), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
                return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("PrestamoMutable"), cfv_text("prestar_mut"), cfv_member(cfv_token, "linea"), cfv_argumentos});
            }
            if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_token, "lexema"), cfv_text("soltar_prestamo"))))) {
                (void)(cfv_assert(cfv_bool(cfv_equal(cfv_length(cfv_argumentos), cfv_number(1))), cfv_add(cfv_text("soltar_prestamo requiere un argumento en línea "), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
                return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("SoltarPrestamo"), cfv_text("soltar_prestamo"), cfv_member(cfv_token, "linea"), cfv_argumentos});
            }
            return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Llamada"), cfv_member(cfv_token, "lexema"), cfv_member(cfv_token, "linea"), cfv_argumentos});
        }
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Identificador"), cfv_member(cfv_token, "lexema"), cfv_member(cfv_token, "linea")});
    }
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("(")}))) {
        Value cfv_expresion = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
        (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")"), cfv_text("se esperaba ')' después de la expresión")}));
        return cfv_expresion;
    }
    (void)(cfv_assert(cfv_bool(false), cfv_add(cfv_text("expresión inválida en línea "), cfv_text(cfv_format(cfv_member(cfv_token, "linea"))))));
    return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Inalcanzable"), cfv_text(""), cfv_member(cfv_token, "linea")});
    return Value();
}
static Value cfv_fn_postfix_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "postfix_parser_core");
    Value cfv_expresion = cfv_fn_primaria_parser_core(std::vector<Value>{cfv_estado});
    Value cfv_seguir_postfijo = cfv_bool(true);
    while (cfv_truth(cfv_seguir_postfijo)) {
        if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("[")}))) {
            Value cfv_indice = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
            (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("]"), cfv_text("se esperaba ']'")}));
            cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Indice"), cfv_text("[]"), cfv_member(cfv_expresion, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_indice})});
        } else {
            if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(".")}))) {
                Value cfv_miembro = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el miembro")});
                if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("(")}))) {
                    Value cfv_argumentos = cfv_fn_lista_argumentos_parser_core(std::vector<Value>{cfv_estado, cfv_text(")")});
                    Value cfv_hijos = cfv_list(std::vector<Value>{cfv_expresion});
                    Value cfv_indice_argumento = cfv_number(0);
                    while (cfv_truth(cfv_compare(cfv_indice_argumento, cfv_length(cfv_argumentos), "<"))) {
                        (void)(cfv_append(cfv_hijos, cfv_index_any(cfv_argumentos, cfv_indice_argumento)));
                        cfv_indice_argumento = cfv_add(cfv_indice_argumento, cfv_number(1));
                    }
                    cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("LlamadaMetodo"), cfv_member(cfv_miembro, "lexema"), cfv_member(cfv_miembro, "linea"), cfv_hijos});
                } else {
                    cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Miembro"), cfv_member(cfv_miembro, "lexema"), cfv_member(cfv_miembro, "linea"), cfv_list(std::vector<Value>{cfv_expresion})});
                }
            } else {
                cfv_seguir_postfijo = cfv_bool(false);
            }
        }
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_unaria_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "unaria_parser_core");
    if (cfv_truth(cfv_bool(cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("no")})) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("-")}))))) {
        Value cfv_operador = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
        return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Unario"), cfv_member(cfv_operador, "lexema"), cfv_member(cfv_operador, "linea"), cfv_list(std::vector<Value>{cfv_fn_unaria_parser_core(std::vector<Value>{cfv_estado})})});
    }
    return cfv_fn_postfix_parser_core(std::vector<Value>{cfv_estado});
    return Value();
}
static Value cfv_fn_producto_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "producto_parser_core");
    Value cfv_expresion = cfv_fn_unaria_parser_core(std::vector<Value>{cfv_estado});
    while (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("*")})) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("/")})))) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("%")}))))) {
        Value cfv_operador = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
        Value cfv_derecho = cfv_fn_unaria_parser_core(std::vector<Value>{cfv_estado});
        cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Binario"), cfv_member(cfv_operador, "lexema"), cfv_member(cfv_operador, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_derecho})});
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_suma_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "suma_parser_core");
    Value cfv_expresion = cfv_fn_producto_parser_core(std::vector<Value>{cfv_estado});
    while (cfv_truth(cfv_bool(cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("+")})) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("-")}))))) {
        Value cfv_operador = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
        Value cfv_derecho = cfv_fn_producto_parser_core(std::vector<Value>{cfv_estado});
        cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Binario"), cfv_member(cfv_operador, "lexema"), cfv_member(cfv_operador, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_derecho})});
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_comparacion_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "comparacion_parser_core");
    Value cfv_expresion = cfv_fn_suma_parser_core(std::vector<Value>{cfv_estado});
    while (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("<")})) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("<=")})))) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(">")})))) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(">=")}))))) {
        Value cfv_operador = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
        Value cfv_derecho = cfv_fn_suma_parser_core(std::vector<Value>{cfv_estado});
        cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Binario"), cfv_member(cfv_operador, "lexema"), cfv_member(cfv_operador, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_derecho})});
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_igualdad_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "igualdad_parser_core");
    Value cfv_expresion = cfv_fn_comparacion_parser_core(std::vector<Value>{cfv_estado});
    while (cfv_truth(cfv_bool(cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("==")})) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("!=")}))))) {
        Value cfv_operador = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
        Value cfv_derecho = cfv_fn_comparacion_parser_core(std::vector<Value>{cfv_estado});
        cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Binario"), cfv_member(cfv_operador, "lexema"), cfv_member(cfv_operador, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_derecho})});
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_conjuncion_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "conjuncion_parser_core");
    Value cfv_expresion = cfv_fn_igualdad_parser_core(std::vector<Value>{cfv_estado});
    while (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("y")}))) {
        Value cfv_derecho = cfv_fn_igualdad_parser_core(std::vector<Value>{cfv_estado});
        cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Binario"), cfv_text("y"), cfv_member(cfv_expresion, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_derecho})});
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_expresion_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "expresion_parser_core");
    Value cfv_expresion = cfv_fn_conjuncion_parser_core(std::vector<Value>{cfv_estado});
    while (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("o")}))) {
        Value cfv_derecho = cfv_fn_conjuncion_parser_core(std::vector<Value>{cfv_estado});
        cfv_expresion = cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Binario"), cfv_text("o"), cfv_member(cfv_expresion, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_derecho})});
    }
    return cfv_expresion;
    return Value();
}
static Value cfv_fn_bloque_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "bloque_parser_core");
    Value cfv_apertura = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("{"), cfv_text("se esperaba '{'")});
    Value cfv_sentencias = cfv_list(std::vector<Value>{});
    while (cfv_truth(cfv_bool(cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}")})))) && cfv_truth(cfv_bool(!cfv_truth(cfv_fn_esta_al_final_core(std::vector<Value>{cfv_estado}))))))) {
        (void)(cfv_append(cfv_sentencias, cfv_fn_sentencia_parser_core(std::vector<Value>{cfv_estado})));
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}"), cfv_text("se esperaba '}'")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Bloque"), cfv_text("bloque"), cfv_member(cfv_apertura, "linea"), cfv_sentencias});
    return Value();
}
static Value cfv_fn_declaracion_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "declaracion_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("sea"), cfv_text("se esperaba la declaración 'sea'")});
    Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre de la variable")});
    Value cfv_tipo_declarado = cfv_text("");
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":")}))) {
        cfv_tipo_declarado = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("="), cfv_text("se esperaba '=' en la declaración")}));
    Value cfv_valor = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Declaracion"), cfv_add(cfv_add(cfv_member(cfv_nombre, "lexema"), cfv_text(":")), cfv_tipo_declarado), cfv_member(cfv_palabra, "linea"), cfv_list(std::vector<Value>{cfv_valor})});
    return Value();
}
static Value cfv_fn_impresion_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "impresion_parser_core");
    Value cfv_palabra = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("("), cfv_text("se esperaba '(' después de mostrar")}));
    Value cfv_valor = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")"), cfv_text("se esperaba ')' después del valor")}));
    (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Mostrar"), cfv_member(cfv_palabra, "lexema"), cfv_member(cfv_palabra, "linea"), cfv_list(std::vector<Value>{cfv_valor})});
    return Value();
}
static Value cfv_fn_sentencia_si_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "sentencia_si_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("si"), cfv_text("se esperaba 'si'")});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("("), cfv_text("se esperaba '(' después de si")}));
    Value cfv_condicion = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")"), cfv_text("se esperaba ')' después de condición")}));
    Value cfv_hijos = cfv_list(std::vector<Value>{cfv_condicion, cfv_fn_bloque_parser_core(std::vector<Value>{cfv_estado})});
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("sino")}))) {
        (void)(cfv_append(cfv_hijos, cfv_fn_bloque_parser_core(std::vector<Value>{cfv_estado})));
    }
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Si"), cfv_text("si"), cfv_member(cfv_palabra, "linea"), cfv_hijos});
    return Value();
}
static Value cfv_fn_sentencia_mientras_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "sentencia_mientras_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("mientras"), cfv_text("se esperaba 'mientras'")});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("("), cfv_text("se esperaba '(' después de mientras")}));
    Value cfv_condicion = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")"), cfv_text("se esperaba ')' después de condición")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Mientras"), cfv_text("mientras"), cfv_member(cfv_palabra, "linea"), cfv_list(std::vector<Value>{cfv_condicion, cfv_fn_bloque_parser_core(std::vector<Value>{cfv_estado})})});
    return Value();
}
static Value cfv_fn_sentencia_retorno_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "sentencia_retorno_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("retornar"), cfv_text("se esperaba 'retornar'")});
    Value cfv_hijos = cfv_list(std::vector<Value>{});
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}")})))) && cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}))))))) {
        (void)(cfv_append(cfv_hijos, cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado})));
    }
    (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Retornar"), cfv_text("retornar"), cfv_member(cfv_palabra, "linea"), cfv_hijos});
    return Value();
}
static Value cfv_fn_sentencia_lanzar_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "sentencia_lanzar_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("lanzar"), cfv_text("se esperaba 'lanzar'")});
    Value cfv_error = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Lanzar"), cfv_text("lanzar"), cfv_member(cfv_palabra, "linea"), cfv_list(std::vector<Value>{cfv_error})});
    return Value();
}
static Value cfv_fn_sentencia_intentar_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "sentencia_intentar_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("intentar"), cfv_text("se esperaba 'intentar'")});
    Value cfv_protegido = cfv_fn_bloque_parser_core(std::vector<Value>{cfv_estado});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("capturar"), cfv_text("se esperaba 'capturar' después de intentar")}));
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("("), cfv_text("se esperaba '(' después de capturar")}));
    Value cfv_error = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre del error")});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")"), cfv_text("se esperaba ')' después del error")}));
    Value cfv_captura = cfv_fn_bloque_parser_core(std::vector<Value>{cfv_estado});
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Intentar"), cfv_member(cfv_error, "lexema"), cfv_member(cfv_palabra, "linea"), cfv_list(std::vector<Value>{cfv_protegido, cfv_captura})});
    return Value();
}
static Value cfv_fn_sentencia_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "sentencia_parser_core");
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("sea")}))) {
        return cfv_fn_declaracion_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_bool(cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("mostrar")})) || cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("print")}))))) {
        return cfv_fn_impresion_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("si")}))) {
        return cfv_fn_sentencia_si_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("mientras")}))) {
        return cfv_fn_sentencia_mientras_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("retornar")}))) {
        return cfv_fn_sentencia_retorno_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("lanzar")}))) {
        return cfv_fn_sentencia_lanzar_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("intentar")}))) {
        return cfv_fn_sentencia_intentar_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}))) {
        Value cfv_vacia = cfv_fn_token_anterior_core(std::vector<Value>{cfv_estado});
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Vacia"), cfv_text("vacia"), cfv_member(cfv_vacia, "linea")});
    }
    Value cfv_expresion = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("=")}))) {
        Value cfv_valor = cfv_fn_expresion_parser_core(std::vector<Value>{cfv_estado});
        (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
        return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Asignacion"), cfv_text("="), cfv_member(cfv_expresion, "linea"), cfv_list(std::vector<Value>{cfv_expresion, cfv_valor})});
    }
    (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Expresion"), cfv_text("expresion"), cfv_member(cfv_expresion, "linea"), cfv_list(std::vector<Value>{cfv_expresion})});
    return Value();
}
static Value cfv_fn_parametros_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "parametros_parser_core");
    Value cfv_parametros = cfv_list(std::vector<Value>{});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("("), cfv_text("se esperaba '('")}));
    if (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")")}))))) {
        Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el parámetro")});
        Value cfv_tipo = cfv_text("");
        if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":")}))) {
            cfv_tipo = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
        }
        (void)(cfv_append(cfv_parametros, cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Parametro"), cfv_add(cfv_add(cfv_member(cfv_nombre, "lexema"), cfv_text(":")), cfv_tipo), cfv_member(cfv_nombre, "linea")})));
        while (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(",")}))) {
            cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el parámetro")});
            cfv_tipo = cfv_text("");
            if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":")}))) {
                cfv_tipo = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
            }
            (void)(cfv_append(cfv_parametros, cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Parametro"), cfv_add(cfv_add(cfv_member(cfv_nombre, "lexema"), cfv_text(":")), cfv_tipo), cfv_member(cfv_nombre, "linea")})));
        }
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(")"), cfv_text("se esperaba ')'")}));
    return cfv_parametros;
    return Value();
}
static Value cfv_fn_funcion_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "funcion_parser_core");
    Value cfv_clase_metodo = cfv_arg(cfv_args, 1, "funcion_parser_core");
    Value cfv_palabra = cfv_fn_avanzar_parser_core(std::vector<Value>{cfv_estado});
    Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre de la función")});
    Value cfv_hijos = cfv_fn_parametros_parser_core(std::vector<Value>{cfv_estado});
    Value cfv_retorno = cfv_text("");
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":")}))) {
        cfv_retorno = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
    }
    (void)(cfv_append(cfv_hijos, cfv_fn_bloque_parser_core(std::vector<Value>{cfv_estado})));
    Value cfv_tipo_nodo = cfv_text("Funcion");
    if (cfv_truth(cfv_clase_metodo)) {
        cfv_tipo_nodo = cfv_text("Metodo");
    }
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_tipo_nodo, cfv_add(cfv_add(cfv_member(cfv_nombre, "lexema"), cfv_text(":")), cfv_retorno), cfv_member(cfv_palabra, "linea"), cfv_hijos});
    return Value();
}
static Value cfv_fn_estructura_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "estructura_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("estructura"), cfv_text("se esperaba 'estructura'")});
    Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre de la estructura")});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("{"), cfv_text("se esperaba '{'")}));
    Value cfv_campos = cfv_list(std::vector<Value>{});
    while (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}")}))))) {
        Value cfv_campo = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el campo")});
        (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":"), cfv_text("se esperaba ':'")}));
        Value cfv_tipo = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
        (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
        (void)(cfv_append(cfv_campos, cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Campo"), cfv_add(cfv_add(cfv_member(cfv_campo, "lexema"), cfv_text(":")), cfv_tipo), cfv_member(cfv_campo, "linea")})));
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}"), cfv_text("se esperaba '}'")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Estructura"), cfv_member(cfv_nombre, "lexema"), cfv_member(cfv_palabra, "linea"), cfv_campos});
    return Value();
}
static Value cfv_fn_metodo_contrato_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "metodo_contrato_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("metodo"), cfv_text("se esperaba 'metodo'")});
    Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre del método")});
    Value cfv_hijos = cfv_fn_parametros_parser_core(std::vector<Value>{cfv_estado});
    Value cfv_retorno = cfv_text("cualquiera");
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":")}))) {
        cfv_retorno = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
    }
    (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("MetodoContrato"), cfv_add(cfv_add(cfv_member(cfv_nombre, "lexema"), cfv_text(":")), cfv_retorno), cfv_member(cfv_palabra, "linea"), cfv_hijos});
    return Value();
}
static Value cfv_fn_interfaz_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "interfaz_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("interfaz"), cfv_text("se esperaba 'interfaz'")});
    Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre de la interfaz")});
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("{"), cfv_text("se esperaba '{'")}));
    Value cfv_metodos = cfv_list(std::vector<Value>{});
    while (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}")}))))) {
        (void)(cfv_append(cfv_metodos, cfv_fn_metodo_contrato_parser_core(std::vector<Value>{cfv_estado})));
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}"), cfv_text("se esperaba '}'")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Interfaz"), cfv_member(cfv_nombre, "lexema"), cfv_member(cfv_palabra, "linea"), cfv_metodos});
    return Value();
}
static Value cfv_fn_clase_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "clase_parser_core");
    Value cfv_palabra = cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("clase"), cfv_text("se esperaba 'clase'")});
    Value cfv_nombre = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el nombre de la clase")});
    Value cfv_miembros = cfv_list(std::vector<Value>{});
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("implementa")}))) {
        Value cfv_contrato = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba una interfaz")});
        (void)(cfv_append(cfv_miembros, cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Implementa"), cfv_member(cfv_contrato, "lexema"), cfv_member(cfv_contrato, "linea")})));
        while (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(",")}))) {
            cfv_contrato = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba una interfaz")});
            (void)(cfv_append(cfv_miembros, cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Implementa"), cfv_member(cfv_contrato, "lexema"), cfv_member(cfv_contrato, "linea")})));
        }
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("{"), cfv_text("se esperaba '{'")}));
    while (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}")}))))) {
        if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("campo")}))) {
            Value cfv_campo = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("IDENT"), cfv_text("se esperaba el campo")});
            (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text(":"), cfv_text("se esperaba ':'")}));
            Value cfv_tipo = cfv_fn_tipo_parser_core(std::vector<Value>{cfv_estado});
            (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
            (void)(cfv_append(cfv_miembros, cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Campo"), cfv_add(cfv_add(cfv_member(cfv_campo, "lexema"), cfv_text(":")), cfv_tipo), cfv_member(cfv_campo, "linea")})));
        } else {
            (void)(cfv_assert(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("metodo")}), cfv_text("se esperaba campo o método")));
            (void)(cfv_append(cfv_miembros, cfv_fn_funcion_parser_core(std::vector<Value>{cfv_estado, cfv_bool(true)})));
        }
    }
    (void)(cfv_fn_requerir_lexema_core(std::vector<Value>{cfv_estado, cfv_text("}"), cfv_text("se esperaba '}'")}));
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Clase"), cfv_member(cfv_nombre, "lexema"), cfv_member(cfv_palabra, "linea"), cfv_miembros});
    return Value();
}
static Value cfv_fn_declaracion_superior_parser_core(const std::vector<Value>& cfv_args) {
    Value cfv_estado = cfv_arg(cfv_args, 0, "declaracion_superior_parser_core");
    if (cfv_truth(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("importar")}))) {
        Value cfv_palabra = cfv_fn_token_anterior_core(std::vector<Value>{cfv_estado});
        Value cfv_ruta = cfv_fn_requerir_tipo_core(std::vector<Value>{cfv_estado, cfv_text("STRING"), cfv_text("se esperaba la ruta del módulo")});
        (void)(cfv_fn_tomar_lexema_core(std::vector<Value>{cfv_estado, cfv_text(";")}));
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Importar"), cfv_member(cfv_ruta, "lexema"), cfv_member(cfv_palabra, "linea")});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("interfaz")}))) {
        return cfv_fn_interfaz_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("estructura")}))) {
        return cfv_fn_estructura_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("clase")}))) {
        return cfv_fn_clase_parser_core(std::vector<Value>{cfv_estado});
    }
    if (cfv_truth(cfv_fn_comprobar_lexema_core(std::vector<Value>{cfv_estado, cfv_text("funcion")}))) {
        return cfv_fn_funcion_parser_core(std::vector<Value>{cfv_estado, cfv_bool(false)});
    }
    return cfv_fn_sentencia_parser_core(std::vector<Value>{cfv_estado});
    return Value();
}
static Value cfv_fn_parsear_tokens_core(const std::vector<Value>& cfv_args) {
    Value cfv_tokens = cfv_arg(cfv_args, 0, "parsear_tokens_core");
    Value cfv_estado = cfv_object("EstadoParserCore", std::vector<std::string>{"tokens", "posicion"}, std::vector<Value>{cfv_tokens, cfv_number(0)});
    Value cfv_declaraciones = cfv_list(std::vector<Value>{});
    while (cfv_truth(cfv_bool(!cfv_truth(cfv_fn_esta_al_final_core(std::vector<Value>{cfv_estado}))))) {
        (void)(cfv_append(cfv_declaraciones, cfv_fn_declaracion_superior_parser_core(std::vector<Value>{cfv_estado})));
    }
    Value cfv_eof = cfv_fn_token_actual_core(std::vector<Value>{cfv_estado});
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_text("Programa"), cfv_text("Core-0.5"), cfv_member(cfv_eof, "linea"), cfv_declaraciones});
    return Value();
}
static Value cfv_fn_parsear_fuente_core(const std::vector<Value>& cfv_args) {
    Value cfv_fuente = cfv_arg(cfv_args, 0, "parsear_fuente_core");
    return cfv_fn_parsear_tokens_core(std::vector<Value>{cfv_fn_tokenizar_core(std::vector<Value>{cfv_fuente})});
    return Value();
}
static Value cfv_method_marcar_movido(Value cfv_este, const std::vector<Value>& cfv_args) {
    cfv_member_ref(cfv_este, "movido") = cfv_bool(true);
    return Value();
}
static Value cfv_fn_diagnostico_core(const std::vector<Value>& cfv_args) {
    Value cfv_codigo = cfv_arg(cfv_args, 0, "diagnostico_core");
    Value cfv_linea = cfv_arg(cfv_args, 1, "diagnostico_core");
    Value cfv_mensaje = cfv_arg(cfv_args, 2, "diagnostico_core");
    return cfv_add(cfv_add(cfv_add(cfv_add(cfv_codigo, cfv_text(" línea ")), cfv_text(cfv_format(cfv_linea))), cfv_text(": ")), cfv_mensaje);
    return Value();
}
static Value cfv_fn_indice_dos_puntos_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "indice_dos_puntos_core");
    Value cfv_posicion = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_posicion, cfv_length(cfv_valor), "<"))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_index_any(cfv_valor, cfv_posicion), cfv_text(":"))))) {
            return cfv_posicion;
        }
        cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
    }
    return cfv_neg(cfv_number(1));
    return Value();
}
static Value cfv_fn_segmento_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "segmento_core");
    Value cfv_inicio = cfv_arg(cfv_args, 1, "segmento_core");
    Value cfv_final = cfv_arg(cfv_args, 2, "segmento_core");
    Value cfv_salida = cfv_text("");
    Value cfv_posicion = cfv_inicio;
    while (cfv_truth(cfv_compare(cfv_posicion, cfv_final, "<"))) {
        cfv_salida = cfv_add(cfv_salida, cfv_index_any(cfv_valor, cfv_posicion));
        cfv_posicion = cfv_add(cfv_posicion, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_nombre_declaracion_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "nombre_declaracion_core");
    Value cfv_separador = cfv_fn_indice_dos_puntos_core(std::vector<Value>{cfv_valor});
    if (cfv_truth(cfv_compare(cfv_separador, cfv_number(0), "<"))) {
        return cfv_valor;
    }
    return cfv_fn_segmento_core(std::vector<Value>{cfv_valor, cfv_number(0), cfv_separador});
    return Value();
}
static Value cfv_fn_tipo_declaracion_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "tipo_declaracion_core");
    Value cfv_separador = cfv_fn_indice_dos_puntos_core(std::vector<Value>{cfv_valor});
    if (cfv_truth(cfv_compare(cfv_separador, cfv_number(0), "<"))) {
        return cfv_text("");
    }
    return cfv_fn_segmento_core(std::vector<Value>{cfv_valor, cfv_add(cfv_separador, cfv_number(1)), cfv_length(cfv_valor)});
    return Value();
}
static Value cfv_fn_buscar_simbolo_core(const std::vector<Value>& cfv_args) {
    Value cfv_simbolos = cfv_arg(cfv_args, 0, "buscar_simbolo_core");
    Value cfv_nombre = cfv_arg(cfv_args, 1, "buscar_simbolo_core");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_simbolos), "<"))) {
        Value cfv_simbolo = cfv_index_any(cfv_simbolos, cfv_indice);
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_simbolo, "nombre"), cfv_nombre)))) {
            return cfv_indice;
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_neg(cfv_number(1));
    return Value();
}
static Value cfv_fn_tipo_identificador_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "tipo_identificador_core");
    Value cfv_simbolos = cfv_arg(cfv_args, 1, "tipo_identificador_core");
    Value cfv_errores = cfv_arg(cfv_args, 2, "tipo_identificador_core");
    Value cfv_indice = cfv_fn_buscar_simbolo_core(std::vector<Value>{cfv_simbolos, cfv_member(cfv_nodo, "valor")});
    if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), "<"))) {
        (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2002"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("variable no declarada '"), cfv_member(cfv_nodo, "valor")), cfv_text("'"))})));
        return cfv_text("error");
    }
    Value cfv_simbolo = cfv_index_any(cfv_simbolos, cfv_indice);
    if (cfv_truth(cfv_member(cfv_simbolo, "movido"))) {
        (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2003"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("uso después de mover '"), cfv_member(cfv_nodo, "valor")), cfv_text("'"))})));
        return cfv_text("error");
    }
    return cfv_member(cfv_simbolo, "tipo");
    return Value();
}
static Value cfv_fn_tipo_expresion_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "tipo_expresion_core");
    Value cfv_simbolos = cfv_arg(cfv_args, 1, "tipo_expresion_core");
    Value cfv_errores = cfv_arg(cfv_args, 2, "tipo_expresion_core");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Numero"))))) {
        return cfv_text("numero");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Texto"))))) {
        return cfv_text("texto");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Booleano"))))) {
        return cfv_text("booleano");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Lista"))))) {
        Value cfv_elementos_lista = cfv_member(cfv_nodo, "hijos");
        Value cfv_indice_lista = cfv_number(0);
        while (cfv_truth(cfv_compare(cfv_indice_lista, cfv_length(cfv_elementos_lista), "<"))) {
            (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_elementos_lista, cfv_indice_lista), cfv_simbolos, cfv_errores}));
            cfv_indice_lista = cfv_add(cfv_indice_lista, cfv_number(1));
        }
        return cfv_text("lista");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mapa"))))) {
        Value cfv_entradas_mapa = cfv_member(cfv_nodo, "hijos");
        Value cfv_indice_mapa = cfv_number(0);
        Value cfv_tipo_valor_mapa = cfv_text("");
        while (cfv_truth(cfv_compare(cfv_indice_mapa, cfv_length(cfv_entradas_mapa), "<"))) {
            Value cfv_tipo_clave = cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_entradas_mapa, cfv_indice_mapa), cfv_simbolos, cfv_errores});
            if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(!cfv_equal(cfv_tipo_clave, cfv_text("texto")))) && cfv_truth(cfv_bool(!cfv_equal(cfv_tipo_clave, cfv_text("error"))))))) {
                (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2010"), cfv_member(cfv_nodo, "linea"), cfv_text("la clave de un mapa Core debe ser texto")})));
            }
            Value cfv_tipo_entrada = cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_entradas_mapa, cfv_add(cfv_indice_mapa, cfv_number(1))), cfv_simbolos, cfv_errores});
            if (cfv_truth(cfv_bool(cfv_equal(cfv_tipo_valor_mapa, cfv_text(""))))) {
                cfv_tipo_valor_mapa = cfv_tipo_entrada;
            } else {
                if (cfv_truth(cfv_bool(!cfv_equal(cfv_tipo_entrada, cfv_tipo_valor_mapa)))) {
                    cfv_tipo_valor_mapa = cfv_text("cualquiera");
                }
            }
            cfv_indice_mapa = cfv_add(cfv_indice_mapa, cfv_number(2));
        }
        return cfv_add(cfv_add(cfv_text("mapa<"), cfv_tipo_valor_mapa), cfv_text(">"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Identificador"))))) {
        return cfv_fn_tipo_identificador_core(std::vector<Value>{cfv_nodo, cfv_simbolos, cfv_errores});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mover"))))) {
        Value cfv_hijos_mover = cfv_member(cfv_nodo, "hijos");
        Value cfv_objetivo = cfv_index_any(cfv_hijos_mover, cfv_number(0));
        if (cfv_truth(cfv_bool(!cfv_equal(cfv_member(cfv_objetivo, "tipo"), cfv_text("Identificador"))))) {
            (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2004"), cfv_member(cfv_nodo, "linea"), cfv_text("mover requiere una variable identificable")})));
            return cfv_text("error");
        }
        Value cfv_indice = cfv_fn_buscar_simbolo_core(std::vector<Value>{cfv_simbolos, cfv_member(cfv_objetivo, "valor")});
        Value cfv_tipo_objetivo = cfv_fn_tipo_identificador_core(std::vector<Value>{cfv_objetivo, cfv_simbolos, cfv_errores});
        if (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">=")) && cfv_truth(cfv_bool(cfv_equal(cfv_tipo_objetivo, cfv_text("texto"))))))) {
            Value cfv_simbolo = cfv_index_any(cfv_simbolos, cfv_indice);
            (void)(cfv_method_marcar_movido(cfv_simbolo, std::vector<Value>{}));
        }
        return cfv_tipo_objetivo;
    }
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoCompartido")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoMutable"))))))) {
        return cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_simbolos, cfv_errores});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("SoltarPrestamo"))))) {
        (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_simbolos, cfv_errores}));
        return cfv_text("nulo");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Indice"))))) {
        Value cfv_partes_indice = cfv_member(cfv_nodo, "hijos");
        Value cfv_tipo_base = cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_partes_indice, cfv_number(0)), cfv_simbolos, cfv_errores});
        (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_partes_indice, cfv_number(1)), cfv_simbolos, cfv_errores}));
        if (cfv_truth(cfv_bool(cfv_equal(cfv_fn_segmento_core(std::vector<Value>{cfv_tipo_base, cfv_number(0), cfv_number(5)}), cfv_text("mapa<"))))) {
            return cfv_fn_segmento_core(std::vector<Value>{cfv_tipo_base, cfv_number(5), cfv_sub(cfv_length(cfv_tipo_base), cfv_number(1))});
        }
        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_tipo_base, cfv_text("lista")))) || cfv_truth(cfv_bool(cfv_equal(cfv_tipo_base, cfv_text("texto"))))))) {
            return cfv_text("cualquiera");
        }
        return cfv_text("error");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Binario"))))) {
        Value cfv_hijos_binarios = cfv_member(cfv_nodo, "hijos");
        Value cfv_tipo_izquierdo = cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos_binarios, cfv_number(0)), cfv_simbolos, cfv_errores});
        Value cfv_tipo_derecho = cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos_binarios, cfv_number(1)), cfv_simbolos, cfv_errores});
        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_tipo_izquierdo, cfv_text("error")))) || cfv_truth(cfv_bool(cfv_equal(cfv_tipo_derecho, cfv_text("error"))))))) {
            return cfv_text("error");
        }
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("+"))))) {
            if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_tipo_izquierdo, cfv_tipo_derecho))) && cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_tipo_izquierdo, cfv_text("numero")))) || cfv_truth(cfv_bool(cfv_equal(cfv_tipo_izquierdo, cfv_text("texto"))))))))) {
                return cfv_tipo_izquierdo;
            }
        } else {
            if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_tipo_izquierdo, cfv_text("numero")))) && cfv_truth(cfv_bool(cfv_equal(cfv_tipo_derecho, cfv_text("numero"))))))) {
                return cfv_text("numero");
            }
        }
        (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2001"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("operador '"), cfv_member(cfv_nodo, "valor")), cfv_text("' incompatible con ")), cfv_tipo_izquierdo), cfv_text(" y ")), cfv_tipo_derecho)})));
        return cfv_text("error");
    }
    (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2099"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("nodo de expresión desconocido '"), cfv_member(cfv_nodo, "tipo")), cfv_text("'"))})));
    return cfv_text("error");
    return Value();
}
static Value cfv_fn_analizar_sentencia_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "analizar_sentencia_core");
    Value cfv_simbolos = cfv_arg(cfv_args, 1, "analizar_sentencia_core");
    Value cfv_errores = cfv_arg(cfv_args, 2, "analizar_sentencia_core");
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Declaracion"))))) {
        Value cfv_nombre = cfv_fn_nombre_declaracion_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
        Value cfv_declarado = cfv_fn_tipo_declaracion_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
        if (cfv_truth(cfv_compare(cfv_fn_buscar_simbolo_core(std::vector<Value>{cfv_simbolos, cfv_nombre}), cfv_number(0), ">="))) {
            (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2005"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("variable duplicada '"), cfv_nombre), cfv_text("'"))})));
            return cfv_errores;
        }
        Value cfv_inferido = cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_simbolos, cfv_errores});
        Value cfv_tipo_final = cfv_inferido;
        if (cfv_truth(cfv_bool(!cfv_equal(cfv_declarado, cfv_text(""))))) {
            cfv_tipo_final = cfv_declarado;
            if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(!cfv_equal(cfv_inferido, cfv_text("error")))) && cfv_truth(cfv_bool(!cfv_equal(cfv_declarado, cfv_inferido)))))) {
                (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2001"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("la variable '"), cfv_nombre), cfv_text("' requiere ")), cfv_declarado), cfv_text(" pero recibió ")), cfv_inferido)})));
            }
        }
        (void)(cfv_append(cfv_simbolos, cfv_object("SimboloCore", std::vector<std::string>{"nombre", "tipo", "movido"}, std::vector<Value>{cfv_nombre, cfv_tipo_final, cfv_bool(false)})));
        return cfv_errores;
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mostrar"))))) {
        (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_simbolos, cfv_errores}));
        return cfv_errores;
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Asignacion"))))) {
        (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_simbolos, cfv_errores}));
        (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_simbolos, cfv_errores}));
        return cfv_errores;
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Lanzar"))))) {
        (void)(cfv_fn_tipo_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_simbolos, cfv_errores}));
        return cfv_errores;
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Intentar"))))) {
        Value cfv_protegidas = cfv_member(cfv_index_any(cfv_hijos, cfv_number(0)), "hijos");
        Value cfv_indice_protegido = cfv_number(0);
        while (cfv_truth(cfv_compare(cfv_indice_protegido, cfv_length(cfv_protegidas), "<"))) {
            (void)(cfv_fn_analizar_sentencia_core(std::vector<Value>{cfv_index_any(cfv_protegidas, cfv_indice_protegido), cfv_simbolos, cfv_errores}));
            cfv_indice_protegido = cfv_add(cfv_indice_protegido, cfv_number(1));
        }
        Value cfv_simbolos_captura = cfv_list(std::vector<Value>{});
        Value cfv_copia = cfv_number(0);
        while (cfv_truth(cfv_compare(cfv_copia, cfv_length(cfv_simbolos), "<"))) {
            (void)(cfv_append(cfv_simbolos_captura, cfv_index_any(cfv_simbolos, cfv_copia)));
            cfv_copia = cfv_add(cfv_copia, cfv_number(1));
        }
        (void)(cfv_append(cfv_simbolos_captura, cfv_object("SimboloCore", std::vector<std::string>{"nombre", "tipo", "movido"}, std::vector<Value>{cfv_member(cfv_nodo, "valor"), cfv_text("texto"), cfv_bool(false)})));
        Value cfv_capturas = cfv_member(cfv_index_any(cfv_hijos, cfv_number(1)), "hijos");
        Value cfv_indice_captura = cfv_number(0);
        while (cfv_truth(cfv_compare(cfv_indice_captura, cfv_length(cfv_capturas), "<"))) {
            (void)(cfv_fn_analizar_sentencia_core(std::vector<Value>{cfv_index_any(cfv_capturas, cfv_indice_captura), cfv_simbolos_captura, cfv_errores}));
            cfv_indice_captura = cfv_add(cfv_indice_captura, cfv_number(1));
        }
        return cfv_errores;
    }
    (void)(cfv_append(cfv_errores, cfv_fn_diagnostico_core(std::vector<Value>{cfv_text("CFB2098"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("sentencia desconocida '"), cfv_member(cfv_nodo, "tipo")), cfv_text("'"))})));
    return Value();
}
static Value cfv_fn_analizar_semantica_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "analizar_semantica_core");
    Value cfv_declaraciones = cfv_member(cfv_programa, "hijos");
    Value cfv_cursor_declaracion = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_cursor_declaracion, cfv_length(cfv_declaraciones), "<"))) {
        Value cfv_declaracion_actual = cfv_index_any(cfv_declaraciones, cfv_cursor_declaracion);
        Value cfv_tipo_declaracion = cfv_member(cfv_declaracion_actual, "tipo");
        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_tipo_declaracion, cfv_text("Funcion")))) || cfv_truth(cfv_bool(cfv_equal(cfv_tipo_declaracion, cfv_text("Estructura")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_tipo_declaracion, cfv_text("Clase"))))))) {
            return cfv_object("ResultadoSemanticoCore", std::vector<std::string>{"valido", "errores"}, std::vector<Value>{cfv_bool(true), cfv_list(std::vector<Value>{})});
        }
        cfv_cursor_declaracion = cfv_add(cfv_cursor_declaracion, cfv_number(1));
    }
    Value cfv_simbolos = cfv_list(std::vector<Value>{});
    Value cfv_errores = cfv_list(std::vector<Value>{});
    Value cfv_sentencias = cfv_member(cfv_programa, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_sentencias), "<"))) {
        (void)(cfv_fn_analizar_sentencia_core(std::vector<Value>{cfv_index_any(cfv_sentencias, cfv_indice), cfv_simbolos, cfv_errores}));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_object("ResultadoSemanticoCore", std::vector<std::string>{"valido", "errores"}, std::vector<Value>{cfv_bool(cfv_equal(cfv_length(cfv_errores), cfv_number(0))), cfv_errores});
    return Value();
}
static Value cfv_fn_diagnosticos_semanticos_core(const std::vector<Value>& cfv_args) {
    Value cfv_resultado = cfv_arg(cfv_args, 0, "diagnosticos_semanticos_core");
    Value cfv_errores = cfv_member(cfv_resultado, "errores");
    Value cfv_salida = cfv_text("");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_errores), "<"))) {
        if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">"))) {
            cfv_salida = cfv_add(cfv_salida, cfv_text("\n"));
        }
        cfv_salida = cfv_add(cfv_salida, cfv_index_any(cfv_errores, cfv_indice));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_nombre_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "nombre_propiedad_core");
    Value cfv_salida = cfv_text("");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_valor), "<")) && cfv_truth(cfv_bool(!cfv_equal(cfv_index_any(cfv_valor, cfv_indice), cfv_text(":"))))))) {
        cfv_salida = cfv_add(cfv_salida, cfv_index_any(cfv_valor, cfv_indice));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_indice_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_contexto = cfv_arg(cfv_args, 0, "indice_propiedad_core");
    Value cfv_nombre = cfv_arg(cfv_args, 1, "indice_propiedad_core");
    Value cfv_indice = cfv_sub(cfv_length(cfv_member(cfv_contexto, "valores")), cfv_number(1));
    while (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">="))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice), "nombre"), cfv_nombre)))) {
            return cfv_indice;
        }
        cfv_indice = cfv_sub(cfv_indice, cfv_number(1));
    }
    return cfv_neg(cfv_number(1));
    return Value();
}
static Value cfv_fn_error_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_contexto = cfv_arg(cfv_args, 0, "error_propiedad_core");
    Value cfv_codigo = cfv_arg(cfv_args, 1, "error_propiedad_core");
    Value cfv_linea = cfv_arg(cfv_args, 2, "error_propiedad_core");
    Value cfv_mensaje = cfv_arg(cfv_args, 3, "error_propiedad_core");
    (void)(cfv_append(cfv_member(cfv_contexto, "errores"), cfv_add(cfv_add(cfv_add(cfv_add(cfv_codigo, cfv_text(" línea ")), cfv_text(cfv_format(cfv_linea))), cfv_text(": ")), cfv_mensaje)));
    return Value();
}
static Value cfv_fn_propiedad_activa_core(const std::vector<Value>& cfv_args) {
    Value cfv_contexto = cfv_arg(cfv_args, 0, "propiedad_activa_core");
    Value cfv_nombre = cfv_arg(cfv_args, 1, "propiedad_activa_core");
    Value cfv_linea = cfv_arg(cfv_args, 2, "propiedad_activa_core");
    Value cfv_indice = cfv_fn_indice_propiedad_core(std::vector<Value>{cfv_contexto, cfv_nombre});
    if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), "<"))) {
        (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2101"), cfv_linea, cfv_add(cfv_add(cfv_text("variable no declarada '"), cfv_nombre), cfv_text("'"))}));
        return cfv_neg(cfv_number(1));
    }
    if (cfv_truth(cfv_member(cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice), "movido"))) {
        (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2102"), cfv_linea, cfv_add(cfv_add(cfv_text("uso después de mover o liberar '"), cfv_nombre), cfv_text("'"))}));
        return cfv_neg(cfv_number(1));
    }
    return cfv_indice;
    return Value();
}
static Value cfv_fn_objetivo_identificable_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "objetivo_identificable_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "objetivo_identificable_core");
    Value cfv_operacion = cfv_arg(cfv_args, 2, "objetivo_identificable_core");
    if (cfv_truth(cfv_bool(!cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Identificador"))))) {
        (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2103"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_operacion, cfv_text(" requiere una variable identificable"))}));
        return cfv_neg(cfv_number(1));
    }
    return cfv_fn_propiedad_activa_core(std::vector<Value>{cfv_contexto, cfv_member(cfv_nodo, "valor"), cfv_member(cfv_nodo, "linea")});
    return Value();
}
static Value cfv_fn_analizar_expresion_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "analizar_expresion_propiedad_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "analizar_expresion_propiedad_core");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Identificador"))))) {
        (void)(cfv_fn_propiedad_activa_core(std::vector<Value>{cfv_contexto, cfv_member(cfv_nodo, "valor"), cfv_member(cfv_nodo, "linea")}));
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mover"))))) {
        Value cfv_indice = cfv_fn_objetivo_identificable_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto, cfv_text("mover")});
        if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">="))) {
            Value cfv_valor = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice);
            if (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_member(cfv_valor, "prestamos"), cfv_number(0), ">")) || cfv_truth(cfv_member(cfv_valor, "prestamo_mutable"))))) {
                (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2104"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("no se puede mover '"), cfv_member(cfv_valor, "nombre")), cfv_text("' mientras está prestado"))}));
            } else {
                cfv_member_ref(cfv_valor, "movido") = cfv_bool(true);
            }
        }
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoCompartido"))))) {
        Value cfv_indice_compartido = cfv_fn_objetivo_identificable_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto, cfv_text("prestar")});
        if (cfv_truth(cfv_compare(cfv_indice_compartido, cfv_number(0), ">="))) {
            Value cfv_propietario = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice_compartido);
            if (cfv_truth(cfv_member(cfv_propietario, "prestamo_mutable"))) {
                (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2105"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("'"), cfv_member(cfv_propietario, "nombre")), cfv_text("' ya tiene un préstamo mutable"))}));
            } else {
                cfv_member_ref(cfv_propietario, "prestamos") = cfv_add(cfv_member(cfv_propietario, "prestamos"), cfv_number(1));
            }
        }
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoMutable"))))) {
        Value cfv_indice_mutable = cfv_fn_objetivo_identificable_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto, cfv_text("prestar_mut")});
        if (cfv_truth(cfv_compare(cfv_indice_mutable, cfv_number(0), ">="))) {
            Value cfv_propietario_mutable = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice_mutable);
            if (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_member(cfv_propietario_mutable, "prestamos"), cfv_number(0), ">")) || cfv_truth(cfv_member(cfv_propietario_mutable, "prestamo_mutable"))))) {
                (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2106"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("'"), cfv_member(cfv_propietario_mutable, "nombre")), cfv_text("' ya está prestado"))}));
            } else {
                cfv_member_ref(cfv_propietario_mutable, "prestamo_mutable") = cfv_bool(true);
            }
        }
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("SoltarPrestamo"))))) {
        Value cfv_indice_alias = cfv_fn_objetivo_identificable_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto, cfv_text("soltar_prestamo")});
        if (cfv_truth(cfv_compare(cfv_indice_alias, cfv_number(0), ">="))) {
            Value cfv_alias = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice_alias);
            if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_alias, "prestado_de"), cfv_text(""))))) {
                (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2107"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("'"), cfv_member(cfv_alias, "nombre")), cfv_text("' no es un préstamo"))}));
            } else {
                Value cfv_indice_dueno = cfv_fn_indice_propiedad_core(std::vector<Value>{cfv_contexto, cfv_member(cfv_alias, "prestado_de")});
                if (cfv_truth(cfv_compare(cfv_indice_dueno, cfv_number(0), ">="))) {
                    Value cfv_dueno = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice_dueno);
                    if (cfv_truth(cfv_member(cfv_alias, "alias_mutable"))) {
                        cfv_member_ref(cfv_dueno, "prestamo_mutable") = cfv_bool(false);
                    } else {
                        cfv_member_ref(cfv_dueno, "prestamos") = cfv_sub(cfv_member(cfv_dueno, "prestamos"), cfv_number(1));
                    }
                }
                cfv_member_ref(cfv_alias, "movido") = cfv_bool(true);
            }
        }
        return cfv_number(0);
    }
    Value cfv_indice_hijo = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice_hijo, cfv_length(cfv_member(cfv_nodo, "hijos")), "<"))) {
        (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_indice_hijo), cfv_contexto}));
        cfv_indice_hijo = cfv_add(cfv_indice_hijo, cfv_number(1));
    }
    return Value();
}
static Value cfv_fn_liberar_ambito_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_contexto = cfv_arg(cfv_args, 0, "liberar_ambito_propiedad_core");
    Value cfv_profundidad = cfv_arg(cfv_args, 1, "liberar_ambito_propiedad_core");
    Value cfv_indice = cfv_sub(cfv_length(cfv_member(cfv_contexto, "valores")), cfv_number(1));
    while (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">="))) {
        Value cfv_valor = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice);
        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_valor, "profundidad"), cfv_profundidad))) && cfv_truth(cfv_bool(!cfv_equal(cfv_member(cfv_valor, "prestado_de"), cfv_text("")))))) && cfv_truth(cfv_bool(!cfv_truth(cfv_member(cfv_valor, "movido"))))))) {
            Value cfv_indice_dueno = cfv_fn_indice_propiedad_core(std::vector<Value>{cfv_contexto, cfv_member(cfv_valor, "prestado_de")});
            if (cfv_truth(cfv_compare(cfv_indice_dueno, cfv_number(0), ">="))) {
                Value cfv_dueno = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice_dueno);
                if (cfv_truth(cfv_member(cfv_valor, "alias_mutable"))) {
                    cfv_member_ref(cfv_dueno, "prestamo_mutable") = cfv_bool(false);
                } else {
                    cfv_member_ref(cfv_dueno, "prestamos") = cfv_sub(cfv_member(cfv_dueno, "prestamos"), cfv_number(1));
                }
            }
            cfv_member_ref(cfv_valor, "movido") = cfv_bool(true);
        }
        cfv_indice = cfv_sub(cfv_indice, cfv_number(1));
    }
    Value cfv_conservados = cfv_list(std::vector<Value>{});
    cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_member(cfv_contexto, "valores")), "<"))) {
        if (cfv_truth(cfv_compare(cfv_member(cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice), "profundidad"), cfv_profundidad, "<"))) {
            (void)(cfv_append(cfv_conservados, cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice)));
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    cfv_member_ref(cfv_contexto, "valores") = cfv_conservados;
    return Value();
}
static Value cfv_fn_analizar_bloque_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_bloque = cfv_arg(cfv_args, 0, "analizar_bloque_propiedad_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "analizar_bloque_propiedad_core");
    cfv_member_ref(cfv_contexto, "profundidad") = cfv_add(cfv_member(cfv_contexto, "profundidad"), cfv_number(1));
    Value cfv_profundidad_actual = cfv_member(cfv_contexto, "profundidad");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_member(cfv_bloque, "hijos")), "<"))) {
        (void)(cfv_fn_analizar_sentencia_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_bloque, "hijos"), cfv_indice), cfv_contexto}));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    (void)(cfv_fn_liberar_ambito_propiedad_core(std::vector<Value>{cfv_contexto, cfv_profundidad_actual}));
    cfv_member_ref(cfv_contexto, "profundidad") = cfv_sub(cfv_member(cfv_contexto, "profundidad"), cfv_number(1));
    return Value();
}
static Value cfv_fn_analizar_declaracion_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "analizar_declaracion_propiedad_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "analizar_declaracion_propiedad_core");
    Value cfv_nombre = cfv_fn_nombre_propiedad_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
    if (cfv_truth(cfv_compare(cfv_fn_indice_propiedad_core(std::vector<Value>{cfv_contexto, cfv_nombre}), cfv_number(0), ">="))) {
        (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2108"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("variable duplicada '"), cfv_nombre), cfv_text("'"))}));
        return cfv_number(0);
    }
    Value cfv_inicializador = cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0));
    (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_inicializador, cfv_contexto}));
    Value cfv_prestado_de = cfv_text("");
    Value cfv_mutable = cfv_bool(false);
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_inicializador, "tipo"), cfv_text("PrestamoCompartido")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_inicializador, "tipo"), cfv_text("PrestamoMutable"))))))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_index_any(cfv_member(cfv_inicializador, "hijos"), cfv_number(0)), "tipo"), cfv_text("Identificador"))))) {
            cfv_prestado_de = cfv_member(cfv_index_any(cfv_member(cfv_inicializador, "hijos"), cfv_number(0)), "valor");
            cfv_mutable = cfv_bool(cfv_equal(cfv_member(cfv_inicializador, "tipo"), cfv_text("PrestamoMutable")));
        }
    }
    (void)(cfv_append(cfv_member(cfv_contexto, "valores"), cfv_object("PropiedadCore", std::vector<std::string>{"nombre", "movido", "prestamos", "prestamo_mutable", "prestado_de", "alias_mutable", "profundidad"}, std::vector<Value>{cfv_nombre, cfv_bool(false), cfv_number(0), cfv_bool(false), cfv_prestado_de, cfv_mutable, cfv_member(cfv_contexto, "profundidad")})));
    return Value();
}
static Value cfv_fn_analizar_sentencia_propiedad_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "analizar_sentencia_propiedad_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "analizar_sentencia_propiedad_core");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Declaracion"))))) {
        (void)(cfv_fn_analizar_declaracion_propiedad_core(std::vector<Value>{cfv_nodo, cfv_contexto}));
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Asignacion"))))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), "tipo"), cfv_text("Identificador"))))) {
            Value cfv_indice = cfv_fn_propiedad_activa_core(std::vector<Value>{cfv_contexto, cfv_member(cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), "valor"), cfv_member(cfv_nodo, "linea")});
            if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">="))) {
                Value cfv_destino = cfv_index_any(cfv_member(cfv_contexto, "valores"), cfv_indice);
                if (cfv_truth(cfv_bool(cfv_truth(cfv_compare(cfv_member(cfv_destino, "prestamos"), cfv_number(0), ">")) || cfv_truth(cfv_member(cfv_destino, "prestamo_mutable"))))) {
                    (void)(cfv_fn_error_propiedad_core(std::vector<Value>{cfv_contexto, cfv_text("CFOWN2109"), cfv_member(cfv_nodo, "linea"), cfv_add(cfv_add(cfv_text("no se puede modificar '"), cfv_member(cfv_destino, "nombre")), cfv_text("' mientras está prestado"))}));
                }
            }
        } else {
            (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto}));
        }
        (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(1)), cfv_contexto}));
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Si"))))) {
        (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto}));
        (void)(cfv_fn_analizar_bloque_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(1)), cfv_contexto}));
        if (cfv_truth(cfv_compare(cfv_length(cfv_member(cfv_nodo, "hijos")), cfv_number(2), ">"))) {
            (void)(cfv_fn_analizar_bloque_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(2)), cfv_contexto}));
        }
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mientras"))))) {
        (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto}));
        (void)(cfv_fn_analizar_bloque_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(1)), cfv_contexto}));
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Intentar"))))) {
        (void)(cfv_fn_analizar_bloque_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0)), cfv_contexto}));
        cfv_member_ref(cfv_contexto, "profundidad") = cfv_add(cfv_member(cfv_contexto, "profundidad"), cfv_number(1));
        (void)(cfv_append(cfv_member(cfv_contexto, "valores"), cfv_object("PropiedadCore", std::vector<std::string>{"nombre", "movido", "prestamos", "prestamo_mutable", "prestado_de", "alias_mutable", "profundidad"}, std::vector<Value>{cfv_member(cfv_nodo, "valor"), cfv_bool(false), cfv_number(0), cfv_bool(false), cfv_text(""), cfv_bool(false), cfv_member(cfv_contexto, "profundidad")})));
        Value cfv_indice_captura = cfv_number(0);
        while (cfv_truth(cfv_compare(cfv_indice_captura, cfv_length(cfv_member(cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(1)), "hijos")), "<"))) {
            (void)(cfv_fn_analizar_sentencia_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(1)), "hijos"), cfv_indice_captura), cfv_contexto}));
            cfv_indice_captura = cfv_add(cfv_indice_captura, cfv_number(1));
        }
        cfv_member_ref(cfv_contexto, "profundidad") = cfv_sub(cfv_member(cfv_contexto, "profundidad"), cfv_number(1));
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Funcion"))))) {
        Value cfv_valores_anteriores = cfv_member(cfv_contexto, "valores");
        cfv_member_ref(cfv_contexto, "valores") = cfv_list(std::vector<Value>{});
        cfv_member_ref(cfv_contexto, "profundidad") = cfv_number(0);
        Value cfv_parametro = cfv_number(0);
        while (cfv_truth(cfv_compare(cfv_add(cfv_parametro, cfv_number(1)), cfv_length(cfv_member(cfv_nodo, "hijos")), "<"))) {
            (void)(cfv_append(cfv_member(cfv_contexto, "valores"), cfv_object("PropiedadCore", std::vector<std::string>{"nombre", "movido", "prestamos", "prestamo_mutable", "prestado_de", "alias_mutable", "profundidad"}, std::vector<Value>{cfv_fn_nombre_propiedad_core(std::vector<Value>{cfv_member(cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_parametro), "valor")}), cfv_bool(false), cfv_number(0), cfv_bool(false), cfv_text(""), cfv_bool(false), cfv_number(0)})));
            cfv_parametro = cfv_add(cfv_parametro, cfv_number(1));
        }
        (void)(cfv_fn_analizar_bloque_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_parametro), cfv_contexto}));
        cfv_member_ref(cfv_contexto, "valores") = cfv_valores_anteriores;
        cfv_member_ref(cfv_contexto, "profundidad") = cfv_number(0);
        return cfv_number(0);
    }
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Clase")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Estructura")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Interfaz")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Importar"))))))) {
        return cfv_number(0);
    }
    Value cfv_indice_hijo = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice_hijo, cfv_length(cfv_member(cfv_nodo, "hijos")), "<"))) {
        (void)(cfv_fn_analizar_expresion_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_indice_hijo), cfv_contexto}));
        cfv_indice_hijo = cfv_add(cfv_indice_hijo, cfv_number(1));
    }
    return Value();
}
static Value cfv_fn_verificar_ownership_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "verificar_ownership_core");
    Value cfv_contexto = cfv_object("ContextoPropiedadCore", std::vector<std::string>{"valores", "errores", "profundidad"}, std::vector<Value>{cfv_list(std::vector<Value>{}), cfv_list(std::vector<Value>{}), cfv_number(0)});
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_member(cfv_programa, "hijos")), "<"))) {
        (void)(cfv_fn_analizar_sentencia_propiedad_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_programa, "hijos"), cfv_indice), cfv_contexto}));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_member(cfv_contexto, "errores");
    return Value();
}
static Value cfv_fn_si_error_ownership_core(const std::vector<Value>& cfv_args) {
    Value cfv_errores = cfv_arg(cfv_args, 0, "si_error_ownership_core");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_length(cfv_errores), cfv_number(0))))) {
        return cfv_text("");
    }
    Value cfv_salida = cfv_index_any(cfv_errores, cfv_number(0));
    Value cfv_indice = cfv_number(1);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_errores), "<"))) {
        cfv_salida = cfv_add(cfv_add(cfv_salida, cfv_text("\n")), cfv_index_any(cfv_errores, cfv_indice));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_bajar_expresion_ownership_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "bajar_expresion_ownership_core");
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mover")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoCompartido")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoMutable"))))))) {
        return cfv_fn_bajar_expresion_ownership_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_number(0))});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("SoltarPrestamo"))))) {
        return cfv_fn_nodo_ast_core(std::vector<Value>{cfv_text("Numero"), cfv_text("0"), cfv_member(cfv_nodo, "linea")});
    }
    Value cfv_hijos = cfv_list(std::vector<Value>{});
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_member(cfv_nodo, "hijos")), "<"))) {
        (void)(cfv_append(cfv_hijos, cfv_fn_bajar_expresion_ownership_core(std::vector<Value>{cfv_index_any(cfv_member(cfv_nodo, "hijos"), cfv_indice)})));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_fn_nodo_ast_core_con_hijos(std::vector<Value>{cfv_member(cfv_nodo, "tipo"), cfv_member(cfv_nodo, "valor"), cfv_member(cfv_nodo, "linea"), cfv_hijos});
    return Value();
}
static Value cfv_fn_bajar_ownership_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "bajar_ownership_core");
    return cfv_fn_bajar_expresion_ownership_core(std::vector<Value>{cfv_programa});
    return Value();
}
static Value cfv_fn_runtime_cpp_core(const std::vector<Value>& cfv_args) {
    return cfv_text("#include <cmath>\n#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n#include <fstream>\n#include <iomanip>\n#include <iostream>\n#include <map>\n#include <memory>\n#include <sstream>\n#include <stdexcept>\n#include <string>\n#include <variant>\n#include <vector>\n\nstruct Value;\nusing List = std::vector<Value>;\nusing Object = std::map<std::string, Value>;\nstruct Value {\n    using Data = std::variant<std::monostate, double, bool, std::string,\n                              std::shared_ptr<List>, std::shared_ptr<Object>>;\n    Data data;\n    Value() = default;\n    explicit Value(double value) : data(value) {}\n    explicit Value(bool value) : data(value) {}\n    explicit Value(std::string value) : data(std::move(value)) {}\n    explicit Value(std::shared_ptr<List> value) : data(std::move(value)) {}\n    explicit Value(std::shared_ptr<Object> value) : data(std::move(value)) {}\n};\nstatic std::vector<Value> cfv_process_args;\nstatic Value cfv_number(double value) { return Value(value); }\nstatic Value cfv_text(std::string value) { return Value(std::move(value)); }\nstatic Value cfv_bool(bool value) { return Value(value); }\nstatic double cfv_num(const Value& value) {\n    if (const auto* found = std::get_if<double>(&value.data)) return *found;\n    throw std::runtime_error(\"se esperaba numero\");\n}\nstatic bool cfv_truth(const Value& value) {\n    if (const auto* found = std::get_if<bool>(&value.data)) return *found;\n    if (const auto* found = std::get_if<double>(&value.data)) return *found != 0;\n    if (const auto* found = std::get_if<std::string>(&value.data)) return !found->empty();\n    if (const auto* found = std::get_if<std::shared_ptr<List>>(&value.data))\n        return !(*found)->empty();\n    return !std::holds_alternative<std::monostate>(value.data);\n}\nstatic std::string cfv_format(const Value& value) {\n    if (std::holds_alternative<std::monostate>(value.data)) return \"nulo\";\n    if (const auto* found = std::get_if<bool>(&value.data))\n        return *found ? \"verdadero\" : \"falso\";\n    if (const auto* found = std::get_if<std::string>(&value.data)) return *found;\n    if (const auto* found = std::get_if<double>(&value.data)) {\n        if (std::floor(*found) == *found) return std::to_string(static_cast<long long>(*found));\n        std::ostringstream output; output << std::setprecision(15) << *found;\n        return output.str();\n    }\n    return \"<objeto>\";\n}\nstatic bool cfv_equal(const Value& left, const Value& right) {\n    if (left.data.index() != right.data.index()) return false;\n    if (const auto* a = std::get_if<double>(&left.data))\n        return *a == std::get<double>(right.data);\n    if (const auto* a = std::get_if<bool>(&left.data))\n        return *a == std::get<bool>(right.data);\n    if (const auto* a = std::get_if<std::string>(&left.data))\n        return *a == std::get<std::string>(right.data);\n    return left.data == right.data;\n}\nstatic Value cfv_add(const Value& left, const Value& right) {\n    if (const auto* a = std::get_if<double>(&left.data)) {\n        if (const auto* b = std::get_if<double>(&right.data)) return Value(*a + *b);\n    }\n    if (const auto* a = std::get_if<std::string>(&left.data)) {\n        if (const auto* b = std::get_if<std::string>(&right.data)) return Value(*a + *b);\n    }\n    throw std::runtime_error(\"tipos incompatibles para '+'\");\n}\nstatic Value cfv_sub(const Value& a, const Value& b) { return Value(cfv_num(a) - cfv_num(b)); }\nstatic Value cfv_mul(const Value& a, const Value& b) { return Value(cfv_num(a) * cfv_num(b)); }\nstatic Value cfv_div(const Value& a, const Value& b) {\n    const double divisor = cfv_num(b);\n    if (divisor == 0) throw std::runtime_error(\"división por cero\");\n    return Value(cfv_num(a) / divisor);\n}\nstatic Value cfv_mod(const Value& a, const Value& b) {\n    return Value(std::fmod(cfv_num(a), cfv_num(b)));\n}\nstatic Value cfv_neg(const Value& value) { return Value(-cfv_num(value)); }\nstatic Value cfv_compare(const Value& a, const Value& b, const std::string& op) {\n    if (const auto* left = std::get_if<double>(&a.data)) {\n        const double right = cfv_num(b);\n        return Value(op == \"<\" ? *left < right : op == \"<=\" ? *left <= right :\n                     op == \">\" ? *left > right : *left >= right);\n    }\n    const auto* left = std::get_if<std::string>(&a.data);\n    const auto* right = std::get_if<std::string>(&b.data);\n    if (!left || !right) throw std::runtime_error(\"comparación incompatible\");\n    return Value(op == \"<\" ? *left < *right : op == \"<=\" ? *left <= *right :\n                 op == \">\" ? *left > *right : *left >= *right);\n}\nstatic Value cfv_list(const std::vector<Value>& values) {\n    return Value(std::make_shared<List>(values));\n}\nstatic Value cfv_object(const std::string& type,\n                        const std::vector<std::string>& fields,\n                        const std::vector<Value>& values) {\n    if (fields.size() != values.size())\n        throw std::runtime_error(type + \" recibió una cantidad de campos inválida\");\n    auto object = std::make_shared<Object>();\n    (*object)[\"__tipo\"] = Value(type);\n    for (std::size_t i = 0; i < fields.size(); ++i) (*object)[fields[i]] = values[i];\n    return Value(object);\n}\nstatic Value cfv_index(const Value& value, const Value& index) {\n    const auto position = static_cast<std::size_t>(cfv_num(index));\n    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value.data)) {\n        if (position >= (*list)->size()) throw std::runtime_error(\"índice fuera de rango\");\n        return (**list)[position];\n    }\n    if (const auto* text = std::get_if<std::string>(&value.data)) {\n        if (position >= text->size()) throw std::runtime_error(\"índice fuera de rango\");\n        return Value(std::string(1, (*text)[position]));\n    }\n    throw std::runtime_error(\"el valor no admite índices\");\n}\nstatic Value cfv_member(const Value& value, const std::string& field) {\n    const auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);\n    if (!object) throw std::runtime_error(\"se esperaba un objeto\");\n    const auto found = (*object)->find(field);\n    if (found == (*object)->end()) throw std::runtime_error(\"campo desconocido \" + field);\n    return found->second;\n}\nstatic Value& cfv_member_ref(Value& value, const std::string& field) {\n    auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);\n    if (!object) throw std::runtime_error(\"se esperaba un objeto mutable\");\n    const auto found = (*object)->find(field);\n    if (found == (*object)->end()) throw std::runtime_error(\"campo desconocido \" + field);\n    return found->second;\n}\nstatic Value cfv_length(const Value& value) {\n    if (const auto* text = std::get_if<std::string>(&value.data))\n        return Value(static_cast<double>(text->size()));\n    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value.data))\n        return Value(static_cast<double>((*list)->size()));\n    throw std::runtime_error(\"longitud requiere texto o lista\");\n}\nstatic Value cfv_append(Value value, const Value& item) {\n    auto* list = std::get_if<std::shared_ptr<List>>(&value.data);\n    if (!list) throw std::runtime_error(\"agregar requiere una lista\");\n    (*list)->push_back(item);\n    return Value();\n}\nstatic Value cfv_assert(const Value& condition, const Value& message) {\n    if (!cfv_truth(condition)) throw std::runtime_error(cfv_format(message));\n    return Value();\n}\nstatic std::string cfv_required_text(const Value& value,\n                                     const std::string& function) {\n    if (const auto* text = std::get_if<std::string>(&value.data)) return *text;\n    throw std::runtime_error(function + \" requiere texto\");\n}\nstatic Value cfv_arguments() {\n    return cfv_list(cfv_process_args);\n}\nstatic Value cfv_read_file(const Value& path_value) {\n    const std::string path = cfv_required_text(path_value, \"leer_archivo\");\n    std::ifstream stream(path, std::ios::binary);\n    if (!stream) throw std::runtime_error(\"no se pudo abrir \" + path);\n    return Value(std::string(std::istreambuf_iterator<char>(stream),\n                             std::istreambuf_iterator<char>()));\n}\nstatic Value cfv_write_file(const Value& path_value, const Value& content_value) {\n    const std::string path = cfv_required_text(path_value, \"escribir_archivo\");\n    const std::string content =\n        cfv_required_text(content_value, \"escribir_archivo\");\n    std::ofstream stream(path, std::ios::binary);\n    if (!stream) throw std::runtime_error(\"no se pudo escribir \" + path);\n    stream << content;\n    if (!stream) throw std::runtime_error(\"escritura incompleta en \" + path);\n    return Value(true);\n}\nstatic Value cfv_remove_file(const Value& path_value) {\n    const std::string path = cfv_required_text(path_value, \"eliminar_archivo\");\n    return Value(std::remove(path.c_str()) == 0);\n}\nstatic std::string cfv_shell_quote(const std::string& value) {\n    std::string quoted = \"'\";\n    for (const char byte : value) {\n        if (byte == '\\'') quoted += \"'\\\\''\";\n        else quoted.push_back(byte);\n    }\n    return quoted + \"'\";\n}\n#if defined(__APPLE__)\nstatic bool cfv_normalize_macho_uuid(const std::string& path) {\n    std::fstream stream(path, std::ios::in | std::ios::out | std::ios::binary);\n    if (!stream) return false;\n    std::uint32_t magic = 0;\n    std::uint32_t commands = 0;\n    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));\n    if (magic != 0xfeedfacf && magic != 0xfeedface) return false;\n    stream.seekg(16);\n    stream.read(reinterpret_cast<char*>(&commands), sizeof(commands));\n    std::streamoff offset = magic == 0xfeedfacf ? 32 : 28;\n    for (std::uint32_t index = 0; index < commands; ++index) {\n        std::uint32_t command = 0;\n        std::uint32_t size = 0;\n        stream.seekg(offset);\n        stream.read(reinterpret_cast<char*>(&command), sizeof(command));\n        stream.read(reinterpret_cast<char*>(&size), sizeof(size));\n        if (!stream || size < 8) return false;\n        if (command == 0x1b && size >= 24) {\n            const char zero_uuid[16] = {};\n            stream.seekp(offset + 8);\n            stream.write(zero_uuid, sizeof(zero_uuid));\n            stream.flush();\n            return static_cast<bool>(stream);\n        }\n        offset += size;\n    }\n    return false;\n}\nstatic bool cfv_sign_reproducible_macos(const std::string& path) {\n    if (!cfv_normalize_macho_uuid(path)) return false;\n    const std::string command =\n        \"codesign --force --sign - --identifier \"\n        \"org.vemoris.cforge.bootstrap \" +\n        cfv_shell_quote(path) + \" >/dev/null 2>&1\";\n    return std::system(command.c_str()) == 0;\n}\n#endif\nstatic Value cfv_compile_cpp(const Value& source_value, const Value& output_value) {\n    const std::string source =\n        cfv_required_text(source_value, \"compilar_cpp_nativo\");\n    const std::string output =\n        cfv_required_text(output_value, \"compilar_cpp_nativo\");\n    const char* cfv_cxx_env = std::getenv(\"CXX\");\n    std::string cfv_cxx =\n        (cfv_cxx_env && *cfv_cxx_env) ? std::string(cfv_cxx_env) :\n#if defined(__linux__)\n        \"g++\";\n#else\n        \"clang++\";\n#endif\n    std::string command =\n        cfv_cxx + \" -std=c++17 -O2 \" + cfv_shell_quote(source) +\n        \" -o \" + cfv_shell_quote(output);\n#if defined(__linux__)\n    command += \" -Wl,--build-id=none\";\n#endif\n    if (std::system(command.c_str()) != 0) return Value(false);\n#if defined(__APPLE__)\n    return Value(cfv_sign_reproducible_macos(output));\n#else\n    return Value(true);\n#endif\n}\nstatic Value cfv_arg(const std::vector<Value>& args, std::size_t index,\n                     const std::string& function) {\n    if (index >= args.size()) throw std::runtime_error(function + \": faltan argumentos\");\n    return args[index];\n}\nstatic void cfv_print(const Value& value) { std::cout << cfv_format(value) << '\\n'; }\n");
    return Value();
}
static Value cfv_fn_runtime_mapas_cpp_core(const std::vector<Value>& cfv_args) {
    return cfv_text("static Value cfv_map(const std::vector<Value>& entries) {\n    if (entries.size() % 2 != 0) throw std::runtime_error(\"mapa interno incompleto\");\n    auto object = std::make_shared<Object>();\n    for (std::size_t i = 0; i < entries.size(); i += 2) {\n        const auto* key = std::get_if<std::string>(&entries[i].data);\n        if (!key) throw std::runtime_error(\"la clave del mapa debe ser texto\");\n        (*object)[*key] = entries[i + 1];\n    }\n    return Value(object);\n}\nstatic Value cfv_index_any(const Value& value, const Value& index) {\n    if (const auto* object = std::get_if<std::shared_ptr<Object>>(&value.data)) {\n        const auto* key = std::get_if<std::string>(&index.data);\n        if (!key) throw std::runtime_error(\"la clave del mapa debe ser texto\");\n        const auto found = (*object)->find(*key);\n        if (found == (*object)->end()) throw std::runtime_error(\"clave desconocida \" + *key);\n        return found->second;\n    }\n    return cfv_index(value, index);\n}\nstatic Value& cfv_index_ref(Value& value, const Value& index) {\n    if (auto* object = std::get_if<std::shared_ptr<Object>>(&value.data)) {\n        const auto* key = std::get_if<std::string>(&index.data);\n        if (!key) throw std::runtime_error(\"la clave del mapa debe ser texto\");\n        return (**object)[*key];\n    }\n    auto* list = std::get_if<std::shared_ptr<List>>(&value.data);\n    if (!list) throw std::runtime_error(\"el valor no admite asignación indexada\");\n    const auto position = static_cast<std::size_t>(cfv_num(index));\n    if (position >= (*list)->size()) throw std::runtime_error(\"índice fuera de rango\");\n    return (**list)[position];\n}\n");
    return Value();
}
static Value cfv_fn_nombre_cpp_core(const std::vector<Value>& cfv_args) {
    Value cfv_nombre = cfv_arg(cfv_args, 0, "nombre_cpp_core");
    return cfv_add(cfv_text("cfv_"), cfv_nombre);
    return Value();
}
static Value cfv_fn_contiene_texto_core(const std::vector<Value>& cfv_args) {
    Value cfv_valores = cfv_arg(cfv_args, 0, "contiene_texto_core");
    Value cfv_buscado = cfv_arg(cfv_args, 1, "contiene_texto_core");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_valores), "<"))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_index_any(cfv_valores, cfv_indice), cfv_buscado)))) {
            return cfv_bool(true);
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_bool(false);
    return Value();
}
static Value cfv_fn_nombre_firma_core(const std::vector<Value>& cfv_args) {
    Value cfv_valor = cfv_arg(cfv_args, 0, "nombre_firma_core");
    return cfv_fn_nombre_declaracion_core(std::vector<Value>{cfv_valor});
    return Value();
}
static Value cfv_fn_campos_nodo_tipo_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "campos_nodo_tipo_core");
    Value cfv_campos = cfv_list(std::vector<Value>{});
    Value cfv_miembros = cfv_member(cfv_nodo, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_miembros), "<"))) {
        Value cfv_miembro = cfv_index_any(cfv_miembros, cfv_indice);
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_miembro, "tipo"), cfv_text("Campo"))))) {
            (void)(cfv_append(cfv_campos, cfv_fn_nombre_declaracion_core(std::vector<Value>{cfv_member(cfv_miembro, "valor")})));
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_campos;
    return Value();
}
static Value cfv_fn_contexto_emision_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "contexto_emision_core");
    Value cfv_tipos = cfv_list(std::vector<Value>{});
    Value cfv_funciones = cfv_list(std::vector<Value>{});
    Value cfv_metodos = cfv_list(std::vector<Value>{});
    Value cfv_declaraciones = cfv_member(cfv_programa, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_declaraciones), "<"))) {
        Value cfv_nodo = cfv_index_any(cfv_declaraciones, cfv_indice);
        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Estructura")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Clase"))))))) {
            (void)(cfv_append(cfv_tipos, cfv_object("TipoNativoCore", std::vector<std::string>{"nombre", "campos"}, std::vector<Value>{cfv_member(cfv_nodo, "valor"), cfv_fn_campos_nodo_tipo_core(std::vector<Value>{cfv_nodo})})));
        }
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Funcion"))))) {
            (void)(cfv_append(cfv_funciones, cfv_fn_nombre_firma_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")})));
        }
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Clase"))))) {
            Value cfv_miembros = cfv_member(cfv_nodo, "hijos");
            Value cfv_cursor = cfv_number(0);
            while (cfv_truth(cfv_compare(cfv_cursor, cfv_length(cfv_miembros), "<"))) {
                Value cfv_miembro_actual = cfv_index_any(cfv_miembros, cfv_cursor);
                if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_miembro_actual, "tipo"), cfv_text("Metodo"))))) {
                    (void)(cfv_append(cfv_metodos, cfv_fn_nombre_firma_core(std::vector<Value>{cfv_member(cfv_miembro_actual, "valor")})));
                }
                cfv_cursor = cfv_add(cfv_cursor, cfv_number(1));
            }
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_object("ContextoEmisionCore", std::vector<std::string>{"tipos", "funciones", "metodos"}, std::vector<Value>{cfv_tipos, cfv_funciones, cfv_metodos});
    return Value();
}
static Value cfv_fn_indice_tipo_nativo_core(const std::vector<Value>& cfv_args) {
    Value cfv_contexto = cfv_arg(cfv_args, 0, "indice_tipo_nativo_core");
    Value cfv_nombre = cfv_arg(cfv_args, 1, "indice_tipo_nativo_core");
    Value cfv_tipos = cfv_member(cfv_contexto, "tipos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_tipos), "<"))) {
        Value cfv_tipo_actual = cfv_index_any(cfv_tipos, cfv_indice);
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_tipo_actual, "nombre"), cfv_nombre)))) {
            return cfv_indice;
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_neg(cfv_number(1));
    return Value();
}
static Value cfv_fn_vector_textos_cpp_core(const std::vector<Value>& cfv_args) {
    Value cfv_valores = cfv_arg(cfv_args, 0, "vector_textos_cpp_core");
    Value cfv_salida = cfv_text("std::vector<std::string>{");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_valores), "<"))) {
        if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">"))) {
            cfv_salida = cfv_add(cfv_salida, cfv_text(", "));
        }
        cfv_salida = cfv_add(cfv_add(cfv_add(cfv_salida, cfv_text("\"")), cfv_index_any(cfv_valores, cfv_indice)), cfv_text("\""));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_add(cfv_salida, cfv_text("}"));
    return Value();
}
static Value cfv_fn_emitir_argumentos_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodos = cfv_arg(cfv_args, 0, "emitir_argumentos_core");
    Value cfv_inicio = cfv_arg(cfv_args, 1, "emitir_argumentos_core");
    Value cfv_contexto = cfv_arg(cfv_args, 2, "emitir_argumentos_core");
    Value cfv_salida = cfv_text("std::vector<Value>{");
    Value cfv_indice = cfv_inicio;
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_nodos), "<"))) {
        if (cfv_truth(cfv_compare(cfv_indice, cfv_inicio, ">"))) {
            cfv_salida = cfv_add(cfv_salida, cfv_text(", "));
        }
        cfv_salida = cfv_add(cfv_salida, cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_nodos, cfv_indice), cfv_contexto}));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_add(cfv_salida, cfv_text("}"));
    return Value();
}
static Value cfv_fn_emitir_llamada_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "emitir_llamada_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_llamada_core");
    Value cfv_nombre = cfv_member(cfv_nodo, "valor");
    Value cfv_argumentos = cfv_member(cfv_nodo, "hijos");
    Value cfv_vector = cfv_fn_emitir_argumentos_core(std::vector<Value>{cfv_argumentos, cfv_number(0), cfv_contexto});
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("longitud"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_length("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("a_texto"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_text(cfv_format("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text("))"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("agregar"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_append("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(1)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("afirmar"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_assert("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(1)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("argumentos_programa"))))) {
        return cfv_text("cfv_arguments()");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("leer_archivo"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_read_file("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("escribir_archivo"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_write_file("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(1)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("eliminar_archivo"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_remove_file("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_nombre, cfv_text("compilar_cpp_nativo"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_compile_cpp("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_argumentos, cfv_number(1)), cfv_contexto})), cfv_text(")"));
    }
    Value cfv_indice_tipo = cfv_fn_indice_tipo_nativo_core(std::vector<Value>{cfv_contexto, cfv_nombre});
    if (cfv_truth(cfv_compare(cfv_indice_tipo, cfv_number(0), ">="))) {
        Value cfv_tipos = cfv_member(cfv_contexto, "tipos");
        Value cfv_tipo = cfv_index_any(cfv_tipos, cfv_indice_tipo);
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_object(\""), cfv_nombre), cfv_text("\", ")), cfv_fn_vector_textos_cpp_core(std::vector<Value>{cfv_member(cfv_tipo, "campos")})), cfv_text(", ")), cfv_vector), cfv_text(")"));
    }
    return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_fn_"), cfv_nombre), cfv_text("(")), cfv_vector), cfv_text(")"));
    return Value();
}
static Value cfv_fn_emitir_binario_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "emitir_binario_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_binario_core");
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    Value cfv_izquierdo = cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto});
    Value cfv_derecho = cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_contexto});
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("+"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_add("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("-"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_sub("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("*"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_mul("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("/"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_div("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("%"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_mod("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("=="))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_bool(cfv_equal("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text("))"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("!="))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_bool(!cfv_equal("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text("))"));
    }
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("<")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("<=")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text(">")))))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text(">="))))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_compare("), cfv_izquierdo), cfv_text(", ")), cfv_derecho), cfv_text(", \"")), cfv_member(cfv_nodo, "valor")), cfv_text("\")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("y"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_bool(cfv_truth("), cfv_izquierdo), cfv_text(") && cfv_truth(")), cfv_derecho), cfv_text("))"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("o"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_bool(cfv_truth("), cfv_izquierdo), cfv_text(") || cfv_truth(")), cfv_derecho), cfv_text("))"));
    }
    (void)(cfv_assert(cfv_bool(false), cfv_add(cfv_add(cfv_text("operador no emitible '"), cfv_member(cfv_nodo, "valor")), cfv_text("'"))));
    return cfv_text("");
    return Value();
}
static Value cfv_fn_emitir_expresion_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "emitir_expresion_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_expresion_core");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Numero"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_number("), cfv_member(cfv_nodo, "valor")), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Texto"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_text("), cfv_member(cfv_nodo, "valor")), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Booleano"))))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("verdadero"))))) {
            return cfv_text("cfv_bool(true)");
        }
        return cfv_text("cfv_bool(false)");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Identificador"))))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("este"))))) {
            return cfv_text("cfv_este");
        }
        return cfv_fn_nombre_cpp_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
    }
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mover"))))) {
        return cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto});
    }
    if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoCompartido")))) || cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("PrestamoMutable"))))))) {
        return cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("SoltarPrestamo"))))) {
        return cfv_text("Value()");
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Lista"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_list("), cfv_fn_emitir_argumentos_core(std::vector<Value>{cfv_hijos, cfv_number(0), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mapa"))))) {
        return cfv_add(cfv_add(cfv_text("cfv_map("), cfv_fn_emitir_argumentos_core(std::vector<Value>{cfv_hijos, cfv_number(0), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Binario"))))) {
        return cfv_fn_emitir_binario_core(std::vector<Value>{cfv_nodo, cfv_contexto});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Unario"))))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "valor"), cfv_text("no"))))) {
            return cfv_add(cfv_add(cfv_text("cfv_bool(!cfv_truth("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text("))"));
        }
        return cfv_add(cfv_add(cfv_text("cfv_neg("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Llamada"))))) {
        return cfv_fn_emitir_llamada_core(std::vector<Value>{cfv_nodo, cfv_contexto});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Indice"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_index_any("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_contexto})), cfv_text(")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Miembro"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_member("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(", \"")), cfv_member(cfv_nodo, "valor")), cfv_text("\")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("LlamadaMetodo"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_method_"), cfv_member(cfv_nodo, "valor")), cfv_text("(")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_argumentos_core(std::vector<Value>{cfv_hijos, cfv_number(1), cfv_contexto})), cfv_text(")"));
    }
    (void)(cfv_assert(cfv_bool(false), cfv_add(cfv_add(cfv_text("B5 no puede emitir el nodo de expresión '"), cfv_member(cfv_nodo, "tipo")), cfv_text("'"))));
    return cfv_text("");
    return Value();
}
static Value cfv_fn_emitir_bloque_core(const std::vector<Value>& cfv_args) {
    Value cfv_bloque = cfv_arg(cfv_args, 0, "emitir_bloque_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_bloque_core");
    Value cfv_sangria = cfv_arg(cfv_args, 2, "emitir_bloque_core");
    Value cfv_salida = cfv_text("");
    Value cfv_sentencias = cfv_member(cfv_bloque, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_sentencias), "<"))) {
        cfv_salida = cfv_add(cfv_salida, cfv_fn_emitir_sentencia_core(std::vector<Value>{cfv_index_any(cfv_sentencias, cfv_indice), cfv_contexto, cfv_sangria}));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_emitir_objetivo_asignacion_core(const std::vector<Value>& cfv_args) {
    Value cfv_objetivo = cfv_arg(cfv_args, 0, "emitir_objetivo_asignacion_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_objetivo_asignacion_core");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_objetivo, "tipo"), cfv_text("Identificador"))))) {
        return cfv_fn_nombre_cpp_core(std::vector<Value>{cfv_member(cfv_objetivo, "valor")});
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_objetivo, "tipo"), cfv_text("Miembro"))))) {
        Value cfv_hijos_objetivo = cfv_member(cfv_objetivo, "hijos");
        Value cfv_objeto = cfv_index_any(cfv_hijos_objetivo, cfv_number(0));
        Value cfv_objeto_emitido = cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_objeto, cfv_contexto});
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_member_ref("), cfv_objeto_emitido), cfv_text(", \"")), cfv_member(cfv_objetivo, "valor")), cfv_text("\")"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_objetivo, "tipo"), cfv_text("Indice"))))) {
        Value cfv_hijos_indice = cfv_member(cfv_objetivo, "hijos");
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("cfv_index_ref("), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos_indice, cfv_number(0)), cfv_contexto})), cfv_text(", ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos_indice, cfv_number(1)), cfv_contexto})), cfv_text(")"));
    }
    (void)(cfv_assert(cfv_bool(false), cfv_text("objetivo de asignación no soportado")));
    return cfv_text("");
    return Value();
}
static Value cfv_fn_emitir_sentencia_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "emitir_sentencia_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_sentencia_core");
    Value cfv_sangria = cfv_arg(cfv_args, 2, "emitir_sentencia_core");
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Declaracion"))))) {
        Value cfv_nombre = cfv_fn_nombre_declaracion_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("Value ")), cfv_fn_nombre_cpp_core(std::vector<Value>{cfv_nombre})), cfv_text(" = ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(";\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mostrar"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("cfv_print(")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(");\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Asignacion"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_fn_emitir_objetivo_asignacion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(" = ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_contexto})), cfv_text(";\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Expresion"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("(void)(")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(");\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Retornar"))))) {
        if (cfv_truth(cfv_bool(cfv_equal(cfv_length(cfv_hijos), cfv_number(0))))) {
            return cfv_add(cfv_sangria, cfv_text("return Value();\n"));
        }
        return cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("return ")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(";\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Si"))))) {
        Value cfv_salida = cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("if (cfv_truth(")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(")) {\n"));
        cfv_salida = cfv_add(cfv_salida, cfv_fn_emitir_bloque_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_contexto, cfv_add(cfv_sangria, cfv_text("    "))}));
        cfv_salida = cfv_add(cfv_add(cfv_salida, cfv_sangria), cfv_text("}"));
        if (cfv_truth(cfv_compare(cfv_length(cfv_hijos), cfv_number(2), ">"))) {
            cfv_salida = cfv_add(cfv_add(cfv_add(cfv_add(cfv_salida, cfv_text(" else {\n")), cfv_fn_emitir_bloque_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(2)), cfv_contexto, cfv_add(cfv_sangria, cfv_text("    "))})), cfv_sangria), cfv_text("}"));
        }
        return cfv_add(cfv_salida, cfv_text("\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Mientras"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("while (cfv_truth(")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text(")) {\n")), cfv_fn_emitir_bloque_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_contexto, cfv_add(cfv_sangria, cfv_text("    "))})), cfv_sangria), cfv_text("}\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Lanzar"))))) {
        return cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("throw std::runtime_error(cfv_format(")), cfv_fn_emitir_expresion_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto})), cfv_text("));\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Intentar"))))) {
        Value cfv_nombre_error = cfv_fn_nombre_cpp_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
        return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_sangria, cfv_text("try {\n")), cfv_fn_emitir_bloque_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(0)), cfv_contexto, cfv_add(cfv_sangria, cfv_text("    "))})), cfv_sangria), cfv_text("} catch (const std::exception& cfv_exception) {\n")), cfv_sangria), cfv_text("    Value ")), cfv_nombre_error), cfv_text(" = cfv_text(cfv_exception.what());\n")), cfv_fn_emitir_bloque_core(std::vector<Value>{cfv_index_any(cfv_hijos, cfv_number(1)), cfv_contexto, cfv_add(cfv_sangria, cfv_text("    "))})), cfv_sangria), cfv_text("}\n"));
    }
    if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Vacia"))))) {
        return cfv_text("");
    }
    (void)(cfv_assert(cfv_bool(false), cfv_add(cfv_add(cfv_text("B5 no puede emitir la sentencia '"), cfv_member(cfv_nodo, "tipo")), cfv_text("'"))));
    return cfv_text("");
    return Value();
}
static Value cfv_fn_emitir_parametros_funcion_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "emitir_parametros_funcion_core");
    Value cfv_nombre = cfv_arg(cfv_args, 1, "emitir_parametros_funcion_core");
    Value cfv_metodo = cfv_arg(cfv_args, 2, "emitir_parametros_funcion_core");
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    Value cfv_limite = cfv_sub(cfv_length(cfv_hijos), cfv_number(1));
    Value cfv_salida = cfv_text("");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_limite, "<"))) {
        Value cfv_nodo_parametro = cfv_index_any(cfv_hijos, cfv_indice);
        Value cfv_parametro = cfv_fn_nombre_declaracion_core(std::vector<Value>{cfv_member(cfv_nodo_parametro, "valor")});
        cfv_salida = cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_salida, cfv_text("    Value ")), cfv_fn_nombre_cpp_core(std::vector<Value>{cfv_parametro})), cfv_text(" = cfv_arg(cfv_args, ")), cfv_text(cfv_format(cfv_indice))), cfv_text(", \"")), cfv_nombre), cfv_text("\");\n"));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_emitir_funcion_core(const std::vector<Value>& cfv_args) {
    Value cfv_nodo = cfv_arg(cfv_args, 0, "emitir_funcion_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "emitir_funcion_core");
    Value cfv_metodo = cfv_arg(cfv_args, 2, "emitir_funcion_core");
    Value cfv_nombre = cfv_fn_nombre_firma_core(std::vector<Value>{cfv_member(cfv_nodo, "valor")});
    Value cfv_encabezado = cfv_add(cfv_add(cfv_text("static Value cfv_fn_"), cfv_nombre), cfv_text("(const std::vector<Value>& cfv_args) {\n"));
    if (cfv_truth(cfv_metodo)) {
        cfv_encabezado = cfv_add(cfv_add(cfv_text("static Value cfv_method_"), cfv_nombre), cfv_text("(Value cfv_este, const std::vector<Value>& cfv_args) {\n"));
    }
    Value cfv_hijos = cfv_member(cfv_nodo, "hijos");
    Value cfv_bloque = cfv_index_any(cfv_hijos, cfv_sub(cfv_length(cfv_hijos), cfv_number(1)));
    return cfv_add(cfv_add(cfv_add(cfv_encabezado, cfv_fn_emitir_parametros_funcion_core(std::vector<Value>{cfv_nodo, cfv_nombre, cfv_metodo})), cfv_fn_emitir_bloque_core(std::vector<Value>{cfv_bloque, cfv_contexto, cfv_text("    ")})), cfv_text("    return Value();\n}\n"));
    return Value();
}
static Value cfv_fn_prototipos_core(const std::vector<Value>& cfv_args) {
    Value cfv_contexto = cfv_arg(cfv_args, 0, "prototipos_core");
    Value cfv_salida = cfv_text("");
    Value cfv_funciones = cfv_member(cfv_contexto, "funciones");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_funciones), "<"))) {
        cfv_salida = cfv_add(cfv_add(cfv_add(cfv_salida, cfv_text("static Value cfv_fn_")), cfv_index_any(cfv_funciones, cfv_indice)), cfv_text("(const std::vector<Value>&);\n"));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    Value cfv_metodos = cfv_member(cfv_contexto, "metodos");
    cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_metodos), "<"))) {
        cfv_salida = cfv_add(cfv_add(cfv_add(cfv_salida, cfv_text("static Value cfv_method_")), cfv_index_any(cfv_metodos, cfv_indice)), cfv_text("(Value, const std::vector<Value>&);\n"));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_add(cfv_salida, cfv_text("\n"));
    return Value();
}
static Value cfv_fn_definiciones_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "definiciones_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "definiciones_core");
    Value cfv_salida = cfv_text("");
    Value cfv_declaraciones = cfv_member(cfv_programa, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_declaraciones), "<"))) {
        Value cfv_nodo = cfv_index_any(cfv_declaraciones, cfv_indice);
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Funcion"))))) {
            cfv_salida = cfv_add(cfv_salida, cfv_fn_emitir_funcion_core(std::vector<Value>{cfv_nodo, cfv_contexto, cfv_bool(false)}));
        }
        if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Clase"))))) {
            Value cfv_miembros = cfv_member(cfv_nodo, "hijos");
            Value cfv_cursor = cfv_number(0);
            while (cfv_truth(cfv_compare(cfv_cursor, cfv_length(cfv_miembros), "<"))) {
                Value cfv_miembro_actual = cfv_index_any(cfv_miembros, cfv_cursor);
                if (cfv_truth(cfv_bool(cfv_equal(cfv_member(cfv_miembro_actual, "tipo"), cfv_text("Metodo"))))) {
                    cfv_salida = cfv_add(cfv_salida, cfv_fn_emitir_funcion_core(std::vector<Value>{cfv_miembro_actual, cfv_contexto, cfv_bool(true)}));
                }
                cfv_cursor = cfv_add(cfv_cursor, cfv_number(1));
            }
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
static Value cfv_fn_cuerpo_principal_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "cuerpo_principal_core");
    Value cfv_contexto = cfv_arg(cfv_args, 1, "cuerpo_principal_core");
    Value cfv_salida = cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_text("int main(int argc, char** argv) {\n"), cfv_text("    try {\n")), cfv_text("        cfv_process_args.clear();\n")), cfv_text("        for (int index = 0; index < argc; ++index) {\n")), cfv_text("            cfv_process_args.emplace_back(std::string(argv[index]));\n")), cfv_text("        }\n"));
    Value cfv_declaraciones = cfv_member(cfv_programa, "hijos");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_declaraciones), "<"))) {
        Value cfv_nodo = cfv_index_any(cfv_declaraciones, cfv_indice);
        if (cfv_truth(cfv_bool(cfv_truth(cfv_bool(cfv_truth(cfv_bool(!cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Funcion")))) && cfv_truth(cfv_bool(!cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Estructura")))))) && cfv_truth(cfv_bool(!cfv_equal(cfv_member(cfv_nodo, "tipo"), cfv_text("Clase"))))))) {
            cfv_salida = cfv_add(cfv_salida, cfv_fn_emitir_sentencia_core(std::vector<Value>{cfv_nodo, cfv_contexto, cfv_text("        ")}));
        }
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_add(cfv_salida, cfv_text("        return 0;\n")), cfv_text("    } catch (const std::exception& error) {\n")), cfv_text("        std::cerr << \"[C-Forge Core Runtime Error] \" ")), cfv_text("<< error.what() << '\\n';\n")), cfv_text("        return 1;\n")), cfv_text("    }\n")), cfv_text("}\n"));
    return Value();
}
static Value cfv_fn_emitir_programa_core(const std::vector<Value>& cfv_args) {
    Value cfv_programa = cfv_arg(cfv_args, 0, "emitir_programa_core");
    Value cfv_semantica = cfv_fn_analizar_semantica_core(std::vector<Value>{cfv_programa});
    if (cfv_truth(cfv_bool(!cfv_truth(cfv_member(cfv_semantica, "valido"))))) {
        return cfv_object("ResultadoEmisionCore", std::vector<std::string>{"valido", "codigo", "errores"}, std::vector<Value>{cfv_bool(false), cfv_text(""), cfv_member(cfv_semantica, "errores")});
    }
    Value cfv_contexto = cfv_fn_contexto_emision_core(std::vector<Value>{cfv_programa});
    Value cfv_codigo = cfv_add(cfv_add(cfv_add(cfv_add(cfv_fn_runtime_cpp_core(std::vector<Value>{}), cfv_fn_runtime_mapas_cpp_core(std::vector<Value>{})), cfv_fn_prototipos_core(std::vector<Value>{cfv_contexto})), cfv_fn_definiciones_core(std::vector<Value>{cfv_programa, cfv_contexto})), cfv_fn_cuerpo_principal_core(std::vector<Value>{cfv_programa, cfv_contexto}));
    return cfv_object("ResultadoEmisionCore", std::vector<std::string>{"valido", "codigo", "errores"}, std::vector<Value>{cfv_bool(true), cfv_codigo, cfv_list(std::vector<Value>{})});
    return Value();
}
static Value cfv_fn_compilar_fuente_stage1(const std::vector<Value>& cfv_args) {
    Value cfv_fuente = cfv_arg(cfv_args, 0, "compilar_fuente_stage1");
    Value cfv_tokens = cfv_fn_tokenizar_core(std::vector<Value>{cfv_fuente});
    Value cfv_programa = cfv_fn_parsear_tokens_core(std::vector<Value>{cfv_tokens});
    Value cfv_errores_ownership = cfv_fn_verificar_ownership_core(std::vector<Value>{cfv_programa});
    if (cfv_truth(cfv_compare(cfv_length(cfv_errores_ownership), cfv_number(0), ">"))) {
        return cfv_object("ResultadoCompilacionCore", std::vector<std::string>{"valido", "codigo", "errores"}, std::vector<Value>{cfv_bool(false), cfv_text(""), cfv_errores_ownership});
    }
    Value cfv_semantica = cfv_fn_analizar_semantica_core(std::vector<Value>{cfv_programa});
    if (cfv_truth(cfv_bool(!cfv_truth(cfv_member(cfv_semantica, "valido"))))) {
        return cfv_object("ResultadoCompilacionCore", std::vector<std::string>{"valido", "codigo", "errores"}, std::vector<Value>{cfv_bool(false), cfv_text(""), cfv_member(cfv_semantica, "errores")});
    }
    Value cfv_emision = cfv_fn_emitir_programa_core(std::vector<Value>{cfv_programa});
    return cfv_object("ResultadoCompilacionCore", std::vector<std::string>{"valido", "codigo", "errores"}, std::vector<Value>{cfv_member(cfv_emision, "valido"), cfv_member(cfv_emision, "codigo"), cfv_member(cfv_emision, "errores")});
    return Value();
}
static Value cfv_fn_diagnosticos_stage1(const std::vector<Value>& cfv_args) {
    Value cfv_resultado = cfv_arg(cfv_args, 0, "diagnosticos_stage1");
    Value cfv_errores = cfv_member(cfv_resultado, "errores");
    Value cfv_salida = cfv_text("");
    Value cfv_indice = cfv_number(0);
    while (cfv_truth(cfv_compare(cfv_indice, cfv_length(cfv_errores), "<"))) {
        if (cfv_truth(cfv_compare(cfv_indice, cfv_number(0), ">"))) {
            cfv_salida = cfv_add(cfv_salida, cfv_text("\n"));
        }
        cfv_salida = cfv_add(cfv_salida, cfv_index_any(cfv_errores, cfv_indice));
        cfv_indice = cfv_add(cfv_indice, cfv_number(1));
    }
    return cfv_salida;
    return Value();
}
int main(int argc, char** argv) {
    try {
        cfv_process_args.clear();
        for (int index = 0; index < argc; ++index) {
            cfv_process_args.emplace_back(std::string(argv[index]));
        }
        Value cfv_argumentos_stage1 = cfv_arguments();
        (void)(cfv_assert(cfv_bool(cfv_equal(cfv_length(cfv_argumentos_stage1), cfv_number(4))), cfv_text("uso: cforge-stage1 archivo.cfv (-o ejecutable | --emitir-cpp salida.cpp)")));
        Value cfv_entrada_stage1 = cfv_index_any(cfv_argumentos_stage1, cfv_number(1));
        Value cfv_salida_stage1 = cfv_index_any(cfv_argumentos_stage1, cfv_number(3));
        Value cfv_fuente_stage1 = cfv_read_file(cfv_entrada_stage1);
        Value cfv_resultado_stage1 = cfv_fn_compilar_fuente_stage1(std::vector<Value>{cfv_fuente_stage1});
        (void)(cfv_assert(cfv_member(cfv_resultado_stage1, "valido"), cfv_fn_diagnosticos_stage1(std::vector<Value>{cfv_resultado_stage1})));
        if (cfv_truth(cfv_bool(cfv_equal(cfv_index_any(cfv_argumentos_stage1, cfv_number(2)), cfv_text("--emitir-cpp"))))) {
            (void)(cfv_write_file(cfv_salida_stage1, cfv_member(cfv_resultado_stage1, "codigo")));
            cfv_print(cfv_add(cfv_text("C-Forge Stage 1 emitió: "), cfv_salida_stage1));
        } else {
            (void)(cfv_assert(cfv_bool(cfv_equal(cfv_index_any(cfv_argumentos_stage1, cfv_number(2)), cfv_text("-o"))), cfv_text("uso: cforge-stage1 archivo.cfv (-o ejecutable | --emitir-cpp salida.cpp)")));
            Value cfv_temporal_stage1 = cfv_add(cfv_entrada_stage1, cfv_text(".stage1.cpp"));
            (void)(cfv_write_file(cfv_temporal_stage1, cfv_member(cfv_resultado_stage1, "codigo")));
            Value cfv_nativo_stage1 = cfv_compile_cpp(cfv_temporal_stage1, cfv_salida_stage1);
            (void)(cfv_remove_file(cfv_temporal_stage1));
            (void)(cfv_assert(cfv_nativo_stage1, cfv_text("el backend nativo no pudo producir el ejecutable")));
            cfv_print(cfv_add(cfv_text("C-Forge Stage 1 creó: "), cfv_salida_stage1));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[C-Forge Core Runtime Error] " << error.what() << '\n';
        return 1;
    }
}
