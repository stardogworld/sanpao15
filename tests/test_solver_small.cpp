#include "test_common.h"

#include "sanpao15/bitboard.h"
#include "sanpao15/solver.h"

using namespace sanpao15;

SANPAO15_TEST(analyzeTerminalPositionWithoutSoldiers) {
    Position pos;
    pos.cannons = setBit(0, 12);
    pos.side = Side::Cannon;

    const Analysis analysis = analyzePosition(pos);
    SANPAO15_REQUIRE(analysis.outcome == Outcome::CannonWin);
    SANPAO15_REQUIRE(analysis.legalMoves.empty());
}

SANPAO15_TEST(solveFromInitialProducesReachableStats) {
    const SolveResult result = solveFromInitial(SolveOptions{5000});
    SANPAO15_REQUIRE(result.stats.reachableStates > 0);
    SANPAO15_REQUIRE(
        result.stats.reachableStates ==
        result.stats.cannonWinStates + result.stats.soldierWinStates + result.stats.drawStates +
            result.stats.unknownStates);
    SANPAO15_REQUIRE(result.stats.statesBySoldierCount[15] > 0);
    SANPAO15_REQUIRE(result.stats.truncated);
    SANPAO15_REQUIRE(result.stats.unknownStates > 0);
    SANPAO15_REQUIRE(result.initialOutcome == Outcome::Unknown);
}

SANPAO15_TEST(boundedAnalyzeReportsTruncationInsteadOfDraw) {
    const Analysis analysis = analyzePosition(initialPosition(), SolveOptions{1});
    SANPAO15_REQUIRE(analysis.tableTruncated);
    SANPAO15_REQUIRE(analysis.outcome == Outcome::Unknown);
    SANPAO15_REQUIRE(!analysis.legalMoves.empty());
}
