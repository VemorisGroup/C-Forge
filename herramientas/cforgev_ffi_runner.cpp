#include "cforgev_ffi.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    if (argc < 3) return 2;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]);
    auto function = library ? reinterpret_cast<CfvForeignFunction>(GetProcAddress(library, argv[2])) : nullptr;
#else
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    auto function = library ? reinterpret_cast<CfvForeignFunction>(dlsym(library, argv[2])) : nullptr;
#endif
    if (!function) { std::cerr << "no se pudo cargar la función extranjera"; return 3; }
    std::vector<std::string> texts;
    std::vector<CfvValue> values;
    texts.reserve(argc - 3);
    values.reserve(argc - 3);
    for (int i = 3; i < argc; ++i) {
        std::string value = argv[i];
        if (value == "n:") values.push_back({CFV_NULL, 0, 0, nullptr});
        else if (value.rfind("i:", 0) == 0) values.push_back({CFV_INTEGER, std::stoll(value.substr(2)), 0, nullptr});
        else if (value.rfind("d:", 0) == 0) values.push_back({CFV_DECIMAL, 0, std::stod(value.substr(2)), nullptr});
        else if (value.rfind("s:", 0) == 0) { texts.push_back(value.substr(2)); values.push_back({CFV_TEXT, 0, 0, texts.back().c_str()}); }
        else { std::cerr << "argumento ABI inválido"; return 4; }
    }
    CfvValue result{CFV_NULL, 0, 0, nullptr, nullptr, nullptr};
    char error[1024] = {};
    int status = function(values.data(), values.size(), &result, error, sizeof(error));
    if (status) { std::cerr << (error[0] ? error : "función extranjera falló"); return status; }
    std::cout << result.type << '\n';
    if (result.type == CFV_INTEGER) std::cout << result.integer;
    else if (result.type == CFV_DECIMAL) std::cout << result.decimal;
    else if (result.type == CFV_TEXT && result.text) std::cout << result.text;
    std::cout.flush();
    if (result.release) result.release(result.owner);
    return 0;
}
