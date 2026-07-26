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

extern "C" CFV_EXPORT int native_toggle(
    const CfvValue* arguments, size_t count, CfvValue* result,
    char* error, size_t error_size) {
    if (count != 1 || arguments[0].type != CFV_BOOLEAN) {
        std::snprintf(error, error_size, "native_toggle requiere booleano");
        return 1;
    }
    *result = {CFV_BOOLEAN, arguments[0].integer ? 0 : 1, 0.0, nullptr, nullptr, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_echo_v2(
    uint32_t abi_version, const CfvValueV2* arguments, size_t count,
    CfvValueV2* result, char* error, size_t error_size) {
    if (abi_version != CFV_ABI_V2 || count != 1 || arguments[0].type != CFV_TEXT) {
        std::snprintf(error, error_size, "native_echo_v2 requiere texto ABI V2");
        return 1;
    }
    *result = {sizeof(CfvValueV2), CFV_TEXT, 0, arguments[0].length, 0, 0.0,
               arguments[0].data, nullptr, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_owned_echo_v2(
    uint32_t abi_version, const CfvValueV2* arguments, size_t count,
    CfvValueV2* result, char* error, size_t error_size) {
    if (abi_version != CFV_ABI_V2 || count != 1 || arguments[0].type != CFV_TEXT) {
        std::snprintf(error, error_size, "native_owned_echo_v2 requiere texto ABI V2");
        return 1;
    }
    const size_t length = static_cast<size_t>(arguments[0].length);
    char* copy = static_cast<char*>(std::malloc(length ? length : 1));
    if (!copy) { std::snprintf(error, error_size, "sin memoria ABI V2"); return 2; }
    if (length) std::memcpy(copy, arguments[0].data, length);
    *result = {sizeof(CfvValueV2), CFV_TEXT, 0, length, 0, 0.0,
               copy, copy, release_owned_text};
    return 0;
}

extern "C" CFV_EXPORT int native_list_sum_v2(
    uint32_t abi_version, const CfvValueV2* arguments, size_t count,
    CfvValueV2* result, char* error, size_t error_size) {
    if (abi_version != CFV_ABI_V2 || count != 1 || arguments[0].type != CFV_LIST) {
        std::snprintf(error, error_size, "native_list_sum_v2 requiere lista ABI V2");
        return 1;
    }
    const auto* values = static_cast<const CfvValueV2*>(arguments[0].data);
    double total = 0.0;
    for (uint64_t index = 0; index < arguments[0].length; ++index) {
        if (values[index].type == CFV_INTEGER) total += values[index].integer;
        else if (values[index].type == CFV_DECIMAL) total += values[index].decimal;
        else {
            std::snprintf(error, error_size, "la lista ABI V2 debe ser numérica");
            return 2;
        }
    }
    *result = {sizeof(CfvValueV2), CFV_DECIMAL, 0, 0, 0, total,
               nullptr, nullptr, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_map_count_v2(
    uint32_t abi_version, const CfvValueV2* arguments, size_t count,
    CfvValueV2* result, char* error, size_t error_size) {
    if (abi_version != CFV_ABI_V2 || count != 1 || arguments[0].type != CFV_MAP) {
        std::snprintf(error, error_size, "native_map_count_v2 requiere mapa ABI V2");
        return 1;
    }
    *result = {sizeof(CfvValueV2), CFV_INTEGER, 0, 0,
               static_cast<int64_t>(arguments[0].length), 0.0,
               nullptr, nullptr, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_record_count_v2(
    uint32_t abi_version, const CfvValueV2* arguments, size_t count,
    CfvValueV2* result, char* error, size_t error_size) {
    if (abi_version != CFV_ABI_V2 || count != 1 || arguments[0].type != CFV_RECORD) {
        std::snprintf(error, error_size, "native_record_count_v2 requiere registro ABI V2");
        return 1;
    }
    const auto* record = static_cast<const CfvRecordV2*>(arguments[0].data);
    *result = {sizeof(CfvValueV2), CFV_INTEGER, 0, 0,
               static_cast<int64_t>(record->field_count), 0.0,
               nullptr, nullptr, nullptr};
    return 0;
}

extern "C" CFV_EXPORT int native_owned_range_v2(
    uint32_t abi_version, const CfvValueV2* arguments, size_t count,
    CfvValueV2* result, char* error, size_t error_size) {
    if (abi_version != CFV_ABI_V2 || count != 1 || arguments[0].type != CFV_INTEGER
            || arguments[0].integer < 0 || arguments[0].integer > 1000000) {
        std::snprintf(error, error_size, "native_owned_range_v2 requiere tamaño válido");
        return 1;
    }
    const size_t length = static_cast<size_t>(arguments[0].integer);
    auto* values = static_cast<CfvValueV2*>(
        std::calloc(length ? length : 1, sizeof(CfvValueV2))
    );
    if (!values) {
        std::snprintf(error, error_size, "sin memoria para lista ABI V2");
        return 2;
    }
    for (size_t index = 0; index < length; ++index) {
        values[index] = {sizeof(CfvValueV2), CFV_INTEGER, CFV_V2_BORROWED, 0,
                         static_cast<int64_t>(index), 0.0, nullptr, nullptr, nullptr};
    }
    *result = {sizeof(CfvValueV2), CFV_LIST, CFV_V2_OWNED, length, 0, 0.0,
               values, values, release_owned_text};
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
        cfv_register_function("toggle", native_toggle);
        cfv_register_function_v2("echo_v2", native_echo_v2);
        cfv_register_function_v2("owned_echo_v2", native_owned_echo_v2);
        cfv_register_function_v2("list_sum_v2", native_list_sum_v2);
        cfv_register_function_v2("map_count_v2", native_map_count_v2);
        cfv_register_function_v2("record_count_v2", native_record_count_v2);
        cfv_register_function_v2("owned_range_v2", native_owned_range_v2);
    }
} registration;
}
#endif
