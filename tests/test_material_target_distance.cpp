#include "test_common.h"

#include <filesystem>
#include <fstream>

#include "sanpao15/dense_index.h"
#include "sanpao15/lowk_tablebase.h"
#include "sanpao15/material_target_distance.h"
#include "sanpao15/table.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeByteAt(const std::filesystem::path& path, std::streamoff offset, uint8_t value) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(offset);
    file.put(static_cast<char>(value));
}

}  // namespace

SANPAO15_TEST(mtdEntryEncodeDecodeRoundtrip) {
    const MtdEntry entry{7, 254};
    const uint16_t encoded = encodeMtdEntry(entry);
    SANPAO15_REQUIRE(encoded == static_cast<uint16_t>(7u | (254u << 4u)));
    SANPAO15_REQUIRE(decodeMtdEntry(encoded) == entry);
    SANPAO15_REQUIRE(saturatedAdd1(0) == 1);
    SANPAO15_REQUIRE(saturatedAdd1(253) == 254);
    SANPAO15_REQUIRE(saturatedAdd1(254) == MtdSaturatedDistance);
    SANPAO15_REQUIRE(saturatedAdd1(255) == MtdSaturatedDistance);
}

SANPAO15_TEST(packedMtdTableSupportsOddStateCounts) {
    PackedMtdTable12 table(5);
    table.set(0, MtdEntry{0, 0});
    table.set(1, MtdEntry{1, 10});
    table.set(2, MtdEntry{2, 20});
    table.set(3, MtdEntry{3, 30});
    table.set(4, MtdEntry{4, 40});
    SANPAO15_REQUIRE(table.bytes() == 8);
    SANPAO15_REQUIRE(table.get(0) == (MtdEntry{0, 0}));
    SANPAO15_REQUIRE(table.get(1) == (MtdEntry{1, 10}));
    SANPAO15_REQUIRE(table.get(2) == (MtdEntry{2, 20}));
    SANPAO15_REQUIRE(table.get(3) == (MtdEntry{3, 30}));
    SANPAO15_REQUIRE(table.get(4) == (MtdEntry{4, 40}));
}

