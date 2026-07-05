#include "sanpao15/mapped_file.h"

#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sanpao15 {

namespace {

#if defined(_WIN32)
std::string windowsErrorMessage(DWORD error) {
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string message = size == 0 ? "unknown Windows error" : std::string(buffer, size);
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }
    return message;
}
#endif

}  // namespace

struct ReadOnlyMappedFile::Impl {
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
#else
    int fd = -1;
#endif
    const uint8_t* data = nullptr;
    uint64_t size = 0;

    Impl() = default;
    explicit Impl(const std::filesystem::path& path) {
#if defined(_WIN32)
        file = CreateFileW(
            path.wstring().c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("failed to open file for mmap: " + path.string() + ": " + windowsErrorMessage(GetLastError()));
        }

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0) {
            const DWORD error = GetLastError();
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            throw std::runtime_error("failed to stat file for mmap: " + path.string() + ": " + windowsErrorMessage(error));
        }
        size = static_cast<uint64_t>(fileSize.QuadPart);
        if (size == 0) {
            return;
        }

        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping == nullptr) {
            const DWORD error = GetLastError();
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            throw std::runtime_error("failed to create read-only file mapping: " + path.string() + ": " + windowsErrorMessage(error));
        }

        data = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (data == nullptr) {
            const DWORD error = GetLastError();
            CloseHandle(mapping);
            CloseHandle(file);
            mapping = nullptr;
            file = INVALID_HANDLE_VALUE;
            throw std::runtime_error("failed to map file read-only: " + path.string() + ": " + windowsErrorMessage(error));
        }
#else
        fd = open(path.string().c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("failed to open file for mmap: " + path.string() + ": " + std::strerror(errno));
        }
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size < 0) {
            const int error = errno;
            close(fd);
            fd = -1;
            throw std::runtime_error("failed to stat file for mmap: " + path.string() + ": " + std::strerror(error));
        }
        size = static_cast<uint64_t>(st.st_size);
        if (size == 0) {
            return;
        }

        void* mapped = mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            const int error = errno;
            close(fd);
            fd = -1;
            throw std::runtime_error("failed to map file read-only: " + path.string() + ": " + std::strerror(error));
        }
        data = static_cast<const uint8_t*>(mapped);
#endif
    }

    ~Impl() {
#if defined(_WIN32)
        if (data != nullptr) {
            UnmapViewOfFile(data);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
#else
        if (data != nullptr) {
            munmap(const_cast<uint8_t*>(data), static_cast<size_t>(size));
        }
        if (fd >= 0) {
            close(fd);
        }
#endif
    }
};

ReadOnlyMappedFile::ReadOnlyMappedFile()
    : impl_(std::make_unique<Impl>()) {}

ReadOnlyMappedFile::ReadOnlyMappedFile(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {}

ReadOnlyMappedFile::~ReadOnlyMappedFile() = default;

ReadOnlyMappedFile::ReadOnlyMappedFile(ReadOnlyMappedFile&& other) noexcept = default;

ReadOnlyMappedFile& ReadOnlyMappedFile::operator=(ReadOnlyMappedFile&& other) noexcept = default;

const uint8_t* ReadOnlyMappedFile::data() const noexcept {
    return impl_ == nullptr ? nullptr : impl_->data;
}

uint64_t ReadOnlyMappedFile::size() const noexcept {
    return impl_ == nullptr ? 0 : impl_->size;
}

bool ReadOnlyMappedFile::empty() const noexcept {
    return size() == 0;
}

}  // namespace sanpao15
