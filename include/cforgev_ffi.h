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
    CFV_TEXT = 3
} CfvType;

typedef void (*CfvReleaseFunction)(void* owner);

typedef struct CfvValue {
    int32_t type;
    int64_t integer;
    double decimal;
    const char* text;
    void* owner;
    CfvReleaseFunction release;
} CfvValue;

typedef int (*CfvForeignFunction)(
    const CfvValue* arguments,
    size_t argument_count,
    CfvValue* result,
    char* error_buffer,
    size_t error_buffer_size
);

CFV_EXPORT int cfv_register_function(const char* name, CfvForeignFunction function);

#ifdef __cplusplus
}
#endif

#endif
