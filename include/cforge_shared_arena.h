#ifndef CFORGE_SHARED_ARENA_H
#define CFORGE_SHARED_ARENA_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cforge::arena {

using Offset = std::uint64_t;

inline constexpr std::uint64_t kMagic = 0x43464F5247454131ULL;  // "CFORGEA1"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint64_t kAlignment = 64;
inline constexpr std::uint32_t kRecordLive = 1;
inline constexpr std::uint32_t kRecordReleased = 2;

enum class ValueType : std::uint32_t {
    Null = 0,
    Boolean = 1,
    Integer = 2,
    Decimal = 3,
    Utf8 = 4,
    Bytes = 5,
    Json = 6,
    Float64Array = 7,
};

struct alignas(64) RecordHeader final {
    std::uint64_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t type = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> references{1};
    std::atomic<std::uint32_t> state{kRecordLive};
    std::uint32_t checksum = 0;
    std::uint8_t reserved[8]{};
};

struct alignas(64) ArenaHeader final {
    std::uint64_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t header_size = 0;
    std::uint64_t capacity = 0;
    std::atomic<std::uint64_t> used{0};
    std::atomic<std::uint64_t> generation{1};
    std::atomic<std::uint64_t> live_records{0};
#ifndef _WIN32
    pthread_mutex_t mutex{};
#else
    std::uint8_t mutex_placeholder[64]{};
#endif
};

struct ByteView final {
    const std::byte* data = nullptr;
    std::uint64_t size = 0;
    ValueType type = ValueType::Null;
    Offset record = 0;
};

inline std::uint64_t align_up(std::uint64_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max() - (kAlignment - 1))
        throw std::overflow_error("ForgeSharedArena: overflow de alineación");
    return (value + kAlignment - 1) & ~(kAlignment - 1);
}

