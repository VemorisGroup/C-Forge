#include "cforge_shared_arena.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path = argv[1];
    auto writer = cforge::arena::ForgeSharedArena::create(path, 1024 * 1024);
    const std::string json = R"({"motor":"C-Forge","valor":42})";
    const auto offset = writer.store_text(cforge::arena::ValueType::Json, json);

    auto reader = cforge::arena::ForgeSharedArena::open(path);
    const auto view = reader.view(offset);
    const std::string received(
        reinterpret_cast<const char*>(view.data), static_cast<std::size_t>(view.size));
    assert(received == json);
    assert(view.type == cforge::arena::ValueType::Json);
    assert(reader.live_records() == 1);

    reader.retain(offset);
    writer.release(offset);
    assert(reader.view(offset).size == json.size());
    reader.release(offset);
    assert(reader.live_records() == 0);

    std::cout << "ForgeSharedArena OK offset=" << offset << " bytes=" << view.size << '\n';
}
