#include "test_common.h"

#include <numeric>

#include "sanpao15/bitboard.h"
#include "sanpao15/solver.h"

using namespace sanpao15;

namespace {

uint64_t sumArray(const std::array<uint64_t, 16>& values) {
    return std::accumulate(values.begin(), values.end(), uint64_t{0});
}

void requireEdgeAccountingInvariant(const SolveStats& stats) {
    SANPAO15_REQUIRE(stats.generatedEdges == stats.storedEdges + stats.droppedEdges);
    SANPAO15_REQUIRE(stats.storedEdges == stats.totalEdges);
    SANPAO15_REQUIRE(stats.edgesBySoldierCount == stats.storedEdgesBySoldierCount);
    for (size_t i = 0; i < stats.generatedEdgesBySoldierCount.size(); ++i) {
        SANPAO15_REQUIRE(
            stats.generatedEdgesBySoldierCount[i] ==
            stats.storedEdgesBySoldierCount[i] + stats.droppedEdgesBySoldierCount[i]);
    }
}

Position oneSoldierPosition() {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.soldiers = setBit(0, 10);
    pos.side = Side::Cannon;
    return pos;
}

}  // namespace

SANPAO15_TEST(formatBytesUsesExpectedUnits) {
    SANPAO15_REQUIRE(formatBytes(123) == "123 B");
    SANPAO15_REQUIRE(formatBytes(12 * 1024 + 410) == "12.4 KB");
    SANPAO15_REQUIRE(formatBytes(88 * 1024 * 1024 + 100 * 1024) == "88.1 MB");
    SANPAO15_REQUIRE(formatBytes(17ull * 1024 * 1024 * 1024 / 10) == "1.7 GB");
}

SANPAO15_TEST(formatDropRatioUsesExpectedPercentages) {
    SANPAO15_REQUIRE(formatDropRatio(0, 0) == "0.0%");
    SANPAO15_REQUIRE(formatDropRatio(25, 100) == "25.0%");
    SANPAO15_REQUIRE(formatDropRatio(1, 3) == "33.3%");
}

SANPAO15_TEST(memoryEstimateHasExpectedRelationships) {
    SolveStats stats;
    stats.reachableStates = 100;
    stats.totalEdges = 250;
    stats.storesPred = true;

    const MemoryEstimate estimate = estimateMemoryUse(stats);
    SANPAO15_REQUIRE(estimate.compactTable16Bytes == 1600);
    SANPAO15_REQUIRE(estimate.compactTable24Bytes == 2400);
    SANPAO15_REQUIRE(estimate.flatEdgesBytesBothDirections == estimate.flatEdgesBytesOneDirection * 2);
    SANPAO15_REQUIRE(estimate.vectorVectorOverheadSucc > 0);
    SANPAO15_REQUIRE(estimate.vectorVectorOverheadPred > 0);
    SANPAO15_REQUIRE(estimate.roughTotalCurrentGraphBytes > estimate.statesVectorBytes);
}

SANPAO15_TEST(statsOnlyDoesNotRunRetrogradeOrFinalizeDraws) {
    GraphStatsOptions options;
    options.maxStates = 20;
    const GraphStatsResult result = collectGraphStatsFromInitial(options);

    SANPAO15_REQUIRE(result.stats.truncated);
    SANPAO15_REQUIRE(!result.stats.ranRetrograde);
    SANPAO15_REQUIRE(result.stats.unknownStates == result.stats.reachableStates);
    SANPAO15_REQUIRE(result.stats.drawStates == 0);
    SANPAO15_REQUIRE(result.stats.totalEdges > 0);
    requireEdgeAccountingInvariant(result.stats);
}

SANPAO15_TEST(statsOnlyNoPredIsAllowedAndRecorded) {
    GraphStatsOptions options;
    options.maxStates = 20;
    options.storePred = false;
    const GraphStatsResult result = collectGraphStatsFromInitial(options);

    SANPAO15_REQUIRE(!result.stats.storesPred);
    SANPAO15_REQUIRE(result.memory.vectorVectorOverheadPred == 0);
    SANPAO15_REQUIRE(result.stats.reachableStates > 0);
    requireEdgeAccountingInvariant(result.stats);
}

SANPAO15_TEST(layeredStatsSumToTotals) {
    GraphStatsOptions options;
    options.maxStates = 100;
    const GraphStatsResult result = collectGraphStatsFromInitial(options);

    SANPAO15_REQUIRE(sumArray(result.stats.statesBySoldierCount) == result.stats.reachableStates);
    SANPAO15_REQUIRE(sumArray(result.stats.edgesBySoldierCount) == result.stats.totalEdges);
    SANPAO15_REQUIRE(sumArray(result.stats.generatedEdgesBySoldierCount) == result.stats.generatedEdges);
    SANPAO15_REQUIRE(sumArray(result.stats.storedEdgesBySoldierCount) == result.stats.storedEdges);
    SANPAO15_REQUIRE(sumArray(result.stats.droppedEdgesBySoldierCount) == result.stats.droppedEdges);
    requireEdgeAccountingInvariant(result.stats);
}

SANPAO15_TEST(truncatedStatsTrackDroppedEdges) {
    GraphStatsOptions options;
    options.maxStates = 1;
    const GraphStatsResult result = collectGraphStatsFromInitial(options);

    SANPAO15_REQUIRE(result.stats.truncated);
    SANPAO15_REQUIRE(result.stats.generatedEdges > 0);
    SANPAO15_REQUIRE(result.stats.storedEdges == 0);
    SANPAO15_REQUIRE(result.stats.droppedEdges == result.stats.generatedEdges);
    requireEdgeAccountingInvariant(result.stats);
}

SANPAO15_TEST(completeSmallGraphHasNoDroppedEdges) {
    GraphStatsOptions options;
    options.maxStates = 0;
    options.graphBackend = GraphBackend::Csr;
    const GraphStatsResult result = collectGraphStats(oneSoldierPosition(), options);

    SANPAO15_REQUIRE(!result.stats.truncated);
    SANPAO15_REQUIRE(result.stats.reachableStates > 1);
    SANPAO15_REQUIRE(result.stats.generatedEdges == result.stats.storedEdges);
    SANPAO15_REQUIRE(result.stats.droppedEdges == 0);
    requireEdgeAccountingInvariant(result.stats);
}
