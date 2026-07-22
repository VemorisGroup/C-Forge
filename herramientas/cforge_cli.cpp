#include <cerrno>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::filesystem::path find_engine(const char* executable) {
    const auto binary = std::filesystem::absolute(executable).parent_path();
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "cforgev.py",
        binary / "cforgev.py",
        binary.parent_path() / "cforgev.py",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::canonical(candidate);
        }
    }
    throw std::runtime_error("no se encontró cforgev.py junto al proyecto");
}

void print_help() {
    std::cout
        << "C-Forge Toolchain 1.5.0 Developer Preview\n"
        << "Uso:\n"
        << "  cforge fmt archivo.cfv\n"
        << "  cforge test archivo.cfv\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        print_help();
        return 0;
    }
    if (argc != 3 || (std::string(argv[1]) != "fmt" && std::string(argv[1]) != "test")) {
        print_help();
        return 2;
    }
    try {
        const auto engine = find_engine(argv[0]).string();
        std::vector<std::string> owned = {"python3", engine, argv[1], argv[2]};
        std::vector<char*> arguments;
        for (auto& value : owned) {
            arguments.push_back(value.data());
        }
        arguments.push_back(nullptr);
#ifdef _WIN32
        const int status = _spawnvp(_P_WAIT, "python3", arguments.data());
        if (status < 0) {
            throw std::runtime_error("no se pudo iniciar python3");
        }
        return status;
#else
        execvp("python3", arguments.data());
        throw std::runtime_error("no se pudo iniciar python3: error " + std::to_string(errno));
#endif
    } catch (const std::exception& error) {
        std::cerr << "[C-Forge Toolchain Exception] " << error.what() << '\n';
        return 1;
    }
}
