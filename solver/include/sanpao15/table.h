#pragma once

#include <cstdint>
#include <filesystem>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sanpao15/move.h"

namespace sanpao15 {

struct ProgressInfo {
    std::string_view stage;
    uint64_t processed = 0;
    uint64_t total = 0;
    uint64_t states = 0;
    // Compatibility field for edge count in progress output. During graph
    // building this is the number of stored graph edges.
    uint64_t edges = 0;
    uint64_t queueSize = 0;
    uint64_t solved = 0;
    double elapsedSeconds = 0.0;
    std::string_view message;
    uint64_t generatedEdges = 0;
    uint64_t storedEdges = 0;
    uint64_t droppedEdges = 0;
};

using ProgressCallback = std::function<void(const ProgressInfo& info)>;

constexpr uint32_t ResultTableVersion = 1;
constexpr uint32_t ResultTableFlagExact = 1u << 0;
constexpr uint32_t ResultTableFlagTruncated = 1u << 1;

// Fixed identifier for the current rules:
// 5x5, soldiers 0..14, cannons 21/22/23, cannon first,
// orthogonal moves, cannon-empty-soldier capture.
constexpr uint64_t StandardRulesetHash = 0x5331355F76315F01ull;

enum class GraphBackend {
    Vector,
    Csr,
};

struct CsrGraph {
    std::vector<uint32_t> succFlat;
    std::vector<uint64_t> succOffset;
    std::vector<uint32_t> predFlat;
    std::vector<uint64_t> predOffset;
};

struct StateGraph {
    std::unordered_map<uint64_t, int> id;
    std::vector<uint64_t> states;
    std::vector<std::vector<int>> succ;
    std::vector<std::vector<Move>> succMoves;
    std::vector<std::vector<int>> pred;
    CsrGraph csr;
    std::vector<int> outdegree;
    std::array<uint64_t, 16> generatedEdgesBySoldierCount{};
    std::array<uint64_t, 16> storedEdgesBySoldierCount{};
    std::array<uint64_t, 16> droppedEdgesBySoldierCount{};
    // Compatibility field. It is kept equal to storedEdgesBySoldierCount.
    std::array<uint64_t, 16> edgesBySoldierCount{};
    uint64_t generatedEdges = 0;
    uint64_t storedEdges = 0;
    uint64_t droppedEdges = 0;
    // Compatibility field. It is kept equal to storedEdges.
    uint64_t totalEdges = 0;
    uint64_t maxBfsQueueSize = 0;
    double buildGraphSeconds = 0.0;
    GraphBackend backend = GraphBackend::Csr;
    bool storesPred = true;
    bool truncated = false;
};

struct StateInfo {
    Outcome outcome = Outcome::Unknown;
    int distance = -1;
    std::optional<Move> bestMove;
};

struct ResultTable {
    uint64_t initialKey = 0;
    uint64_t rulesetHash = StandardRulesetHash;
    bool exact = false;
    bool truncated = false;
    std::unordered_map<uint64_t, StateInfo> entries;
};

struct GraphBuildOptions {
    uint64_t maxStates = 0;
    GraphBackend backend = GraphBackend::Csr;
    bool storePred = true;
    uint64_t progressInterval = 0;
    ProgressCallback progress;
};

StateGraph buildReachableGraph(uint64_t initialKey, const GraphBuildOptions& options);
StateGraph buildReachableGraph(uint64_t initialKey, uint64_t maxStates = 0);

const StateInfo* findStateInfo(const ResultTable& table, uint64_t key);

void saveResultTable(
    const ResultTable& table,
    const std::filesystem::path& path,
    uint64_t progressInterval = 0,
    const ProgressCallback& progress = {});
ResultTable loadResultTable(const std::filesystem::path& path);

}  // namespace sanpao15
