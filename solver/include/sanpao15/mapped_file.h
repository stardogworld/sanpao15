#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace sanpao15 {

class ReadOnlyMappedFile {
public:
    ReadOnlyMappedFile();
    explicit ReadOnlyMappedFile(const std::filesystem::path& path);
    ~ReadOnlyMappedFile();

    ReadOnlyMappedFile(const ReadOnlyMappedFile&) = delete;
    ReadOnlyMappedFile& operator=(const ReadOnlyMappedFile&) = delete;

    ReadOnlyMappedFile(ReadOnlyMappedFile&& other) noexcept;
    ReadOnlyMappedFile& operator=(ReadOnlyMappedFile&& other) noexcept;

    const uint8_t* data() const noexcept;
    uint64_t size() const noexcept;
    bool empty() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sanpao15
