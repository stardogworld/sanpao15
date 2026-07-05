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

struct PartitionedClosureSnapshotInfo {
    std::string name;
    std::filesystem::path path;
    bool activeCheckpointInput = false;
    int soldierCount = 0;
    uint64_t keyCount = 0;
    uint32_t bucketCount = 0;
    std::string partitionMethod;
    uint64_t minBucketSize = 0;
    uint64_t maxBucketSize = 0;
    double averageBucketSize = 0.0;
    uint64_t emptyBuckets = 0;
    uint64_t totalBucketFileBytes = 0;
    double buildSeconds = 0.0;
};

struct PartitionedClosureCheckpointInfo {
    std::filesystem::path outputDir;
    std::filesystem::path sourceCheckpoint;
    std::filesystem::path sourceLayerDir;
    int soldierCount = 0;
    std::string checkpointKind;
    uint64_t expandedStates = 0;
    bool complete = false;
    bool truncated = false;
    bool requiresTransientRuns = false;
    uint32_t bucketCount = 0;
    std::string partitionMethod;
    std::vector<PartitionedClosureSnapshotInfo> snapshots;
};

struct ClosureCheckpointMigrationOptions {
    std::filesystem::path checkpointDir;
    std::filesystem::path outputDir;
    std::optional<int> expectedSoldierCount = std::nullopt;
    uint32_t bucketCount = 256;
    std::string partitionMethod = "splitmix64_mod";
    uint64_t progressInterval = 0;
    bool dryRun = false;
    bool overwrite = false;
    std::function<void(const std::string& snapshotName, uint64_t scanned)> progress;
};

struct ClosureCheckpointMigrationResult {
    std::filesystem::path checkpointDir;
    std::filesystem::path outputDir;
    bool dryRun = false;
    bool outputExists = false;
    bool overwrite = false;
    int soldierCount = 0;
    std::string checkpointKind;
    uint64_t expandedStates = 0;
    bool complete = false;
    bool truncated = false;
    uint32_t bucketCount = 0;
    std::string partitionMethod;
    std::vector<PartitionedClosureSnapshotInfo> snapshots;
    uint64_t totalBucketFileBytes = 0;
    double totalSeconds = 0.0;
};

struct PartitionedClosureProgressInfo {
    int soldierCount = 0;
    uint64_t expandedThisRun = 0;
    uint64_t finalExpandedStates = 0;
    uint64_t sameEdgesGenerated = 0;
    uint64_t captureEdgesGenerated = 0;
    uint32_t bucketId = 0;
    double elapsedSeconds = 0.0;
};

using PartitionedClosureProgressCallback =
    std::function<void(const PartitionedClosureProgressInfo& info)>;

struct PartitionedClosureOptions {
    std::filesystem::path layerDir;
    std::filesystem::path partitionedCheckpointDir;
    int soldierCount = 0;
    uint64_t expandedBudget = 0;
    uint64_t maxIterations = 0;
    uint64_t progressInterval = 0;
    uint32_t bucketCount = 0;
    std::string partitionMethod;
    uint64_t candidateChunkKeyLimit = 1000000;
    uint32_t partitionCacheBuckets = 32;
    bool keepTempFiles = false;
    bool dryRun = false;
    PartitionedClosureProgressCallback progress;
};

struct PartitionedClosureRunStats {
    int soldierCount = 0;
    bool resumed = true;
    bool dryRun = false;
    bool complete = false;
    bool truncated = false;
    std::string truncationReason = "none";
    std::string initialCheckpointKind;
    std::string finalCheckpointKind;
    uint64_t initialExpandedStates = 0;
    uint64_t finalExpandedStates = 0;
    uint64_t expandedThisRun = 0;
    uint64_t initialVisitedStates = 0;
    uint64_t finalVisitedStates = 0;
    uint64_t initialFrontierStates = 0;
    uint64_t finalFrontierStates = 0;
    uint64_t initialNextSeedStates = 0;
    uint64_t finalNextSeedStates = 0;
    uint64_t pendingCandidateStates = 0;
    uint64_t sameEdgesGenerated = 0;
    uint64_t captureEdgesGenerated = 0;
    uint64_t iterationsCompleted = 0;
    uint64_t snapshotsWritten = 0;
    std::filesystem::path checkpointDir;
    std::filesystem::path runDir;
    double totalSeconds = 0.0;
    double expansionSeconds = 0.0;
    double partitionMergeSeconds = 0.0;
};

std::filesystem::path closureCheckpointManifestPath(const std::filesystem::path& checkpointDir);
std::filesystem::path partitionedClosureCheckpointManifestPath(const std::filesystem::path& outputDir);
ExternalClosureCheckpointInfo inspectClosureCheckpoint(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount = std::nullopt);
PartitionedClosureCheckpointInfo inspectPartitionedClosureCheckpoint(
    const std::filesystem::path& outputDir,
    std::optional<int> expectedSoldierCount = std::nullopt);
uint64_t cleanupStaleClosureRuns(const std::filesystem::path& checkpointDir);
ClosureCheckpointRepairResult repairClosureCheckpoint(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount = std::nullopt,
    bool dryRun = false);
ClosureCheckpointMigrationResult migrateClosureCheckpointToPartitioned(
    const ClosureCheckpointMigrationOptions& options);
PartitionedClosureRunStats resumePartitionedClosure(
    const PartitionedClosureOptions& options);

ExternalClosureStats buildLayerClosureExternal(const ExternalClosureOptions& options);

}  // namespace sanpao15
