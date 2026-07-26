#ifndef CFORGEV_FFI_H
#define CFORGEV_FFI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define CFV_EXPORT __declspec(dllexport)
#else
#define CFV_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CfvType {
    CFV_NULL = 0,
    CFV_INTEGER = 1,
    CFV_DECIMAL = 2,
    CFV_TEXT = 3,
    CFV_BOOLEAN = 4,
    CFV_LIST = 5,
    CFV_MAP = 6,
    CFV_RECORD = 7
} CfvType;

typedef void (*CfvReleaseFunction)(void* owner);

/* Vista prestada, contigua y de solo lectura. El puntero solo es válido durante
   la llamada extranjera y nunca debe liberarse ni conservarse. */
typedef struct CfvNumberSlice {
    const double* data;
    uint64_t length;
} CfvNumberSlice;

/* Resultado propietario para extern_c segura. C-Forge copia el contenido a su
   runtime y luego invoca release(owner) una vez. */
typedef struct CfvOwnedNumberList {
    const double* data;
    uint64_t length;
    void* owner;
    CfvReleaseFunction release;
} CfvOwnedNumberList;

/* Texto UTF-8 propietario. data no contiene NUL dentro de length. */
typedef struct CfvOwnedText {
    const char* data;
    uint64_t length;
    void* owner;
    CfvReleaseFunction release;
} CfvOwnedText;

/* Vista prestada de mapa texto->número. keys y values contienen length entradas. */
typedef struct CfvNumberMapView {
    const char* const* keys;
    const double* values;
    uint64_t length;
} CfvNumberMapView;

typedef struct CfvValue {
    int32_t type;
    int64_t integer;
    double decimal;
    const char* text;
    void* owner;
    CfvReleaseFunction release;
} CfvValue;

typedef struct CfvRecordField {
    const char* name;
    CfvValue value;
} CfvRecordField;

/* Vista prestada de un objeto nominal con campos escalares. */
typedef struct CfvRecordView {
    const char* type_name;
    const CfvRecordField* fields;
    uint64_t field_count;
} CfvRecordView;

typedef int (*CfvForeignFunction)(
    const CfvValue* arguments,
    size_t argument_count,
    CfvValue* result,
    char* error_buffer,
    size_t error_buffer_size
);

#define CFV_ABI_V2 0x00020000u
#define CFV_V2_BORROWED 0x00000001ull
#define CFV_V2_OWNED 0x00000002ull
#define CFV_V2_MAX_DEPTH 64u

typedef struct CfvValueV2 {
    uint32_t struct_size;
    uint32_t type;
    uint64_t flags;
    uint64_t length;
    int64_t integer;
    double decimal;
    const void* data;
    void* owner;
    CfvReleaseFunction release;
} CfvValueV2;

/* Una lista V2 usa data=CfvValueV2[length]. Sus elementos son vistas
   recursivas y no pueden conservarse después de la llamada si BORROWED. */
typedef struct CfvMapEntryV2 {
    CfvValueV2 key;
    CfvValueV2 value;
} CfvMapEntryV2;

/* Un mapa V2 usa data=CfvMapEntryV2[length]. Las claves actuales deben ser
   CFV_TEXT; la forma permite ampliar el ABI sin cambiar CfvValueV2. */
typedef struct CfvRecordFieldV2 {
    const char* name;
    uint64_t name_length;
    CfvValueV2 value;
} CfvRecordFieldV2;

typedef struct CfvRecordV2 {
    const char* type_name;
    uint64_t type_name_length;
    const CfvRecordFieldV2* fields;
    uint64_t field_count;
} CfvRecordV2;

typedef int (*CfvForeignFunctionV2)(
    uint32_t abi_version,
    const CfvValueV2* arguments,
    size_t argument_count,
    CfvValueV2* result,
    char* error_buffer,
    size_t error_buffer_size
);

CFV_EXPORT int cfv_register_function(const char* name, CfvForeignFunction function);
CFV_EXPORT int cfv_register_function_v2(const char* name, CfvForeignFunctionV2 function);

#ifdef __cplusplus
}
#endif

#endif
