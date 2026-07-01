#include "sanpao15/table.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>

#include "sanpao15/bitboard.h"
#include "sanpao15/position.h"
#include "sanpao15/rules.h"

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> TableMagic{'S', '1', '5', 'T', 'B', 'L', '1', '\0'};
using Clock = std::chrono::steady_clock;

uint32_t checkedStateId(size_t id) {
    if (id > std::numeric_limits<uint32_t>::max()) {
        throw std::overflow_error("state id exceeds uint32_t range required by CSR graph");
    }
    return static_cast<uint32_t>(id);
}

void recordEdges(
    StateGraph& graph,
    int soldierCount,
    uint64_t generatedEdges,
    uint64_t storedEdges,
    uint64_t droppedEdges) {
    graph.generatedEdges += generatedEdges;
    graph.storedEdges += storedEdges;
    graph.droppedEdges += droppedEdges;
    graph.totalEdges = graph.storedEdges;

    if (soldierCount < 0 || soldierCount >= static_cast<int>(graph.edgesBySoldierCount.size())) {
        return;
    }

    graph.generatedEdgesBySoldierCount[soldierCount] += generatedEdges;
    graph.storedEdgesBySoldierCount[soldierCount] += storedEdges;
    graph.droppedEdgesBySoldierCount[soldierCount] += droppedEdges;
    graph.edgesBySoldierCount[soldierCount] = graph.storedEdgesBySoldierCount[soldierCount];
}

ProgressInfo graphProgress(
    uint64_t processed,
    uint64_t states,
    uint64_t storedEdges,
    uint64_t queueSize,
    double elapsedSeconds,
    const StateGraph& graph) {
    ProgressInfo info;
    info.stage = "build graph";
    info.processed = processed;
    info.total = states;
    info.states = states;
    info.edges = storedEdges;
    info.queueSize = queueSize;
    info.elapsedSeconds = elapsedSeconds;
    info.generatedEdges = graph.generatedEdges;
    info.storedEdges = graph.storedEdges;
    info.droppedEdges = graph.droppedEdges;
    return info;
}

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

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error("failed to write table byte");
    }
}

void writeI8(std::ostream& output, int8_t value) {
    writeU8(output, static_cast<uint8_t>(value));
}

void writeU32LE(std::ostream& output, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void writeI32LE(std::ostream& output, int32_t value) {
    writeU32LE(output, static_cast<uint32_t>(value));
}

void writeU64LE(std::ostream& output, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

uint8_t readU8(std::istream& input) {
    const int value = input.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected end of table file");
    }
    return static_cast<uint8_t>(value);
}

int8_t readI8(std::istream& input) {
    return static_cast<int8_t>(readU8(input));
}

uint32_t readU32LE(std::istream& input) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(readU8(input)) << shift;
    }
    return value;
}

int32_t readI32LE(std::istream& input) {
    return static_cast<int32_t>(readU32LE(input));
}

uint64_t readU64LE(std::istream& input) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(readU8(input)) << shift;
    }
    return value;
}

uint32_t tableFlags(const ResultTable& table) {
    uint32_t flags = 0;
    if (table.exact) {
        flags |= ResultTableFlagExact;
    }
    if (table.truncated) {
        flags |= ResultTableFlagTruncated;
    }
    return flags;
}

uint8_t moveFlags(const std::optional<Move>& move) {
    if (!move.has_value()) {
        return 0;
    }
    uint8_t flags = 1u;
    if (move->capture) {
        flags |= 1u << 1;
    }
    return flags;
}

Outcome readOutcome(uint8_t value) {
    switch (value) {
        case 0:
            return Outcome::Unknown;
        case 1:
            return Outcome::CannonWin;
        case 2:
            return Outcome::SoldierWin;
        case 3:
            return Outcome::Draw;
        default:
            throw std::runtime_error("table contains an invalid outcome value");
    }
}

