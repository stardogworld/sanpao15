#include "sanpao15/lowk_tablebase.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <deque>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

bool bitIsSet(uint32_t mask, int square) {
    return (mask & (uint32_t{1} << square)) != 0;
}

uint32_t bitForSquare(int square) {
    return uint32_t{1} << square;
}

template <typename Fn>
void forEachSetSquare(uint32_t mask, Fn&& fn) {
    mask &= BoardMask;
    while (mask != 0) {
        const int square = static_cast<int>(std::countr_zero(mask));
        fn(square);
        mask &= mask - 1u;
    }
}

template <typename Fn>
void forEachOrthogonalNeighbor(int square, Fn&& fn) {
    const int row = square / BoardSize;
    const int col = square % BoardSize;
    if (row > 0) {
        fn(square - BoardSize);
    }
    if (row + 1 < BoardSize) {
        fn(square + BoardSize);
    }
    if (col > 0) {
        fn(square - 1);
    }
    if (col + 1 < BoardSize) {
        fn(square + 1);
    }
}

template <typename Fn>
void forEachCannonJump(int square, Fn&& fn) {
    const int row = square / BoardSize;
    const int col = square % BoardSize;
    const auto addJump = [&](int dRow, int dCol) {
        const int overRow = row + dRow;
        const int overCol = col + dCol;
        const int landingRow = row + 2 * dRow;
        const int landingCol = col + 2 * dCol;
        if (overRow < 0 || overRow >= BoardSize || overCol < 0 || overCol >= BoardSize) {
            return;
        }
        if (landingRow < 0 || landingRow >= BoardSize || landingCol < 0 || landingCol >= BoardSize) {
            return;
        }
        fn(overRow * BoardSize + overCol, landingRow * BoardSize + landingCol);
    };

    addJump(-1, 0);
    addJump(1, 0);
    addJump(0, -1);
    addJump(0, 1);
}

void requireLowK(int maxK) {
    if (maxK < 0 || maxK > 3) {
        throw std::invalid_argument("low-k tablebase prototype supports K in 0..3");
    }
}

void requireStreamingLowK(int maxK) {
    if (maxK < 0 || maxK > 4) {
        throw std::invalid_argument("streaming low-k tablebase prototype supports K in 0..4");
    }
}

void requireLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 3) {
        throw std::invalid_argument("low-k dense layer solver supports soldier count in 0..3");
    }
}

void requireStreamingLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 4) {
        throw std::invalid_argument("streaming dense layer solver supports soldier count in 0..4");
    }
}

bool isWinForSide(Outcome outcome, Side side) {
    return outcome == winFor(side);
}

bool isOpponentWinForSide(Outcome outcome, Side side) {
    return outcome == opponentWinFor(side);
}

struct SolveGraph {
    std::vector<std::vector<uint32_t>> predecessors;
    std::vector<uint32_t> remainingNonOpponentWin;
};

uint64_t remainingArrayBytes(uint64_t stateCount) {
    return stateCount * static_cast<uint64_t>(sizeof(uint8_t));
}

void validateLayerSolveInputs(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    const PackedOutcomeTable2Bit& output) {
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (output.size() != stateCount) {
        throw std::invalid_argument("output table size does not match dense layer");
    }
    if (soldierCount > 0 && lowerLayer == nullptr) {
        throw std::invalid_argument("lower-layer table is required for soldierCount > 0");
    }
    if (soldierCount == 0 && lowerLayer != nullptr) {
        throw std::invalid_argument("lower-layer table must be null for soldierCount == 0");
    }
}

