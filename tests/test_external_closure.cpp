#include "test_common.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/external_closure.h"
#include "sanpao15/layered.h"
#include "sanpao15/partitioned_keyset.h"
#include "sanpao15/position.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

Position fourSoldierCapturePosition() {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.soldiers = setBit(setBit(setBit(setBit(0, 1), 2), 3), 10);
    pos.side = Side::Cannon;
    return pos;
}

Position fourSoldierBlockedPosition() {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.soldiers = setBit(setBit(setBit(setBit(0, 10), 15), 16), 21);
    pos.side = Side::Cannon;
    return pos;
}

std::vector<uint64_t> keysFor(int soldierCount, std::initializer_list<uint64_t> suffixes) {
    std::vector<uint64_t> keys;
    for (uint64_t suffix : suffixes) {
        Position pos;
        pos.cannons = setBit(0, 20);
        pos.side = suffix % 2 == 0 ? Side::Cannon : Side::Soldier;
        for (int i = 0; i < soldierCount; ++i) {
            pos.soldiers = setBit(pos.soldiers, i);
        }
        if (soldierCount == 0) {
            pos.cannons = setBit(0, static_cast<int>(suffix % 25));
        } else {
            pos.cannons = setBit(0, 20 + static_cast<int>(suffix % 5));
        }
        keys.push_back(packPosition(pos));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<uint64_t> readPartitionedKeys(const std::filesystem::path& partitionDir) {
    const PartitionInspection inspection = inspectPartitionedKeySet(partitionDir);
    std::vector<uint64_t> keys;
    for (uint32_t bucket = 0; bucket < inspection.bucketCount; ++bucket) {
        std::vector<uint64_t> bucketKeys = readPartitionBucketKeys(partitionDir, bucket);
        keys.insert(keys.end(), bucketKeys.begin(), bucketKeys.end());
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::filesystem::path partitionedSnapshotPath(
    const PartitionedClosureCheckpointInfo& checkpoint,
    const std::string& name) {
    for (const PartitionedClosureSnapshotInfo& snapshot : checkpoint.snapshots) {
        if (snapshot.name == name) {
            return snapshot.path;
        }
    }
    throw std::runtime_error("missing partitioned snapshot in test: " + name);
}

}  // namespace

SANPAO15_TEST(sortedDifferenceSubtractsRightFromLeft) {
    const std::filesystem::path dir = tempDir("sanpao15-external-diff");
    const auto left = dir / "left.s15keys";
    const auto right = dir / "right.s15keys";
    const auto output = dir / "out.s15keys";
    const std::vector<uint64_t> values = keysFor(0, {1, 2, 3, 5, 8});
    const std::vector<uint64_t> remove = {values[1], values[3]};
    writeKeysFile(left, 0, values);
    writeKeysFile(right, 0, remove);

    const KeySetOpStats stats = sortedDifference(left, right, output);
    const KeysFileData result = readKeysFile(output, 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.leftKeys == 5);
    SANPAO15_REQUIRE(stats.rightKeys == 2);
    SANPAO15_REQUIRE(stats.outputKeys == 3);
    SANPAO15_REQUIRE((result.keys == std::vector<uint64_t>{values[0], values[2], values[4]}));
}

SANPAO15_TEST(sortedUnionMergesUniqueKeys) {
    const std::filesystem::path dir = tempDir("sanpao15-external-union");
    const auto left = dir / "left.s15keys";
    const auto right = dir / "right.s15keys";
    const auto output = dir / "out.s15keys";
    const std::vector<uint64_t> values = keysFor(0, {1, 2, 3, 5, 8});
    writeKeysFile(left, 0, {values[0], values[1], values[3]});
    writeKeysFile(right, 0, {values[1], values[2], values[3], values[4]});

    const KeySetOpStats stats = sortedUnion(left, right, output);
    const KeysFileData result = readKeysFile(output, 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.leftKeys == 3);
    SANPAO15_REQUIRE(stats.rightKeys == 4);
    SANPAO15_REQUIRE(stats.outputKeys == 5);
    SANPAO15_REQUIRE(result.keys == values);
}

SANPAO15_TEST(sortedSetOpsHandleEmptyInputs) {
    const std::filesystem::path dir = tempDir("sanpao15-external-empty");
    const auto empty = dir / "empty.s15keys";
    const auto nonempty = dir / "nonempty.s15keys";
    const auto out1 = dir / "out1.s15keys";
    const auto out2 = dir / "out2.s15keys";
    const auto out3 = dir / "out3.s15keys";
    const std::vector<uint64_t> values = keysFor(0, {1, 2});
    writeKeysFile(empty, 0, {});
    writeKeysFile(nonempty, 0, values);

    (void)sortedDifference(empty, empty, out1);
    (void)sortedDifference(nonempty, empty, out2);
    (void)sortedUnion(empty, nonempty, out3);
    const KeysFileData diffEmpty = readKeysFile(out1, 0);
    const KeysFileData diffNonempty = readKeysFile(out2, 0);
    const KeysFileData unionNonempty = readKeysFile(out3, 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(diffEmpty.keys.empty());
    SANPAO15_REQUIRE(diffNonempty.keys == values);
    SANPAO15_REQUIRE(unionNonempty.keys == values);
}

SANPAO15_TEST(externalClosureMatchesInMemoryClosureForFourSoldierSeed) {
    const std::filesystem::path memoryDir = tempDir("sanpao15-external-closure-memory");
    LayeredBuildOptions memoryOptions;
    memoryOptions.outputDir = memoryDir;
    const LayeredBuildStats memoryStats = buildReachableLayersFromSeed(fourSoldierBlockedPosition(), memoryOptions);

    const std::filesystem::path externalDir = tempDir("sanpao15-external-closure-work");
    const auto seed = externalDir / "seeds-04.s15seed";
    const auto layer = externalDir / "layer-04.s15layer";
    const auto nextSeed = externalDir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierBlockedPosition())});

    ExternalClosureOptions options;
    options.workDir = externalDir / "work";
    options.seedFile = seed;
    options.outputLayerFile = layer;
    options.outputNextSeedFile = nextSeed;
    options.soldierCount = 4;
    options.chunkKeyLimit = 3;
    const ExternalClosureStats externalStats = buildLayerClosureExternal(options);

    const LayerFileData memoryLayer = readLayerFile(layerFilePath(memoryDir, 4), 4);
    const SeedFileData memoryNextSeed = readSeedFile(seedFilePath(memoryDir, 3), 3);
    const LayerFileData externalLayer = readLayerFile(layer, 4);
    const SeedFileData externalNextSeed = readSeedFile(nextSeed, 3);
    std::filesystem::remove_all(memoryDir);
    std::filesystem::remove_all(externalDir);

    SANPAO15_REQUIRE(!memoryStats.truncated);
    SANPAO15_REQUIRE(!externalStats.truncated);
    SANPAO15_REQUIRE(memoryLayer.keys == externalLayer.keys);
    SANPAO15_REQUIRE(memoryNextSeed.keys == externalNextSeed.keys);
    SANPAO15_REQUIRE(memoryStats.layers[4].generatedSameLayerEdges == externalStats.generatedSameLayerEdges);
    SANPAO15_REQUIRE(memoryStats.layers[4].generatedCaptureEdges == externalStats.generatedCaptureEdges);
}

SANPAO15_TEST(externalClosureTruncationWritesValidOutputs) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-trunc");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions options;
    options.workDir = dir / "work";
    options.seedFile = seed;
    options.outputLayerFile = layer;
    options.outputNextSeedFile = nextSeed;
    options.soldierCount = 4;
    options.chunkKeyLimit = 2;
    options.maxIterations = 1;
    const ExternalClosureStats stats = buildLayerClosureExternal(options);
    const KeyListFileSummary layerSummary = validateLayerFile(layer, 4);
    const KeyListFileSummary seedSummary = validateSeedFile(nextSeed, 3);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.truncated);
    SANPAO15_REQUIRE(layerSummary.keyCount > 0);
    SANPAO15_REQUIRE(seedSummary.soldierCount == 3);
}