StateGraph buildVectorGraph(uint64_t initialKey, const GraphBuildOptions& options, const Clock::time_point& started) {
    StateGraph graph;
    graph.backend = GraphBackend::Vector;
    graph.storesPred = options.storePred;
    std::queue<int> pending;

    const auto addState = [&](uint64_t key) -> int {
        const auto existing = graph.id.find(key);
        if (existing != graph.id.end()) {
            return existing->second;
        }
        if (options.maxStates != 0 && graph.states.size() >= options.maxStates) {
            graph.truncated = true;
            return -1;
        }

        const int newId = static_cast<int>(graph.states.size());
        graph.id.emplace(key, newId);
        graph.states.push_back(key);
        graph.succ.emplace_back();
        graph.succMoves.emplace_back();
        if (graph.storesPred) {
            graph.pred.emplace_back();
        }
        graph.outdegree.push_back(0);
        pending.push(newId);
        graph.maxBfsQueueSize = std::max<uint64_t>(graph.maxBfsQueueSize, pending.size());
        return newId;
    };

    addState(initialKey);

    uint64_t processed = 0;
    reportProgress(
        options.progress,
        graphProgress(processed, graph.states.size(), graph.storedEdges, pending.size(), 0.0, graph),
        options.progressInterval);

    while (!pending.empty()) {
        const int stateId = pending.front();
        pending.pop();
        ++processed;

        const Position pos = unpackPosition(graph.states[stateId]);
        if (isTerminal(pos)) {
            const auto now = Clock::now();
            reportProgress(
                options.progress,
                graphProgress(
                    processed,
                    graph.states.size(),
                    graph.storedEdges,
                    pending.size(),
                    std::chrono::duration<double>(now - started).count(),
                    graph),
                options.progressInterval);
            continue;
        }

        const std::vector<Move> moves = generateLegalMoves(pos);
        graph.outdegree[stateId] = static_cast<int>(moves.size());
        const int soldierCount = popcount25(pos.soldiers);
        const uint64_t generatedEdges = static_cast<uint64_t>(moves.size());
        uint64_t storedEdges = 0;
        uint64_t droppedEdges = 0;
        for (const Move& move : moves) {
            const Position next = applyMove(pos, move);
            const uint64_t nextKey = packPosition(next);
            const int nextId = addState(nextKey);
            if (nextId < 0) {
                ++droppedEdges;
                continue;
            }
            graph.succ[stateId].push_back(nextId);
            graph.succMoves[stateId].push_back(move);
            ++storedEdges;
            if (graph.storesPred) {
                graph.pred[nextId].push_back(stateId);
            }
        }
        recordEdges(graph, soldierCount, generatedEdges, storedEdges, droppedEdges);
        const auto now = Clock::now();
        reportProgress(
            options.progress,
            graphProgress(
                processed,
                graph.states.size(),
                graph.storedEdges,
                pending.size(),
                std::chrono::duration<double>(now - started).count(),
                graph),
            options.progressInterval);
    }

    graph.buildGraphSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    reportProgress(
        options.progress,
        graphProgress(graph.states.size(), graph.states.size(), graph.storedEdges, pending.size(), graph.buildGraphSeconds, graph),
        options.progressInterval);
    return graph;
}

void buildCsrPred(StateGraph& graph, const GraphBuildOptions& options, const Clock::time_point& started) {
    if (!graph.storesPred) {
        return;
    }

    const uint64_t stateCount = static_cast<uint64_t>(graph.states.size());
    reportProgress(
        options.progress,
        ProgressInfo{"build pred", 0, stateCount, stateCount, graph.totalEdges, 0, 0,
                     std::chrono::duration<double>(Clock::now() - started).count(), {}},
        options.progressInterval);

    std::vector<uint32_t> predDegree(graph.states.size(), 0);
    uint64_t processed = 0;
    for (uint32_t target : graph.csr.succFlat) {
        ++predDegree[target];
        ++processed;
        reportProgress(
            options.progress,
            ProgressInfo{"build pred", processed, graph.csr.succFlat.size(), stateCount, graph.totalEdges, 0, 0,
                         std::chrono::duration<double>(Clock::now() - started).count(), {}},
            options.progressInterval);
    }

    graph.csr.predOffset.assign(graph.states.size() + 1, 0);
    for (size_t i = 0; i < graph.states.size(); ++i) {
        graph.csr.predOffset[i + 1] = graph.csr.predOffset[i] + predDegree[i];
    }
    if (graph.csr.predOffset.back() != graph.csr.succFlat.size()) {
        throw std::logic_error("CSR predecessor edge count does not match successor edge count");
    }

    graph.csr.predFlat.assign(graph.csr.predOffset.back(), 0);
    std::vector<uint64_t> cursor = graph.csr.predOffset;
    for (size_t u = 0; u < graph.states.size(); ++u) {
        for (uint64_t edge = graph.csr.succOffset[u]; edge < graph.csr.succOffset[u + 1]; ++edge) {
            const uint32_t v = graph.csr.succFlat[edge];
            graph.csr.predFlat[cursor[v]++] = checkedStateId(u);
        }
    }

    reportProgress(
        options.progress,
        ProgressInfo{"build pred", stateCount, stateCount, stateCount, graph.totalEdges, 0, 0,
                     std::chrono::duration<double>(Clock::now() - started).count(), {}},
        options.progressInterval);
}