void countOutcome(DenseLayerSolveResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

void addOutcomeCount(DenseLayerSolveResult& result, Outcome outcome, uint64_t count) {
    switch (outcome) {
        case Outcome::Unknown:
            result.unknown += count;
            break;
        case Outcome::CannonWin:
            result.cannonWin += count;
            break;
        case Outcome::SoldierWin:
            result.soldierWin += count;
            break;
        case Outcome::Draw:
            result.draw += count;
            break;
    }
}

void countVerifyOutcome(LowKTablebaseVerifyLayerResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

void noteQueueSize(DenseLayerSolveResult& result, uint64_t queueSize) {
    result.queuePeak = std::max(result.queuePeak, queueSize);
}

bool solveForcedMaterialLayer(
    int soldierCount,
    PackedOutcomeTable2Bit& output,
    DenseLayerSolveResult& result,
    uint64_t estimatedMemoryBytes) {
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(soldierCount);
    if (!material.has_value()) {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < output.size(); ++index) {
        output.set(index, *material);
    }
    const auto finish = std::chrono::steady_clock::now();

    result.terminalStates = result.stateCount;
    result.resolvedByTerminal = result.stateCount;
    addOutcomeCount(result, *material, result.stateCount);
    result.unknown = 0;
    result.estimatedMemoryBytes = estimatedMemoryBytes;
    result.initializationSeconds = std::chrono::duration<double>(finish - start).count();
    result.seconds = result.initializationSeconds;
    return true;
}

LowKTablebaseLayerResult saveSolvedLayer(
    const DenseLayerSolveResult& solve,
    const PackedOutcomeTable2Bit& table,
    const std::filesystem::path& outputDir,
    DenseResultEncoding encoding) {
    const std::filesystem::path outputPath = lowKLayerResultPath(outputDir, solve.soldierCount);
    if (encoding == DenseResultEncoding::Packed2Bit) {
        saveDenseResultTable2Bit(table, solve.soldierCount, outputPath, StandardRulesetHash);
    } else {
        DenseOutcomeTable byteTable(table.size());
        for (uint64_t index = 0; index < table.size(); ++index) {
            byteTable.set(index, table.get(index));
        }
        saveDenseResultTable(byteTable, solve.soldierCount, outputPath, StandardRulesetHash);
    }

    (void)validateDenseResultFile(outputPath, StandardRulesetHash, solve.soldierCount);
    return LowKTablebaseLayerResult{
        solve,
        outputPath,
        std::filesystem::file_size(outputPath),
        encoding,
    };
}

PackedOutcomeTable2Bit loadDenseResultAnyEncoding(
    const std::filesystem::path& path,
    int soldierCount) {
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, soldierCount);
    if (info.encoding == DenseResultEncoding::Packed2Bit) {
        return loadDenseResultTable2Bit(path, StandardRulesetHash, soldierCount);
    }

    const DenseOutcomeTable byteTable = loadDenseResultTableByte(path, StandardRulesetHash, soldierCount);
    PackedOutcomeTable2Bit packed(byteTable.size());
    for (uint64_t index = 0; index < byteTable.size(); ++index) {
        packed.set(index, byteTable.get(index));
    }
    return packed;
}

Outcome successorOutcome(
    const DenseSuccessor& successor,
    const PackedOutcomeTable2Bit& sameLayer,
    const PackedOutcomeTable2Bit* lowerLayer) {
    if (successor.kind == DenseSuccessorKind::SameLayer) {
        return sameLayer.get(successor.toIndex);
    }
    if (lowerLayer == nullptr) {
        throw std::logic_error("capture successor requires lower-layer outcome table");
    }
    return lowerLayer->get(successor.toIndex);
}

void verifySolvedState(
    int soldierCount,
    uint64_t index,
    const PackedOutcomeTable2Bit& table,
    const PackedOutcomeTable2Bit* lowerLayer) {
    const Position pos = positionFromDenseIndex(soldierCount, index);
    const Outcome outcome = table.get(index);
    if (outcome == Outcome::Unknown) {
        throw std::runtime_error("low-k verifier found Unknown outcome");
    }

    const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
    if (terminal.terminal) {
        if (outcome != terminal.outcome) {
            throw std::runtime_error("low-k verifier found terminal outcome mismatch");
        }
        return;
    }

    const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
    if (successors.empty()) {
        if (outcome != opponentWinFor(pos.side)) {
            throw std::runtime_error("low-k verifier found no-move non-terminal mismatch");
        }
        return;
    }

    bool hasCurrentSideWin = false;
    bool hasDraw = false;
    for (const DenseSuccessor& successor : successors) {
        const Outcome child = successorOutcome(successor, table, lowerLayer);
        if (child == Outcome::Unknown) {
            throw std::runtime_error("low-k verifier found Unknown successor outcome");
        }
        if (isWinForSide(child, pos.side)) {
            hasCurrentSideWin = true;
        }
        if (child == Outcome::Draw) {
            hasDraw = true;
        }
    }

    if (isWinForSide(outcome, pos.side) && !hasCurrentSideWin) {
        throw std::runtime_error("low-k verifier found side-win state without winning successor");
    }
    if (isOpponentWinForSide(outcome, pos.side) && (hasCurrentSideWin || hasDraw)) {
        throw std::runtime_error("low-k verifier found opponent-win state with winning successor");
    }
    if (outcome == Outcome::Draw && (hasCurrentSideWin || !hasDraw)) {
        throw std::runtime_error("low-k verifier found invalid draw state");
    }
}

}  // namespace