SANPAO15_TEST(externalClosureWritesCheckpointOnTruncation) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-checkpoint");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions options;
    options.workDir = dir / "work";
    options.seedFile = seed;
    options.outputLayerFile = layer;
    options.outputNextSeedFile = nextSeed;
    options.soldierCount = 4;
    options.chunkKeyLimit = 2;
    options.maxIterations = 1;
    const ExternalClosureStats stats = buildLayerClosureExternal(options);
    const ExternalClosureCheckpointInfo checkpoint = inspectClosureCheckpoint(options.workDir, 4);
    const std::string manifest = readText(closureCheckpointManifestPath(options.workDir));
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.truncated);
    SANPAO15_REQUIRE(stats.checkpointWritten);
    SANPAO15_REQUIRE(!stats.complete);
    SANPAO15_REQUIRE(checkpoint.checkpointVersion == 2);
    SANPAO15_REQUIRE(!checkpoint.requiresTransientRuns);
    SANPAO15_REQUIRE(checkpoint.stableCheckpoint);
    SANPAO15_REQUIRE(manifest.find("\"requiresTransientRuns\": false") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"checkpointKind\":") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"remainingFrontierFile\":") != std::string::npos);
    SANPAO15_REQUIRE(checkpoint.soldierCount == 4);
    SANPAO15_REQUIRE(checkpoint.visitedStates == stats.finalStates);
    SANPAO15_REQUIRE(checkpoint.frontierStates == stats.finalFrontierStates);
    SANPAO15_REQUIRE(checkpoint.nextSeedStates == stats.nextSeedStates);
}