SANPAO15_TEST(mtdHeaderAndPayloadRoundtrip) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-roundtrip");
    const std::filesystem::path path = mtdLayerPath(dir, 4);
    PackedMtdTable12 table(denseStateCount(4), MtdEntry{4, 0});
    table.set(1, MtdEntry{3, 12});
    table.set(table.size() - 1, MtdEntry{2, 254});

    saveMtdTable(table, 4, path, StandardRulesetHash);
    const MtdFileInfo info = validateMtdFileFull(path, StandardRulesetHash, 4);
    SANPAO15_REQUIRE(info.version == 2);
    SANPAO15_REQUIRE(info.rulesetHash == StandardRulesetHash);
    SANPAO15_REQUIRE(info.soldierCount == 4);
    SANPAO15_REQUIRE(info.stateCount == denseStateCount(4));
    SANPAO15_REQUIRE(info.payloadBytes == mtdPayloadBytes(denseStateCount(4)));

    const PackedMtdTable12 loaded = loadMtdTable(path, StandardRulesetHash, 4);
    SANPAO15_REQUIRE(loaded.get(1) == (MtdEntry{3, 12}));
    SANPAO15_REQUIRE(loaded.get(loaded.size() - 1) == (MtdEntry{2, 254}));

    const MtdInspectStats stats = inspectMtdTable(path);
    SANPAO15_REQUIRE(stats.materialTargetCounts[4] == denseStateCount(4) - 2);
    SANPAO15_REQUIRE(stats.materialTargetCounts[3] == 1);
    SANPAO15_REQUIRE(stats.materialTargetCounts[2] == 1);
    SANPAO15_REQUIRE(stats.maxExactDistance == 254);
    SANPAO15_REQUIRE(stats.saturatedDistanceCount == 0);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdHeaderOnlyValidationDoesNotScanPayload) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-header-only");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, path, StandardRulesetHash);

    writeByteAt(path, 44, 0x01u);
    const MtdFileInfo info = validateMtdHeaderOnly(path, StandardRulesetHash, 0);
    SANPAO15_REQUIRE(info.version == 2);
    SANPAO15_REQUIRE(info.soldierCount == 0);
    sanpao15::test::requireThrows([&] {
        (void)validateMtdFileFull(path, StandardRulesetHash, 0);
    }, "full MTD validation should reject invalid payload entries");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdVersion1PrototypeFilesAreRejected) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-v1-obsolete");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, path, StandardRulesetHash);

    writeByteAt(path, 8, 1u);
    sanpao15::test::requireThrows([&] {
        (void)validateMtdFile(path, StandardRulesetHash, 0);
    }, "prototype MTD semantic version 1 should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdValidationRejectsWrongRulesetAndInvalidPayload) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-invalid");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, path, StandardRulesetHash);
    sanpao15::test::requireThrows([&] {
        (void)validateMtdFile(path, StandardRulesetHash + 1u, 0);
    }, "wrong ruleset hash should be rejected");

    writeByteAt(path, 44, 0x01u);
    sanpao15::test::requireThrows([&] {
        (void)validateMtdFile(path, StandardRulesetHash, 0);
    }, "materialTarget > k should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdStreamingWriterRoundtripAndStats) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-streaming-writer");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    const uint64_t stateCount = denseStateCount(0);
    std::vector<uint8_t> material(static_cast<size_t>(stateCount), 0);
    std::vector<uint16_t> distance(static_cast<size_t>(stateCount), 0);
    distance[0] = 7;
    distance[1] = MtdSaturatedDistance;

    const MtdLayerWriteStats stats = writeMtdTableFromArrays(path, 0, material, distance, StandardRulesetHash);
    SANPAO15_REQUIRE(stats.stateCount == stateCount);
    SANPAO15_REQUIRE(stats.outputBytes == 44 + mtdPayloadBytes(stateCount));
    SANPAO15_REQUIRE(stats.materialTargetCounts[0] == stateCount);
    SANPAO15_REQUIRE(stats.distanceCounts[7] == 1);
    SANPAO15_REQUIRE(stats.saturatedDistanceCount == 1);
    SANPAO15_REQUIRE(stats.maxExactDistance == 7);

    const PackedMtdTable12 loaded = loadMtdTable(path, StandardRulesetHash, 0);
    SANPAO15_REQUIRE(loaded.get(0) == (MtdEntry{0, 7}));
    SANPAO15_REQUIRE(loaded.get(1) == (MtdEntry{0, MtdSaturatedDistance}));
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdStreamingWriterRejectsUnassignedMaterial) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-streaming-invalid");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    const uint64_t stateCount = denseStateCount(0);
    std::vector<uint8_t> material(static_cast<size_t>(stateCount), 0);
    std::vector<uint16_t> distance(static_cast<size_t>(stateCount), 0);
    material[0] = 0xffu;
    sanpao15::test::requireThrows([&] {
        (void)writeMtdTableFromArrays(path, 0, material, distance, StandardRulesetHash);
    }, "streaming MTD writer should reject unassigned material");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdStreamingWriterRejectsUnsolvedDistance) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-streaming-unsolved-distance");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    const uint64_t stateCount = denseStateCount(0);
    std::vector<uint8_t> material(static_cast<size_t>(stateCount), 0);
    std::vector<uint16_t> distance(static_cast<size_t>(stateCount), 0);
    distance[0] = 256;
    sanpao15::test::requireThrows([&] {
        (void)writeMtdTableFromArrays(path, 0, material, distance, StandardRulesetHash);
    }, "streaming MTD writer should reject unsolved distance");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdRangeResumeUsesHeaderOnlyValidation) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-range-resume-header-only");
    const std::filesystem::path mtdDir = dir / "mtd";
    const std::filesystem::path path = mtdLayerPath(mtdDir, 0);
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, path, StandardRulesetHash);
    writeByteAt(path, 44, 0x01u);

    MtdRangeSolveOptions options;
    options.startLayer = 0;
    options.endLayer = 0;
    options.wdlDir = dir / "wdl";
    options.mtdDir = mtdDir;
    options.resume = true;
    const MtdRangeSolveResult result = solveMtdRange(options);
    SANPAO15_REQUIRE(result.layers.size() == 1);
    SANPAO15_REQUIRE(result.layers[0].soldierCount == 0);
    SANPAO15_REQUIRE(result.layers[0].stateCount == denseStateCount(0));
    SANPAO15_REQUIRE(result.layers[0].outputBytes == 44 + mtdPayloadBytes(denseStateCount(0)));
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdSolveRejectsUnknownWdl) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-unknown-wdl");
    const std::filesystem::path wdlDir = dir / "wdl";
    const std::filesystem::path mtdDir = dir / "mtd";
    std::filesystem::create_directories(wdlDir);
    std::filesystem::create_directories(mtdDir);
    createEmptyDenseResultFile(0, lowKLayerResultPath(wdlDir, 0), DenseResultEncoding::Packed2Bit, StandardRulesetHash);

    MtdLayerSolveOptions options;
    options.soldierCount = 0;
    options.wdlDir = wdlDir;
    options.mtdDir = mtdDir;
    options.overwrite = true;
    options.writeStatsJson = false;
    sanpao15::test::requireThrows([&] {
        (void)solveMtdLayer(options);
    }, "MTD solve should reject Unknown WDL outcomes");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdBaseLayersUseCurrentMaterialAndZeroDistance) {
    for (int k = 0; k <= 3; ++k) {
        PackedMtdTable12 table(denseStateCount(k), MtdEntry{static_cast<uint8_t>(k), 0});
        SANPAO15_REQUIRE(table.get(0).materialTarget == k);
        SANPAO15_REQUIRE(table.get(0).guaranteeDistance == 0);
        SANPAO15_REQUIRE(table.get(table.size() - 1).materialTarget == k);
        SANPAO15_REQUIRE(table.get(table.size() - 1).guaranteeDistance == 0);
    }
}

SANPAO15_TEST(mtdLookupMissingLayerReportsError) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-missing");
    sanpao15::test::requireThrows([&] {
        (void)lookupMtdEntryAt(dir, 0, 0);
    }, "missing MTD layer should throw");
    std::filesystem::remove_all(dir);
}
