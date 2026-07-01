#include "test_common.h"

#include <algorithm>
#include <array>

#include "sanpao15/position.h"
#include "sanpao15/solver.h"
#include "sanpao15/table.h"

using namespace sanpao15;

namespace {

GraphBuildOptions graphOptions(uint64_t limit, GraphBackend backend, bool storePred = true) {
    GraphBuildOptions options;
    options.maxStates = limit;
    options.backend = backend;
    options.storePred = storePred;
    return options;
}

SolveOptions solveOptions(uint64_t limit, GraphBackend backend) {
    SolveOptions options;
    options.maxStates = limit;
    options.graphBackend = backend;
    return options;
}

bool containsPred(const StateGraph& graph, uint32_t target, uint32_t predecessor) {
    for (uint64_t i = graph.csr.predOffset[target]; i < graph.csr.predOffset[target + 1]; ++i) {
        if (graph.csr.predFlat[i] == predecessor) {
            return true;
        }
    }
    return false;
}

void requireStatsMatch(const SolveResult& vectorResult, const SolveResult& csrResult) {
    SANPAO15_REQUIRE(vectorResult.initialOutcome == csrResult.initialOutcome);
    SANPAO15_REQUIRE(vectorResult.stats.reachableStates == csrResult.stats.reachableStates);
    SANPAO15_REQUIRE(vectorResult.stats.generatedEdges == csrResult.stats.generatedEdges);
    SANPAO15_REQUIRE(vectorResult.stats.storedEdges == csrResult.stats.storedEdges);
    SANPAO15_REQUIRE(vectorResult.stats.droppedEdges == csrResult.stats.droppedEdges);
    SANPAO15_REQUIRE(vectorResult.stats.totalEdges == csrResult.stats.totalEdges);
    SANPAO15_REQUIRE(vectorResult.stats.statesBySoldierCount == csrResult.stats.statesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.generatedEdgesBySoldierCount == csrResult.stats.generatedEdgesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.storedEdgesBySoldierCount == csrResult.stats.storedEdgesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.droppedEdgesBySoldierCount == csrResult.stats.droppedEdgesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.edgesBySoldierCount == csrResult.stats.edgesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.cannonWinStates == csrResult.stats.cannonWinStates);
    SANPAO15_REQUIRE(vectorResult.stats.soldierWinStates == csrResult.stats.soldierWinStates);
    SANPAO15_REQUIRE(vectorResult.stats.drawStates == csrResult.stats.drawStates);
    SANPAO15_REQUIRE(vectorResult.stats.unknownStates == csrResult.stats.unknownStates);
}

}  // namespace

SANPAO15_TEST(csrGraphOffsetsAndPredsAreConsistent) {
    const StateGraph graph = buildReachableGraph(packPosition(initialPosition()), graphOptions(100, GraphBackend::Csr));

    SANPAO15_REQUIRE(graph.backend == GraphBackend::Csr);
    SANPAO15_REQUIRE(graph.csr.succOffset.size() == graph.states.size() + 1);
    SANPAO15_REQUIRE(graph.csr.succFlat.size() == graph.totalEdges);
    SANPAO15_REQUIRE(graph.csr.succFlat.size() == graph.storedEdges);
    SANPAO15_REQUIRE(graph.generatedEdges == graph.storedEdges + graph.droppedEdges);
    SANPAO15_REQUIRE(graph.totalEdges == graph.storedEdges);
    SANPAO15_REQUIRE(graph.edgesBySoldierCount == graph.storedEdgesBySoldierCount);
    SANPAO15_REQUIRE(graph.csr.predOffset.size() == graph.states.size() + 1);
    SANPAO15_REQUIRE(graph.csr.predFlat.size() == graph.csr.succFlat.size());

    for (size_t i = 0; i + 1 < graph.csr.succOffset.size(); ++i) {
        SANPAO15_REQUIRE(graph.csr.succOffset[i] <= graph.csr.succOffset[i + 1]);
        SANPAO15_REQUIRE(graph.csr.predOffset[i] <= graph.csr.predOffset[i + 1]);
    }

    for (uint32_t u = 0; u < graph.states.size(); ++u) {
        for (uint64_t edge = graph.csr.succOffset[u]; edge < graph.csr.succOffset[u + 1]; ++edge) {
            const uint32_t v = graph.csr.succFlat[edge];
            SANPAO15_REQUIRE(v < graph.states.size());
            SANPAO15_REQUIRE(containsPred(graph, v, u));
        }
    }
}

