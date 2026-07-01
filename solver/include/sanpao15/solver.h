#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "sanpao15/move.h"
#include "sanpao15/position.h"
#include "sanpao15/table.h"

namespace sanpao15 {

struct MoveAnalysis {
    Move move;
    Outcome resultingOutcome = Outcome::Unknown;
    int distance = -1;
    bool isBest = false;
};

struct Analysis {
    uint64_t key = 0;
    Outcome outcome = Outcome::Unknown;
    int distance = -1;
    std::optional<Move> bestMove;
    bool foundInTable = false;
    bool tableExact = false;
    bool tableTruncated = false;
    std::vector<MoveAnalysis> legalMoves;
};

struct SolveStats {
    uint64_t reachableStates = 0;
    uint64_t generatedEdges = 0;
    uint64_t storedEdges = 0;
    uint64_t droppedEdges = 0;
    // Compatibility field. It currently means storedEdges.
    uint64_t totalEdges = 0;
    uint64_t cannonWinStates = 0;
    uint64_t soldierWinStates = 0;
    uint64_t drawStates = 0;
    uint64_t unknownStates = 0;
    bool truncated = false;
    std::array<uint64_t, 16> statesBySoldierCount{};
    std::array<uint64_t, 16> generatedEdgesBySoldierCount{};
    std::array<uint64_t, 16> storedEdgesBySoldierCount{};
    std::array<uint64_t, 16> droppedEdgesBySoldierCount{};
    // Compatibility field. It currently means storedEdgesBySoldierCount.
    std::array<uint64_t, 16> edgesBySoldierCount{};
    double buildGraphSeconds = 0.0;
    double retrogradeSeconds = 0.0;
    double finalizeSeconds = 0.0;
    double totalSeconds = 0.0;
    uint64_t maxBfsQueueSize = 0;
    uint64_t maxRetrogradeQueueSize = 0;
    GraphBackend graphBackend = GraphBackend::Csr;
    bool storesPred = true;
    bool ranRetrograde = true;
};

struct MemoryEstimate {
    // These values are rough planning estimates for the current in-memory graph
    // shape and compact table formats; they are not allocator-accurate.
    uint64_t compactTable16Bytes = 0;
    uint64_t compactTable24Bytes = 0;
    uint64_t statesVectorBytes = 0;
    uint64_t outcomeVectorBytes = 0;
    uint64_t remainingVectorBytes = 0;
    uint64_t flatEdgesBytesOneDirection = 0;
    uint64_t flatEdgesBytesBothDirections = 0;
    uint64_t vectorVectorOverheadSucc = 0;
    uint64_t vectorVectorOverheadPred = 0;
    uint64_t roughTotalCurrentGraphBytes = 0;
    uint64_t csrSuccFlatBytes = 0;
    uint64_t csrSuccOffsetBytes = 0;
    uint64_t csrPredFlatBytes = 0;
    uint64_t csrPredOffsetBytes = 0;
    uint64_t csrTotalGraphBytes = 0;
    uint64_t vectorSuccOverheadBytes = 0;
    uint64_t vectorPredOverheadBytes = 0;
    uint64_t vectorTotalGraphEstimateBytes = 0;
};

struct SolveResult {
    Outcome initialOutcome = Outcome::Unknown;
    SolveStats stats;
    ResultTable table;
};

struct SolveOptions {
    // First-phase builds can cap graph generation so the CLI stays responsive.
    // maxStates == 0 means no cap.
    uint64_t maxStates = 100000;
    GraphBackend graphBackend = GraphBackend::Csr;
    uint64_t progressInterval = 0;
    ProgressCallback progress;
};

struct GraphStatsOptions {
    uint64_t maxStates = 100000;
    GraphBackend graphBackend = GraphBackend::Csr;
    bool storePred = true;
    uint64_t progressInterval = 0;
    ProgressCallback progress;
};

struct GraphStatsResult {
    SolveStats stats;
    MemoryEstimate memory;
};

SolveResult solveFromInitial(const SolveOptions& options);
SolveResult solveFromInitial();
GraphStatsResult collectGraphStatsFromInitial(const GraphStatsOptions& options);
GraphStatsResult collectGraphStats(const Position& initial, const GraphStatsOptions& options);
MemoryEstimate estimateMemoryUse(const SolveStats& stats);
std::string formatBytes(uint64_t bytes);
std::string formatDuration(double seconds);
std::string formatDropRatio(uint64_t droppedEdges, uint64_t generatedEdges);
Analysis analyzePosition(const Position& pos, const SolveOptions& options);
Analysis analyzePosition(const Position& pos);
Analysis analyzePositionFromTable(const Position& pos, const ResultTable& table);

}  // namespace sanpao15
