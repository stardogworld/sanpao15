#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sanpao15 {

struct KeysFileData {
    int soldierCount = 0;
    std::vector<uint64_t> keys;
};

struct KeySetFileInfo {
    std::filesystem::path path;
    int soldierCount = 0;
    uint64_t keyCount = 0;
};

struct KeySetOpStats {
    uint64_t leftKeys = 0;
    uint64_t rightKeys = 0;
    uint64_t outputKeys = 0;
    double seconds = 0.0;
};

void writeKeysFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys);

KeysFileData readKeysFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt);

KeySetFileInfo inspectKeysFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount = std::nullopt);

KeySetOpStats sortedDifference(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::filesystem::path& output);

KeySetOpStats sortedUnion(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::filesystem::path& output);

struct ExternalClosureProgressInfo {
    int soldierCount = 0;
    uint64_t iteration = 0;
    uint64_t frontierStates = 0;
    uint64_t visitedStates = 0;
    uint64_t candidateStates = 0;
    uint64_t nextFrontierStates = 0;
    uint64_t expandedStates = 0;
    uint64_t generatedSameLayerEdges = 0;
    uint64_t generatedCaptureEdges = 0;
    uint64_t nextSeedStates = 0;
    uint64_t duplicateNextSeeds = 0;
    bool complete = false;
    bool truncated = false;
    double elapsedSeconds = 0.0;
};

using ExternalClosureProgressCallback =
    std::function<void(const ExternalClosureProgressInfo& info)>;

struct ExternalClosureOptions {
    std::filesystem::path workDir;
    std::filesystem::path seedFile;
    std::filesystem::path outputLayerFile;
    std::filesystem::path outputNextSeedFile;
    std::filesystem::path checkpointDir;

    int soldierCount = 0;

    uint64_t chunkKeyLimit = 1000000;
    uint64_t progressInterval = 0;
    uint64_t checkpointInterval = 0;
    uint64_t maxIterations = 0;
    uint64_t maxExpandedStates = 0;
    uint64_t maxDiscoveredStates = 0;

    bool keepTempFiles = false;
    bool resume = false;
    bool writeCheckpoint = true;
    bool partitionedClosure = false;
    uint32_t closurePartitionBuckets = 256;
    std::string closurePartitionMethod = "splitmix64_mod";
    ExternalClosureProgressCallback progress;
};

struct ExternalClosureStats {
    int soldierCount = 0;

    uint64_t seedStates = 0;
    uint64_t finalStates = 0;

    uint64_t iterations = 0;
    uint64_t expandedStates = 0;

    uint64_t generatedSameLayerEdges = 0;
    uint64_t generatedCaptureEdges = 0;

    uint64_t generatedCandidateKeys = 0;
    uint64_t newFrontierStates = 0;
    uint64_t duplicateOrVisitedCandidates = 0;

    uint64_t nextSeedStates = 0;
    uint64_t duplicateNextSeeds = 0;

    bool truncated = false;
    bool truncatedByMaxStates = false;
    bool truncatedByMaxExpandedStates = false;
    bool truncatedByMaxIterations = false;
    bool resumed = false;
    bool complete = false;
    bool checkpointWritten = false;
    bool partitionedClosure = false;
    uint32_t closurePartitionBuckets = 0;
    uint64_t partitionedSnapshotsWritten = 0;

    uint64_t initialVisitedStates = 0;
    uint64_t initialFrontierStates = 0;
    uint64_t finalFrontierStates = 0;
    std::string checkpointDir;

    double totalSeconds = 0.0;
    double expansionSeconds = 0.0;
    double candidateDedupSeconds = 0.0;
    double differenceSeconds = 0.0;
    double unionSeconds = 0.0;
    double partitionSeconds = 0.0;
};

struct ExternalClosureCheckpointInfo {
    std::filesystem::path checkpointDir;
    uint32_t checkpointVersion = 1;
    std::string checkpointKind;
    bool requiresTransientRuns = false;
    int soldierCount = 0;
    uint64_t seedStates = 0;
    uint64_t visitedStates = 0;
    uint64_t frontierStates = 0;
    uint64_t nextSeedStates = 0;
    uint64_t iterations = 0;
    uint64_t expandedStates = 0;
    bool complete = false;
    bool truncated = false;
    bool stableCheckpoint = false;
};

struct ClosureCheckpointRepairResult {
    std::filesystem::path checkpointDir;
    bool repaired = false;
    bool dryRun = false;
    std::string beforeKind;
    std::string afterKind;
    bool beforeRequiresTransientRuns = false;
    bool afterRequiresTransientRuns = false;
};

std::filesystem::path closureCheckpointManifestPath(const std::filesystem::path& checkpointDir);
ExternalClosureCheckpointInfo inspectClosureCheckpoint(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount = std::nullopt);
uint64_t cleanupStaleClosureRuns(const std::filesystem::path& checkpointDir);
ClosureCheckpointRepairResult repairClosureCheckpoint(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount = std::nullopt,
    bool dryRun = false);

ExternalClosureStats buildLayerClosureExternal(const ExternalClosureOptions& options);

}  // namespace sanpao15
