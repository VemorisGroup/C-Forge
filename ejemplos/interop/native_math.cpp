#include "cforgev_ffi.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

static int released_texts = 0;

static void release_owned_text(void* owner) {
    ++released_texts;
    std::free(owner);
}

extern "C" CFV_EXPORT int native_multiply(
    const CfvValue* arguments, size_t count, CfvValue* result,
    char* error, size_t error_size) {
    if (count != 2 || arguments[0].type != CFV_INTEGER || arguments[1].type != CFV_INTEGER) {
        std::snprintf(error, error_size, "native_multiply requiere dos enteros");
        return 1;
    }
    *result = {CFV_INTEGER, arguments[0].integer * arguments[1].integer, 0.0, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_half(
    const CfvValue* arguments, size_t count, CfvValue* result,
    char* error, size_t error_size) {
    if (count != 1 || (arguments[0].type != CFV_INTEGER && arguments[0].type != CFV_DECIMAL)) {
        std::snprintf(error, error_size, "native_half requiere un número");
        return 1;
    }
    double value = arguments[0].type == CFV_INTEGER
        ? static_cast<double>(arguments[0].integer) : arguments[0].decimal;
    *result = {CFV_DECIMAL, 0, value / 2.0, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_greet(
    const CfvValue* arguments, size_t count, CfvValue* result,
    char* error, size_t error_size) {
    static thread_local char message[512];
    if (count != 1 || arguments[0].type != CFV_TEXT) {
        std::snprintf(error, error_size, "native_greet requiere texto");
        return 1;
    }
    std::snprintf(message, sizeof(message), "Hola %s desde C++", arguments[0].text);
    *result = {CFV_TEXT, 0, 0.0, message};
    return 0;
}

extern "C" CFV_EXPORT int native_owned_greet(
    const CfvValue* arguments, size_t count, CfvValue* result,
    char* error, size_t error_size) {
    if (count != 1 || arguments[0].type != CFV_TEXT) {
        std::snprintf(error, error_size, "native_owned_greet requiere texto");
        return 1;
    }
    const char* prefix = "Texto RAII para ";
    const size_t size = std::strlen(prefix) + std::strlen(arguments[0].text) + 1;
    char* message = static_cast<char*>(std::malloc(size));
    if (!message) {
        std::snprintf(error, error_size, "no se pudo reservar memoria nativa");
        return 1;
    }
    std::snprintf(message, size, "%s%s", prefix, arguments[0].text);
    *result = {CFV_TEXT, 0, 0.0, message, message, release_owned_text};
    return 0;
}

extern "C" CFV_EXPORT int native_release_count(
    const CfvValue*, size_t, CfvValue* result, char*, size_t) {
    *result = {CFV_INTEGER, released_texts, 0.0, nullptr, nullptr, nullptr};
    return 0;
}

#ifndef CFV_NO_AUTO_REGISTER
namespace {
struct RegisterNativeMath {
    RegisterNativeMath() {
        cfv_register_function("multiply", native_multiply);
        cfv_register_function("half", native_half);
        cfv_register_function("greet", native_greet);
        cfv_register_function("owned_greet", native_owned_greet);
        cfv_register_function("release_count", native_release_count);
    }
} registration;
}
#endif
