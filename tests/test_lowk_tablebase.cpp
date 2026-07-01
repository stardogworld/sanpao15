#include "test_common.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/lowk_tablebase.h"
#include "sanpao15/notation.h"
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

uint32_t maskOf(std::initializer_list<int> bits) {
    uint32_t mask = 0;
    for (int bit : bits) {
        mask = setBit(mask, bit);
    }
    return mask;
}

Position captureOneSoldierPosition() {
    Position pos;
    pos.cannons = maskOf({20, 21, 22});
    pos.soldiers = maskOf({10});
    pos.side = Side::Cannon;
    return pos;
}

uint64_t nextRandom(uint64_t& state) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::vector<uint64_t> scanSampleIndexes(int soldierCount) {
    const uint64_t count = denseStateCount(soldierCount);
    std::vector<uint64_t> indexes{0, count / 2, count - 1};
    uint64_t rng = 0x5343414E4C4F574Bull ^ static_cast<uint64_t>(soldierCount);
    for (int sample = 0; sample < 16; ++sample) {
        indexes.push_back(nextRandom(rng) % count);
    }
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    return indexes;
}

DenseStreamingInitScan referenceStreamingInitScan(
    int soldierCount,
    uint64_t index,
    const Position& pos,
    const PackedOutcomeTable2Bit* lowerLayer) {
    DenseStreamingInitScan scan;
    std::vector<DenseSuccessor> successors;
    generateDenseSuccessorsFromPosition(soldierCount, index, pos, successors);
    scan.terminal = terminalOutcomeForPositionWithSuccessors(pos, successors);
    if (scan.terminal.terminal) {
        return scan;
    }

    for (const DenseSuccessor& successor : successors) {
        ++scan.successorCount;
        if (successor.kind == DenseSuccessorKind::SameLayer) {
            ++scan.sameLayerEdges;
            if (!scan.resolved) {
                ++scan.remainingCount;
            }
            continue;
        }

        ++scan.captureEdges;
        const Outcome child = lowerLayer->get(successor.toIndex);
        if (child == winFor(pos.side) && !scan.resolved) {
            scan.resolved = true;
            scan.resolvedOutcome = child;
        } else if (child == Outcome::Draw && !scan.resolved) {
            ++scan.remainingCount;
        }
    }
    return scan;
}

}  // namespace

