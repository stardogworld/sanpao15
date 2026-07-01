#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sanpao15/external_closure.h"
#include "sanpao15/external_keyset.h"
#include "sanpao15/position.h"

namespace sanpao15 {

enum class LayerClosureBackend {
    InMemory,
    External,
};

enum class LayerTruncationReason {
    None,
    MaxStatesPerLayer,
    MaxExpandedStates,
    MaxIterations,
};

struct LayeredProgressInfo {
    int soldierCount = 0;
    LayerClosureBackend closureBackend = LayerClosureBackend::InMemory;
    uint64_t iteration = 0;
    uint64_t seedStates = 0;
    uint64_t visitedStates = 0;
    uint64_t expandedStates = 0;
    uint64_t queueSize = 0;
    uint64_t candidateStates = 0;
    uint64_t nextFrontierStates = 0;
    uint64_t generatedSameLayerEdges = 0;
    uint64_t generatedCaptureEdges = 0;
    uint64_t newNextLayerSeeds = 0;
    uint64_t duplicateNextLayerSeeds = 0;
    bool complete = false;
    bool truncated = false;
    double elapsedSeconds = 0.0;
};

using LayeredProgressCallback = std::function<void(const LayeredProgressInfo& info)>;

struct LayeredBuildOptions {
    std::filesystem::path outputDir;
    uint64_t maxStatesPerLayer = 0;
    uint64_t maxExpandedStates = 0;
    uint64_t maxIterations = 0;
    uint64_t progressInterval = 0;
    uint64_t checkpointInterval = 0;
    bool keepTempFiles = false;
    bool resumeClosure = false;
    bool partitionedClosure = false;
    uint32_t closurePartitionBuckets = 256;
    std::string closurePartitionMethod = "splitmix64_mod";
    std::optional<int> startLayer;
    std::optional<int> stopAfterLayer;
    LayerClosureBackend closureBackend = LayerClosureBackend::InMemory;
    bool useExternalSeedDedup = false;
    uint64_t externalChunkKeyLimit = 1000000;
    std::filesystem::path tempDir;
    LayeredProgressCallback progress;
};

struct LayerStats {
    int soldierCount = 0;
    uint64_t seedStates = 0;
    uint64_t reachableStates = 0;
    uint64_t generatedSameLayerEdges = 0;
    uint64_t generatedCaptureEdges = 0;
    uint64_t newNextLayerSeeds = 0;
    uint64_t duplicateNextLayerSeeds = 0;
    bool truncated = false;
    bool skipped = false;
    LayerClosureBackend closureBackend = LayerClosureBackend::InMemory;
    LayerTruncationReason truncationReason = LayerTruncationReason::None;
    std::string layerFile;
    std::string seedFile;
    std::string nextSeedFile;
    bool externalSeedDedup = false;
    ExternalKeySetStats externalSeedStats;
    uint64_t iterations = 0;
    uint64_t expandedStates = 0;
    uint64_t generatedCandidateKeys = 0;
    uint64_t newFrontierStates = 0;
    uint64_t duplicateOrVisitedCandidates = 0;
    bool resumedClosure = false;
    bool complete = false;
    bool checkpointWritten = false;
    bool partitionedClosure = false;
    uint32_t closurePartitionBuckets = 0;
    uint64_t partitionedSnapshotsWritten = 0;
    uint64_t initialVisitedStates = 0;
    uint64_t initialFrontierStates = 0;
    uint64_t finalFrontierStates = 0;
    std::string checkpointDir;
    double expansionSeconds = 0.0;
    double candidateDedupSeconds = 0.0;
    double differenceSeconds = 0.0;
    double unionSeconds = 0.0;
    double partitionSeconds = 0.0;
    double buildSeconds = 0.0;
};

struct LayeredBuildStats {
    std::array<LayerStats, 16> layers{};
    uint64_t totalLayerStates = 0;
    uint64_t totalGeneratedSameLayerEdges = 0;
    uint64_t totalGeneratedCaptureEdges = 0;
    bool truncated = false;
    bool resumed = false;
    int startLayer = 15;
    int stopAfterLayer = 0;
    double totalSeconds = 0.0;
};

struct LayerFileData {
    int soldierCount = 0;
    std::vector<uint64_t> keys;
};

using SeedFileData = LayerFileData;

struct KeyListFileSummary {
    std::filesystem::path path;
    int soldierCount = 0;
    uint64_t keyCount = 0;
    uint64_t minKey = 0;
    uint64_t maxKey = 0;
    std::vector<uint64_t> firstKeys;
    std::vector<uint64_t> lastKeys;
};

const char* layerClosureBackendToString(LayerClosureBackend backend);
const char* layerTruncationReasonToString(LayerTruncationReason reason);

std::filesystem::path layerFilePath(const std::filesystem::path& outputDir, int soldierCount);
std::filesystem::path seedFilePath(const std::filesystem::path& outputDir, int soldierCount);
std::filesystem::path manifestFilePath(const std::filesystem::path& outputDir);

void writeLayerFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys);
void writeSeedFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys);

LayerFileData readLayerFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt);
SeedFileData readSeedFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt);

KeyListFileSummary validateLayerFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt);
KeyListFileSummary validateSeedFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt);
KeyListFileSummary inspectLayerFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt,
    size_t sampleLimit = 3);
KeyListFileSummary inspectSeedFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt,
    size_t sampleLimit = 3);

LayeredBuildStats buildReachableLayers(const LayeredBuildOptions& options);
LayeredBuildStats buildReachableLayersFromSeed(const Position& initial, const LayeredBuildOptions& options);

}  // namespace sanpao15