StateGraph buildCsrGraph(uint64_t initialKey, const GraphBuildOptions& options, const Clock::time_point& started) {
    StateGraph graph;
    graph.backend = GraphBackend::Csr;
    graph.storesPred = options.storePred;
    std::queue<uint32_t> pending;

    const auto addState = [&](uint64_t key) -> int {
        const auto existing = graph.id.find(key);
        if (existing != graph.id.end()) {
            return existing->second;
        }
        if (options.maxStates != 0 && graph.states.size() >= options.maxStates) {
            graph.truncated = true;
            return -1;
        }

        const uint32_t newId = checkedStateId(graph.states.size());
        graph.id.emplace(key, static_cast<int>(newId));
        graph.states.push_back(key);
        graph.outdegree.push_back(0);
        pending.push(newId);
        graph.maxBfsQueueSize = std::max<uint64_t>(graph.maxBfsQueueSize, pending.size());
        return static_cast<int>(newId);
    };

    addState(initialKey);
    graph.csr.succOffset.push_back(0);

    uint64_t processed = 0;
    reportProgress(
        options.progress,
        graphProgress(processed, graph.states.size(), graph.storedEdges, pending.size(), 0.0, graph),
        options.progressInterval);

    while (!pending.empty()) {
        const uint32_t stateId = pending.front();
        pending.pop();
        ++processed;

        while (graph.csr.succOffset.size() <= static_cast<size_t>(stateId)) {
            graph.csr.succOffset.push_back(graph.csr.succFlat.size());
        }

        const Position pos = unpackPosition(graph.states[stateId]);
        if (!isTerminal(pos)) {
            const std::vector<Move> moves = generateLegalMoves(pos);
            graph.outdegree[stateId] = static_cast<int>(moves.size());
            const int soldierCount = popcount25(pos.soldiers);
            const uint64_t generatedEdges = static_cast<uint64_t>(moves.size());
            uint64_t storedEdges = 0;
            uint64_t droppedEdges = 0;

            for (const Move& move : moves) {
                const Position next = applyMove(pos, move);
                const uint64_t nextKey = packPosition(next);
                const int nextId = addState(nextKey);
                if (nextId < 0) {
                    ++droppedEdges;
                    continue;
                }
                graph.csr.succFlat.push_back(checkedStateId(static_cast<size_t>(nextId)));
                ++storedEdges;
            }
            recordEdges(graph, soldierCount, generatedEdges, storedEdges, droppedEdges);
        }

        while (graph.csr.succOffset.size() <= static_cast<size_t>(stateId + 1)) {
            graph.csr.succOffset.push_back(graph.csr.succFlat.size());
        }

        const auto now = Clock::now();
        reportProgress(
            options.progress,
            graphProgress(
                processed,
                graph.states.size(),
                graph.storedEdges,
                pending.size(),
                std::chrono::duration<double>(now - started).count(),
                graph),
            options.progressInterval);
    }

    while (graph.csr.succOffset.size() < graph.states.size() + 1) {
        graph.csr.succOffset.push_back(graph.csr.succFlat.size());
    }
    if (graph.csr.succOffset.size() != graph.states.size() + 1) {
        throw std::logic_error("CSR successor offsets have invalid size");
    }
    if (graph.csr.succFlat.size() != graph.storedEdges || graph.storedEdges != graph.totalEdges) {
        throw std::logic_error("CSR successor edge count does not match totalEdges");
    }

    buildCsrPred(graph, options, started);

    graph.buildGraphSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    reportProgress(
        options.progress,
        graphProgress(graph.states.size(), graph.states.size(), graph.storedEdges, pending.size(), graph.buildGraphSeconds, graph),
        options.progressInterval);
    return graph;
}

}  // namespace

