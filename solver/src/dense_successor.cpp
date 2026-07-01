#include "sanpao15/dense_successor.h"

#include <algorithm>
#include <tuple>
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

bool hasSameLayerSuccessorTo(
    int soldierCount,
    uint64_t parentIndex,
    uint64_t childIndex,
    Move* moveOut) {
    for (const DenseSuccessor& successor : generateDenseSuccessors(soldierCount, parentIndex)) {
        if (successor.kind == DenseSuccessorKind::SameLayer && successor.toIndex == childIndex) {
            if (moveOut != nullptr) {
                *moveOut = successor.move;
            }
            return true;
        }
    }
    return false;
}

void addPredecessorIfValid(
    std::vector<DensePredecessor>& predecessors,
    int soldierCount,
    uint64_t childIndex,
    Side childSide,
    const Position& parent,
    Move move,
    DensePredecessorValidation validation) {
    if (popcount25(parent.cannons) != 3 || popcount25(parent.soldiers) != soldierCount ||
        (parent.cannons & parent.soldiers) != 0 || parent.side != opposite(childSide)) {
        return;
    }

    const uint64_t parentIndex = denseIndex(parent);
    if (parentIndex >= denseStateCount(soldierCount)) {
        throw std::logic_error("generated dense predecessor index is outside source layer");
    }

    if (validation == DensePredecessorValidation::Checked) {
        Move checkedMove;
        if (!hasSameLayerSuccessorTo(soldierCount, parentIndex, childIndex, &checkedMove)) {
            return;
        }
        move = checkedMove;
    }
    predecessors.push_back(DensePredecessor{
        soldierCount,
        parentIndex,
        move,
    });
}

void deduplicatePredecessors(std::vector<DensePredecessor>& predecessors) {
    std::sort(predecessors.begin(), predecessors.end(), [](const DensePredecessor& lhs, const DensePredecessor& rhs) {
        return std::tie(lhs.index, lhs.move.from, lhs.move.to, lhs.move.capture, lhs.move.capturedSquare) <
               std::tie(rhs.index, rhs.move.from, rhs.move.to, rhs.move.capture, rhs.move.capturedSquare);
    });
    predecessors.erase(
        std::unique(predecessors.begin(), predecessors.end(), [](const DensePredecessor& lhs, const DensePredecessor& rhs) {
            return lhs.index == rhs.index && lhs.move == rhs.move;
        }),
        predecessors.end());
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

std::vector<DensePredecessor> generateDensePredecessors(
    int soldierCount,
    uint64_t childIndex,
    DensePredecessorValidation validation) {
    requireSoldierCount(soldierCount);
    if (childIndex >= denseStateCount(soldierCount)) {
        throw std::out_of_range("dense predecessor child index is outside the layer state count");
    }

    const Position child = unrankDensePosition(soldierCount, childIndex);
    const uint32_t occupied = child.cannons | child.soldiers;
    std::vector<DensePredecessor> predecessors;

    if (child.side == Side::Soldier) {
        for (int to : squaresInMask(child.cannons)) {
            for (int from : orthogonalNeighbors(to)) {
                if (hasBit(occupied, from)) {
                    continue;
                }
                Position parent = child;
                parent.side = Side::Cannon;
                parent.cannons = clearBit(parent.cannons, to);
                parent.cannons = setBit(parent.cannons, from);
                addPredecessorIfValid(
                    predecessors,
                    soldierCount,
                    childIndex,
                    child.side,
                    parent,
                    Move{from, to, false, -1},
                    validation);
            }
        }
        deduplicatePredecessors(predecessors);
        return predecessors;
    }

    for (int to : squaresInMask(child.soldiers)) {
        for (int from : orthogonalNeighbors(to)) {
            if (hasBit(occupied, from)) {
                continue;
            }
            Position parent = child;
            parent.side = Side::Soldier;
            parent.soldiers = clearBit(parent.soldiers, to);
            parent.soldiers = setBit(parent.soldiers, from);
            addPredecessorIfValid(
                predecessors,
                soldierCount,
                childIndex,
                child.side,
                parent,
                Move{from, to, false, -1},
                validation);
        }
    }
    deduplicatePredecessors(predecessors);
    return predecessors;
}

std::vector<DensePredecessor> generateDensePredecessors(int soldierCount, uint64_t childIndex) {
    return generateDensePredecessors(soldierCount, childIndex, DensePredecessorValidation::Checked);
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
