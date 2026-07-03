#include "test_common.h"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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

std::string readBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

template <typename Fn>
void requireThrowsContaining(Fn&& fn, const std::string& expected, const char* message) {
    try {
        fn();
    } catch (const std::exception& ex) {
        SANPAO15_REQUIRE(std::string(ex.what()).find(expected) != std::string::npos);
        return;
    }
    sanpao15::test::require(false, message);
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

SANPAO15_TEST(mtdDrawMaterialThresholdStartsAtSurvivalLimit) {
    SANPAO15_REQUIRE(firstMtdDrawMaterialThreshold() == 4);
    SANPAO15_REQUIRE(mtdDrawMaterialThresholdRounds(0) == 0);
    SANPAO15_REQUIRE(mtdDrawMaterialThresholdRounds(3) == 0);
    SANPAO15_REQUIRE(mtdDrawMaterialThresholdRounds(4) == 0);
    SANPAO15_REQUIRE(mtdDrawMaterialThresholdRounds(5) == 1);
    SANPAO15_REQUIRE(mtdDrawMaterialThresholdRounds(6) == 2);
}

SANPAO15_TEST(mtdThresholdStampTreatsAssignedMaterialAsTrueAndWraps) {
    MtdThresholdStampScratch scratch(3);
    SANPAO15_REQUIRE(scratch.currentStamp() == 0);
    SANPAO15_REQUIRE(scratch.isTrue(1, true));
    SANPAO15_REQUIRE(!scratch.isTrue(1, false));

    scratch.nextRound();
    SANPAO15_REQUIRE(scratch.currentStamp() == 1);
    SANPAO15_REQUIRE(scratch.mark(1, false));
    SANPAO15_REQUIRE(scratch.isTrue(1, false));
    SANPAO15_REQUIRE(!scratch.mark(1, false));
    SANPAO15_REQUIRE(!scratch.mark(2, true));

    for (int i = 0; i < 255; ++i) {
        scratch.nextRound();
    }
    SANPAO15_REQUIRE(scratch.currentStamp() == 1);
    SANPAO15_REQUIRE(!scratch.isTrue(1, false));
    SANPAO15_REQUIRE(scratch.mark(1, false));
}

SANPAO15_TEST(mtdSolvedWdlScanCountsOutcomesAndRejectsUnknown) {
    PackedOutcomeTable2Bit table(8, Outcome::CannonWin);
    table.set(1, Outcome::SoldierWin);
    table.set(2, Outcome::Draw);
    table.set(3, Outcome::Draw);

    const MtdWdlLayerScanSummary single = scanSolvedWdlLayer(table, 0, "test", 1);
    const MtdWdlLayerScanSummary threaded = scanSolvedWdlLayer(table, 0, "test", 4);
    SANPAO15_REQUIRE(single.outcomeCounts == threaded.outcomeCounts);
    SANPAO15_REQUIRE(!threaded.hasUnknown);
    SANPAO15_REQUIRE(threaded.outcomeCounts[static_cast<size_t>(Outcome::Unknown)] == 0);
    SANPAO15_REQUIRE(threaded.outcomeCounts[static_cast<size_t>(Outcome::CannonWin)] == 5);
    SANPAO15_REQUIRE(threaded.outcomeCounts[static_cast<size_t>(Outcome::SoldierWin)] == 1);
    SANPAO15_REQUIRE(threaded.outcomeCounts[static_cast<size_t>(Outcome::Draw)] == 2);

    table.set(6, Outcome::Unknown);
    sanpao15::test::requireThrows([&] {
        (void)scanSolvedWdlLayer(table, 0, "test", 4);
    }, "solved WDL scan should reject Unknown");
}

SANPAO15_TEST(mtdDistanceWorkTracksSolvedSeparatelyFromSaturatedValue) {
    MtdDistanceWork distance(3);
    SANPAO15_REQUIRE(distance.size() == 3);
    SANPAO15_REQUIRE(distance.bytes() == 11);
    SANPAO15_REQUIRE(!distance.isSolved(0));
    sanpao15::test::requireThrows([&] {
        (void)distance.get(0);
    }, "unsolved MTD distance should throw");

    distance.set(0, 7);
    SANPAO15_REQUIRE(distance.isSolved(0));
    SANPAO15_REQUIRE(distance.get(0) == 7);

    distance.set(1, MtdSaturatedDistance);
    SANPAO15_REQUIRE(distance.isSolved(1));
    SANPAO15_REQUIRE(distance.get(1) == MtdSaturatedDistance);

    distance.markUnsolved(1);
    SANPAO15_REQUIRE(!distance.isSolved(1));
    sanpao15::test::requireThrows([&] {
        (void)distance.get(1);
    }, "markUnsolved should clear solved bit");

    distance.fillSolved(0);
    SANPAO15_REQUIRE(distance.get(0) == 0);
    SANPAO15_REQUIRE(distance.get(1) == 0);
    SANPAO15_REQUIRE(distance.get(2) == 0);
}

SANPAO15_TEST(mtdWorkArrayEntryRejectsUnsolvedButAcceptsSaturated) {
    std::vector<uint8_t> material{2, 2};
    MtdDistanceWork distance(2);
    distance.set(0, MtdSaturatedDistance);

    const MtdEntry saturated = mtdEntryFromWorkArrays(material, distance, 0);
    SANPAO15_REQUIRE(saturated.materialTarget == 2);
    SANPAO15_REQUIRE(saturated.guaranteeDistance == MtdSaturatedDistance);

    sanpao15::test::requireThrows([&] {
        (void)mtdEntryFromWorkArrays(material, distance, 1);
    }, "same-layer unsolved MTD work-array entry should throw");
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

    const MtdInspectStats stats = inspectMtdTable(path, 4);
    SANPAO15_REQUIRE(stats.threads == 4);
    SANPAO15_REQUIRE(stats.materialTargetCounts[4] == denseStateCount(4) - 2);
    SANPAO15_REQUIRE(stats.materialTargetCounts[3] == 1);
    SANPAO15_REQUIRE(stats.materialTargetCounts[2] == 1);
    SANPAO15_REQUIRE(stats.maxExactDistance == 254);
    SANPAO15_REQUIRE(stats.saturatedDistanceCount == 0);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdChunkedPayloadReadReportsTruncatedByteCounts) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-truncated-payload");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, path, StandardRulesetHash);
    const uint64_t payloadBytes = mtdPayloadBytes(denseStateCount(0));
    std::filesystem::resize_file(path, 44 + payloadBytes - 3);

    requireThrowsContaining([&] {
        (void)loadMtdTable(path, StandardRulesetHash, 0);
    }, "after 6897 of 6900 bytes", "truncated MTD payload should report byte counts");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdInspectThreadedStatsMatchSingleThread) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-threaded-inspect");
    const std::filesystem::path path = mtdLayerPath(dir, 1);
    PackedMtdTable12 table(denseStateCount(1), MtdEntry{1, 0});
    table.set(0, MtdEntry{0, 3});
    table.set(1, MtdEntry{1, MtdSaturatedDistance});
    table.set(table.size() - 1, MtdEntry{0, 7});
    saveMtdTable(table, 1, path, StandardRulesetHash);

    const MtdInspectStats single = inspectMtdTable(path, 1);
    const MtdInspectStats threaded = inspectMtdTable(path, 8);
    SANPAO15_REQUIRE(threaded.threads == 8);
    SANPAO15_REQUIRE(threaded.minMaterialTarget == single.minMaterialTarget);
    SANPAO15_REQUIRE(threaded.maxMaterialTarget == single.maxMaterialTarget);
    SANPAO15_REQUIRE(threaded.maxExactDistance == single.maxExactDistance);
    SANPAO15_REQUIRE(threaded.saturatedDistanceCount == single.saturatedDistanceCount);
    SANPAO15_REQUIRE(threaded.materialTargetCounts == single.materialTargetCounts);
    SANPAO15_REQUIRE(threaded.cannonMaxCapturesCounts == single.cannonMaxCapturesCounts);
    SANPAO15_REQUIRE(threaded.distanceCounts == single.distanceCounts);
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
    MtdDistanceWork distance(stateCount);
    distance.fillSolved(0);
    distance.set(0, 7);
    distance.set(1, MtdSaturatedDistance);

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

