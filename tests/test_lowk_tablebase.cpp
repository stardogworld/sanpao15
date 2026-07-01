#include "test_common.h"

#include <filesystem>

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