StateGraph buildReachableGraph(uint64_t initialKey, const GraphBuildOptions& options) {
    const auto started = Clock::now();
    if (options.backend == GraphBackend::Vector) {
        return buildVectorGraph(initialKey, options, started);
    }
    return buildCsrGraph(initialKey, options, started);
}

StateGraph buildReachableGraph(uint64_t initialKey, uint64_t maxStates) {
    GraphBuildOptions options;
    options.maxStates = maxStates;
    return buildReachableGraph(initialKey, options);
}

const StateInfo* findStateInfo(const ResultTable& table, uint64_t key) {
    const auto found = table.entries.find(key);
    return found == table.entries.end() ? nullptr : &found->second;
}

void saveResultTable(
    const ResultTable& table,
    const std::filesystem::path& path,
    uint64_t progressInterval,
    const ProgressCallback& progress) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open table file for writing: " + path.string());
    }

    output.write(TableMagic.data(), static_cast<std::streamsize>(TableMagic.size()));
    writeU32LE(output, ResultTableVersion);
    writeU32LE(output, tableFlags(table));
    writeU64LE(output, static_cast<uint64_t>(table.entries.size()));
    writeU64LE(output, table.initialKey);
    writeU64LE(output, table.rulesetHash);

    std::vector<uint64_t> keys;
    keys.reserve(table.entries.size());
    for (const auto& [key, _] : table.entries) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    uint64_t written = 0;
    reportProgress(progress, ProgressInfo{"write table", written, keys.size(), 0, 0, 0, 0, 0.0, {}}, progressInterval);
    for (uint64_t key : keys) {
        const StateInfo& info = table.entries.at(key);
        const bool hasBestMove = info.bestMove.has_value();
        const int8_t bestFrom = hasBestMove ? static_cast<int8_t>(info.bestMove->from) : int8_t{-1};
        const int8_t bestTo = hasBestMove ? static_cast<int8_t>(info.bestMove->to) : int8_t{-1};
        const int8_t bestCapturedSquare =
            hasBestMove && info.bestMove->capture ? static_cast<int8_t>(info.bestMove->capturedSquare) : int8_t{-1};

        writeU64LE(output, key);
        writeU8(output, static_cast<uint8_t>(info.outcome));
        writeI32LE(output, static_cast<int32_t>(info.distance));
        writeI8(output, bestFrom);
        writeI8(output, bestTo);
        writeI8(output, bestCapturedSquare);
        writeU8(output, moveFlags(info.bestMove));

        ++written;
        reportProgress(progress, ProgressInfo{"write table", written, keys.size(), 0, 0, 0, 0, 0.0, {}}, progressInterval);
    }
}

ResultTable loadResultTable(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open table file for reading: " + path.string());
    }

    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != TableMagic) {
        throw std::runtime_error("invalid table magic");
    }

    const uint32_t version = readU32LE(input);
    if (version != ResultTableVersion) {
        throw std::runtime_error("unsupported table version");
    }

    const uint32_t flags = readU32LE(input);
    const uint64_t stateCount = readU64LE(input);

    ResultTable table;
    table.exact = (flags & ResultTableFlagExact) != 0;
    table.truncated = (flags & ResultTableFlagTruncated) != 0;
    table.initialKey = readU64LE(input);
    table.rulesetHash = readU64LE(input);
    table.entries.reserve(static_cast<size_t>(stateCount));

    for (uint64_t index = 0; index < stateCount; ++index) {
        const uint64_t key = readU64LE(input);
        StateInfo info;
        info.outcome = readOutcome(readU8(input));
        info.distance = readI32LE(input);
        const int8_t bestFrom = readI8(input);
        const int8_t bestTo = readI8(input);
        const int8_t bestCapturedSquare = readI8(input);
        const uint8_t entryFlags = readU8(input);
        const bool hasBestMove = (entryFlags & 1u) != 0;
        const bool isCapture = (entryFlags & (1u << 1)) != 0;

        if (hasBestMove) {
            info.bestMove = Move{
                static_cast<int>(bestFrom),
                static_cast<int>(bestTo),
                isCapture,
                isCapture ? static_cast<int>(bestCapturedSquare) : -1,
            };
        }

        table.entries.emplace(key, info);
    }

    return table;
}

}  // namespace sanpao15
