#include "test_common.h"

#include <filesystem>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/lowk_tablebase.h"
#include "sanpao15/notation.h"
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

SANPAO15_TEST(lowKSolvesK0AsAllCannonWin) {
    PackedOutcomeTable2Bit table(denseStateCount(0));
    const DenseLayerSolveResult result = solveDenseLayerOutcome(0, nullptr, table);

    SANPAO15_REQUIRE(result.stateCount == 4600);
    SANPAO15_REQUIRE(result.cannonWin == 4600);
    SANPAO15_REQUIRE(result.soldierWin == 0);
    SANPAO15_REQUIRE(result.draw == 0);
    SANPAO15_REQUIRE(result.unknown == 0);
    SANPAO15_REQUIRE(result.terminalStates == 4600);
    SANPAO15_REQUIRE(table.get(0) == Outcome::CannonWin);
    SANPAO15_REQUIRE(table.get(table.size() - 1) == Outcome::CannonWin);
}

SANPAO15_TEST(lowKSolvesK1WithoutUnknown) {
    PackedOutcomeTable2Bit k0(denseStateCount(0));
    (void)solveDenseLayerOutcome(0, nullptr, k0);

    PackedOutcomeTable2Bit k1(denseStateCount(1));
    const DenseLayerSolveResult result = solveDenseLayerOutcome(1, &k0, k1);

    SANPAO15_REQUIRE(result.stateCount == denseStateCount(1));
    SANPAO15_REQUIRE(result.unknown == 0);
    SANPAO15_REQUIRE(result.cannonWin + result.soldierWin + result.draw == denseStateCount(1));
    SANPAO15_REQUIRE(result.captureEdges > 0);
}

SANPAO15_TEST(lowKTerminalPriorityKeepsK0CannonWin) {
    const Position pos = parsePositionNotation("CCC../...../...../...../..... c");
    const uint64_t index = denseIndex(pos);
    SANPAO15_REQUIRE(!generateDenseSuccessors(0, index).empty());

    PackedOutcomeTable2Bit table(denseStateCount(0));
    (void)solveDenseLayerOutcome(0, nullptr, table);
    SANPAO15_REQUIRE(table.get(index) == Outcome::CannonWin);
}

SANPAO15_TEST(lowKCaptureUsesLowerLayerOutcome) {
    PackedOutcomeTable2Bit k0(denseStateCount(0));
    (void)solveDenseLayerOutcome(0, nullptr, k0);

    PackedOutcomeTable2Bit k1(denseStateCount(1));
    (void)solveDenseLayerOutcome(1, &k0, k1);

    const Position pos = captureOneSoldierPosition();
    const uint64_t index = denseIndex(pos);
    bool sawCaptureToCannonWin = false;
    for (const DenseSuccessor& successor : generateDenseSuccessors(1, index)) {
        if (successor.kind == DenseSuccessorKind::CaptureToLowerLayer &&
            k0.get(successor.toIndex) == Outcome::CannonWin) {
            sawCaptureToCannonWin = true;
        }
    }
    SANPAO15_REQUIRE(sawCaptureToCannonWin);
    SANPAO15_REQUIRE(k1.get(index) == Outcome::CannonWin);
}

SANPAO15_TEST(lowKFileSolveAndVerifyRoundtrip) {
    const std::filesystem::path dir = tempDir("sanpao15-lowk-roundtrip");
    LowKTablebaseSolveOptions options;
    options.maxK = 1;
    options.outputDir = dir;
    options.encoding = DenseResultEncoding::Packed2Bit;

    const std::vector<LowKTablebaseLayerResult> solved = solveLowKTablebase(options);
    SANPAO15_REQUIRE(solved.size() == 2);
    SANPAO15_REQUIRE(std::filesystem::exists(lowKLayerResultPath(dir, 0)));
    SANPAO15_REQUIRE(std::filesystem::exists(lowKLayerResultPath(dir, 1)));
    SANPAO15_REQUIRE(validateDenseResultFile(lowKLayerResultPath(dir, 1), StandardRulesetHash, 1).encoding ==
                     DenseResultEncoding::Packed2Bit);

    const LowKTablebaseVerifyResult verified = verifyLowKTablebase(dir, 1, 1000);
    std::filesystem::remove_all(dir);

    SANPAO15_REQUIRE(verified.layers.size() == 2);
    SANPAO15_REQUIRE(verified.layers[0].unknown == 0);
    SANPAO15_REQUIRE(verified.layers[1].unknown == 0);
    SANPAO15_REQUIRE(verified.layers[0].cannonWin == denseStateCount(0));
}

SANPAO15_TEST(lowKRejectsKAbovePrototypeLimit) {
    sanpao15::test::requireThrows([] {
        LowKTablebaseSolveOptions options;
        options.maxK = 4;
        options.outputDir = std::filesystem::temp_directory_path() / "sanpao15-lowk-too-large";
        (void)solveLowKTablebase(options);
    }, "K above prototype limit should be rejected");
}
