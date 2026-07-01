#include "test_common.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/edge_probe.h"
#include "sanpao15/layered.h"
#include "sanpao15/partitioned_keyset.h"
#include "sanpao15/position.h"
#include "sanpao15/rules.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

Position oneSoldierPosition(int cannonSquare, int soldierSquare) {
    Position pos;
    pos.cannons = setBit(0, cannonSquare);
    pos.soldiers = setBit(0, soldierSquare);
    pos.side = Side::Cannon;
    return pos;
}

PartitionedKeySetOptions partitionOptions(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    uint32_t buckets) {
    PartitionedKeySetOptions options;
    options.inputFile = input;
    options.outputDir = output;
    options.bucketCount = buckets;
    options.overwrite = true;
    return options;
}

LayerEdgeProbeStats runProbe(
    const std::filesystem::path& layerPartition,
    const std::filesystem::path& seedPartition,
    uint64_t sampleStates,
    uint32_t cacheBuckets) {
    LayerEdgeProbeOptions options;
    options.layerPartitionDir = layerPartition;
    options.nextSeedPartitionDir = seedPartition;
    options.sampleStates = sampleStates;
    options.cacheBuckets = cacheBuckets;
    return probeLayerEdges(options);
}

}  // namespace

SANPAO15_TEST(edgeProbeMatchesCompleteTinyLayerMembership) {
    const std::filesystem::path dir = tempDir("sanpao15-edge-probe-complete");
    const Position root = oneSoldierPosition(20, 0);
    const std::vector<Move> rootMoves = generateLegalMoves(root);

    std::set<uint64_t> layerKeys{packPosition(root)};
    std::set<uint64_t> seedKeys;
    uint64_t expectedSame = 0;
    uint64_t expectedCapture = 0;
    for (const Move& move : rootMoves) {
        const Position next = applyMove(root, move);
        const uint64_t nextKey = packPosition(next);
        if (popcount25(next.soldiers) == 1) {
            layerKeys.insert(nextKey);
            ++expectedSame;
        } else {
            seedKeys.insert(nextKey);
            ++expectedCapture;
        }
    }

    const std::filesystem::path layerFile = layerFilePath(dir, 1);
    const std::filesystem::path seedFile = seedFilePath(dir, 0);
    writeLayerFile(layerFile, 1, std::vector<uint64_t>(layerKeys.begin(), layerKeys.end()));
    writeSeedFile(seedFile, 0, std::vector<uint64_t>(seedKeys.begin(), seedKeys.end()));

    const std::filesystem::path layerPartition = dir / "layer-partition";
    const std::filesystem::path seedPartition = dir / "seed-partition";
    (void)buildPartitionedKeySet(partitionOptions(layerFile, layerPartition, 4));
    (void)buildPartitionedKeySet(partitionOptions(seedFile, seedPartition, 4));

    const LayerEdgeProbeStats stats = runProbe(layerPartition, seedPartition, 1, 2);
    SANPAO15_REQUIRE(stats.sampledStates == 1);
    SANPAO15_REQUIRE(stats.sameLayerGenerated == expectedSame);
    SANPAO15_REQUIRE(stats.captureGenerated == expectedCapture);
    SANPAO15_REQUIRE(stats.sameLayerMissing == 0);
    SANPAO15_REQUIRE(stats.captureMissing == 0);
    SANPAO15_REQUIRE(stats.sameLayerFound == expectedSame);
    SANPAO15_REQUIRE(stats.captureFound == expectedCapture);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(edgeProbeAllowsPartialMissingAndCacheSizesAgree) {
    const std::filesystem::path dir = tempDir("sanpao15-edge-probe-partial");
    const Position root = oneSoldierPosition(20, 0);

    const std::filesystem::path layerFile = layerFilePath(dir, 1);
    const std::filesystem::path seedFile = seedFilePath(dir, 0);
    writeLayerFile(layerFile, 1, {packPosition(root)});
    writeSeedFile(seedFile, 0, {});

    const std::filesystem::path layerPartition = dir / "layer-partition";
    const std::filesystem::path seedPartition = dir / "seed-partition";
    (void)buildPartitionedKeySet(partitionOptions(layerFile, layerPartition, 4));
    (void)buildPartitionedKeySet(partitionOptions(seedFile, seedPartition, 4));

    const LayerEdgeProbeStats cacheOne = runProbe(layerPartition, seedPartition, 1, 1);
    const LayerEdgeProbeStats cacheFour = runProbe(layerPartition, seedPartition, 1, 4);
    SANPAO15_REQUIRE(cacheOne.generatedMoves == cacheFour.generatedMoves);
    SANPAO15_REQUIRE(cacheOne.sameLayerMissing + cacheOne.captureMissing >= 1);
    SANPAO15_REQUIRE(cacheOne.sameLayerFound == cacheFour.sameLayerFound);
    SANPAO15_REQUIRE(cacheOne.captureFound == cacheFour.captureFound);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(edgeProbeRequiresNextSeedPartitionForNonZeroLayer) {
    const std::filesystem::path dir = tempDir("sanpao15-edge-probe-missing-seed");
    const std::filesystem::path layerFile = layerFilePath(dir, 1);
    writeLayerFile(layerFile, 1, {packPosition(oneSoldierPosition(20, 0))});
    const std::filesystem::path layerPartition = dir / "layer-partition";
    (void)buildPartitionedKeySet(partitionOptions(layerFile, layerPartition, 4));

    LayerEdgeProbeOptions options;
    options.layerPartitionDir = layerPartition;
    options.sampleStates = 1;
    sanpao15::test::requireThrows(
        [&] { (void)probeLayerEdges(options); },
        "missing next seed partition should be rejected for non-zero layer");
    std::filesystem::remove_all(dir);
}
