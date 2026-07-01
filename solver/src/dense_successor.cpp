#include "sanpao15/dense_successor.h"

#include <algorithm>
#include <bit>
#include <optional>
#include <stdexcept>
#include <tuple>

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

bool cannonHasAnyMoveFast(const Position& pos) {
    const uint32_t occupied = pos.cannons | pos.soldiers;
    bool found = false;
    forEachSetSquare(pos.cannons, [&](int from) {
        if (found) {
            return;
        }
        forEachOrthogonalNeighbor(from, [&](int to) {
            if (!found && !bitIsSet(occupied, to)) {
                found = true;
            }
        });
        forEachCannonJump(from, [&](int over, int landing) {
            if (!found && !bitIsSet(occupied, over) && bitIsSet(pos.soldiers, landing)) {
                found = true;
            }
        });
    });
    return found;
}

bool soldierHasAnyMoveFast(const Position& pos) {
    const uint32_t occupied = pos.cannons | pos.soldiers;
    bool found = false;
    forEachSetSquare(pos.soldiers, [&](int from) {
        if (found) {
            return;
        }
        forEachOrthogonalNeighbor(from, [&](int to) {
            if (!found && !bitIsSet(occupied, to)) {
                found = true;
            }
        });
    });
    return found;
}

Position applyDenseMoveFast(const Position& pos, const Move& move) {
    Position next = pos;
    const uint32_t fromBit = bitForSquare(move.from);
    const uint32_t toBit = bitForSquare(move.to);
    if (pos.side == Side::Cannon) {
        next.cannons = (next.cannons & ~fromBit) | toBit;
        if (move.capture) {
            next.soldiers &= ~bitForSquare(move.capturedSquare);
        }
    } else {
        next.soldiers = (next.soldiers & ~fromBit) | toBit;
    }
    next.side = opposite(pos.side);
    return next;
}

void addDenseSuccessor(
    std::vector<DenseSuccessor>& successors,
    int soldierCount,
    uint64_t denseIndexValue,
    const Position& pos,
    Move move) {
    const Position next = applyDenseMoveFast(pos, move);
    const int toSoldierCount = popcount25(next.soldiers);
    const DenseSuccessorKind kind = successorKindForCounts(soldierCount, toSoldierCount);
    const uint64_t toIndex = denseIndex(next);
    successors.push_back(DenseSuccessor{
        kind,
        soldierCount,
        toSoldierCount,
        denseIndexValue,
        toIndex,
        move,
    });
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
    if (validation == DensePredecessorValidation::None) {
        predecessors.push_back(DensePredecessor{
            soldierCount,
            denseIndex(parent),
            move,
        });
        return;
    }

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
    return generateDenseSuccessorsFromPosition(soldierCount, denseIndexValue, pos);
}

std::vector<DenseSuccessor> generateDenseSuccessorsFromPosition(
    int soldierCount,
    uint64_t denseIndexValue,
    const Position& pos) {
    std::vector<DenseSuccessor> successors;
    generateDenseSuccessorsFromPosition(soldierCount, denseIndexValue, pos, successors);
    return successors;
}

void generateDenseSuccessorsFromPosition(
    int soldierCount,
    uint64_t denseIndexValue,
    const Position& pos,
    std::vector<DenseSuccessor>& out) {
    requireSoldierCount(soldierCount);
    if (denseIndexValue >= denseStateCount(soldierCount)) {
        throw std::out_of_range("dense successor source index is outside the layer state count");
    }
    if (popcount25(pos.cannons) != 3 || popcount25(pos.soldiers) != soldierCount ||
        (pos.cannons & pos.soldiers) != 0) {
        throw std::invalid_argument("position does not match dense successor source index");
    }

    out.clear();
    if (pos.side == Side::Cannon) {
        out.reserve(18);
        const uint32_t occupied = pos.cannons | pos.soldiers;
        forEachSetSquare(pos.cannons, [&](int from) {
            forEachOrthogonalNeighbor(from, [&](int to) {
                if (!bitIsSet(occupied, to)) {
                    addDenseSuccessor(out, soldierCount, denseIndexValue, pos, Move{from, to, false, -1});
                }
            });
            forEachCannonJump(from, [&](int over, int landing) {
                if (!bitIsSet(occupied, over) && bitIsSet(pos.soldiers, landing)) {
                    addDenseSuccessor(
                        out,
                        soldierCount,
                        denseIndexValue,
                        pos,
                        Move{from, landing, true, landing});
                }
            });
        });
    } else {
        out.reserve(64);
        const uint32_t occupied = pos.cannons | pos.soldiers;
        forEachSetSquare(pos.soldiers, [&](int from) {
            forEachOrthogonalNeighbor(from, [&](int to) {
                if (!bitIsSet(occupied, to)) {
                    addDenseSuccessor(out, soldierCount, denseIndexValue, pos, Move{from, to, false, -1});
                }
            });
        });
    }
}

