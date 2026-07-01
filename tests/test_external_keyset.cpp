#include "test_common.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "sanpao15/external_keyset.h"
#include "sanpao15/layered.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

ExternalKeySetOptions keyOptions(const std::filesystem::path& dir, uint64_t chunkLimit, bool keepTemp = false) {
    ExternalKeySetOptions options;
    options.tempDir = dir;
    options.chunkKeyLimit = chunkLimit;
    options.keepTempFiles = keepTemp;
    return options;
}

size_t countRuns(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) {
        return 0;
    }
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".s15run") {
            ++count;
        }
    }
    return count;
}

void writeBadRunFile(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output.write("BADRUN1", 7);
    output.put('\0');
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

void writeRawRunFile(const std::filesystem::path& path, const std::vector<uint64_t>& keys) {
    std::ofstream output(path, std::ios::binary);
    output.write("S15RUN1", 7);
    writeU8(output, 0);
    writeU32LE(output, 1);
    writeU64LE(output, static_cast<uint64_t>(keys.size()));
    for (uint64_t key : keys) {
        writeU64LE(output, key);
    }
}

}  // namespace

SANPAO15_TEST(externalKeySetEmptyFinishesToEmptyVector) {
    const std::filesystem::path dir = tempDir("sanpao15-keyset-empty");
    ExternalKeySetBuilder builder(keyOptions(dir, 2));
    const std::vector<uint64_t> keys = builder.finishToVector();
    const ExternalKeySetStats stats = builder.stats();
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(keys.empty());
    SANPAO15_REQUIRE(stats.addedKeys == 0);
    SANPAO15_REQUIRE(stats.uniqueKeys == 0);
    SANPAO15_REQUIRE(stats.duplicateKeys == 0);
}

SANPAO15_TEST(externalKeySetSingleChunkDeduplicates) {
    const std::filesystem::path dir = tempDir("sanpao15-keyset-single");
    ExternalKeySetBuilder builder(keyOptions(dir, 100));
    builder.addBatch({5, 3, 5, 1, 3});
    const std::vector<uint64_t> keys = builder.finishToVector();
    const ExternalKeySetStats stats = builder.stats();
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE((keys == std::vector<uint64_t>{1, 3, 5}));
    SANPAO15_REQUIRE(stats.addedKeys == 5);
    SANPAO15_REQUIRE(stats.uniqueKeys == 3);
    SANPAO15_REQUIRE(stats.duplicateKeys == 2);
}

SANPAO15_TEST(externalKeySetMultipleChunksDeduplicateWithHeapMerge) {
    const std::filesystem::path dir = tempDir("sanpao15-keyset-multi");
    ExternalKeySetBuilder builder(keyOptions(dir, 2));
    builder.addBatch({10, 1, 10, 2, 1, 3});
    const std::vector<uint64_t> keys = builder.finishToVector();
    const ExternalKeySetStats stats = builder.stats();
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE((keys == std::vector<uint64_t>{1, 2, 3, 10}));
    SANPAO15_REQUIRE(stats.chunksWritten >= 3);
    SANPAO15_REQUIRE(stats.addedKeys == 6);
    SANPAO15_REQUIRE(stats.uniqueKeys == 4);
    SANPAO15_REQUIRE(stats.duplicateKeys == 2);
}

SANPAO15_TEST(externalKeySetManyChunksMergeWithBoundedOpenRuns) {
    const std::filesystem::path dir = tempDir("sanpao15-keyset-many-runs");
    ExternalKeySetBuilder builder(keyOptions(dir, 1));
    for (uint64_t i = 0; i < 300; ++i) {
        builder.add(1000 - i);
        builder.add(i % 75);
    }
    std::vector<uint64_t> keys = builder.finishToVector();
    const ExternalKeySetStats stats = builder.stats();
    std::filesystem::remove_all(dir);

    std::vector<uint64_t> expected;
    for (uint64_t i = 0; i < 75; ++i) {
        expected.push_back(i);
    }
    for (uint64_t i = 701; i <= 1000; ++i) {
        expected.push_back(i);
    }
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());

    SANPAO15_REQUIRE(stats.chunksWritten == 600);
    SANPAO15_REQUIRE(keys == expected);
    SANPAO15_REQUIRE(stats.uniqueKeys == expected.size());
    SANPAO15_REQUIRE(countRuns(dir) == 0);
}