SANPAO15_TEST(externalClosureCheckpointIgnoresStaleTransientRuns) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-stale-runs");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions first;
    first.workDir = dir / "work";
    first.seedFile = seed;
    first.outputLayerFile = layer;
    first.outputNextSeedFile = nextSeed;
    first.soldierCount = 4;
    first.chunkKeyLimit = 1;
    first.maxExpandedStates = 1;
    const ExternalClosureStats firstStats = buildLayerClosureExternal(first);
    SANPAO15_REQUIRE(firstStats.truncated);

    const std::filesystem::path staleRuns = first.workDir / "runs" / "stale";
    std::filesystem::create_directories(staleRuns);
    writeRunFile(staleRuns / "keys-0.s15run", {1, 2, 3});
    SANPAO15_REQUIRE(std::filesystem::exists(staleRuns / "keys-0.s15run"));

    const ExternalClosureCheckpointInfo before = inspectClosureCheckpoint(first.workDir, 4);
    SANPAO15_REQUIRE(!before.requiresTransientRuns);
    const uint64_t removed = cleanupStaleClosureRuns(first.workDir);
    SANPAO15_REQUIRE(removed >= 1);
    SANPAO15_REQUIRE(!std::filesystem::exists(first.workDir / "runs"));
    const ExternalClosureCheckpointInfo after = inspectClosureCheckpoint(first.workDir, 4);
    SANPAO15_REQUIRE(after.visitedStates == before.visitedStates);

    ExternalClosureOptions second = first;
    second.resume = true;
    second.maxExpandedStates = 3;
    const ExternalClosureStats secondStats = buildLayerClosureExternal(second);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(secondStats.resumed);
}

SANPAO15_TEST(externalClosureResumeMatchesUninterruptedRun) {
    const std::filesystem::path fullDir = tempDir("sanpao15-external-closure-full");
    const auto fullSeed = fullDir / "seeds-04.s15seed";
    const auto fullLayer = fullDir / "layer-04.s15layer";
    const auto fullNextSeed = fullDir / "seeds-03.s15seed";
    writeSeedFile(fullSeed, 4, {packPosition(fourSoldierCapturePosition())});
    ExternalClosureOptions full;
    full.workDir = fullDir / "work";
    full.seedFile = fullSeed;
    full.outputLayerFile = fullLayer;
    full.outputNextSeedFile = fullNextSeed;
    full.soldierCount = 4;
    full.chunkKeyLimit = 3;
    full.maxExpandedStates = 20;
    const ExternalClosureStats fullStats = buildLayerClosureExternal(full);

    const std::filesystem::path resumeDir = tempDir("sanpao15-external-closure-resume");
    const auto resumeSeed = resumeDir / "seeds-04.s15seed";
    const auto resumeLayer = resumeDir / "layer-04.s15layer";
    const auto resumeNextSeed = resumeDir / "seeds-03.s15seed";
    writeSeedFile(resumeSeed, 4, {packPosition(fourSoldierCapturePosition())});
    ExternalClosureOptions first;
    first.workDir = resumeDir / "work";
    first.seedFile = resumeSeed;
    first.outputLayerFile = resumeLayer;
    first.outputNextSeedFile = resumeNextSeed;
    first.soldierCount = 4;
    first.chunkKeyLimit = 3;
    first.maxExpandedStates = 10;
    const ExternalClosureStats firstStats = buildLayerClosureExternal(first);
    SANPAO15_REQUIRE(firstStats.truncated);

    ExternalClosureOptions second = first;
    second.resume = true;
    second.maxExpandedStates = 10;
    const ExternalClosureStats secondStats = buildLayerClosureExternal(second);

    const LayerFileData fullLayerData = readLayerFile(fullLayer, 4);
    const SeedFileData fullSeedData = readSeedFile(fullNextSeed, 3);
    const LayerFileData resumeLayerData = readLayerFile(resumeLayer, 4);
    const SeedFileData resumeSeedData = readSeedFile(resumeNextSeed, 3);
    std::filesystem::remove_all(fullDir);
    std::filesystem::remove_all(resumeDir);

    SANPAO15_REQUIRE(fullStats.truncated);
    SANPAO15_REQUIRE(secondStats.truncated);
    SANPAO15_REQUIRE(secondStats.resumed);
    SANPAO15_REQUIRE(secondStats.expandedStates == fullStats.expandedStates);
    SANPAO15_REQUIRE(secondStats.generatedSameLayerEdges == fullStats.generatedSameLayerEdges);
    SANPAO15_REQUIRE(secondStats.generatedCaptureEdges == fullStats.generatedCaptureEdges);
    SANPAO15_REQUIRE(fullLayerData.keys == resumeLayerData.keys);
    SANPAO15_REQUIRE(fullSeedData.keys == resumeSeedData.keys);
}

