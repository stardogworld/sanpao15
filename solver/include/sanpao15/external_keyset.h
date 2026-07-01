#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace sanpao15 {

struct ExternalKeySetOptions {
    std::filesystem::path tempDir;
    uint64_t chunkKeyLimit = 1000000;
    bool keepTempFiles = false;
    uint64_t progressInterval = 0;
};

struct ExternalKeySetStats {
    uint64_t addedKeys = 0;
    uint64_t chunksWritten = 0;
    uint64_t keysAfterChunkDedup = 0;
    uint64_t uniqueKeys = 0;
    uint64_t duplicateKeys = 0;
    uint64_t tempBytesWritten = 0;
    uint64_t finalBytesWritten = 0;
    double chunkSortSeconds = 0.0;
    double mergeSeconds = 0.0;
    double totalSeconds = 0.0;
};

struct RunFileData {
    std::vector<uint64_t> keys;
};

class ExternalKeySetBuilder {
public:
    explicit ExternalKeySetBuilder(ExternalKeySetOptions options);
    ~ExternalKeySetBuilder();

    ExternalKeySetBuilder(const ExternalKeySetBuilder&) = delete;
    ExternalKeySetBuilder& operator=(const ExternalKeySetBuilder&) = delete;

    void add(uint64_t key);
    void addBatch(const std::vector<uint64_t>& keys);
    void flush();

    std::vector<uint64_t> finishToVector();
    void finishToStream(const std::function<void(uint64_t)>& emitKey);

    const ExternalKeySetStats& stats() const;

private:
    struct Impl;
    Impl* impl_;
};

void writeRunFile(const std::filesystem::path& path, const std::vector<uint64_t>& sortedUniqueKeys);
RunFileData readRunFile(const std::filesystem::path& path);

}  // namespace sanpao15