SANPAO15_TEST(runFileRoundtripAndRejectsBadData) {
    const std::filesystem::path dir = tempDir("sanpao15-run-file");
    const std::filesystem::path path = dir / "ok.s15run";
    writeRunFile(path, {1, 3, 5});
    const RunFileData loaded = readRunFile(path);

    SANPAO15_REQUIRE((loaded.keys == std::vector<uint64_t>{1, 3, 5}));
    sanpao15::test::requireThrows(
        [&] { writeRunFile(dir / "dup.s15run", {1, 1, 2}); },
        "duplicate run keys should be rejected");
    sanpao15::test::requireThrows(
        [&] { writeRunFile(dir / "unsorted.s15run", {2, 1}); },
        "unsorted run keys should be rejected");
    writeBadRunFile(dir / "bad.s15run");
    sanpao15::test::requireThrows(
        [&] { (void)readRunFile(dir / "bad.s15run"); },
        "bad magic should be rejected");
    writeRawRunFile(dir / "nonascending.s15run", {3, 1});
    sanpao15::test::requireThrows(
        [&] { (void)readRunFile(dir / "nonascending.s15run"); },
        "non-ascending run keys should be rejected");
    writeRawRunFile(dir / "duplicate.s15run", {1, 1, 2});
    sanpao15::test::requireThrows(
        [&] { (void)readRunFile(dir / "duplicate.s15run"); },
        "duplicate run keys should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(externalKeySetFinishToStreamIsSortedUnique) {
    const std::filesystem::path dir = tempDir("sanpao15-keyset-stream");
    ExternalKeySetBuilder builder(keyOptions(dir, 1));
    builder.addBatch({4, 4, 2, 5, 2});
    std::vector<uint64_t> streamed;
    builder.finishToStream([&](uint64_t key) { streamed.push_back(key); });
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE((streamed == std::vector<uint64_t>{2, 4, 5}));
}

SANPAO15_TEST(externalKeySetTempCleanupHonorsKeepTemp) {
    const std::filesystem::path cleanDir = tempDir("sanpao15-keyset-clean");
    {
        ExternalKeySetBuilder builder(keyOptions(cleanDir, 1, false));
        builder.addBatch({3, 2, 1});
        (void)builder.finishToVector();
    }
    SANPAO15_REQUIRE(countRuns(cleanDir) == 0);
    std::filesystem::remove_all(cleanDir);

    const std::filesystem::path keepDir = tempDir("sanpao15-keyset-keep");
    {
        ExternalKeySetBuilder builder(keyOptions(keepDir, 1, true));
        builder.addBatch({3, 2, 1});
        (void)builder.finishToVector();
    }
    SANPAO15_REQUIRE(countRuns(keepDir) > 0);
    std::filesystem::remove_all(keepDir);
}

SANPAO15_TEST(layeredExternalSeedDedupMatchesMemoryDedupForSmallLayer) {
    const std::filesystem::path memoryDir = tempDir("sanpao15-layer-memory-dedup");
    LayeredBuildOptions memoryOptions;
    memoryOptions.outputDir = memoryDir;
    memoryOptions.maxStatesPerLayer = 1000;
    memoryOptions.stopAfterLayer = 15;
    const LayeredBuildStats memoryStats = buildReachableLayers(memoryOptions);

    const std::filesystem::path externalDir = tempDir("sanpao15-layer-external-dedup");
    LayeredBuildOptions externalOptions;
    externalOptions.outputDir = externalDir;
    externalOptions.maxStatesPerLayer = 1000;
    externalOptions.stopAfterLayer = 15;
    externalOptions.useExternalSeedDedup = true;
    externalOptions.externalChunkKeyLimit = 7;
    const LayeredBuildStats externalStats = buildReachableLayers(externalOptions);

    const LayerFileData memoryLayer = readLayerFile(layerFilePath(memoryDir, 15), 15);
    const LayerFileData externalLayer = readLayerFile(layerFilePath(externalDir, 15), 15);
    const SeedFileData memorySeeds = readSeedFile(seedFilePath(memoryDir, 14), 14);
    const SeedFileData externalSeeds = readSeedFile(seedFilePath(externalDir, 14), 14);

    std::filesystem::remove_all(memoryDir);
    std::filesystem::remove_all(externalDir);

    SANPAO15_REQUIRE(memoryLayer.keys == externalLayer.keys);
    SANPAO15_REQUIRE(memorySeeds.keys == externalSeeds.keys);
    SANPAO15_REQUIRE(memoryStats.layers[15].newNextLayerSeeds == externalStats.layers[15].newNextLayerSeeds);
    SANPAO15_REQUIRE(memoryStats.layers[15].duplicateNextLayerSeeds == externalStats.layers[15].duplicateNextLayerSeeds);
    SANPAO15_REQUIRE(externalStats.layers[15].externalSeedDedup);
    SANPAO15_REQUIRE(externalStats.layers[15].externalSeedStats.chunksWritten > 0);
}