SANPAO15_TEST(mtdArrayWriterIsByteIdenticalToPackedWriter) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-writer-identical");
    const std::filesystem::path packedPath = dir / "packed.s15mtd";
    const std::filesystem::path arrayPath = dir / "array.s15mtd";
    const uint64_t stateCount = denseStateCount(0);
    PackedMtdTable12 table(stateCount, MtdEntry{0, 0});
    table.set(0, MtdEntry{0, 7});
    table.set(1, MtdEntry{0, MtdSaturatedDistance});
    table.set(stateCount - 1, MtdEntry{0, 11});

    std::vector<uint8_t> material(static_cast<size_t>(stateCount), 0);
    MtdDistanceWork distance(stateCount);
    distance.fillSolved(0);
    distance.set(0, 7);
    distance.set(1, MtdSaturatedDistance);
    distance.set(stateCount - 1, 11);

    saveMtdTable(table, 0, packedPath, StandardRulesetHash);
    const MtdLayerWriteStats stats = writeMtdTableFromArrays(arrayPath, 0, material, distance, StandardRulesetHash);
    SANPAO15_REQUIRE(readBinaryFile(packedPath) == readBinaryFile(arrayPath));
    SANPAO15_REQUIRE(stats.stateCount == stateCount);
    SANPAO15_REQUIRE(stats.distanceCounts[7] == 1);
    SANPAO15_REQUIRE(stats.distanceCounts[11] == 1);
    SANPAO15_REQUIRE(stats.saturatedDistanceCount == 1);
    SANPAO15_REQUIRE(stats.maxExactDistance == 11);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(mtdStreamingWriterRejectsUnassignedMaterial) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-streaming-invalid");
    const std::filesystem::path path = mtdLayerPath(dir, 0);
    const uint64_t stateCount = denseStateCount(0);
    std::vector<uint8_t> material(static_cast<size_t>(stateCount), 0);
    MtdDistanceWork distance(stateCount);
    distance.fillSolved(0);
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
    MtdDistanceWork distance(stateCount);
    distance.fillSolved(0);
    distance.markUnsolved(0);
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