SANPAO15_TEST(externalClosureResumePreservesCaptureSeeds) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-capture-resume");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions first;
    first.workDir = dir / "work";
    first.seedFile = seed;
    first.outputLayerFile = layer;
    first.outputNextSeedFile = nextSeed;
    first.soldierCount = 4;
    first.chunkKeyLimit = 2;
    first.maxExpandedStates = 1;
    const ExternalClosureStats firstStats = buildLayerClosureExternal(first);
    const SeedFileData firstSeeds = readSeedFile(nextSeed, 3);

    ExternalClosureOptions second = first;
    second.resume = true;
    second.maxExpandedStates = 10;
    const ExternalClosureStats secondStats = buildLayerClosureExternal(second);
    const SeedFileData resumedSeeds = readSeedFile(nextSeed, 3);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(firstStats.truncated);
    SANPAO15_REQUIRE(secondStats.resumed);
    SANPAO15_REQUIRE(!firstSeeds.keys.empty());
    for (uint64_t key : firstSeeds.keys) {
        SANPAO15_REQUIRE(std::binary_search(resumedSeeds.keys.begin(), resumedSeeds.keys.end(), key));
    }
}

SANPAO15_TEST(externalClosureRejectsBadCheckpoint) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-bad-checkpoint");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions options;
    options.workDir = dir / "work";
    options.seedFile = seed;
    options.outputLayerFile = layer;
    options.outputNextSeedFile = nextSeed;
    options.soldierCount = 4;
    options.maxIterations = 1;
    (void)buildLayerClosureExternal(options);

    std::filesystem::remove(options.workDir / "frontier.s15keys");
    ExternalClosureOptions missing = options;
    missing.resume = true;
    sanpao15::test::requireThrows(
        [&] { (void)buildLayerClosureExternal(missing); },
        "resume should reject a checkpoint with a missing frontier file");
    sanpao15::test::requireThrows(
        [&] { (void)inspectClosureCheckpoint(options.workDir, 4); },
        "checkpoint validation should reject a missing frontier file");

    const std::filesystem::path dir2 = tempDir("sanpao15-external-closure-bad-checkpoint-rules");
    const auto seed2 = dir2 / "seeds-04.s15seed";
    const auto layer2 = dir2 / "layer-04.s15layer";
    const auto nextSeed2 = dir2 / "seeds-03.s15seed";
    writeSeedFile(seed2, 4, {packPosition(fourSoldierCapturePosition())});
    ExternalClosureOptions options2 = options;
    options2.workDir = dir2 / "work";
    options2.seedFile = seed2;
    options2.outputLayerFile = layer2;
    options2.outputNextSeedFile = nextSeed2;
    (void)buildLayerClosureExternal(options2);
    std::string manifest = readText(closureCheckpointManifestPath(options2.workDir));
    const std::string goodHash = "\"rulesetHash\": " + std::to_string(StandardRulesetHash);
    const size_t hashPos = manifest.find(goodHash);
    SANPAO15_REQUIRE(hashPos != std::string::npos);
    manifest.replace(hashPos, goodHash.size(), "\"rulesetHash\": 1");
    {
        std::ofstream output(closureCheckpointManifestPath(options2.workDir));
        output << manifest;
    }
    ExternalClosureOptions badRules = options2;
    badRules.resume = true;
    sanpao15::test::requireThrows(
        [&] { (void)buildLayerClosureExternal(badRules); },
        "resume should reject a checkpoint with a bad ruleset hash");

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(dir2);
}

SANPAO15_TEST(externalClosureRepairRewritesStableManifest) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-repair");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions options;
    options.workDir = dir / "work";
    options.seedFile = seed;
    options.outputLayerFile = layer;
    options.outputNextSeedFile = nextSeed;
    options.soldierCount = 4;
    options.maxExpandedStates = 1;
    (void)buildLayerClosureExternal(options);

    ClosureCheckpointRepairResult dryRun = repairClosureCheckpoint(options.workDir, 4, true);
    const std::string before = readText(closureCheckpointManifestPath(options.workDir));
    ClosureCheckpointRepairResult repaired = repairClosureCheckpoint(options.workDir, 4, false);
    const std::string after = readText(closureCheckpointManifestPath(options.workDir));
    const ExternalClosureCheckpointInfo info = inspectClosureCheckpoint(options.workDir, 4);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(dryRun.dryRun);
    SANPAO15_REQUIRE(!dryRun.afterRequiresTransientRuns);
    SANPAO15_REQUIRE(!repaired.afterRequiresTransientRuns);
    SANPAO15_REQUIRE(before.find("\"checkpointVersion\": 2") != std::string::npos);
    SANPAO15_REQUIRE(after.find("\"requiresTransientRuns\": false") != std::string::npos);
    SANPAO15_REQUIRE(info.checkpointVersion == 2);
}