uint8_t checkedStreamingRemainingCount(uint64_t remainingCount) {
    if (remainingCount > std::numeric_limits<uint8_t>::max()) {
        throw std::overflow_error("streaming remaining counter exceeds uint8_t");
    }
    return static_cast<uint8_t>(remainingCount);
}

DenseStreamingInitScan scanDenseStateForStreamingInitialization(
    int soldierCount,
    uint64_t index,
    const Position& pos,
    const PackedOutcomeTable2Bit* lowerLayer) {
    DenseStreamingInitScan scan;
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(soldierCount);
    if (material.has_value()) {
        scan.terminal = DenseTerminalInfo{true, *material};
        return scan;
    }
    if (soldierCount > 0 && lowerLayer == nullptr) {
        throw std::invalid_argument("lower-layer table is required for streaming initialization scan");
    }
    if (popcount25(pos.cannons) != 3 || popcount25(pos.soldiers) != soldierCount ||
        (pos.cannons & pos.soldiers) != 0) {
        throw std::invalid_argument("position does not match streaming initialization layer");
    }

    const uint32_t occupied = pos.cannons | pos.soldiers;
    bool cannonCanMove = false;
    if (pos.side == Side::Cannon) {
        forEachSetSquare(pos.cannons, [&](int from) {
            forEachOrthogonalNeighbor(from, [&](int to) {
                if (bitIsSet(occupied, to)) {
                    return;
                }
                cannonCanMove = true;
                ++scan.successorCount;
                ++scan.sameLayerEdges;
                if (!scan.resolved) {
                    ++scan.remainingCount;
                }
            });
            forEachCannonJump(from, [&](int over, int landing) {
                if (bitIsSet(occupied, over) || !bitIsSet(pos.soldiers, landing)) {
                    return;
                }
                cannonCanMove = true;
                ++scan.successorCount;
                ++scan.captureEdges;

                Position next = pos;
                next.cannons = (next.cannons & ~bitForSquare(from)) | bitForSquare(landing);
                next.soldiers &= ~bitForSquare(landing);
                next.side = Side::Soldier;
                const uint64_t toIndex = denseIndex(next);
                const Outcome child = lowerLayer->getUnchecked(toIndex);
                if (isWinForSide(child, pos.side) && !scan.resolved) {
                    scan.resolved = true;
                    scan.resolvedOutcome = child;
                } else if (child == Outcome::Draw && !scan.resolved) {
                    ++scan.remainingCount;
                }
            });
        });
        if (!cannonCanMove) {
            scan.terminal = DenseTerminalInfo{true, Outcome::SoldierWin};
        }
        return scan;
    }

    forEachSetSquare(pos.cannons, [&](int from) {
        if (cannonCanMove) {
            return;
        }
        forEachOrthogonalNeighbor(from, [&](int to) {
            if (!cannonCanMove && !bitIsSet(occupied, to)) {
                cannonCanMove = true;
            }
        });
        forEachCannonJump(from, [&](int over, int landing) {
            if (!cannonCanMove && !bitIsSet(occupied, over) && bitIsSet(pos.soldiers, landing)) {
                cannonCanMove = true;
            }
        });
    });
    if (!cannonCanMove) {
        scan.terminal = DenseTerminalInfo{true, Outcome::SoldierWin};
        return scan;
    }

    forEachSetSquare(pos.soldiers, [&](int from) {
        forEachOrthogonalNeighbor(from, [&](int to) {
            if (bitIsSet(occupied, to)) {
                return;
            }
            ++scan.successorCount;
            ++scan.sameLayerEdges;
            if (!scan.resolved) {
                ++scan.remainingCount;
            }
        });
    });
    if (scan.successorCount == 0) {
        scan.terminal = DenseTerminalInfo{true, Outcome::CannonWin};
    }
    (void)index;
    return scan;
}