SANPAO15_TEST(mtdBaseLayerThreadedSolveWritesThreadMetadataAndVerifies) {
    const std::filesystem::path dir = tempDir("sanpao15-mtd-threaded-base-solve");
    const std::filesystem::path wdlDir = dir / "wdl";
    const std::filesystem::path mtdDir = dir / "mtd";
    std::filesystem::create_directories(wdlDir);
    std::filesystem::create_directories(mtdDir);

    PackedOutcomeTable2Bit wdl(denseStateCount(0), Outcome::CannonWin);
    saveDenseResultTable2Bit(wdl, 0, lowKLayerResultPath(wdlDir, 0), StandardRulesetHash);

    MtdLayerSolveOptions solveOptions;
    solveOptions.soldierCount = 0;
    solveOptions.wdlDir = wdlDir;
    solveOptions.mtdDir = mtdDir;
    solveOptions.overwrite = true;
    solveOptions.threads = 8;
    const MtdLayerSolveResult solve = solveMtdLayer(solveOptions);
    SANPAO15_REQUIRE(solve.threads == 8);
    SANPAO15_REQUIRE(solve.outcomeCounts[static_cast<size_t>(Outcome::CannonWin)] == denseStateCount(0));
    SANPAO15_REQUIRE(solve.materialTargetCounts[0] == denseStateCount(0));
    SANPAO15_REQUIRE(solve.distanceCounts[0] == denseStateCount(0));

    std::string json;
    {
        std::ifstream statsJson(solve.statsPath);
        json.assign(
            std::istreambuf_iterator<char>(statsJson),
            std::istreambuf_iterator<char>());
    }
    SANPAO15_REQUIRE(json.find("\"threads\": 8") != std::string::npos);
    SANPAO15_REQUIRE(json.find("\"wdlSolvedOutcomeScan\"") != std::string::npos);

    MtdLayerVerifyOptions verifyOptions;
    verifyOptions.soldierCount = 0;
    verifyOptions.wdlDir = wdlDir;
    verifyOptions.mtdDir = mtdDir;
    verifyOptions.sampleLimit = 0;
    verifyOptions.threads = 8;
    const MtdLayerVerifyResult verify = verifyMtdLayer(verifyOptions);
    SANPAO15_REQUIRE(verify.threads == 8);
    SANPAO15_REQUIRE(verify.sampledStates == denseStateCount(0));
    SANPAO15_REQUIRE(verify.materialTargetKDistanceZero == denseStateCount(0));
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