SANPAO15_TEST(csrNoPredSkipsPredecessorStorage) {
    const StateGraph graph =
        buildReachableGraph(packPosition(initialPosition()), graphOptions(100, GraphBackend::Csr, false));

    SANPAO15_REQUIRE(graph.backend == GraphBackend::Csr);
    SANPAO15_REQUIRE(!graph.storesPred);
    SANPAO15_REQUIRE(graph.csr.predFlat.empty());
    SANPAO15_REQUIRE(graph.csr.predOffset.empty());
    SANPAO15_REQUIRE(graph.csr.succOffset.size() == graph.states.size() + 1);
}

SANPAO15_TEST(vectorAndCsrStatsMatchForSmallLimits) {
    for (uint64_t limit : {20ull, 100ull, 1000ull}) {
        const SolveResult vectorResult = solveFromInitial(solveOptions(limit, GraphBackend::Vector));
        const SolveResult csrResult = solveFromInitial(solveOptions(limit, GraphBackend::Csr));
        requireStatsMatch(vectorResult, csrResult);

        for (const auto& [key, vectorInfo] : vectorResult.table.entries) {
            const StateInfo* csrInfo = findStateInfo(csrResult.table, key);
            SANPAO15_REQUIRE(csrInfo != nullptr);
            SANPAO15_REQUIRE(csrInfo->outcome == vectorInfo.outcome);
        }
    }
}

SANPAO15_TEST(vectorAndCsrGraphStatsMatchForSmallNoPredLimit) {
    GraphStatsOptions vectorOptions;
    vectorOptions.maxStates = 1000;
    vectorOptions.graphBackend = GraphBackend::Vector;
    vectorOptions.storePred = false;

    GraphStatsOptions csrOptions = vectorOptions;
    csrOptions.graphBackend = GraphBackend::Csr;

    const GraphStatsResult vectorResult = collectGraphStatsFromInitial(vectorOptions);
    const GraphStatsResult csrResult = collectGraphStatsFromInitial(csrOptions);

    SANPAO15_REQUIRE(vectorResult.stats.reachableStates == csrResult.stats.reachableStates);
    SANPAO15_REQUIRE(vectorResult.stats.generatedEdges == csrResult.stats.generatedEdges);
    SANPAO15_REQUIRE(vectorResult.stats.storedEdges == csrResult.stats.storedEdges);
    SANPAO15_REQUIRE(vectorResult.stats.droppedEdges == csrResult.stats.droppedEdges);
    SANPAO15_REQUIRE(vectorResult.stats.statesBySoldierCount == csrResult.stats.statesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.generatedEdgesBySoldierCount == csrResult.stats.generatedEdgesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.storedEdgesBySoldierCount == csrResult.stats.storedEdgesBySoldierCount);
    SANPAO15_REQUIRE(vectorResult.stats.droppedEdgesBySoldierCount == csrResult.stats.droppedEdgesBySoldierCount);
}

SANPAO15_TEST(csrTruncatedUnknownsStayUnknown) {
    const SolveResult result = solveFromInitial(solveOptions(20, GraphBackend::Csr));

    SANPAO15_REQUIRE(result.stats.truncated);
    SANPAO15_REQUIRE(result.stats.unknownStates > 0);
    SANPAO15_REQUIRE(result.stats.drawStates == 0);
}