inline std::uint32_t checksum32(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

class ForgeSharedArena final {
public:
    static ForgeSharedArena create(const std::filesystem::path& path, std::uint64_t capacity) {
        if (capacity < align_up(sizeof(ArenaHeader)) + align_up(sizeof(RecordHeader)) + 1)
            throw std::invalid_argument("ForgeSharedArena: capacidad demasiado pequeña");
        ForgeSharedArena arena;
        arena.open_mapping(path, capacity, true);
        std::memset(arena.base_, 0, static_cast<std::size_t>(capacity));
        auto* header = new (arena.base_) ArenaHeader{};
        header->header_size = static_cast<std::uint32_t>(sizeof(ArenaHeader));
        header->capacity = capacity;
        header->used.store(align_up(sizeof(ArenaHeader)), std::memory_order_release);
#ifndef _WIN32
        pthread_mutexattr_t attributes;
        if (pthread_mutexattr_init(&attributes) != 0)
            throw std::runtime_error("ForgeSharedArena: mutexattr_init falló");
        pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
#ifdef PTHREAD_MUTEX_ROBUST
        pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST);
#endif
        const int status = pthread_mutex_init(&header->mutex, &attributes);
        pthread_mutexattr_destroy(&attributes);
        if (status != 0) throw std::runtime_error("ForgeSharedArena: mutex compartido falló");
#endif
        arena.header_ = header;
        return arena;
    }

    static ForgeSharedArena open(const std::filesystem::path& path) {
        ForgeSharedArena arena;
        arena.open_mapping(path, 0, false);
        arena.header_ = reinterpret_cast<ArenaHeader*>(arena.base_);
        arena.validate_arena();
        return arena;
    }

    ForgeSharedArena() = default;
    ~ForgeSharedArena() { close(); }
    ForgeSharedArena(const ForgeSharedArena&) = delete;
    ForgeSharedArena& operator=(const ForgeSharedArena&) = delete;
    ForgeSharedArena(ForgeSharedArena&& other) noexcept { move_from(other); }
    ForgeSharedArena& operator=(ForgeSharedArena&& other) noexcept {
        if (this != &other) { close(); move_from(other); }
        return *this;
    }

    Offset store(ValueType type, const void* payload, std::uint64_t size) {
        if (size && !payload) throw std::invalid_argument("ForgeSharedArena: payload nulo");
        Lock guard(*this);
        const auto record_offset = align_up(header_->used.load(std::memory_order_acquire));
        const auto payload_offset = align_up(record_offset + sizeof(RecordHeader));
        if (size > header_->capacity || payload_offset > header_->capacity - size)
            throw std::runtime_error("ForgeSharedArena: espacio agotado");
        auto* record = new (address(record_offset, sizeof(RecordHeader))) RecordHeader{};
        record->type = static_cast<std::uint32_t>(type);
        record->payload_size = size;
        record->payload_offset = payload_offset;
        record->generation = header_->generation.fetch_add(1, std::memory_order_acq_rel);
        void* destination = address(payload_offset, size);
        if (size) std::memcpy(destination, payload, static_cast<std::size_t>(size));
        record->checksum = checksum32(destination, static_cast<std::size_t>(size));
        header_->used.store(align_up(payload_offset + size), std::memory_order_release);
        header_->live_records.fetch_add(1, std::memory_order_acq_rel);
        return record_offset;
    }

    Offset store_text(ValueType type, std::string_view value) {
        if (type != ValueType::Utf8 && type != ValueType::Json)
            throw std::invalid_argument("ForgeSharedArena: store_text requiere Utf8 o Json");
        return store(type, value.data(), value.size());
    }

    ByteView view(Offset offset) const {
        validate_arena();
        const auto* record = record_at(offset);
        if (record->state.load(std::memory_order_acquire) != kRecordLive)
            throw std::runtime_error("ForgeSharedArena: registro liberado");
        const auto* payload = static_cast<const std::byte*>(address(record->payload_offset, record->payload_size));
        if (checksum32(payload, static_cast<std::size_t>(record->payload_size)) != record->checksum)
            throw std::runtime_error("ForgeSharedArena: checksum inválido");
        return {payload, record->payload_size, static_cast<ValueType>(record->type), offset};
    }

    void retain(Offset offset) {
        auto* record = record_at(offset);
        if (record->state.load(std::memory_order_acquire) != kRecordLive)
            throw std::runtime_error("ForgeSharedArena: retain sobre registro liberado");
        record->references.fetch_add(1, std::memory_order_acq_rel);
    }

    void release(Offset offset) {
        auto* record = record_at(offset);
        const auto previous = record->references.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0) {
            record->references.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("ForgeSharedArena: doble liberación");
        }
        if (previous == 1) {
            record->state.store(kRecordReleased, std::memory_order_release);
            header_->live_records.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    std::uint64_t capacity() const noexcept { return header_ ? header_->capacity : 0; }
    std::uint64_t used() const noexcept {
        return header_ ? header_->used.load(std::memory_order_acquire) : 0;
    }
    std::uint64_t live_records() const noexcept {
        return header_ ? header_->live_records.load(std::memory_order_acquire) : 0;
    }

private:
    class Lock final {
    public:
        explicit Lock(ForgeSharedArena& arena) : arena_(arena) { arena_.lock(); }
        ~Lock() { arena_.unlock(); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    private:
        ForgeSharedArena& arena_;
    };

    void validate_arena() const {
        if (!header_ || header_->magic != kMagic || header_->version != kVersion ||
            header_->header_size != sizeof(ArenaHeader))
            throw std::runtime_error("ForgeSharedArena: cabecera incompatible");
        const auto used = header_->used.load(std::memory_order_acquire);
        if (used < align_up(sizeof(ArenaHeader)) || used > header_->capacity)
            throw std::runtime_error("ForgeSharedArena: límites corruptos");
    }

    RecordHeader* record_at(Offset offset) const {
        auto* record = static_cast<RecordHeader*>(address(offset, sizeof(RecordHeader)));
        if (record->magic != kMagic || record->version != kVersion)
            throw std::runtime_error("ForgeSharedArena: registro incompatible");
        if (record->payload_offset < offset + sizeof(RecordHeader))
            throw std::runtime_error("ForgeSharedArena: offset de payload corrupto");
        address(record->payload_offset, record->payload_size);
        return record;
    }

    void* address(Offset offset, std::uint64_t size) const {
        if (!base_ || offset > mapped_size_ || size > mapped_size_ - offset)
            throw std::out_of_range("ForgeSharedArena: acceso fuera de límites");
        return static_cast<std::byte*>(base_) + offset;
    }

    void lock() {
#ifdef _WIN32
        const DWORD result = WaitForSingleObject(mutex_, INFINITE);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            throw std::runtime_error("ForgeSharedArena: WaitForSingleObject falló");
#else
        const int result = pthread_mutex_lock(&header_->mutex);
#if defined(EOWNERDEAD) && !defined(__APPLE__)
        if (result == EOWNERDEAD) { pthread_mutex_consistent(&header_->mutex); return; }
#endif
        if (result != 0) throw std::runtime_error("ForgeSharedArena: lock falló");
#endif
    }

    void unlock() noexcept {
#ifdef _WIN32
        if (mutex_) ReleaseMutex(mutex_);
#else
        if (header_) pthread_mutex_unlock(&header_->mutex);
#endif
    }

    void open_mapping(const std::filesystem::path& path, std::uint64_t size, bool create) {
#ifdef _WIN32
        const auto wide = path.wstring();
        file_ = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            create ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) throw std::runtime_error("ForgeSharedArena: CreateFile falló");
        if (!create) {
            LARGE_INTEGER value{};
            if (!GetFileSizeEx(file_, &value) || value.QuadPart <= 0) throw std::runtime_error("ForgeSharedArena: tamaño inválido");
            size = static_cast<std::uint64_t>(value.QuadPart);
        } else {
            LARGE_INTEGER value{}; value.QuadPart = static_cast<LONGLONG>(size);
            if (!SetFilePointerEx(file_, value, nullptr, FILE_BEGIN) || !SetEndOfFile(file_))
                throw std::runtime_error("ForgeSharedArena: no se pudo dimensionar");
        }
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE,
            static_cast<DWORD>(size >> 32), static_cast<DWORD>(size), nullptr);
        if (!mapping_) throw std::runtime_error("ForgeSharedArena: CreateFileMapping falló");
        base_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(size));
        if (!base_) throw std::runtime_error("ForgeSharedArena: MapViewOfFile falló");
        const auto mutex_name = L"Local\\CForgeArena-" + std::to_wstring(checksum32(wide.data(), wide.size() * sizeof(wchar_t)));
        mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
        if (!mutex_) throw std::runtime_error("ForgeSharedArena: CreateMutex falló");
#else
        descriptor_ = ::open(path.c_str(), create ? (O_RDWR | O_CREAT | O_TRUNC) : O_RDWR, 0600);
        if (descriptor_ < 0) throw std::runtime_error("ForgeSharedArena: open falló");
        if (create) {
            if (ftruncate(descriptor_, static_cast<off_t>(size)) != 0)
                throw std::runtime_error("ForgeSharedArena: ftruncate falló");
        } else {
            struct stat info{};
            if (fstat(descriptor_, &info) != 0 || info.st_size <= 0)
                throw std::runtime_error("ForgeSharedArena: fstat falló");
            size = static_cast<std::uint64_t>(info.st_size);
        }
        base_ = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_, 0);
        if (base_ == MAP_FAILED) { base_ = nullptr; throw std::runtime_error("ForgeSharedArena: mmap falló"); }
