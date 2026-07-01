#include "sanpao15/solver.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

struct RetrogradeResult {
    StateGraph graph;
    std::vector<StateInfo> info;
    double retrogradeSeconds = 0.0;
    double finalizeSeconds = 0.0;
    double totalSeconds = 0.0;
    uint64_t maxRetrogradeQueueSize = 0;
};

using Clock = std::chrono::steady_clock;

bool isWinForSide(Outcome outcome, Side side) {
    return outcome == winFor(side);
}

struct QueueItem {
    int stateId = -1;
    int distance = -1;
    uint64_t sequence = 0;
};

struct QueueOrder {
    bool operator()(const QueueItem& lhs, const QueueItem& rhs) const {
        if (lhs.distance != rhs.distance) {
            return lhs.distance > rhs.distance;
        }
        return lhs.sequence > rhs.sequence;
    }
};

void reportProgress(
    const ProgressCallback& progress,
    const ProgressInfo& info,
    uint64_t interval) {
    if (!progress) {
        return;
    }
    if (info.processed == info.total || interval == 0 || info.processed == 0 || info.processed % interval == 0) {
        progress(info);
    }
}

Move moveForEdge(const StateGraph& graph, int fromState, int toState) {
    if (graph.backend == GraphBackend::Vector) {
        const auto& successors = graph.succ[fromState];
        const auto& moves = graph.succMoves[fromState];
        for (size_t i = 0; i < successors.size(); ++i) {
            if (successors[i] == toState) {
                return moves[i];
            }
        }
    } else {
        const Position pos = unpackPosition(graph.states[fromState]);
        const uint64_t targetKey = graph.states[toState];
        for (const Move& move : generateLegalMoves(pos)) {
            if (packPosition(applyMove(pos, move)) == targetKey) {
                return move;
            }
        }
    }
    throw std::logic_error("state graph is missing edge move metadata");
}

std::vector<int> successorsOf(const StateGraph& graph, int stateId) {
    if (graph.backend == GraphBackend::Vector) {
        return graph.succ[stateId];
    }

    std::vector<int> result;
    for (uint64_t edge = graph.csr.succOffset[stateId]; edge < graph.csr.succOffset[stateId + 1]; ++edge) {
        result.push_back(static_cast<int>(graph.csr.succFlat[edge]));
    }
    return result;
}

std::vector<int> predecessorsOf(const StateGraph& graph, int stateId) {
    if (graph.backend == GraphBackend::Vector) {
        return graph.storesPred ? graph.pred[stateId] : std::vector<int>{};
    }

    std::vector<int> result;
    if (!graph.storesPred) {
        return result;
    }
    for (uint64_t edge = graph.csr.predOffset[stateId]; edge < graph.csr.predOffset[stateId + 1]; ++edge) {
        result.push_back(static_cast<int>(graph.csr.predFlat[edge]));
    }
    return result;
}

void chooseLongestLosingMove(const StateGraph& graph, std::vector<StateInfo>& info, int stateId) {
    StateInfo& state = info[stateId];
    const Position pos = unpackPosition(graph.states[stateId]);
    const Outcome opponentWin = opponentWinFor(pos.side);

    int bestDistance = -1;
    std::optional<Move> bestMove;
    for (int child : successorsOf(graph, stateId)) {
        const StateInfo& childInfo = info[child];
        if (childInfo.outcome == opponentWin && childInfo.distance >= bestDistance) {
            bestDistance = childInfo.distance;
            bestMove = moveForEdge(graph, stateId, child);
        }
    }

    state.distance = bestDistance < 0 ? -1 : bestDistance + 1;
    state.bestMove = bestMove;
}

void chooseDrawMoves(const StateGraph& graph, std::vector<StateInfo>& info) {
    for (size_t stateId = 0; stateId < graph.states.size(); ++stateId) {
        StateInfo& state = info[stateId];
        if (state.outcome != Outcome::Draw || state.bestMove.has_value()) {
            continue;
        }
        for (int child : successorsOf(graph, static_cast<int>(stateId))) {
            if (info[child].outcome == Outcome::Draw) {
                state.bestMove = moveForEdge(graph, static_cast<int>(stateId), child);
                break;
            }
        }
    }
}

