#include "test_common.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/layered.h"
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

Position oneSoldierCapturePosition() {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.soldiers = setBit(0, 10);
    pos.side = Side::Cannon;
    return pos;
}

Position zeroSoldierPosition() {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.side = Side::Cannon;
    return pos;
}

bool containsKey(const std::vector<uint64_t>& keys, uint64_t key) {
    return std::binary_search(keys.begin(), keys.end(), key);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
}

void writeU32LE(std::ostream& output, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void writeU64LE(std::ostream& output, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void writeBadUnsortedLayerFile(const std::filesystem::path& path, int soldierCount, uint64_t highKey, uint64_t lowKey) {
    std::ofstream output(path, std::ios::binary);
    output.write("S15LYR1", 7);
    writeU8(output, 0);
    writeU32LE(output, 1);
    writeU32LE(output, static_cast<uint32_t>(soldierCount));
    writeU64LE(output, 2);
    writeU64LE(output, StandardRulesetHash);
    writeU64LE(output, highKey);
    writeU64LE(output, lowKey);
}

void writeBadUnsortedSeedFile(const std::filesystem::path& path, int soldierCount, uint64_t highKey, uint64_t lowKey) {
    std::ofstream output(path, std::ios::binary);
    output.write("S15SED1", 7);
    writeU8(output, 0);
    writeU32LE(output, 1);
    writeU32LE(output, static_cast<uint32_t>(soldierCount));
    writeU64LE(output, 2);
    writeU64LE(output, StandardRulesetHash);
    writeU64LE(output, highKey);
    writeU64LE(output, lowKey);
}

}  // namespace

SANPAO15_TEST(layerFileRoundtripSortsAndDeduplicatesKeys) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-roundtrip");
    const Position a = zeroSoldierPosition();
    Position b = zeroSoldierPosition();
    b.cannons = setBit(0, 21);

    std::vector<uint64_t> keys{packPosition(b), packPosition(a), packPosition(a)};
    writeLayerFile(layerFilePath(dir, 0), 0, keys);
    const LayerFileData loaded = readLayerFile(layerFilePath(dir, 0), 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(loaded.soldierCount == 0);
    SANPAO15_REQUIRE(loaded.keys.size() == 2);
    SANPAO15_REQUIRE(std::is_sorted(loaded.keys.begin(), loaded.keys.end()));
    SANPAO15_REQUIRE(containsKey(loaded.keys, packPosition(a)));
    SANPAO15_REQUIRE(containsKey(loaded.keys, packPosition(b)));
}

SANPAO15_TEST(layerFileRejectsBadData) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-bad");
    const Position zero = zeroSoldierPosition();
    Position anotherZero = zeroSoldierPosition();
    anotherZero.cannons = setBit(0, 21);
    const uint64_t low = std::min(packPosition(zero), packPosition(anotherZero));
    const uint64_t high = std::max(packPosition(zero), packPosition(anotherZero));

    writeBadUnsortedLayerFile(layerFilePath(dir, 0), 0, high, low);
    sanpao15::test::requireThrows(
        [&] { (void)readLayerFile(layerFilePath(dir, 0), 0); },
        "unsorted layer keys should be rejected");

    sanpao15::test::requireThrows(
        [&] { writeLayerFile(layerFilePath(dir, 1), 1, {packPosition(zero)}); },
        "soldier-count mismatched keys should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(seedFileRoundtripSortsAndDeduplicatesKeys) {
    const std::filesystem::path dir = tempDir("sanpao15-seed-roundtrip");
    const Position a = zeroSoldierPosition();
    Position b = zeroSoldierPosition();
    b.cannons = setBit(0, 21);

    std::vector<uint64_t> keys{packPosition(b), packPosition(a), packPosition(a)};
    writeSeedFile(seedFilePath(dir, 0), 0, keys);
    const SeedFileData loaded = readSeedFile(seedFilePath(dir, 0), 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(loaded.soldierCount == 0);
    SANPAO15_REQUIRE(loaded.keys.size() == 2);
    SANPAO15_REQUIRE(std::is_sorted(loaded.keys.begin(), loaded.keys.end()));
    SANPAO15_REQUIRE(containsKey(loaded.keys, packPosition(a)));
    SANPAO15_REQUIRE(containsKey(loaded.keys, packPosition(b)));
}

SANPAO15_TEST(seedFileRejectsBadData) {
    const std::filesystem::path dir = tempDir("sanpao15-seed-bad");
    const Position zero = zeroSoldierPosition();
    Position anotherZero = zeroSoldierPosition();
    anotherZero.cannons = setBit(0, 21);
    const uint64_t low = std::min(packPosition(zero), packPosition(anotherZero));
    const uint64_t high = std::max(packPosition(zero), packPosition(anotherZero));

    writeBadUnsortedSeedFile(seedFilePath(dir, 0), 0, high, low);
    sanpao15::test::requireThrows(
        [&] { (void)readSeedFile(seedFilePath(dir, 0), 0); },
        "unsorted seed keys should be rejected");

    sanpao15::test::requireThrows(
        [&] { writeSeedFile(seedFilePath(dir, 1), 1, {packPosition(zero)}); },
        "soldier-count mismatched seed keys should be rejected");
    writeLayerFile(layerFilePath(dir, 0), 0, {packPosition(zero)});
    sanpao15::test::requireThrows(
        [&] { (void)readSeedFile(layerFilePath(dir, 0), 0); },
        "wrong magic should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(layeredBuildFromZeroSoldierSeedWritesCompleteZeroLayer) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-zero");
    LayeredBuildOptions options;
    options.outputDir = dir;
    const LayeredBuildStats stats = buildReachableLayersFromSeed(zeroSoldierPosition(), options);
    const LayerFileData layer = readLayerFile(layerFilePath(dir, 0), 0);
    const SeedFileData seed = readSeedFile(seedFilePath(dir, 0), 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(!stats.truncated);
    SANPAO15_REQUIRE(seed.keys.size() == 1);
    SANPAO15_REQUIRE(stats.layers[0].seedStates == 1);
    SANPAO15_REQUIRE(stats.layers[0].reachableStates == 1);
    SANPAO15_REQUIRE(stats.layers[0].generatedSameLayerEdges == 0);
    SANPAO15_REQUIRE(stats.layers[0].generatedCaptureEdges == 0);
    SANPAO15_REQUIRE(layer.keys.size() == 1);
    SANPAO15_REQUIRE(layer.keys[0] == packPosition(zeroSoldierPosition()));
}

SANPAO15_TEST(layeredBuildFromOneSoldierSeedCreatesZeroLayerSeed) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-one");
    LayeredBuildOptions options;
    options.outputDir = dir;
    const LayeredBuildStats stats = buildReachableLayersFromSeed(oneSoldierCapturePosition(), options);
    const LayerFileData oneLayer = readLayerFile(layerFilePath(dir, 1), 1);
    const LayerFileData zeroLayer = readLayerFile(layerFilePath(dir, 0), 0);
    const SeedFileData zeroSeeds = readSeedFile(seedFilePath(dir, 0), 0);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(!stats.truncated);
    SANPAO15_REQUIRE(stats.layers[1].seedStates == 1);
    SANPAO15_REQUIRE(stats.layers[1].generatedCaptureEdges > 0);
    SANPAO15_REQUIRE(stats.layers[1].newNextLayerSeeds > 0);
    SANPAO15_REQUIRE(!oneLayer.keys.empty());
    SANPAO15_REQUIRE(!zeroLayer.keys.empty());
    SANPAO15_REQUIRE(!zeroSeeds.keys.empty());
    for (uint64_t key : oneLayer.keys) {
        SANPAO15_REQUIRE(popcount25(unpackPosition(key).soldiers) == 1);
    }
    for (uint64_t key : zeroLayer.keys) {
        SANPAO15_REQUIRE(popcount25(unpackPosition(key).soldiers) == 0);
    }
}

SANPAO15_TEST(layeredBuildInitialLayerContainsInitialPosition) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-initial");
    LayeredBuildOptions options;
    options.outputDir = dir;
    options.maxStatesPerLayer = 100;
    const LayeredBuildStats stats = buildReachableLayers(options);
    const LayerFileData layer15 = readLayerFile(layerFilePath(dir, 15), 15);
    const SeedFileData seeds15 = readSeedFile(seedFilePath(dir, 15), 15);
    const SeedFileData seeds14 = readSeedFile(seedFilePath(dir, 14), 14);
    const std::string manifest = readText(manifestFilePath(dir));
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(stats.truncated);
    SANPAO15_REQUIRE(stats.layers[15].truncated);
    SANPAO15_REQUIRE(stats.layers[14].skipped);
    SANPAO15_REQUIRE(!std::filesystem::exists(layerFilePath(dir, 14)));
    SANPAO15_REQUIRE(seeds15.keys.size() == 1);
    SANPAO15_REQUIRE(!seeds14.keys.empty());
    SANPAO15_REQUIRE(manifest.find("\"soldierCount\": 15") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"truncated\": true") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"skipped\": true") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("layer-15.s15layer") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("seeds-14.s15seed") != std::string::npos);
    SANPAO15_REQUIRE(containsKey(layer15.keys, packPosition(initialPosition())));
    for (uint64_t key : layer15.keys) {
        SANPAO15_REQUIRE(popcount25(unpackPosition(key).soldiers) == 15);
    }
}