std::vector<DensePredecessor> generateDensePredecessors(
    int soldierCount,
    uint64_t childIndex,
    DensePredecessorValidation validation) {
    std::vector<DensePredecessor> predecessors;
    generateDensePredecessors(soldierCount, childIndex, validation, predecessors);
    return predecessors;
}

void generateDensePredecessors(
    int soldierCount,
    uint64_t childIndex,
    DensePredecessorValidation validation,
    std::vector<DensePredecessor>& out) {
    requireSoldierCount(soldierCount);
    if (childIndex >= denseStateCount(soldierCount)) {
        throw std::out_of_range("dense predecessor child index is outside the layer state count");
    }

    const Position child = unrankDensePosition(soldierCount, childIndex);
    generateDensePredecessorsFromPosition(soldierCount, childIndex, child, validation, out);
}

void generateDensePredecessorsFromPosition(
    int soldierCount,
    uint64_t childIndex,
    const Position& child,
    DensePredecessorValidation validation,
    std::vector<DensePredecessor>& out) {
    requireSoldierCount(soldierCount);
    if (childIndex >= denseStateCount(soldierCount)) {
        throw std::out_of_range("dense predecessor child index is outside the layer state count");
    }
    if (popcount25(child.cannons) != 3 || popcount25(child.soldiers) != soldierCount ||
        (child.cannons & child.soldiers) != 0) {
        throw std::invalid_argument("position does not match dense predecessor child index");
    }

    const uint32_t occupied = child.cannons | child.soldiers;
    out.clear();

    if (child.side == Side::Soldier) {
        forEachSetSquare(child.cannons, [&](int to) {
            forEachOrthogonalNeighbor(to, [&](int from) {
                if (bitIsSet(occupied, from)) {
                    return;
                }
                Position parent = child;
                parent.side = Side::Cannon;
                parent.cannons = (parent.cannons & ~bitForSquare(to)) | bitForSquare(from);
                addPredecessorIfValid(
                    out,
                    soldierCount,
                    childIndex,
                    child.side,
                    parent,
                    Move{from, to, false, -1},
                    validation);
            });
        });
        deduplicatePredecessors(out);
        return;
    }

    forEachSetSquare(child.soldiers, [&](int to) {
        forEachOrthogonalNeighbor(to, [&](int from) {
            if (bitIsSet(occupied, from)) {
                return;
            }
            Position parent = child;
            parent.side = Side::Soldier;
            parent.soldiers = (parent.soldiers & ~bitForSquare(to)) | bitForSquare(from);
            addPredecessorIfValid(
                out,
                soldierCount,
                childIndex,
                child.side,
                parent,
                Move{from, to, false, -1},
                validation);
        });
    });
    deduplicatePredecessors(out);
}

std::vector<DensePredecessor> generateDensePredecessors(int soldierCount, uint64_t childIndex) {
    return generateDensePredecessors(soldierCount, childIndex, DensePredecessorValidation::Checked);
}

DenseTerminalInfo terminalOutcomeForDenseState(int soldierCount, uint64_t denseIndexValue) {
    requireSoldierCount(soldierCount);
    const Position pos = unrankDensePosition(soldierCount, denseIndexValue);
    return terminalOutcomeForPosition(pos);
}

DenseTerminalInfo terminalOutcomeForPosition(const Position& pos) {
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(popcount25(pos.soldiers));
    if (material.has_value()) {
        return DenseTerminalInfo{true, *material};
    }
    const bool cannonCanMove = cannonHasAnyMoveFast(pos);
    if (!cannonCanMove) {
        return DenseTerminalInfo{true, Outcome::SoldierWin};
    }
    if (pos.side == Side::Soldier && !soldierHasAnyMoveFast(pos)) {
        return DenseTerminalInfo{true, Outcome::CannonWin};
    }
    return DenseTerminalInfo{};
}

DenseTerminalInfo terminalOutcomeForPositionWithSuccessors(
    const Position& pos,
    const std::vector<DenseSuccessor>& legalSuccessors) {
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(popcount25(pos.soldiers));
    if (material.has_value()) {
        return DenseTerminalInfo{true, *material};
    }
    if (!cannonHasAnyMoveFast(pos)) {
        return DenseTerminalInfo{true, Outcome::SoldierWin};
    }
    if (legalSuccessors.empty()) {
        return DenseTerminalInfo{true, opponentWinFor(pos.side)};
    }
    return DenseTerminalInfo{};
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