DenseLayerSolveResult solveDenseLayerOutcome(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output) {
    requireLayer(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    validateLayerSolveInputs(soldierCount, lowerLayer, output);
    resetOutcomeTable(output);
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::overflow_error("low-k prototype predecessor graph requires 32-bit state ids");
    }

    const auto start = std::chrono::steady_clock::now();
    DenseLayerSolveResult result;
    result.soldierCount = soldierCount;
    result.stateCount = stateCount;
    if (solveForcedMaterialLayer(
            soldierCount,
            output,
            result,
            output.bytes() + stateCount * static_cast<uint64_t>(sizeof(uint32_t)))) {
        return result;
    }

    SolveGraph graph;
    graph.predecessors.resize(static_cast<size_t>(stateCount));
    graph.remainingNonOpponentWin.assign(static_cast<size_t>(stateCount), 0);

    std::deque<uint32_t> queue;

    for (uint64_t index = 0; index < stateCount; ++index) {
        const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
        if (terminal.terminal) {
            output.set(index, terminal.outcome);
            ++result.terminalStates;
            queue.push_back(static_cast<uint32_t>(index));
            ++result.resolvedByTerminal;
            noteQueueSize(result, queue.size());
            continue;
        }

        const Position pos = positionFromDenseIndex(soldierCount, index);
        const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
        bool resolved = false;
        uint32_t remaining = 0;
        for (const DenseSuccessor& successor : successors) {
            if (successor.kind == DenseSuccessorKind::SameLayer) {
                ++result.sameLayerEdges;
                graph.predecessors[static_cast<size_t>(successor.toIndex)].push_back(static_cast<uint32_t>(index));
                if (!resolved) {
                    ++remaining;
                    result.maxRemaining = std::max<uint64_t>(result.maxRemaining, remaining);
                }
                continue;
            }

            ++result.captureEdges;
            const Outcome child = lowerLayer->get(successor.toIndex);
            if (isWinForSide(child, pos.side) && !resolved) {
                output.set(index, child);
                queue.push_back(static_cast<uint32_t>(index));
                ++result.resolvedByLowerLayer;
                noteQueueSize(result, queue.size());
                resolved = true;
            }
            if (child == Outcome::Draw && !resolved) {
                ++remaining;
                result.maxRemaining = std::max<uint64_t>(result.maxRemaining, remaining);
            }
        }

        if (!resolved) {
            graph.remainingNonOpponentWin[static_cast<size_t>(index)] = remaining;
            if (remaining == 0) {
                output.set(index, opponentWinFor(pos.side));
                queue.push_back(static_cast<uint32_t>(index));
                ++result.resolvedByLowerLayer;
                noteQueueSize(result, queue.size());
            }
        }
    }

    while (!queue.empty()) {
        const uint32_t childIndex = queue.front();
        queue.pop_front();
        const Outcome childOutcome = output.get(childIndex);
        if (childOutcome == Outcome::Unknown || childOutcome == Outcome::Draw) {
            continue;
        }

        for (uint32_t parentIndex : graph.predecessors[childIndex]) {
            if (output.get(parentIndex) != Outcome::Unknown) {
                continue;
            }
            const Position parent = positionFromDenseIndex(soldierCount, parentIndex);
            if (isWinForSide(childOutcome, parent.side)) {
                output.set(parentIndex, childOutcome);
                ++result.retrogradeResolved;
                ++result.resolvedByPropagation;
                queue.push_back(parentIndex);
                noteQueueSize(result, queue.size());
                continue;
            }
            if (isOpponentWinForSide(childOutcome, parent.side)) {
                uint32_t& remaining = graph.remainingNonOpponentWin[parentIndex];
                if (remaining == 0) {
                    throw std::logic_error("remainingNonOpponentWin underflow");
                }
                --remaining;
                if (remaining == 0) {
                    output.set(parentIndex, opponentWinFor(parent.side));
                    ++result.retrogradeResolved;
                    ++result.resolvedByPropagation;
                    queue.push_back(parentIndex);
                    noteQueueSize(result, queue.size());
                }
            }
        }
    }

    for (uint64_t index = 0; index < stateCount; ++index) {
        Outcome outcome = output.get(index);
        if (outcome == Outcome::Unknown) {
            output.set(index, Outcome::Draw);
            outcome = Outcome::Draw;
            ++result.unresolvedAsDraw;
            ++result.drawAfterQueue;
        }
        countOutcome(result, outcome);
    }

    result.unknown = 0;
    result.estimatedMemoryBytes =
        output.bytes() + stateCount * static_cast<uint64_t>(sizeof(uint32_t)) +
        stateCount * static_cast<uint64_t>(sizeof(std::vector<uint32_t>));
    const auto finish = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(finish - start).count();
    return result;
}

