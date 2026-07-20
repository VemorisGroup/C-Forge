#!/usr/bin/env python3
"""Genera la distribución monolítica C++ de C-Forgev desde las fuentes oficiales."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "outputs" / "cforge_master.cpp"

RESOURCES = {
    "cforgev.py": ROOT / "cforgev.py",
    "compilador_nativo.py": ROOT / "compilador_nativo.py",
    "compilador_wasm.py": ROOT / "compilador_wasm.py",
    "include/cforgev_ffi.h": ROOT / "include" / "cforgev_ffi.h",
    "herramientas/cforgev_ffi_runner.cpp": ROOT / "herramientas" / "cforgev_ffi_runner.cpp",
    "herramientas/cforge_cli.cpp": ROOT / "herramientas" / "cforge_cli.cpp",
    "herramientas/vscode-cforgev/package.json": ROOT / "herramientas" / "vscode-cforgev" / "package.json",
    "herramientas/vscode-cforgev/language-configuration.json": ROOT / "herramientas" / "vscode-cforgev" / "language-configuration.json",
    "herramientas/vscode-cforgev/syntaxes/cforgev.tmLanguage.json": ROOT / "herramientas" / "vscode-cforgev" / "syntaxes" / "cforgev.tmLanguage.json",
}


def raw_literal(text: str, index: int) -> str:
    delimiter = f"CFV{index}DATA"
    if f"){delimiter}\"" in text:
        raise RuntimeError(f"delimitador C++ inesperadamente presente: {delimiter}")
    return f'R"{delimiter}({text}){delimiter}"'


def generate() -> str:
    entries: list[str] = []
    for index, (target, source) in enumerate(RESOURCES.items()):
        content = source.read_text(encoding="utf-8")
        entries.append(
            "        {" + raw_literal(target, index * 2) + ", "
            + raw_literal(content, index * 2 + 1) + "}"
        )
    resources = ",\n".join(entries)
    return f'''// C-Forgev 1.3.0 Definitive — distribución monolítica generada.
// Fuente reproducible: herramientas/generar_amalgama.py

#include <Python.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cforgev {{

class PyOwned final {{
public:
    explicit PyOwned(PyObject* value = nullptr) noexcept : value_(value) {{}}
    ~PyOwned() {{ Py_XDECREF(value_); }}
    PyOwned(const PyOwned&) = delete;
    PyOwned& operator=(const PyOwned&) = delete;
    PyOwned(PyOwned&& other) noexcept : value_(other.value_) {{ other.value_ = nullptr; }}
    PyObject* get() const noexcept {{ return value_; }}
    explicit operator bool() const noexcept {{ return value_ != nullptr; }}
private:
    PyObject* value_;
}};

class PythonRuntime final {{
public:
    PythonRuntime() {{
        Py_DontWriteBytecodeFlag = 1;
        Py_Initialize();
        if (!Py_IsInitialized()) throw std::runtime_error("no se pudo inicializar CPython");
    }}
    ~PythonRuntime() {{ if (Py_IsInitialized()) Py_Finalize(); }}
    PythonRuntime(const PythonRuntime&) = delete;
    PythonRuntime& operator=(const PythonRuntime&) = delete;
}};

class TemporaryWorkspace final {{
public:
    TemporaryWorkspace() {{
        auto base = std::filesystem::temp_directory_path();
        auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {{
            path_ = base / ("cforgev-master-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
        }}
        throw std::runtime_error("no se pudo crear el espacio temporal RAII");
    }}
    ~TemporaryWorkspace() {{
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }}
    const std::filesystem::path& path() const noexcept {{ return path_; }}
private:
    std::filesystem::path path_;
}};

const std::map<std::string, std::string>& embedded_resources() {{
    static const std::map<std::string, std::string> resources = {{
{resources}
    }};
    return resources;
}}

void materialize(const std::filesystem::path& root) {{
    for (const auto& [relative, content] : embedded_resources()) {{
        const auto destination = root / relative;
        if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
        std::ofstream stream(destination, std::ios::binary);
        if (!stream) throw std::runtime_error("no se pudo desplegar " + relative);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream) throw std::runtime_error("escritura incompleta de " + relative);
    }}
}}

std::vector<wchar_t*> decode_arguments(int argc, char** argv) {{
    std::vector<wchar_t*> decoded;
    decoded.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {{
        wchar_t* value = Py_DecodeLocale(argv[index], nullptr);
        if (!value) {{
            for (auto* previous : decoded) PyMem_RawFree(previous);
            throw std::runtime_error("argumento CLI no convertible a Unicode");
        }}
        decoded.push_back(value);
    }}
    return decoded;
}}

int run_toolchain(int argc, char** argv, const std::filesystem::path& root) {{
    auto decoded = decode_arguments(argc, argv);
    PySys_SetArgvEx(argc, decoded.data(), 0);
    for (auto* value : decoded) PyMem_RawFree(value);

    PyObject* path = PySys_GetObject("path");  // referencia prestada
    PyOwned root_text(PyUnicode_FromString(root.string().c_str()));
    if (!path || !root_text || PyList_Insert(path, 0, root_text.get()) != 0) {{
        throw std::runtime_error("no se pudo configurar sys.path");
    }}

    PyOwned module(PyImport_ImportModule("cforgev"));
    if (!module) {{ PyErr_Print(); throw std::runtime_error("no se pudo importar el frontend embebido"); }}
    PyOwned main_function(PyObject_GetAttrString(module.get(), "main"));
    if (!main_function || !PyCallable_Check(main_function.get()))
        throw std::runtime_error("el frontend embebido no exporta main()");
    PyOwned result(PyObject_CallObject(main_function.get(), nullptr));
    if (!result) {{ PyErr_Print(); return 1; }}
    long status = PyLong_AsLong(result.get());
    if (PyErr_Occurred()) {{ PyErr_Print(); return 1; }}
    return static_cast<int>(status);
}}

bool command_available(const std::string& command) {{
    const std::string probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}}

int setup_environment() {{
    std::cout << "C-Forgev Setup 1.3.0\\n";
    const bool clang = command_available("clang++");
    const bool python = command_available("python3");
#ifdef __APPLE__
    const bool java = std::system("/usr/libexec/java_home >/dev/null 2>&1") == 0;
#else
    const bool java = command_available("java") && command_available("javac");
#endif
    const bool node = command_available("node");
    std::cout << (clang ? "[OK] C++: clang++ disponible\\n" : "[FALTA] C++: instala las herramientas de desarrollo\\n");
    std::cout << (python ? "[OK] Python 3 disponible\\n" : "[FALTA] Python 3\\n");
    std::cout << (node ? "[OK] JavaScript/TypeScript: Node.js disponible\\n" : "[OPCIONAL] Node.js no instalado\\n");
    if (java) {{
        std::cout << "[OK] Java: JDK y JVM disponibles\\n";
    }} else {{
        std::cout << "[FALTA] Java: instala un JDK con:\\n"
                  << "  brew install --cask temurin\\n"
                  << "Si no tienes Homebrew: https://adoptium.net/temurin/releases/\\n";
    }}
    if (!clang) std::cout << "En macOS ejecuta: xcode-select --install\\n";
    std::cout << "Setup finalizado; no se realizaron instalaciones sin autorización.\\n";
    return clang && python ? 0 : 1;
}}

int install_globally(const char* executable) {{
#ifdef _WIN32
    (void)executable;
    std::cerr << "--install actualmente está diseñado para macOS/Linux.\\n";
    return 1;
#else
    std::error_code error;
    const auto source = std::filesystem::canonical(std::filesystem::absolute(executable), error);
    if (error || !std::filesystem::is_regular_file(source))
        throw std::runtime_error("no se pudo localizar el ejecutable actual");
    const std::filesystem::path directory = "/usr/local/bin";
    const std::filesystem::path destination = directory / "cforge";
    std::filesystem::create_directories(directory, error);
    if (error) {{
        std::cerr << "C-Forgev necesita permisos para crear " << directory << ".\\n"
                  << "Ejecuta: sudo \\\"" << source.string() << "\\\" --install\\n";
        return 1;
    }}
    std::filesystem::copy_file(source, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) {{
        std::cerr << "No se pudo instalar en " << destination << ": " << error.message() << "\\n"
                  << "Ejecuta: sudo \\\"" << source.string() << "\\\" --install\\n";
        return 1;
    }}
    if (::chmod(destination.c_str(), 0755) != 0)
        throw std::runtime_error("instalado, pero no se pudo marcar como ejecutable");
    std::cout << "C-Forgev instalado globalmente en " << destination << "\\n"
              << "Ya puedes ejecutar: cforge --version\\n";
    return 0;
#endif
}}

}}  // namespace cforgev

int main(int argc, char** argv) {{
    try {{
        if (argc == 2 && std::string(argv[1]) == "--setup")
            return cforgev::setup_environment();
        if (argc == 2 && std::string(argv[1]) == "--install")
            return cforgev::install_globally(argv[0]);
        cforgev::TemporaryWorkspace workspace;
        cforgev::materialize(workspace.path());
        cforgev::PythonRuntime python;
        return cforgev::run_toolchain(argc, argv, workspace.path());
    }} catch (const std::exception& error) {{
        std::cerr << "[C-Forgev Bootstrap Exception] " << error.what() << '\\n';
        return 1;
    }} catch (...) {{
        std::cerr << "[C-Forgev Bootstrap Exception] error desconocido\\n";
        return 1;
    }}
}}
'''


def main() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(generate(), encoding="utf-8")
    print(f"Amalgama C-Forgev creada: {OUTPUT}")


if __name__ == "__main__":
    main()