SANPAO15_TEST(externalClosureWritesPartitionedCheckpointSnapshots) {
    const std::filesystem::path dir = tempDir("sanpao15-external-closure-partitioned");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions options;
    options.workDir = dir / "work";
    options.seedFile = seed;
    options.outputLayerFile = layer;
    options.outputNextSeedFile = nextSeed;
    options.soldierCount = 4;
    options.chunkKeyLimit = 2;
    options.maxExpandedStates = 10;
    options.partitionedClosure = true;
    options.closurePartitionBuckets = 4;
    const ExternalClosureStats stats = buildLayerClosureExternal(options);
    const std::string manifest = readText(closureCheckpointManifestPath(options.workDir));
    const PartitionValidationResult visited =
        validatePartitionedKeySet(options.workDir / "partitioned" / "visited");
    const PartitionValidationResult frontier =
        validatePartitionedKeySet(options.workDir / "partitioned" / "frontier");
    const PartitionValidationResult nextSeeds =
        validatePartitionedKeySet(options.workDir / "partitioned" / "next-seeds");
    const ExternalClosureCheckpointInfo checkpoint = inspectClosureCheckpoint(options.workDir, 4);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.partitionedClosure);
    SANPAO15_REQUIRE(stats.closurePartitionBuckets == 4);
    SANPAO15_REQUIRE(stats.partitionedSnapshotsWritten >= 1);
    SANPAO15_REQUIRE(manifest.find("\"partitionedClosure\": true") != std::string::npos);
    SANPAO15_REQUIRE(visited.valid);
    SANPAO15_REQUIRE(frontier.valid);
    SANPAO15_REQUIRE(nextSeeds.valid);
    SANPAO15_REQUIRE(visited.totalKeys == checkpoint.visitedStates);
    SANPAO15_REQUIRE(frontier.totalKeys == checkpoint.frontierStates);
    SANPAO15_REQUIRE(nextSeeds.totalKeys == checkpoint.nextSeedStates);
}

SANPAO15_TEST(closureCheckpointMigrationDryRunDoesNotWrite) {
    const std::filesystem::path dir = tempDir("sanpao15-closure-migrate-dry-run");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxExpandedStates = 1;
    (void)buildLayerClosureExternal(closure);

    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = closure.workDir;
    migration.outputDir = dir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    migration.dryRun = true;
    const ClosureCheckpointMigrationResult result = migrateClosureCheckpointToPartitioned(migration);
    const bool outputExists = std::filesystem::exists(migration.outputDir);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(result.dryRun);
    SANPAO15_REQUIRE(result.snapshots.size() >= 3);
    SANPAO15_REQUIRE(!outputExists);
}

SANPAO15_TEST(closureCheckpointMigrationRoundtripValidatesCounts) {
    const std::filesystem::path dir = tempDir("sanpao15-closure-migrate-roundtrip");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxIterations = 1;
    const ExternalClosureStats stats = buildLayerClosureExternal(closure);

    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = closure.workDir;
    migration.outputDir = dir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    const ClosureCheckpointMigrationResult result = migrateClosureCheckpointToPartitioned(migration);
    const PartitionedClosureCheckpointInfo partitioned =
        inspectPartitionedClosureCheckpoint(migration.outputDir, 4);
    PartitionedKeySetReader visitedReader(migration.outputDir / "visited");
    const KeysFileData visited = readKeysFile(closure.workDir / "visited.s15keys", 4);

    SANPAO15_REQUIRE(stats.truncated);
    SANPAO15_REQUIRE(!result.dryRun);
    SANPAO15_REQUIRE(partitioned.snapshots.size() >= 3);
    SANPAO15_REQUIRE(partitioned.bucketCount == 4);
    SANPAO15_REQUIRE(visitedReader.keyCount() == stats.finalStates);
    SANPAO15_REQUIRE(!visited.keys.empty());
    SANPAO15_REQUIRE(visitedReader.contains(visited.keys.front()));
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(closureCheckpointMigrationIncludesMidIterationSnapshots) {
    const std::filesystem::path dir = tempDir("sanpao15-closure-migrate-mid");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, keysFor(4, {0, 1}));

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxExpandedStates = 1;
    const ExternalClosureStats stats = buildLayerClosureExternal(closure);
    const ExternalClosureCheckpointInfo checkpoint = inspectClosureCheckpoint(closure.workDir, 4);

    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = closure.workDir;
    migration.outputDir = dir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    (void)migrateClosureCheckpointToPartitioned(migration);
    const PartitionedClosureCheckpointInfo partitioned =
        inspectPartitionedClosureCheckpoint(migration.outputDir, 4);

    bool sawBaseVisited = false;
    bool sawPendingCandidates = false;
    bool sawRemainingFrontier = false;
    for (const PartitionedClosureSnapshotInfo& snapshot : partitioned.snapshots) {
        sawBaseVisited = sawBaseVisited || snapshot.name == "base-visited";
        sawPendingCandidates = sawPendingCandidates || snapshot.name == "pending-candidates";
        sawRemainingFrontier = sawRemainingFrontier || snapshot.name == "remaining-frontier";
    }
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.truncated);
    SANPAO15_REQUIRE(checkpoint.checkpointKind == "mid-iteration");
    SANPAO15_REQUIRE(sawBaseVisited);
    SANPAO15_REQUIRE(sawPendingCandidates);
    SANPAO15_REQUIRE(sawRemainingFrontier);
}