DenseLayerSolveResult solveDenseLayerOutcomeStreaming(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output) {
    requireStreamingLayer(soldierCount);
    validateLayerSolveInputs(soldierCount, lowerLayer, output);
    resetOutcomeTable(output);
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("streaming low-k prototype requires size_t-addressable layers");
    }

    const auto start = std::chrono::steady_clock::now();
    DenseLayerSolveResult result;
    result.soldierCount = soldierCount;
    result.stateCount = stateCount;
    if (solveForcedMaterialLayer(
            soldierCount,
            output,
            result,
            output.bytes() + remainingArrayBytes(stateCount))) {
        return result;
    }

    std::vector<uint8_t> remaining(static_cast<size_t>(stateCount), 0);
    std::vector<uint64_t> queue;
    size_t queueHead = 0;
    std::vector<uint64_t> predecessorIndices;
    predecessorIndices.reserve(32);

    const auto initStart = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < stateCount; ++index) {
        const Position pos = positionFromDenseIndex(soldierCount, index);
        const DenseStreamingInitScan scan =
            scanDenseStateForStreamingInitialization(soldierCount, index, pos, lowerLayer);
        if (scan.terminal.terminal) {
            output.setUnchecked(index, scan.terminal.outcome);
            ++result.terminalStates;
            ++result.resolvedByTerminal;
            queue.push_back(index);
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
            continue;
        }

        result.maxSuccessors = std::max(result.maxSuccessors, scan.successorCount);
        result.sameLayerEdges += scan.sameLayerEdges;
        result.captureEdges += scan.captureEdges;
        if (scan.resolved) {
            output.setUnchecked(index, scan.resolvedOutcome);
            ++result.resolvedByLowerLayer;
            queue.push_back(index);
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
            continue;
        }

        remaining[static_cast<size_t>(index)] = checkedStreamingRemainingCount(scan.remainingCount);
        result.maxRemaining = std::max(result.maxRemaining, scan.remainingCount);
        if (scan.remainingCount == 0) {
            output.setUnchecked(index, opponentWinFor(pos.side));
                ++result.resolvedByLowerLayer;
                queue.push_back(index);
                noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
        }
    }
    const auto initFinish = std::chrono::steady_clock::now();
    result.initializationSeconds = std::chrono::duration<double>(initFinish - initStart).count();

    const auto propagationStart = std::chrono::steady_clock::now();
    if (result.terminalStates == stateCount) {
        queue.clear();
        queueHead = 0;
    }
    while (queueHead < queue.size()) {
        const uint64_t childIndex = queue[queueHead++];
        const Outcome childOutcome = output.getUnchecked(childIndex);
        if (childOutcome == Outcome::Unknown || childOutcome == Outcome::Draw) {
            continue;
        }

        const Position child = positionFromDenseIndex(soldierCount, childIndex);
        const Side parentSide = opposite(child.side);
        generateDensePredecessorIndicesFromPosition(
            soldierCount,
            childIndex,
            child,
            predecessorIndices);
        ++result.predecessorCalls;
        result.generatedPredecessors += static_cast<uint64_t>(predecessorIndices.size());
        result.maxPredecessors = std::max<uint64_t>(result.maxPredecessors, predecessorIndices.size());

        for (uint64_t parentIndex : predecessorIndices) {
            if (output.getUnchecked(parentIndex) != Outcome::Unknown) {
                continue;
            }
            if (isWinForSide(childOutcome, parentSide)) {
                output.setUnchecked(parentIndex, childOutcome);
                ++result.retrogradeResolved;
                ++result.resolvedByPropagation;
                queue.push_back(parentIndex);
                noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
                continue;
            }
            if (isOpponentWinForSide(childOutcome, parentSide)) {
                uint8_t& remainingCount = remaining[static_cast<size_t>(parentIndex)];
                if (remainingCount == 0) {
                    throw std::logic_error("streaming remaining counter underflow");
                }
                --remainingCount;
                if (remainingCount == 0) {
                    output.setUnchecked(parentIndex, opponentWinFor(parentSide));
                    ++result.retrogradeResolved;
                    ++result.resolvedByPropagation;
                    queue.push_back(parentIndex);
                    noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
                }
            }
        }
    }
    const auto propagationFinish = std::chrono::steady_clock::now();
    result.propagationSeconds = std::chrono::duration<double>(propagationFinish - propagationStart).count();

    const auto finalizeStart = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < stateCount; ++index) {
        Outcome outcome = output.getUnchecked(index);
        if (outcome == Outcome::Unknown) {
            output.setUnchecked(index, Outcome::Draw);
            outcome = Outcome::Draw;
            ++result.unresolvedAsDraw;
            ++result.drawAfterQueue;
        }
        countOutcome(result, outcome);
    }
    result.unknown = 0;
    result.estimatedMemoryBytes = output.bytes() + remainingArrayBytes(stateCount);
    const auto finalizeFinish = std::chrono::steady_clock::now();
    result.finalizeSeconds = std::chrono::duration<double>(finalizeFinish - finalizeStart).count();
    result.seconds = std::chrono::duration<double>(finalizeFinish - start).count();
    return result;
}