#endif
        mapped_size_ = size;
    }

    void close() noexcept {
#ifdef _WIN32
        if (base_) UnmapViewOfFile(base_);
        if (mapping_) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        if (mutex_) CloseHandle(mutex_);
        mapping_ = nullptr; file_ = INVALID_HANDLE_VALUE; mutex_ = nullptr;
#else
        if (base_) munmap(base_, static_cast<std::size_t>(mapped_size_));
        if (descriptor_ >= 0) ::close(descriptor_);
        descriptor_ = -1;
#endif
        base_ = nullptr; header_ = nullptr; mapped_size_ = 0;
    }

    void move_from(ForgeSharedArena& other) noexcept {
        base_ = other.base_; header_ = other.header_; mapped_size_ = other.mapped_size_;
#ifdef _WIN32
        file_ = other.file_; mapping_ = other.mapping_; mutex_ = other.mutex_;
        other.file_ = INVALID_HANDLE_VALUE; other.mapping_ = nullptr; other.mutex_ = nullptr;
#else
        descriptor_ = other.descriptor_; other.descriptor_ = -1;
#endif
        other.base_ = nullptr; other.header_ = nullptr; other.mapped_size_ = 0;
    }

    void* base_ = nullptr;
    ArenaHeader* header_ = nullptr;
    std::uint64_t mapped_size_ = 0;
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
#else
    int descriptor_ = -1;
#endif
};

}  // namespace cforge::arena

#endif
