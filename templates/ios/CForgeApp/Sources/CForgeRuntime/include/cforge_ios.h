#pragma once
// cforge_ios.h — API pública C del intérprete C-Forge para iOS/macOS
// Incluir en bridging header para usar desde Swift

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// ── Tipos opacos ──────────────────────────────────────────────────────────────
typedef struct CForgeContext CForgeContext;
typedef struct CForgeValue   CForgeValue;

// ── Tipos de valor ─────────────────────────────────────────────────────────────
typedef enum {
    CFORGE_NULL    = 0,
    CFORGE_NUMBER  = 1,
    CFORGE_STRING  = 2,
    CFORGE_BOOL    = 3,
    CFORGE_LIST    = 4,
    CFORGE_MAP     = 5
} CForgeType;

// ── Contexto de ejecución ──────────────────────────────────────────────────────
/// Crea un contexto nuevo. Retorna NULL si falla.
CForgeContext* cforge_context_create(void);

/// Destruye el contexto y libera toda la memoria.
void cforge_context_destroy(CForgeContext* ctx);

/// Ejecuta código C-Forge. Retorna true si éxito.
bool cforge_run_string(CForgeContext* ctx, const char* code);

/// Ejecuta un archivo .cfv. Retorna true si éxito.
bool cforge_run_file(CForgeContext* ctx, const char* path);

/// Retorna el último error (string estático, válido hasta la próxima llamada).
const char* cforge_last_error(CForgeContext* ctx);

/// Retorna la última salida capturada.
const char* cforge_last_output(CForgeContext* ctx);

/// Limpia la salida capturada.
void cforge_clear_output(CForgeContext* ctx);

// ── Variables globales ─────────────────────────────────────────────────────────
/// Obtiene el valor de una variable global del contexto.
CForgeValue* cforge_get_global(CForgeContext* ctx, const char* name);

/// Establece una variable global de tipo número.
void cforge_set_number(CForgeContext* ctx, const char* name, double value);

/// Establece una variable global de tipo texto.
void cforge_set_string(CForgeContext* ctx, const char* name, const char* value);

/// Establece una variable global booleana.
void cforge_set_bool(CForgeContext* ctx, const char* name, bool value);

// ── Inspección de valores ──────────────────────────────────────────────────────
CForgeType   cforge_value_type(CForgeValue* val);
double       cforge_value_number(CForgeValue* val);
const char*  cforge_value_string(CForgeValue* val);
bool         cforge_value_bool(CForgeValue* val);
int          cforge_value_list_length(CForgeValue* val);
CForgeValue* cforge_value_list_get(CForgeValue* val, int index);

// ── Llamar funciones C-Forge desde Swift ──────────────────────────────────────
/// Llama una función C-Forge con argumentos número.
CForgeValue* cforge_call_fn(CForgeContext* ctx, const char* fn_name,
                             double* num_args, const char** str_args,
                             int num_count, int str_count);

// ── Callbacks (Swift → C-Forge) ────────────────────────────────────────────────
typedef const char* (*CForgeCallback)(const char* json_args);

/// Registra una función nativa llamable desde C-Forge.
void cforge_register_native(CForgeContext* ctx, const char* name, CForgeCallback cb);

// ── Utilidades ─────────────────────────────────────────────────────────────────
/// Retorna la versión del intérprete.
const char* cforge_version(void);

/// Verifica que el código sea sintácticamente válido sin ejecutarlo.
bool cforge_validate_syntax(const char* code);

#ifdef __cplusplus
}
#endif
