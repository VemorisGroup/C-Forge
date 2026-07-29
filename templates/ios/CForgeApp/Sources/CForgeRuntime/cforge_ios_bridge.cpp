// cforge_ios_bridge.cpp — Implementación del bridge C-Forge para iOS/macOS
// Envuelve el intérprete cforgev en una API C limpia

#include "include/cforge_ios.h"
#include <string>
#include <sstream>
#include <memory>
#include <map>
#include <functional>
#include <vector>
#include <cstring>

// ── Forward declarations del intérprete ────────────────────────────────────────
// En producción, incluir cforgev.cpp directamente o linkearlo como objeto.
// Aquí definimos stubs de compilación para el template.
#ifdef CFORGE_EMBEDDED
  // Incluir el intérprete completo en modo embedded
  // #include "cforgev_embedded.cpp"
  // Por ahora usamos el stub de demostración
  #define CFORGE_STUB_MODE 1
#endif

// ── Contexto interno ───────────────────────────────────────────────────────────
struct CForgeContext {
    std::string last_error;
    std::string last_output;
    std::map<std::string, std::string> globals_str;
    std::map<std::string, double>      globals_num;
    std::map<std::string, bool>        globals_bool;
    std::map<std::string, CForgeCallback> native_fns;

    // Buffer de salida redirigida
    std::ostringstream output_buffer;

    // En modo real, aquí iría el estado del intérprete
    // ForgeInterpreter* interp = nullptr;
};

struct CForgeValue {
    CForgeType  type;
    double      num_val;
    std::string str_val;
    bool        bool_val;
    std::vector<CForgeValue*> list_val;

    CForgeValue() : type(CFORGE_NULL), num_val(0), bool_val(false) {}
};

// Pool simple de valores (en producción usar GC)
static thread_local std::vector<std::unique_ptr<CForgeValue>> value_pool;

static CForgeValue* make_value(CForgeType t) {
    value_pool.push_back(std::make_unique<CForgeValue>());
    auto* v = value_pool.back().get();
    v->type = t;
    return v;
}

// ── API pública ────────────────────────────────────────────────────────────────

CForgeContext* cforge_context_create() {
    return new CForgeContext();
}

void cforge_context_destroy(CForgeContext* ctx) {
    if (ctx) {
        value_pool.clear();
        delete ctx;
    }
}

bool cforge_run_string(CForgeContext* ctx, const char* code) {
    if (!ctx || !code) return false;

#ifdef CFORGE_STUB_MODE
    // Stub: en modo demo, ejecutar código simple
    std::string c(code);
    ctx->last_error.clear();

    // Detectar mostrar(...)
    if (c.find("mostrar(") != std::string::npos) {
        auto start = c.find("mostrar(\"");
        if (start != std::string::npos) {
            start += 9;
            auto end = c.find("\")", start);
            if (end != std::string::npos) {
                ctx->last_output = c.substr(start, end - start) + "\n";
            }
        }
        return true;
    }

    ctx->last_output = "[C-Forge stub] Código ejecutado: " + c.substr(0, 50) + "\n";
    return true;
#else
    // Modo real: llamar al intérprete completo
    // return ctx->interp->run(code, ctx->output_buffer);
    ctx->last_error = "Intérprete no compilado en modo embedded";
    return false;
#endif
}

bool cforge_run_file(CForgeContext* ctx, const char* path) {
    if (!ctx || !path) return false;

    // Leer archivo
    FILE* f = fopen(path, "r");
    if (!f) {
        ctx->last_error = std::string("No se pudo abrir: ") + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string code(size, '\0');
    fread(&code[0], 1, size, f);
    fclose(f);

    return cforge_run_string(ctx, code.c_str());
}

const char* cforge_last_error(CForgeContext* ctx) {
    if (!ctx) return "ctx nulo";
    return ctx->last_error.c_str();
}

const char* cforge_last_output(CForgeContext* ctx) {
    if (!ctx) return "";
    return ctx->last_output.c_str();
}

void cforge_clear_output(CForgeContext* ctx) {
    if (ctx) ctx->last_output.clear();
}

void cforge_set_number(CForgeContext* ctx, const char* name, double value) {
    if (ctx && name) ctx->globals_num[name] = value;
}

void cforge_set_string(CForgeContext* ctx, const char* name, const char* value) {
    if (ctx && name && value) ctx->globals_str[name] = value;
}

void cforge_set_bool(CForgeContext* ctx, const char* name, bool value) {
    if (ctx && name) ctx->globals_bool[name] = value;
}

CForgeValue* cforge_get_global(CForgeContext* ctx, const char* name) {
    if (!ctx || !name) return nullptr;
    std::string k(name);

    if (ctx->globals_num.count(k)) {
        auto* v = make_value(CFORGE_NUMBER);
        v->num_val = ctx->globals_num[k];
        return v;
    }
    if (ctx->globals_str.count(k)) {
        auto* v = make_value(CFORGE_STRING);
        v->str_val = ctx->globals_str[k];
        return v;
    }
    if (ctx->globals_bool.count(k)) {
        auto* v = make_value(CFORGE_BOOL);
        v->bool_val = ctx->globals_bool[k];
        return v;
    }
    return nullptr;
}

CForgeType cforge_value_type(CForgeValue* val) {
    return val ? val->type : CFORGE_NULL;
}

double cforge_value_number(CForgeValue* val) {
    return val ? val->num_val : 0.0;
}

const char* cforge_value_string(CForgeValue* val) {
    return val ? val->str_val.c_str() : "";
}

bool cforge_value_bool(CForgeValue* val) {
    return val ? val->bool_val : false;
}

int cforge_value_list_length(CForgeValue* val) {
    return val ? (int)val->list_val.size() : 0;
}

CForgeValue* cforge_value_list_get(CForgeValue* val, int index) {
    if (!val || index < 0 || index >= (int)val->list_val.size()) return nullptr;
    return val->list_val[index];
}

CForgeValue* cforge_call_fn(CForgeContext* ctx, const char* fn_name,
                              double* num_args, const char** str_args,
                              int num_count, int str_count) {
    if (!ctx || !fn_name) return nullptr;

    // En modo real: buscar función en el contexto y llamarla
    // Por ahora retorna el nombre de la función como string
    auto* v = make_value(CFORGE_STRING);
    v->str_val = std::string("llamado:") + fn_name;
    return v;
}

void cforge_register_native(CForgeContext* ctx, const char* name, CForgeCallback cb) {
    if (ctx && name && cb) {
        ctx->native_fns[name] = cb;
    }
}

const char* cforge_version() {
    return "2.3.1";
}

bool cforge_validate_syntax(const char* code) {
    if (!code) return false;
    // Validación básica: balanceo de llaves/paréntesis
    int braces = 0, parens = 0, brackets = 0;
    bool in_string = false;
    char prev = 0;
    for (const char* p = code; *p; ++p) {
        if (*p == '"' && prev != '\\') in_string = !in_string;
        if (!in_string) {
            if (*p == '{') braces++;
            else if (*p == '}') braces--;
            else if (*p == '(') parens++;
            else if (*p == ')') parens--;
            else if (*p == '[') brackets++;
            else if (*p == ']') brackets--;
        }
        prev = *p;
    }
    return braces == 0 && parens == 0 && brackets == 0;
}
