#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sanpao15 {

enum class PartitionSourceKind {
    Layer,
    Seed,
    Keys,
};

enum class PartitionMethod {
    KeyMod,
    Splitmix64Mod,
};

enum class PartitionBenchmarkMode {
    Existing,
    Missing,
    Mixed,
};

struct PartitionedKeySetOptions {
    std::filesystem::path inputFile;
    std::filesystem::path outputDir;
    uint32_t bucketCount = 256;
    PartitionMethod method = PartitionMethod::Splitmix64Mod;
    uint64_t chunkKeyLimit = 1000000;
    uint64_t progressInterval = 0;
    bool overwrite = false;
    std::function<void(uint64_t)> progress;
};

struct PartitionBucketInfo {
    uint32_t bucketId = 0;
    std::string file;
    uint64_t keyCount = 0;
    uint64_t minKey = 0;
    uint64_t maxKey = 0;
};

struct PartitionedKeySetStats {
    uint64_t inputKeys = 0;
    uint64_t outputKeys = 0;
    uint32_t bucketCount = 0;
    uint64_t minBucketSize = 0;
    uint64_t maxBucketSize = 0;
    double averageBucketSize = 0.0;
    uint64_t emptyBuckets = 0;
    double buildSeconds = 0.0;
    std::vector<uint64_t> bucketSizes;
    int soldierCount = 0;
    PartitionMethod partitionMethod = PartitionMethod::Splitmix64Mod;
    PartitionSourceKind sourceKind = PartitionSourceKind::Keys;
};

struct PartitionValidationResult {
    bool valid = false;
    uint64_t totalKeys = 0;
    uint32_t bucketCount = 0;
    int soldierCount = 0;
};

struct PartitionInspection {
    uint64_t totalKeys = 0;
    uint32_t bucketCount = 0;
    int soldierCount = 0;
    std::string partitionMethod;
    uint64_t minBucketSize = 0;
    uint64_t maxBucketSize = 0;
    double averageBucketSize = 0.0;
    uint64_t emptyBuckets = 0;
    uint64_t totalBucketFileBytes = 0;
    std::vector<PartitionBucketInfo> buckets;
};

struct PartitionLookupBenchmark {
    uint64_t requestedSamples = 0;
    uint64_t executedLookups = 0;
    uint64_t found = 0;
    uint64_t missing = 0;
    uint64_t bucketsTouched = 0;
    uint64_t bucketLoads = 0;
    double seconds = 0.0;
    double lookupsPerSecond = 0.0;
    PartitionBenchmarkMode mode = PartitionBenchmarkMode::Existing;
    uint32_t cacheBuckets = 32;
};

struct PartitionedKeySetOpStats {
    uint64_t leftKeys = 0;
    uint64_t rightKeys = 0;
    uint64_t outputKeys = 0;
    uint32_t bucketCount = 0;
    int soldierCount = 0;
    PartitionMethod partitionMethod = PartitionMethod::Splitmix64Mod;
    double seconds = 0.0;
};

struct BatchLookupStats {
    uint64_t queryKeys = 0;
    uint64_t foundKeys = 0;
    uint64_t missingKeys = 0;
    uint64_t bucketsTouched = 0;
    uint64_t bucketLoads = 0;
    double seconds = 0.0;
    double lookupsPerSecond = 0.0;
};

struct PartitionedKeySetReaderOptions {
    std::filesystem::path partitionDir;
    uint32_t maxCachedBuckets = 32;
};

const char* partitionSourceKindToString(PartitionSourceKind kind);
PartitionSourceKind partitionSourceKindFromString(const std::string& text);
const char* partitionMethodToString(PartitionMethod method);
PartitionMethod partitionMethodFromString(const std::string& text);
const char* partitionBenchmarkModeToString(PartitionBenchmarkMode mode);
PartitionBenchmarkMode partitionBenchmarkModeFromString(const std::string& text);

uint32_t partitionBucketForKey(uint64_t key, uint32_t bucketCount);
uint32_t partitionBucketForKey(uint64_t key, uint32_t bucketCount, PartitionMethod method);
std::filesystem::path partitionManifestPath(const std::filesystem::path& partitionDir);
std::filesystem::path partitionBucketPath(const std::filesystem::path& partitionDir, uint32_t bucketId);

PartitionedKeySetStats buildPartitionedKeySet(const PartitionedKeySetOptions& options);
PartitionValidationResult validatePartitionedKeySet(const std::filesystem::path& partitionDir);
PartitionInspection inspectPartitionedKeySet(const std::filesystem::path& partitionDir);
std::vector<uint64_t> readPartitionBucketKeys(
    const std::filesystem::path& partitionDir,
    uint32_t bucketId);
PartitionedKeySetOpStats partitionedDifference(
    const std::filesystem::path& leftPartitionDir,
    const std::filesystem::path& rightPartitionDir,
    const std::filesystem::path& outputPartitionDir,
    bool overwrite = false);
PartitionedKeySetOpStats partitionedUnion(
    const std::filesystem::path& leftPartitionDir,
    const std::filesystem::path& rightPartitionDir,
    const std::filesystem::path& outputPartitionDir,
    bool overwrite = false);
PartitionLookupBenchmark benchmarkPartitionedKeySet(
    const std::filesystem::path& partitionDir,
    uint64_t sampleCount,
    PartitionBenchmarkMode mode = PartitionBenchmarkMode::Existing,
    uint32_t cacheBuckets = 32);

class PartitionedKeySetReader {
public:
    explicit PartitionedKeySetReader(std::filesystem::path partitionDir);
    explicit PartitionedKeySetReader(PartitionedKeySetReaderOptions options);
    ~PartitionedKeySetReader();

    PartitionedKeySetReader(const PartitionedKeySetReader&) = delete;
    PartitionedKeySetReader& operator=(const PartitionedKeySetReader&) = delete;

    bool contains(uint64_t key) const;
    std::vector<uint8_t> containsBatch(const std::vector<uint64_t>& keys) const;
    BatchLookupStats containsBatchStats(std::span<const uint64_t> keys) const;
    uint64_t keyCount() const;
    uint32_t bucketCount() const;
    int soldierCount() const;
    PartitionMethod partitionMethod() const;
    uint32_t bucketForKey(uint64_t key) const;
    uint64_t bucketLoadCount() const;
    std::vector<uint64_t> bucketSizes() const;
    std::vector<uint64_t> sampleExistingKeys(uint64_t sampleCount) const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace sanpao15