ResultTable makeResultTable(
    uint64_t initialKey,
    const StateGraph& graph,
    const std::vector<StateInfo>& info) {
    ResultTable table;
    table.initialKey = initialKey;
    table.rulesetHash = StandardRulesetHash;
    table.truncated = graph.truncated;
    table.exact = !graph.truncated;
    table.entries.reserve(graph.states.size());
    for (size_t i = 0; i < graph.states.size(); ++i) {
        table.entries.emplace(graph.states[i], info[i]);
    }
    return table;
}

RetrogradeResult runRetrograde(const Position& initial, const SolveOptions& options) {
    RetrogradeResult result;
    const auto totalStarted = Clock::now();
    const uint64_t initialKey = packPosition(initial);
    GraphBuildOptions graphOptions;
    graphOptions.maxStates = options.maxStates;
    graphOptions.backend = options.graphBackend;
    graphOptions.storePred = true;
    graphOptions.progressInterval = options.progressInterval;
    graphOptions.progress = options.progress;
    result.graph = buildReachableGraph(initialKey, graphOptions);

    const int count = static_cast<int>(result.graph.states.size());
    result.info.assign(count, StateInfo{});
    std::vector<int> remaining(count, 0);
    std::priority_queue<QueueItem, std::vector<QueueItem>, QueueOrder> proven;
    uint64_t queueSequence = 0;

    const auto markStarted = Clock::now();
    reportProgress(
        options.progress,
        ProgressInfo{"mark terminals", 0, result.graph.states.size(), 0, 0, 0, 0, 0.0, {}},
        options.progressInterval);

    for (int id = 0; id < count; ++id) {
        remaining[id] = result.graph.outdegree[id];
        const Position pos = unpackPosition(result.graph.states[id]);
        const Outcome terminal = terminalOutcome(pos);
        if (terminal != Outcome::Unknown) {
            result.info[id].outcome = terminal;
            result.info[id].distance = 0;
            proven.push({id, 0, queueSequence++});
        }
        result.maxRetrogradeQueueSize = std::max<uint64_t>(result.maxRetrogradeQueueSize, proven.size());
        reportProgress(
            options.progress,
            ProgressInfo{
                "mark terminals",
                static_cast<uint64_t>(id + 1),
                result.graph.states.size(),
                0,
                0,
                proven.size(),
                proven.size(),
                std::chrono::duration<double>(Clock::now() - markStarted).count(),
                {},
            },
            options.progressInterval);
    }

    const auto retroStarted = Clock::now();
    uint64_t processed = 0;
    reportProgress(
        options.progress,
        ProgressInfo{"retrograde", processed, result.graph.states.size(), 0, 0, proven.size(), 0, 0.0, {}},
        options.progressInterval);
    while (!proven.empty()) {
        const QueueItem item = proven.top();
        proven.pop();
        const int current = item.stateId;
        if (item.distance != result.info[current].distance) {
            continue;
        }

        ++processed;
        const Outcome currentOutcome = result.info[current].outcome;

        for (int predecessor : predecessorsOf(result.graph, current)) {
            if (result.info[predecessor].outcome != Outcome::Unknown) {
                continue;
            }

            const Position predPos = unpackPosition(result.graph.states[predecessor]);
            if (isWinForSide(currentOutcome, predPos.side)) {
                result.info[predecessor].outcome = currentOutcome;
                result.info[predecessor].distance = result.info[current].distance + 1;
                result.info[predecessor].bestMove = moveForEdge(result.graph, predecessor, current);
                proven.push({predecessor, result.info[predecessor].distance, queueSequence++});
                result.maxRetrogradeQueueSize = std::max<uint64_t>(result.maxRetrogradeQueueSize, proven.size());
                continue;
            }

            --remaining[predecessor];
            if (remaining[predecessor] == 0) {
                result.info[predecessor].outcome = opponentWinFor(predPos.side);
                chooseLongestLosingMove(result.graph, result.info, predecessor);
                proven.push({predecessor, result.info[predecessor].distance, queueSequence++});
                result.maxRetrogradeQueueSize = std::max<uint64_t>(result.maxRetrogradeQueueSize, proven.size());
            }
        }
        reportProgress(
            options.progress,
            ProgressInfo{
                "retrograde",
                processed,
                result.graph.states.size(),
                0,
                0,
                proven.size(),
                processed,
                std::chrono::duration<double>(Clock::now() - retroStarted).count(),
                {},
            },
            options.progressInterval);
    }

    result.retrogradeSeconds = std::chrono::duration<double>(Clock::now() - retroStarted).count();
    reportProgress(
        options.progress,
        ProgressInfo{
            "retrograde",
            processed,
            result.graph.states.size(),
            0,
            0,
            proven.size(),
            processed,
            result.retrogradeSeconds,
            {},
        },
        options.progressInterval);

    const auto finalizeStarted = Clock::now();
    if (!result.graph.truncated) {
        reportProgress(
            options.progress,
            ProgressInfo{"finalize", 0, result.graph.states.size(), 0, 0, 0, 0, 0.0, "exact table: marking remaining Unknown as Draw"},
            options.progressInterval);
        for (size_t id = 0; id < result.info.size(); ++id) {
            if (result.info[id].outcome == Outcome::Unknown) {
                result.info[id].outcome = Outcome::Draw;
                result.info[id].distance = -1;
            }
            reportProgress(
                options.progress,
                ProgressInfo{
                    "finalize",
                    static_cast<uint64_t>(id + 1),
                    result.graph.states.size(),
                    0,
                    0,
                    0,
                    0,
                    std::chrono::duration<double>(Clock::now() - finalizeStarted).count(),
                    {},
                },
                options.progressInterval);
        }
        chooseDrawMoves(result.graph, result.info);
    } else {
        reportProgress(
            options.progress,
            ProgressInfo{
                "finalize",
                result.graph.states.size(),
                result.graph.states.size(),
                0,
                0,
                0,
                0,
                0.0,
                "truncated table: keeping remaining Unknown",
            },
            options.progressInterval);
    }

    result.finalizeSeconds = std::chrono::duration<double>(Clock::now() - finalizeStarted).count();
    result.totalSeconds = std::chrono::duration<double>(Clock::now() - totalStarted).count();
    return result;
}

