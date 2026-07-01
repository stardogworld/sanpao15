#include "sanpao15/dense_successor.h"

#include <algorithm>
#include <stdexcept>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/rules.h"

namespace sanpao15 {

namespace {

void requireSoldierCount(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

DenseSuccessorKind successorKindForCounts(int fromSoldierCount, int toSoldierCount) {
    if (toSoldierCount == fromSoldierCount) {
        return DenseSuccessorKind::SameLayer;
    }
    if (fromSoldierCount > 0 && toSoldierCount == fromSoldierCount - 1) {
        return DenseSuccessorKind::CaptureToLowerLayer;
    }
    throw std::logic_error("dense successor changed soldier count by an invalid amount");
}

}  // namespace

std::vector<DenseSuccessor> generateDenseSuccessors(int soldierCount, uint64_t denseIndexValue) {
    requireSoldierCount(soldierCount);
    const Position pos = unrankDensePosition(soldierCount, denseIndexValue);
    const std::vector<Move> moves = generateLegalMoves(pos);

    std::vector<DenseSuccessor> successors;
    successors.reserve(moves.size());
    for (const Move& move : moves) {
        const Position next = applyMove(pos, move);
        if (popcount25(next.cannons) != 3 || (next.cannons & next.soldiers) != 0) {
            throw std::logic_error("generated successor is not a valid dense position");
        }
        const int toSoldierCount = popcount25(next.soldiers);
        const DenseSuccessorKind kind = successorKindForCounts(soldierCount, toSoldierCount);
        const uint64_t toIndex = denseIndex(next);
        if (toIndex >= denseStateCount(toSoldierCount)) {
            throw std::logic_error("generated dense successor index is outside target layer");
        }
        successors.push_back(DenseSuccessor{
            kind,
            soldierCount,
            toSoldierCount,
            denseIndexValue,
            toIndex,
            move,
        });
    }
    return successors;
}

DenseTerminalInfo terminalOutcomeForDenseState(int soldierCount, uint64_t denseIndexValue) {
    requireSoldierCount(soldierCount);
    const Position pos = unrankDensePosition(soldierCount, denseIndexValue);
    if (!isTerminal(pos)) {
        return DenseTerminalInfo{};
    }
    return DenseTerminalInfo{true, terminalOutcome(pos)};
}

DenseLayerMoveStats analyzeDenseLayerMoves(int soldierCount, uint64_t sampleLimit) {
    requireSoldierCount(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    const uint64_t statesToSample = sampleLimit == 0 ? stateCount : std::min(sampleLimit, stateCount);

    DenseLayerMoveStats stats;
    stats.soldierCount = soldierCount;
    stats.sampledStates = statesToSample;

    for (uint64_t index = 0; index < statesToSample; ++index) {
        const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
        if (terminal.terminal) {
            ++stats.terminalStates;
        }
        const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
        stats.totalSuccessors += static_cast<uint64_t>(successors.size());
        stats.maxSuccessors = std::max<uint64_t>(stats.maxSuccessors, successors.size());
        for (const DenseSuccessor& successor : successors) {
            switch (successor.kind) {
                case DenseSuccessorKind::SameLayer:
                    ++stats.sameLayerSuccessors;
                    break;
                case DenseSuccessorKind::CaptureToLowerLayer:
                    ++stats.captureSuccessors;
                    break;
            }
        }
    }
    return stats;
}

const char* denseSuccessorKindToString(DenseSuccessorKind kind) {
    switch (kind) {
        case DenseSuccessorKind::SameLayer:
            return "same-layer";
        case DenseSuccessorKind::CaptureToLowerLayer:
            return "capture-to-lower-layer";
    }
    return "unknown";
}

}  // namespace sanpao15