std::filesystem::path lowKLayerResultPath(const std::filesystem::path& dir, int soldierCount) {
    std::ostringstream name;
    name << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15res";
    return dir / name.str();
}

std::vector<LowKTablebaseLayerResult> solveLowKTablebase(const LowKTablebaseSolveOptions& options) {
    requireLowK(options.maxK);
    if (options.outputDir.empty()) {
        throw std::invalid_argument("low-k output directory is required");
    }
    std::filesystem::create_directories(options.outputDir);

    std::vector<LowKTablebaseLayerResult> results;
    std::vector<PackedOutcomeTable2Bit> solvedLayers;
    solvedLayers.reserve(static_cast<size_t>(options.maxK + 1));

    for (int k = 0; k <= options.maxK; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solvedLayers[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult solve = solveDenseLayerOutcome(k, lower, table);

        results.push_back(saveSolvedLayer(solve, table, options.outputDir, options.encoding));
        solvedLayers.push_back(std::move(table));
    }
    return results;
}

std::vector<LowKTablebaseLayerResult> solveLowKTablebaseStreaming(const LowKTablebaseSolveOptions& options) {
    requireStreamingLowK(options.maxK);
    if (options.outputDir.empty()) {
        throw std::invalid_argument("streaming low-k output directory is required");
    }
    std::filesystem::create_directories(options.outputDir);

    std::vector<LowKTablebaseLayerResult> results;
    std::vector<PackedOutcomeTable2Bit> solvedLayers;
    solvedLayers.reserve(static_cast<size_t>(options.maxK + 1));

    for (int k = 0; k <= options.maxK; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solvedLayers[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult solve = solveDenseLayerOutcomeStreaming(k, lower, table);

        results.push_back(saveSolvedLayer(solve, table, options.outputDir, options.encoding));
        solvedLayers.push_back(std::move(table));
    }
    return results;
}

LowKTablebaseVerifyResult verifyLowKTablebase(
    const std::filesystem::path& dir,
    int maxK,
    uint64_t sampleLimit) {
    requireStreamingLowK(maxK);
    LowKTablebaseVerifyResult result;
    result.inputDir = dir;
    result.maxK = maxK;
    result.sampleLimit = sampleLimit;

    std::vector<PackedOutcomeTable2Bit> tables;
    tables.reserve(static_cast<size_t>(maxK + 1));
    for (int k = 0; k <= maxK; ++k) {
        const std::filesystem::path path = lowKLayerResultPath(dir, k);
        const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, k);
        PackedOutcomeTable2Bit table = loadDenseResultAnyEncoding(path, k);

        LowKTablebaseVerifyLayerResult layer;
        layer.soldierCount = k;
        layer.stateCount = table.size();
        layer.encoding = info.encoding;

        for (uint64_t index = 0; index < table.size(); ++index) {
            countVerifyOutcome(layer, table.get(index));
        }

        if (soldiersAreBelowSurvivalLimit(k) && layer.cannonWin != layer.stateCount) {
            throw std::runtime_error("low-k verifier expected every material-rule layer state to be CannonWin");
        }
        if (layer.unknown != 0) {
            throw std::runtime_error("low-k verifier found Unknown outcomes");
        }

        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &tables[static_cast<size_t>(k - 1)];
        if (sampleLimit == 0 || sampleLimit >= table.size()) {
            layer.sampledStates = table.size();
            for (uint64_t index = 0; index < table.size(); ++index) {
                verifySolvedState(k, index, table, lower);
            }
        } else {
            layer.sampledStates = sampleLimit;
            uint64_t rng = 0x6C6F776B74626C75ull ^ table.size();
            for (uint64_t i = 0; i < sampleLimit; ++i) {
                rng = rng * 6364136223846793005ull + 1442695040888963407ull;
                verifySolvedState(k, rng % table.size(), table, lower);
            }
        }

        result.layers.push_back(layer);
        tables.push_back(std::move(table));
    }

    return result;
}

}  // namespace sanpao15