SolveStats makeStats(const StateGraph& graph, const std::vector<StateInfo>& info) {
    SolveStats stats;
    stats.reachableStates = static_cast<uint64_t>(graph.states.size());
    stats.generatedEdges = graph.generatedEdges;
    stats.storedEdges = graph.storedEdges;
    stats.droppedEdges = graph.droppedEdges;
    stats.totalEdges = graph.totalEdges;
    stats.truncated = graph.truncated;
    stats.generatedEdgesBySoldierCount = graph.generatedEdgesBySoldierCount;
    stats.storedEdgesBySoldierCount = graph.storedEdgesBySoldierCount;
    stats.droppedEdgesBySoldierCount = graph.droppedEdgesBySoldierCount;
    stats.edgesBySoldierCount = graph.edgesBySoldierCount;
    stats.buildGraphSeconds = graph.buildGraphSeconds;
    stats.maxBfsQueueSize = graph.maxBfsQueueSize;
    stats.graphBackend = graph.backend;
    stats.storesPred = graph.storesPred;

    for (size_t i = 0; i < graph.states.size(); ++i) {
        const Position pos = unpackPosition(graph.states[i]);
        const int soldierCount = popcount25(pos.soldiers);
        if (soldierCount >= 0 && soldierCount < static_cast<int>(stats.statesBySoldierCount.size())) {
            ++stats.statesBySoldierCount[soldierCount];
        }

        switch (info[i].outcome) {
            case Outcome::CannonWin:
                ++stats.cannonWinStates;
                break;
            case Outcome::SoldierWin:
                ++stats.soldierWinStates;
                break;
            case Outcome::Draw:
                ++stats.drawStates;
                break;
            case Outcome::Unknown:
                ++stats.unknownStates;
                break;
        }
    }

    return stats;
}