SANPAO15_TEST(closureCheckpointMigrationRejectsInvalidInputs) {
    const std::filesystem::path dir = tempDir("sanpao15-closure-migrate-invalid");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxIterations = 1;
    (void)buildLayerClosureExternal(closure);

    ClosureCheckpointMigrationOptions exists;
    exists.checkpointDir = closure.workDir;
    exists.outputDir = dir / "partitioned";
    exists.expectedSoldierCount = 4;
    exists.bucketCount = 4;
    std::filesystem::create_directories(exists.outputDir);
    sanpao15::test::requireThrows(
        [&] { (void)migrateClosureCheckpointToPartitioned(exists); },
        "migration should reject existing output without overwrite");

    ClosureCheckpointMigrationOptions badMethod = exists;
    badMethod.outputDir = dir / "bad-method";
    badMethod.partitionMethod = "unknown";
    sanpao15::test::requireThrows(
        [&] { (void)migrateClosureCheckpointToPartitioned(badMethod); },
        "migration should reject unsupported partition method");

    std::filesystem::remove(closure.workDir / "frontier.s15keys");
    ClosureCheckpointMigrationOptions missing = exists;
    missing.outputDir = dir / "missing";
    sanpao15::test::requireThrows(
        [&] { (void)migrateClosureCheckpointToPartitioned(missing); },
        "migration should reject missing source keys file");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedClosureCheckpointValidatorRejectsCorruptManifest) {
    const std::filesystem::path dir = tempDir("sanpao15-closure-migrate-corrupt");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxIterations = 1;
    (void)buildLayerClosureExternal(closure);

    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = closure.workDir;
    migration.outputDir = dir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    (void)migrateClosureCheckpointToPartitioned(migration);

    std::string manifest = readText(partitionedClosureCheckpointManifestPath(migration.outputDir));
    const size_t keyCount = manifest.find("\"keyCount\":");
    SANPAO15_REQUIRE(keyCount != std::string::npos);
    const size_t numberStart = manifest.find_first_of("0123456789", keyCount);
    const size_t numberEnd = manifest.find_first_not_of("0123456789", numberStart);
    manifest.replace(numberStart, numberEnd - numberStart, "999999");
    {
        std::ofstream output(partitionedClosureCheckpointManifestPath(migration.outputDir));
        output << manifest;
    }
    sanpao15::test::requireThrows(
        [&] { (void)inspectPartitionedClosureCheckpoint(migration.outputDir, 4); },
        "partitioned closure validator should reject bad snapshot count");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedClosureResumeCompletesBoundaryIterationLikeFlatResume) {
    const std::filesystem::path flatDir = tempDir("sanpao15-partitioned-resume-flat");
    const auto flatSeed = flatDir / "seeds-04.s15seed";
    const auto flatLayer = flatDir / "layer-04.s15layer";
    const auto flatNextSeed = flatDir / "seeds-03.s15seed";
    writeSeedFile(flatSeed, 4, {packPosition(fourSoldierCapturePosition())});
    ExternalClosureOptions flatFirst;
    flatFirst.workDir = flatDir / "work";
    flatFirst.seedFile = flatSeed;
    flatFirst.outputLayerFile = flatLayer;
    flatFirst.outputNextSeedFile = flatNextSeed;
    flatFirst.soldierCount = 4;
    flatFirst.maxIterations = 1;
    flatFirst.chunkKeyLimit = 2;
    (void)buildLayerClosureExternal(flatFirst);
    ExternalClosureOptions flatSecond = flatFirst;
    flatSecond.resume = true;
    flatSecond.maxIterations = 1;
    const ExternalClosureStats flatStats = buildLayerClosureExternal(flatSecond);
    const KeysFileData flatVisited = readKeysFile(flatSecond.workDir / "visited.s15keys", 4);
    const KeysFileData flatNextSeeds = readKeysFile(flatSecond.workDir / "next-seeds.s15keys", 3);

    const std::filesystem::path partDir = tempDir("sanpao15-partitioned-resume-boundary");
    const auto partSeed = partDir / "seeds-04.s15seed";
    const auto partLayer = partDir / "layer-04.s15layer";
    const auto partNextSeed = partDir / "seeds-03.s15seed";
    writeSeedFile(partSeed, 4, {packPosition(fourSoldierCapturePosition())});
    ExternalClosureOptions partFirst;
    partFirst.workDir = partDir / "work";
    partFirst.seedFile = partSeed;
    partFirst.outputLayerFile = partLayer;
    partFirst.outputNextSeedFile = partNextSeed;
    partFirst.soldierCount = 4;
    partFirst.maxIterations = 1;
    partFirst.chunkKeyLimit = 2;
    (void)buildLayerClosureExternal(partFirst);
    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = partFirst.workDir;
    migration.outputDir = partFirst.workDir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    (void)migrateClosureCheckpointToPartitioned(migration);
    PartitionedClosureOptions resume;
    resume.layerDir = partDir;
    resume.partitionedCheckpointDir = migration.outputDir;
    resume.soldierCount = 4;
    resume.expandedBudget = 1000;
    resume.bucketCount = 4;
    resume.candidateChunkKeyLimit = 2;
    const PartitionedClosureRunStats partStats = resumePartitionedClosure(resume);
    const PartitionedClosureCheckpointInfo checkpoint =
        inspectPartitionedClosureCheckpoint(migration.outputDir, 4);
    const std::vector<uint64_t> partVisited = readPartitionedKeys(partitionedSnapshotPath(checkpoint, "visited"));
    const std::vector<uint64_t> partNextSeeds = readPartitionedKeys(partitionedSnapshotPath(checkpoint, "next-seeds"));
    std::filesystem::remove_all(flatDir);
    std::filesystem::remove_all(partDir);

    SANPAO15_REQUIRE(flatStats.iterations == 2);
    SANPAO15_REQUIRE(partStats.iterationsCompleted == 1);
    SANPAO15_REQUIRE(checkpoint.checkpointKind == "iteration-boundary");
    SANPAO15_REQUIRE(partVisited == flatVisited.keys);
    SANPAO15_REQUIRE(partNextSeeds == flatNextSeeds.keys);
}