SANPAO15_TEST(lowKSolvesK0ThroughK3AsMaterialCannonWin) {
    std::vector<PackedOutcomeTable2Bit> solved;
    for (int k = 0; k <= 3; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solved[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult result = solveDenseLayerOutcome(k, lower, table);

        SANPAO15_REQUIRE(result.stateCount == denseStateCount(k));
        SANPAO15_REQUIRE(result.cannonWin == denseStateCount(k));
        SANPAO15_REQUIRE(result.soldierWin == 0);
        SANPAO15_REQUIRE(result.draw == 0);
        SANPAO15_REQUIRE(result.unknown == 0);
        SANPAO15_REQUIRE(result.terminalStates == denseStateCount(k));
        SANPAO15_REQUIRE(result.sameLayerEdges == 0);
        SANPAO15_REQUIRE(result.captureEdges == 0);
        SANPAO15_REQUIRE(table.get(0) == Outcome::CannonWin);
        SANPAO15_REQUIRE(table.get(table.size() - 1) == Outcome::CannonWin);
        solved.push_back(std::move(table));
    }
}

SANPAO15_TEST(streamingLowKSolvesK0ThroughK3AsMaterialCannonWin) {
    std::vector<PackedOutcomeTable2Bit> solved;
    for (int k = 0; k <= 3; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solved[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult result = solveDenseLayerOutcomeStreaming(k, lower, table);

        SANPAO15_REQUIRE(result.stateCount == denseStateCount(k));
        SANPAO15_REQUIRE(result.cannonWin == denseStateCount(k));
        SANPAO15_REQUIRE(result.soldierWin == 0);
        SANPAO15_REQUIRE(result.draw == 0);
        SANPAO15_REQUIRE(result.unknown == 0);
        SANPAO15_REQUIRE(result.terminalStates == denseStateCount(k));
        SANPAO15_REQUIRE(result.sameLayerEdges == 0);
        SANPAO15_REQUIRE(result.captureEdges == 0);
        SANPAO15_REQUIRE(result.resolvedByTerminal == denseStateCount(k));
        SANPAO15_REQUIRE(result.resolvedByLowerLayer == 0);
        SANPAO15_REQUIRE(result.resolvedByPropagation == 0);
        SANPAO15_REQUIRE(result.drawAfterQueue == 0);
        SANPAO15_REQUIRE(table.get(0) == Outcome::CannonWin);
        SANPAO15_REQUIRE(table.get(table.size() - 1) == Outcome::CannonWin);
        solved.push_back(std::move(table));
    }
}

SANPAO15_TEST(streamingAndGraphLowKCountsMatchForMaterialLayers) {
    std::vector<PackedOutcomeTable2Bit> graphSolved;
    std::vector<PackedOutcomeTable2Bit> streamingSolved;
    for (int k = 0; k <= 3; ++k) {
        PackedOutcomeTable2Bit graphTable(denseStateCount(k));
        const PackedOutcomeTable2Bit* graphLower =
            k == 0 ? nullptr : &graphSolved[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult graph = solveDenseLayerOutcome(k, graphLower, graphTable);

        PackedOutcomeTable2Bit streamingTable(denseStateCount(k));
        const PackedOutcomeTable2Bit* streamingLower =
            k == 0 ? nullptr : &streamingSolved[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult streaming = solveDenseLayerOutcomeStreaming(k, streamingLower, streamingTable);

        SANPAO15_REQUIRE(streaming.cannonWin == graph.cannonWin);
        SANPAO15_REQUIRE(streaming.soldierWin == graph.soldierWin);
        SANPAO15_REQUIRE(streaming.draw == graph.draw);
        SANPAO15_REQUIRE(streaming.unknown == graph.unknown);
        SANPAO15_REQUIRE(streaming.terminalStates == graph.terminalStates);
        SANPAO15_REQUIRE(streaming.sameLayerEdges == graph.sameLayerEdges);
        SANPAO15_REQUIRE(streaming.captureEdges == graph.captureEdges);
        graphSolved.push_back(std::move(graphTable));
        streamingSolved.push_back(std::move(streamingTable));
    }
}

SANPAO15_TEST(lowKSolverResetsPrefilledOutputTable) {
    PackedOutcomeTable2Bit table(denseStateCount(0), Outcome::SoldierWin);
    const DenseLayerSolveResult result = solveDenseLayerOutcome(0, nullptr, table);

    SANPAO15_REQUIRE(result.cannonWin == denseStateCount(0));
    SANPAO15_REQUIRE(result.soldierWin == 0);
    SANPAO15_REQUIRE(table.get(0) == Outcome::CannonWin);
    SANPAO15_REQUIRE(table.get(table.size() - 1) == Outcome::CannonWin);
}

SANPAO15_TEST(streamingLowKSolverResetsPrefilledOutputTable) {
    PackedOutcomeTable2Bit table(denseStateCount(0), Outcome::SoldierWin);
    const DenseLayerSolveResult result = solveDenseLayerOutcomeStreaming(0, nullptr, table);

    SANPAO15_REQUIRE(result.cannonWin == denseStateCount(0));
    SANPAO15_REQUIRE(result.soldierWin == 0);
    SANPAO15_REQUIRE(table.get(0) == Outcome::CannonWin);
    SANPAO15_REQUIRE(table.get(table.size() - 1) == Outcome::CannonWin);
}

SANPAO15_TEST(streamingRemainingCountUsesUint8Guard) {
    SANPAO15_REQUIRE(checkedStreamingRemainingCount(0) == 0);
    SANPAO15_REQUIRE(checkedStreamingRemainingCount(255) == 255);
    sanpao15::test::requireThrows([] {
        (void)checkedStreamingRemainingCount(256);
    }, "streaming remaining counter rejects values above uint8_t");
}

SANPAO15_TEST(streamingInitializationScanMatchesSuccessorReference) {
    for (int soldierCount = 0; soldierCount <= 4; ++soldierCount) {
        PackedOutcomeTable2Bit lower = soldierCount == 0
            ? PackedOutcomeTable2Bit(0)
            : PackedOutcomeTable2Bit(denseStateCount(soldierCount - 1), Outcome::CannonWin);
        const PackedOutcomeTable2Bit* lowerLayer = soldierCount == 0 ? nullptr : &lower;
        for (uint64_t index : scanSampleIndexes(soldierCount)) {
            const Position pos = positionFromDenseIndex(soldierCount, index);
            const DenseStreamingInitScan expected =
                referenceStreamingInitScan(soldierCount, index, pos, lowerLayer);
            const DenseStreamingInitScan actual =
                scanDenseStateForStreamingInitialization(soldierCount, index, pos, lowerLayer);
            SANPAO15_REQUIRE(actual.terminal.terminal == expected.terminal.terminal);
            SANPAO15_REQUIRE(actual.terminal.outcome == expected.terminal.outcome);
            SANPAO15_REQUIRE(actual.successorCount == expected.successorCount);
            SANPAO15_REQUIRE(actual.sameLayerEdges == expected.sameLayerEdges);
            SANPAO15_REQUIRE(actual.captureEdges == expected.captureEdges);
            SANPAO15_REQUIRE(actual.remainingCount == expected.remainingCount);
            SANPAO15_REQUIRE(actual.resolved == expected.resolved);
            SANPAO15_REQUIRE(actual.resolvedOutcome == expected.resolvedOutcome);
        }
    }
}

SANPAO15_TEST(lowKTerminalPriorityKeepsK0CannonWinDespiteLegalMoves) {
    const Position pos = parsePositionNotation("CCC../...../...../...../..... c");
    const uint64_t index = denseIndex(pos);
    SANPAO15_REQUIRE(!generateDenseSuccessors(0, index).empty());

    PackedOutcomeTable2Bit table(denseStateCount(0));
    (void)solveDenseLayerOutcome(0, nullptr, table);
    SANPAO15_REQUIRE(table.get(index) == Outcome::CannonWin);
}

SANPAO15_TEST(lowKMaterialRulePreemptsCaptureLookup) {
    PackedOutcomeTable2Bit k0(denseStateCount(0), Outcome::SoldierWin);
    PackedOutcomeTable2Bit k1(denseStateCount(1));

    const Position pos = captureOneSoldierPosition();
    const uint64_t index = denseIndex(pos);
    bool sawCapture = false;
    for (const DenseSuccessor& successor : generateDenseSuccessors(1, index)) {
        if (successor.kind == DenseSuccessorKind::CaptureToLowerLayer) {
            sawCapture = true;
        }
    }
    SANPAO15_REQUIRE(sawCapture);
    const DenseLayerSolveResult result = solveDenseLayerOutcome(1, &k0, k1);
    SANPAO15_REQUIRE(result.captureEdges == 0);
    SANPAO15_REQUIRE(k1.get(index) == Outcome::CannonWin);
}

SANPAO15_TEST(streamingMaterialRulePreemptsCaptureLookup) {
    PackedOutcomeTable2Bit k0(denseStateCount(0), Outcome::SoldierWin);
    PackedOutcomeTable2Bit k1(denseStateCount(1));

    const Position pos = captureOneSoldierPosition();
    const uint64_t index = denseIndex(pos);
    const DenseLayerSolveResult result = solveDenseLayerOutcomeStreaming(1, &k0, k1);
    SANPAO15_REQUIRE(result.captureEdges == 0);
    SANPAO15_REQUIRE(k1.get(index) == Outcome::CannonWin);
}

SANPAO15_TEST(lowKFileSolveAndVerifyRoundtrip) {
    const std::filesystem::path dir = tempDir("sanpao15-lowk-roundtrip");
    LowKTablebaseSolveOptions options;
    options.maxK = 3;
    options.outputDir = dir;
    options.encoding = DenseResultEncoding::Packed2Bit;

    const std::vector<LowKTablebaseLayerResult> solved = solveLowKTablebase(options);
    SANPAO15_REQUIRE(solved.size() == 4);
    SANPAO15_REQUIRE(std::filesystem::exists(lowKLayerResultPath(dir, 3)));
    SANPAO15_REQUIRE(validateDenseResultFile(lowKLayerResultPath(dir, 3), StandardRulesetHash, 3).encoding ==
                     DenseResultEncoding::Packed2Bit);

    const LowKTablebaseVerifyResult verified = verifyLowKTablebase(dir, 3, 1000);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(verified.layers.size() == 4);
    for (int k = 0; k <= 3; ++k) {
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].unknown == 0);
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].cannonWin == denseStateCount(k));
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].soldierWin == 0);
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].draw == 0);
    }
}