SANPAO15_TEST(layeredBuildCanResumeFromPersistedSeedFile) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-resume");
    LayeredBuildOptions first;
    first.outputDir = dir;
    first.maxStatesPerLayer = 100;
    first.stopAfterLayer = 15;
    const LayeredBuildStats layer15 = buildReachableLayers(first);
    SANPAO15_REQUIRE(layer15.truncated);
    SANPAO15_REQUIRE(std::filesystem::exists(seedFilePath(dir, 14)));

    LayeredBuildOptions second;
    second.outputDir = dir;
    second.startLayer = 14;
    second.stopAfterLayer = 14;
    second.maxStatesPerLayer = 50;
    const LayeredBuildStats layer14 = buildReachableLayers(second);
    const std::string manifest = readText(manifestFilePath(dir));
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(layer14.resumed);
    SANPAO15_REQUIRE(layer14.layers[14].seedStates > 0);
    SANPAO15_REQUIRE(layer14.layers[14].reachableStates > 0);
    SANPAO15_REQUIRE(manifest.find("\"resumed\": true") != std::string::npos);
    SANPAO15_REQUIRE(manifest.find("\"startLayer\": 14") != std::string::npos);
}

SANPAO15_TEST(validateAndInspectReturnExpectedSummaries) {
    const std::filesystem::path dir = tempDir("sanpao15-layer-inspect");
    LayeredBuildOptions options;
    options.outputDir = dir;
    options.maxStatesPerLayer = 25;
    options.stopAfterLayer = 15;
    (void)buildReachableLayers(options);

    const KeyListFileSummary layerSummary = inspectLayerFile(layerFilePath(dir, 15), 15, 2);
    const KeyListFileSummary seedSummary = validateSeedFile(seedFilePath(dir, 14), 14);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(layerSummary.soldierCount == 15);
    SANPAO15_REQUIRE(layerSummary.keyCount == 25);
    SANPAO15_REQUIRE(layerSummary.firstKeys.size() == 2);
    SANPAO15_REQUIRE(layerSummary.lastKeys.size() == 2);
    SANPAO15_REQUIRE(seedSummary.soldierCount == 14);
    SANPAO15_REQUIRE(seedSummary.keyCount > 0);
}

SANPAO15_TEST(layeredMoveGenerationKeepsSoldierCountMonotone) {
    const Position pos = oneSoldierCapturePosition();
    const int currentSoldiers = popcount25(pos.soldiers);
    bool sawCapture = false;
    for (const Move& move : generateLegalMoves(pos)) {
        const Position next = applyMove(pos, move);
        const int nextSoldiers = popcount25(next.soldiers);
        SANPAO15_REQUIRE(nextSoldiers == currentSoldiers || nextSoldiers == currentSoldiers - 1);
        SANPAO15_REQUIRE(nextSoldiers <= currentSoldiers);
        if (nextSoldiers == currentSoldiers - 1) {
            sawCapture = true;
        }
    }
    SANPAO15_REQUIRE(sawCapture);
}