SANPAO15_TEST(partitionedClosureResumeSupportsMidIterationBudgetCheckpoint) {
    const std::filesystem::path dir = tempDir("sanpao15-partitioned-resume-mid");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, keysFor(4, {0, 1}));

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxExpandedStates = 1;
    closure.chunkKeyLimit = 2;
    (void)buildLayerClosureExternal(closure);
    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = closure.workDir;
    migration.outputDir = closure.workDir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    (void)migrateClosureCheckpointToPartitioned(migration);

    PartitionedClosureOptions resume;
    resume.partitionedCheckpointDir = migration.outputDir;
    resume.soldierCount = 4;
    resume.expandedBudget = 1;
    resume.bucketCount = 4;
    resume.candidateChunkKeyLimit = 2;
    const PartitionedClosureRunStats stats = resumePartitionedClosure(resume);
    const PartitionedClosureCheckpointInfo checkpoint =
        inspectPartitionedClosureCheckpoint(migration.outputDir, 4);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.expandedThisRun >= 1);
    SANPAO15_REQUIRE(stats.finalCheckpointKind == "mid-iteration" || stats.finalCheckpointKind == "iteration-boundary");
    SANPAO15_REQUIRE(checkpoint.requiresTransientRuns == false);
    SANPAO15_REQUIRE(!checkpoint.snapshots.empty());
}

SANPAO15_TEST(partitionedClosureDryRunDoesNotModifyManifest) {
    const std::filesystem::path dir = tempDir("sanpao15-partitioned-resume-dry");
    const auto seed = dir / "seeds-04.s15seed";
    const auto layer = dir / "layer-04.s15layer";
    const auto nextSeed = dir / "seeds-03.s15seed";
    writeSeedFile(seed, 4, {packPosition(fourSoldierCapturePosition())});

    ExternalClosureOptions closure;
    closure.workDir = dir / "work";
    closure.seedFile = seed;
    closure.outputLayerFile = layer;
    closure.outputNextSeedFile = nextSeed;
    closure.soldierCount = 4;
    closure.maxIterations = 1;
    (void)buildLayerClosureExternal(closure);
    ClosureCheckpointMigrationOptions migration;
    migration.checkpointDir = closure.workDir;
    migration.outputDir = closure.workDir / "partitioned";
    migration.expectedSoldierCount = 4;
    migration.bucketCount = 4;
    (void)migrateClosureCheckpointToPartitioned(migration);
    const std::string before = readText(partitionedClosureCheckpointManifestPath(migration.outputDir));

    PartitionedClosureOptions resume;
    resume.partitionedCheckpointDir = migration.outputDir;
    resume.soldierCount = 4;
    resume.bucketCount = 4;
    resume.dryRun = true;
    const PartitionedClosureRunStats stats = resumePartitionedClosure(resume);
    const std::string after = readText(partitionedClosureCheckpointManifestPath(migration.outputDir));
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.dryRun);
    SANPAO15_REQUIRE(before == after);
}

SANPAO15_TEST(layeredBuildExternalClosureRecordsMaxExpandedTruncation) {
    const std::filesystem::path dir = tempDir("sanpao15-layered-external-max-expanded");
    LayeredBuildOptions options;
    options.outputDir = dir;
    options.closureBackend = LayerClosureBackend::External;
    options.useExternalSeedDedup = true;
    options.maxExpandedStates = 25;
    options.externalChunkKeyLimit = 5;
    const LayeredBuildStats stats = buildReachableLayers(options);
    const KeyListFileSummary layerSummary = validateLayerFile(layerFilePath(dir, 15), 15);
    const KeyListFileSummary seedSummary = validateSeedFile(seedFilePath(dir, 14), 14);
    const std::string manifest = [&] {
        std::ifstream input(manifestFilePath(dir));
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }();
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.truncated);
    SANPAO15_REQUIRE(stats.layers[15].closureBackend == LayerClosureBackend::External);
    SANPAO15_REQUIRE(stats.layers[15].truncationReason == LayerTruncationReason::MaxExpandedStates);
    SANPAO15_REQUIRE(stats.layers[15].expandedStates == 25);
    SANPAO15_REQUIRE(layerSummary.keyCount >= stats.layers[15].expandedStates);
    SANPAO15_REQUIRE(seedSummary.keyCount == stats.layers[15].newNextLayerSeeds);
    SANPAO15_REQUIRE(manifest.find("\"closureBackend\": \"external\"") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"truncationReason\": \"max-expanded\"") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"checkpointWritten\": true") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"checkpointDir\":") != std::string::npos);
}