SolveStats makeGraphOnlyStats(const StateGraph& graph) {
    SolveStats stats;
    stats.reachableStates = static_cast<uint64_t>(graph.states.size());
    stats.generatedEdges = graph.generatedEdges;
    stats.storedEdges = graph.storedEdges;
    stats.droppedEdges = graph.droppedEdges;
    stats.totalEdges = graph.totalEdges;
    stats.unknownStates = stats.reachableStates;
    stats.truncated = graph.truncated;
    stats.generatedEdgesBySoldierCount = graph.generatedEdgesBySoldierCount;
    stats.storedEdgesBySoldierCount = graph.storedEdgesBySoldierCount;
    stats.droppedEdgesBySoldierCount = graph.droppedEdgesBySoldierCount;
    stats.edgesBySoldierCount = graph.edgesBySoldierCount;
    stats.buildGraphSeconds = graph.buildGraphSeconds;
    stats.totalSeconds = graph.buildGraphSeconds;
    stats.maxBfsQueueSize = graph.maxBfsQueueSize;
    stats.graphBackend = graph.backend;
    stats.storesPred = graph.storesPred;
    stats.ranRetrograde = false;

    for (uint64_t key : graph.states) {
        const Position pos = unpackPosition(key);
        const int soldierCount = popcount25(pos.soldiers);
        if (soldierCount >= 0 && soldierCount < static_cast<int>(stats.statesBySoldierCount.size())) {
            ++stats.statesBySoldierCount[soldierCount];
        }
    }

    return stats;
}

}  // namespace

SolveResult solveFromInitial(const SolveOptions& options) {
    const Position initial = initialPosition();
    const uint64_t initialKey = packPosition(initial);
    RetrogradeResult retro = runRetrograde(initial, options);

    SolveResult solve;
    solve.initialOutcome = retro.info.at(retro.graph.id.at(initialKey)).outcome;
    solve.stats = makeStats(retro.graph, retro.info);
    solve.stats.retrogradeSeconds = retro.retrogradeSeconds;
    solve.stats.finalizeSeconds = retro.finalizeSeconds;
    solve.stats.totalSeconds = retro.totalSeconds;
    solve.stats.maxRetrogradeQueueSize = retro.maxRetrogradeQueueSize;
    solve.table = makeResultTable(initialKey, retro.graph, retro.info);
    return solve;
}

SolveResult solveFromInitial() {
    return solveFromInitial(SolveOptions{});
}

Analysis analyzePosition(const Position& pos, const SolveOptions& options) {
    RetrogradeResult retro = runRetrograde(pos, options);
    const uint64_t initialKey = packPosition(pos);
    ResultTable table = makeResultTable(initialKey, retro.graph, retro.info);
    return analyzePositionFromTable(pos, table);
}

Analysis analyzePosition(const Position& pos) {
    return analyzePosition(pos, SolveOptions{0});
}

Analysis analyzePositionFromTable(const Position& pos, const ResultTable& table) {
    Analysis analysis;
    analysis.key = packPosition(pos);
    analysis.tableExact = table.exact;
    analysis.tableTruncated = table.truncated;

    const StateInfo* currentInfo = findStateInfo(table, analysis.key);
    if (currentInfo != nullptr) {
        analysis.foundInTable = true;
        analysis.outcome = currentInfo->outcome;
        analysis.distance = currentInfo->distance;
        analysis.bestMove = currentInfo->bestMove;
    }

    if (terminalOutcome(pos) != Outcome::Unknown) {
        return analysis;
    }

    for (const Move& move : generateLegalMoves(pos)) {
        const Position next = applyMove(pos, move);
        const uint64_t nextKey = packPosition(next);
        MoveAnalysis item;
        item.move = move;
        if (const StateInfo* nextInfo = findStateInfo(table, nextKey)) {
            item.resultingOutcome = nextInfo->outcome;
            item.distance = nextInfo->distance;
        }
        if (analysis.bestMove.has_value()) {
            item.isBest = move == *analysis.bestMove;
        }
        analysis.legalMoves.push_back(item);
    }

    return analysis;
}

GraphStatsResult collectGraphStatsFromInitial(const GraphStatsOptions& options) {
    return collectGraphStats(initialPosition(), options);
}

