#include "sanpao15/dense_index.h"

#include <bit>
#include <stdexcept>

#include "sanpao15/bitboard.h"
#include "sanpao15/combinadic.h"

namespace sanpao15 {

namespace {

void requireSoldierCount(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

uint64_t ceilDiv(uint64_t value, uint64_t divisor) {
    return (value + divisor - 1) / divisor;
}

uint64_t sideRank(Side side) {
    switch (side) {
        case Side::Cannon:
            return 0;
        case Side::Soldier:
            return 1;
    }
    throw std::invalid_argument("invalid side");
}

Side sideFromRank(uint64_t rank) {
    if (rank == 0) {
        return Side::Cannon;
    }
    if (rank == 1) {
        return Side::Soldier;
    }
    throw std::invalid_argument("invalid side rank");
}

void validateMasks(uint32_t cannonMask, uint32_t soldierMask) {
    if ((cannonMask & ~BoardMask) != 0 || (soldierMask & ~BoardMask) != 0) {
        throw std::invalid_argument("dense position masks must use only board bits");
    }
    if (popcount25(cannonMask) != 3) {
        throw std::invalid_argument("dense position must contain exactly three cannons");
    }
    if ((cannonMask & soldierMask) != 0) {
        throw std::invalid_argument("dense position cannot overlap cannons and soldiers");
    }
}

}  // namespace

uint32_t compressMaskToAvailable(uint32_t mask, uint32_t occupiedMask) {
    if ((mask & ~BoardMask) != 0 || (occupiedMask & ~BoardMask) != 0) {
        throw std::invalid_argument("mask uses bits outside the board");
    }
    if ((mask & occupiedMask) != 0) {
        throw std::invalid_argument("cannot compress occupied squares");
    }

    uint32_t local = 0;
    int localIndex = 0;
    for (int square = 0; square < SquareCount; ++square) {
        const uint32_t bit = uint32_t{1} << square;
        if ((occupiedMask & bit) != 0) {
            continue;
        }
        if ((mask & bit) != 0) {
            local |= uint32_t{1} << localIndex;
        }
        ++localIndex;
    }
    return local;
}

uint32_t expandMaskFromAvailable(uint32_t localMask, uint32_t occupiedMask) {
    if ((occupiedMask & ~BoardMask) != 0) {
        throw std::invalid_argument("occupied mask uses bits outside the board");
    }
    const int available = SquareCount - popcount25(occupiedMask);
    if (available < 0 || available > SquareCount) {
        throw std::invalid_argument("invalid occupied mask");
    }
    const uint32_t validLocalMask = available == 32 ? ~uint32_t{0} : ((uint32_t{1} << available) - 1u);
    if ((localMask & ~validLocalMask) != 0) {
        throw std::invalid_argument("local mask uses bits outside available squares");
    }

    uint32_t expanded = 0;
    int localIndex = 0;
    for (int square = 0; square < SquareCount; ++square) {
        const uint32_t bit = uint32_t{1} << square;
        if ((occupiedMask & bit) != 0) {
            continue;
        }
        if ((localMask & (uint32_t{1} << localIndex)) != 0) {
            expanded |= bit;
        }
        ++localIndex;
    }
    return expanded;
}

uint64_t denseStateCount(int soldierCount) {
    requireSoldierCount(soldierCount);
    return binom(25, 3) * binom(22, soldierCount) * 2u;
}

std::vector<DenseLayerSize> denseLayerSizes() {
    std::vector<DenseLayerSize> sizes;
    sizes.reserve(16);
    for (int soldierCount = 0; soldierCount <= 15; ++soldierCount) {
        const uint64_t states = denseStateCount(soldierCount);
        sizes.push_back(DenseLayerSize{
            soldierCount,
            states,
            ceilDiv(states * 2u, 8u),
            states,
        });
    }
    return sizes;
}

uint64_t totalDenseStateCount() {
    uint64_t total = 0;
    for (const DenseLayerSize& size : denseLayerSizes()) {
        total += size.states;
    }
    return total;
}

uint64_t totalDenseOutcomeBytes2Bit() {
    uint64_t total = 0;
    for (const DenseLayerSize& size : denseLayerSizes()) {
        total += size.bytes2BitOutcome;
    }
    return total;
}

uint64_t totalDenseOutcomeBytes1Byte() {
    uint64_t total = 0;
    for (const DenseLayerSize& size : denseLayerSizes()) {
        total += size.bytes1ByteOutcome;
    }
    return total;
}

uint64_t rankDensePosition(uint32_t cannonMask, uint32_t soldierMask, Side side) {
    validateMasks(cannonMask, soldierMask);
    const int soldierCount = popcount25(soldierMask);
    requireSoldierCount(soldierCount);

    const uint64_t cannonRank = rankCombination(cannonMask, SquareCount, 3);
    const uint32_t soldierLocalMask = compressMaskToAvailable(soldierMask, cannonMask);
    const uint64_t soldierRank = rankCombination(soldierLocalMask, SquareCount - 3, soldierCount);
    return ((cannonRank * binom(22, soldierCount)) + soldierRank) * 2u + sideRank(side);
}

uint64_t denseIndex(const Position& pos) {
    return rankDensePosition(pos.cannons, pos.soldiers, pos.side);
}

Position unrankDensePosition(int soldierCount, uint64_t index) {
    requireSoldierCount(soldierCount);
    const uint64_t states = denseStateCount(soldierCount);
    if (index >= states) {
        throw std::out_of_range("dense index is outside the layer state count");
    }

    const uint64_t side = index % 2u;
    uint64_t rest = index / 2u;
    const uint64_t soldierCombinations = binom(22, soldierCount);
    const uint64_t soldierRank = rest % soldierCombinations;
    const uint64_t cannonRank = rest / soldierCombinations;

    Position pos;
    pos.cannons = unrankCombination(cannonRank, SquareCount, 3);
    const uint32_t soldierLocalMask = unrankCombination(soldierRank, SquareCount - 3, soldierCount);
    pos.soldiers = expandMaskFromAvailable(soldierLocalMask, pos.cannons);
    pos.side = sideFromRank(side);
    return pos;
}

Position positionFromDenseIndex(int soldierCount, uint64_t index) {
    return unrankDensePosition(soldierCount, index);
}

}  // namespace sanpao15