SANPAO15_TEST(layeredBuildExternalClosureCanResumeClosureCheckpoint) {
    const std::filesystem::path dir = tempDir("sanpao15-layered-external-closure-resume-checkpoint");
    LayeredBuildOptions first;
    first.outputDir = dir;
    first.closureBackend = LayerClosureBackend::External;
    first.stopAfterLayer = 15;
    first.maxExpandedStates = 10;
    first.externalChunkKeyLimit = 3;
    const LayeredBuildStats firstStats = buildReachableLayers(first);
    SANPAO15_REQUIRE(firstStats.truncated);

    LayeredBuildOptions second;
    second.outputDir = dir;
    second.closureBackend = LayerClosureBackend::External;
    second.startLayer = 15;
    second.stopAfterLayer = 15;
    second.resumeClosure = true;
    second.maxExpandedStates = 10;
    second.externalChunkKeyLimit = 3;
    const LayeredBuildStats secondStats = buildReachableLayers(second);
    const std::string manifest = readText(manifestFilePath(dir));
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(secondStats.truncated);
    SANPAO15_REQUIRE(secondStats.resumed);
    SANPAO15_REQUIRE(secondStats.layers[15].resumedClosure);
    SANPAO15_REQUIRE(secondStats.layers[15].expandedStates == 20);
    SANPAO15_REQUIRE(secondStats.layers[15].initialVisitedStates == firstStats.layers[15].reachableStates);
    SANPAO15_REQUIRE(manifest.find("\"resumedClosure\": true") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"finalFrontierStates\":") != std::string::npos);
}

SANPAO15_TEST(layeredBuildExternalClosureCanResumeFromSeed) {
    const std::filesystem::path dir = tempDir("sanpao15-layered-external-resume");
    LayeredBuildOptions first;
    first.outputDir = dir;
    first.closureBackend = LayerClosureBackend::External;
    first.stopAfterLayer = 15;
    first.maxExpandedStates = 50;
    first.externalChunkKeyLimit = 7;
    const LayeredBuildStats layer15 = buildReachableLayers(first);
    SANPAO15_REQUIRE(layer15.truncated);
    SANPAO15_REQUIRE(std::filesystem::exists(seedFilePath(dir, 14)));

    LayeredBuildOptions second;
    second.outputDir = dir;
    second.closureBackend = LayerClosureBackend::External;
    second.startLayer = 14;
    second.stopAfterLayer = 14;
    second.maxExpandedStates = 10;
    second.externalChunkKeyLimit = 7;
    const LayeredBuildStats layer14 = buildReachableLayers(second);
    const KeyListFileSummary layerSummary = validateLayerFile(layerFilePath(dir, 14), 14);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(layer14.resumed);
    SANPAO15_REQUIRE(layer14.layers[14].closureBackend == LayerClosureBackend::External);
    SANPAO15_REQUIRE(layer14.layers[14].expandedStates == 10);
    SANPAO15_REQUIRE(layer14.layers[14].truncationReason == LayerTruncationReason::MaxExpandedStates);
    SANPAO15_REQUIRE(layerSummary.keyCount == layer14.layers[14].reachableStates);
}

SANPAO15_TEST(layeredBuildExternalClosureHonorsMaxStatesAndMaxIterations) {
    const std::filesystem::path statesDir = tempDir("sanpao15-layered-external-max-states");
    LayeredBuildOptions statesOptions;
    statesOptions.outputDir = statesDir;
    statesOptions.closureBackend = LayerClosureBackend::External;
    statesOptions.maxStatesPerLayer = 10;
    statesOptions.externalChunkKeyLimit = 3;
    const LayeredBuildStats statesStats = buildReachableLayers(statesOptions);
    std::filesystem::remove_all(statesDir);

    SANPAO15_REQUIRE(statesStats.truncated);
    SANPAO15_REQUIRE(statesStats.layers[15].truncationReason == LayerTruncationReason::MaxStatesPerLayer);
    SANPAO15_REQUIRE(statesStats.layers[15].reachableStates >= 10);

    const std::filesystem::path iterDir = tempDir("sanpao15-layered-external-max-iter");
    LayeredBuildOptions iterOptions;
    iterOptions.outputDir = iterDir;
    iterOptions.closureBackend = LayerClosureBackend::External;
    iterOptions.maxIterations = 1;
    iterOptions.externalChunkKeyLimit = 3;
    const LayeredBuildStats iterStats = buildReachableLayers(iterOptions);
    std::filesystem::remove_all(iterDir);

    SANPAO15_REQUIRE(iterStats.truncated);
    SANPAO15_REQUIRE(iterStats.layers[15].truncationReason == LayerTruncationReason::MaxIterations);
    SANPAO15_REQUIRE(iterStats.layers[15].iterations == 1);
}