GraphStatsResult collectGraphStats(const Position& initial, const GraphStatsOptions& options) {
    GraphBuildOptions graphOptions;
    graphOptions.maxStates = options.maxStates;
    graphOptions.backend = options.graphBackend;
    graphOptions.storePred = options.storePred;
    graphOptions.progressInterval = options.progressInterval;
    graphOptions.progress = options.progress;

    const StateGraph graph = buildReachableGraph(packPosition(initial), graphOptions);
    GraphStatsResult result;
    result.stats = makeGraphOnlyStats(graph);
    result.memory = estimateMemoryUse(result.stats);
    return result;
}

MemoryEstimate estimateMemoryUse(const SolveStats& stats) {
    MemoryEstimate estimate;
    const uint64_t storedEdges = stats.storedEdges != 0 || stats.totalEdges == 0 ? stats.storedEdges : stats.totalEdges;
    estimate.compactTable16Bytes = stats.reachableStates * 16u;
    estimate.compactTable24Bytes = stats.reachableStates * 24u;
    estimate.statesVectorBytes = stats.reachableStates * sizeof(uint64_t);
    estimate.outcomeVectorBytes = stats.reachableStates * sizeof(StateInfo);
    estimate.remainingVectorBytes = stats.reachableStates * sizeof(int);
    estimate.flatEdgesBytesOneDirection = storedEdges * sizeof(uint32_t);
    estimate.flatEdgesBytesBothDirections = estimate.flatEdgesBytesOneDirection * 2u;
    estimate.vectorVectorOverheadSucc = stats.reachableStates * sizeof(std::vector<int>);
    estimate.vectorVectorOverheadPred = stats.storesPred ? stats.reachableStates * sizeof(std::vector<int>) : 0;
    estimate.vectorSuccOverheadBytes = estimate.vectorVectorOverheadSucc;
    estimate.vectorPredOverheadBytes = estimate.vectorVectorOverheadPred;
    estimate.vectorTotalGraphEstimateBytes =
        estimate.statesVectorBytes +
        estimate.flatEdgesBytesBothDirections +
        estimate.vectorVectorOverheadSucc +
        estimate.vectorVectorOverheadPred;
    estimate.csrSuccFlatBytes = storedEdges * sizeof(uint32_t);
    estimate.csrSuccOffsetBytes = (stats.reachableStates + 1u) * sizeof(uint64_t);
    estimate.csrPredFlatBytes = stats.storesPred ? storedEdges * sizeof(uint32_t) : 0;
    estimate.csrPredOffsetBytes = stats.storesPred ? (stats.reachableStates + 1u) * sizeof(uint64_t) : 0;
    estimate.csrTotalGraphBytes =
        estimate.statesVectorBytes +
        estimate.csrSuccFlatBytes +
        estimate.csrSuccOffsetBytes +
        estimate.csrPredFlatBytes +
        estimate.csrPredOffsetBytes;
    estimate.roughTotalCurrentGraphBytes =
        stats.graphBackend == GraphBackend::Csr
            ? estimate.csrTotalGraphBytes + estimate.outcomeVectorBytes + estimate.remainingVectorBytes
            : estimate.vectorTotalGraphEstimateBytes + estimate.outcomeVectorBytes + estimate.remainingVectorBytes;
    return estimate;
}

std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    if (unit == 0) {
        out << bytes << " B";
    } else {
        out << std::fixed << std::setprecision(1) << value << ' ' << units[unit];
    }
    return out.str();
}

std::string formatDuration(double seconds) {
    if (seconds < 60.0) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(seconds < 10.0 ? 2 : 1) << seconds << 's';
        return out.str();
    }

    const auto total = static_cast<uint64_t>(seconds + 0.5);
    const uint64_t hours = total / 3600;
    const uint64_t minutes = (total % 3600) / 60;
    const uint64_t secs = total % 60;

    std::ostringstream out;
    out << std::setfill('0');
    if (hours > 0) {
        out << std::setw(2) << hours << ':' << std::setw(2) << minutes << ':' << std::setw(2) << secs;
    } else {
        out << std::setw(2) << minutes << ':' << std::setw(2) << secs;
    }
    return out.str();
}

std::string formatDropRatio(uint64_t droppedEdges, uint64_t generatedEdges) {
    const double ratio =
        generatedEdges == 0 ? 0.0 : static_cast<double>(droppedEdges) * 100.0 / static_cast<double>(generatedEdges);
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << ratio << '%';
    return out.str();
}

}  // namespace sanpao15