SANPAO15_TEST(streamingLowKFileSolveAndVerifyRoundtrip) {
    const std::filesystem::path dir = tempDir("sanpao15-streaming-lowk-roundtrip");
    LowKTablebaseSolveOptions options;
    options.maxK = 3;
    options.outputDir = dir;
    options.encoding = DenseResultEncoding::Packed2Bit;

    const std::vector<LowKTablebaseLayerResult> solved = solveLowKTablebaseStreaming(options);
    SANPAO15_REQUIRE(solved.size() == 4);
    SANPAO15_REQUIRE(std::filesystem::exists(lowKLayerResultPath(dir, 3)));
    SANPAO15_REQUIRE(validateDenseResultFile(lowKLayerResultPath(dir, 3), StandardRulesetHash, 3).encoding ==
                     DenseResultEncoding::Packed2Bit);

    const LowKTablebaseVerifyResult verified = verifyLowKTablebase(dir, 3, 1000);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(verified.layers.size() == 4);
    for (int k = 0; k <= 3; ++k) {
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].unknown == 0);
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].cannonWin == denseStateCount(k));
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].soldierWin == 0);
        SANPAO15_REQUIRE(verified.layers[static_cast<size_t>(k)].draw == 0);
    }
}

SANPAO15_TEST(productionSolveMaterialLayersWritesResultsAndStats) {
    const std::filesystem::path dir = tempDir("sanpao15-production-material");
    for (int k = 0; k <= 3; ++k) {
        DenseLayerProductionSolveOptions options;
        options.soldierCount = k;
        options.outputResultPath = dir / ("layer-" + std::to_string(k) + ".s15res");
        options.encoding = DenseResultEncoding::Packed2Bit;

        const DenseLayerProductionSolveResult solved = solveDenseLayerProduction(options);
        SANPAO15_REQUIRE(solved.solve.stateCount == denseStateCount(k));
        SANPAO15_REQUIRE(solved.solve.cannonWin == denseStateCount(k));
        SANPAO15_REQUIRE(solved.solve.soldierWin == 0);
        SANPAO15_REQUIRE(solved.solve.draw == 0);
        SANPAO15_REQUIRE(solved.solve.unknown == 0);
        SANPAO15_REQUIRE(std::filesystem::exists(solved.outputResultPath));
        SANPAO15_REQUIRE(std::filesystem::exists(solved.statsJsonPath));

        const std::string json = readTextFile(solved.statsJsonPath);
        SANPAO15_REQUIRE(json.find("\"format\": \"sanpao15-layer-solve-stats\"") != std::string::npos);
        SANPAO15_REQUIRE(json.find("\"rulesetHash\": \"0x5331355F76325F04\"") != std::string::npos);
        SANPAO15_REQUIRE(json.find("\"soldierCount\": " + std::to_string(k)) != std::string::npos);
        SANPAO15_REQUIRE(json.find("\"stateCount\": " + std::to_string(denseStateCount(k))) != std::string::npos);

        DenseLayerVerifyOptions verifyOptions;
        verifyOptions.resultPath = solved.outputResultPath;
        verifyOptions.sampleLimit = 100;
        const DenseLayerVerifyResult verified = verifyDenseLayerResult(verifyOptions);
        SANPAO15_REQUIRE(verified.soldierCount == k);
        SANPAO15_REQUIRE(verified.cannonWin == denseStateCount(k));
        SANPAO15_REQUIRE(verified.unknown == 0);
    }
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(productionSolveRejectsInvalidLowerAndOverwriteCases) {
    const std::filesystem::path dir = tempDir("sanpao15-production-errors");

    DenseLayerProductionSolveOptions k2;
    k2.soldierCount = 2;
    k2.outputResultPath = dir / "layer-02.s15res";
    (void)solveDenseLayerProduction(k2);

    sanpao15::test::requireThrows([&] {
        DenseLayerProductionSolveOptions options;
        options.soldierCount = 4;
        options.outputResultPath = dir / "layer-04.s15res";
        (void)solveDenseLayerProduction(options);
    }, "k=4 production solve should require lower-res");

    sanpao15::test::requireThrows([&] {
        DenseLayerProductionSolveOptions options;
        options.soldierCount = 4;
        options.lowerResultPath = k2.outputResultPath;
        options.outputResultPath = dir / "layer-04.s15res";
        (void)solveDenseLayerProduction(options);
    }, "k=4 production solve should reject k=2 lower-res");

    sanpao15::test::requireThrows([&] {
        DenseLayerProductionSolveOptions options;
        options.soldierCount = 3;
        options.lowerResultPath = k2.outputResultPath;
        options.outputResultPath = dir / "layer-03.s15res";
        (void)solveDenseLayerProduction(options);
    }, "material production layers should reject lower-res");

    sanpao15::test::requireThrows([&] {
        DenseLayerProductionSolveOptions options;
        options.soldierCount = 2;
        options.outputResultPath = k2.outputResultPath;
        (void)solveDenseLayerProduction(options);
    }, "production solve should refuse to overwrite output without overwrite=true");

    DenseLayerProductionSolveOptions overwrite;
    overwrite.soldierCount = 2;
    overwrite.outputResultPath = k2.outputResultPath;
    overwrite.overwrite = true;
    (void)solveDenseLayerProduction(overwrite);

    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(singleLayerVerifierRejectsMissingAndWrongLower) {
    const std::filesystem::path dir = tempDir("sanpao15-production-verify-errors");

    DenseLayerProductionSolveOptions k2;
    k2.soldierCount = 2;
    k2.outputResultPath = dir / "layer-02.s15res";
    (void)solveDenseLayerProduction(k2);

    DenseLayerProductionSolveOptions k3;
    k3.soldierCount = 3;
    k3.outputResultPath = dir / "layer-03.s15res";
    (void)solveDenseLayerProduction(k3);

    PackedOutcomeTable2Bit k4Table(denseStateCount(4), Outcome::CannonWin);
    const std::filesystem::path k4Path = dir / "layer-04-fake.s15res";
    saveDenseResultTable2Bit(k4Table, 4, k4Path, StandardRulesetHash);

    sanpao15::test::requireThrows([&] {
        DenseLayerVerifyOptions options;
        options.resultPath = k4Path;
        options.sampleLimit = 1;
        (void)verifyDenseLayerResult(options);
    }, "verify-layer k>=4 should require lower-res");

    sanpao15::test::requireThrows([&] {
        DenseLayerVerifyOptions options;
        options.resultPath = k4Path;
        options.lowerResultPath = k2.outputResultPath;
        options.sampleLimit = 1;
        (void)verifyDenseLayerResult(options);
    }, "verify-layer should reject wrong lower soldierCount");

    sanpao15::test::requireThrows([&] {
        DenseLayerVerifyOptions options;
        options.resultPath = k3.outputResultPath;
        options.lowerResultPath = k2.outputResultPath;
        options.sampleLimit = 1;
        (void)verifyDenseLayerResult(options);
    }, "verify-layer material layer should reject lower-res");

    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(productionDenseIndex32Safety) {
    SANPAO15_REQUIRE(checkedDenseIndex32(0) == 0);
    SANPAO15_REQUIRE(checkedDenseIndex32(std::numeric_limits<uint32_t>::max()) ==
                     std::numeric_limits<uint32_t>::max());
    sanpao15::test::requireThrows([] {
        (void)checkedDenseIndex32(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1u);
    }, "checkedDenseIndex32 should reject values above uint32_t max");

    for (int k = 0; k <= 15; ++k) {
        SANPAO15_REQUIRE(denseStateCount(k) <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()));
    }
}

SANPAO15_TEST(lowKResultUsesUpdatedRulesetHash) {
    constexpr uint64_t OldRulesetHash = 0x5331355F76315F01ull;
    SANPAO15_REQUIRE(StandardRulesetHash != OldRulesetHash);

    const std::filesystem::path dir = tempDir("sanpao15-lowk-ruleset-hash");
    LowKTablebaseSolveOptions options;
    options.maxK = 0;
    options.outputDir = dir;
    options.encoding = DenseResultEncoding::Packed2Bit;
    (void)solveLowKTablebase(options);

    const std::filesystem::path path = lowKLayerResultPath(dir, 0);
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, 0);
    SANPAO15_REQUIRE(info.rulesetHash == StandardRulesetHash);
    sanpao15::test::requireThrows([&] {
        (void)validateDenseResultFile(path, OldRulesetHash, 0);
    }, "old ruleset hash should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(lowKRejectsKAbovePrototypeLimit) {
    sanpao15::test::requireThrows([] {
        LowKTablebaseSolveOptions options;
        options.maxK = 4;
        options.outputDir = std::filesystem::temp_directory_path() / "sanpao15-lowk-too-large";
        (void)solveLowKTablebase(options);
    }, "K above prototype limit should be rejected");
}

SANPAO15_TEST(streamingLowKRejectsKAbovePrototypeLimit) {
    sanpao15::test::requireThrows([] {
        LowKTablebaseSolveOptions options;
        options.maxK = 5;
        options.outputDir = std::filesystem::temp_directory_path() / "sanpao15-streaming-lowk-too-large";
        (void)solveLowKTablebaseStreaming(options);
    }, "K above streaming prototype limit should be rejected");
}
