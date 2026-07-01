#include "test_common.h"

#include <algorithm>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/move.h"

using namespace sanpao15;

namespace {

uint64_t nextRandom(uint64_t& state) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
}

std::vector<uint64_t> sampleIndexes(int soldierCount) {
    const uint64_t count = denseStateCount(soldierCount);
    std::vector<uint64_t> indexes{0, count / 2, count - 1};
    uint64_t rng = 0x505245444543ull ^ static_cast<uint64_t>(soldierCount);
    for (int sample = 0; sample < 8; ++sample) {
        indexes.push_back(nextRandom(rng) % count);
    }
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    return indexes;
}

bool successorContainsSameLayerTarget(int soldierCount, uint64_t parentIndex, uint64_t childIndex, Move move) {
    for (const DenseSuccessor& successor : generateDenseSuccessors(soldierCount, parentIndex)) {
        if (successor.kind == DenseSuccessorKind::SameLayer &&
            successor.toIndex == childIndex &&
            successor.move == move) {
            return true;
        }
    }
    return false;
}

bool predecessorContainsParent(int soldierCount, uint64_t childIndex, uint64_t parentIndex) {
    for (const DensePredecessor& predecessor : generateDensePredecessors(soldierCount, childIndex)) {
        if (predecessor.index == parentIndex) {
            return true;
        }
    }
    return false;
}

}  // namespace

SANPAO15_TEST(densePredecessorsRoundtripThroughSuccessors) {
    for (int soldierCount : {0, 1, 2, 3, 4, 15}) {
        for (uint64_t childIndex : sampleIndexes(soldierCount)) {
            const Position child = positionFromDenseIndex(soldierCount, childIndex);
            for (const DensePredecessor& predecessor : generateDensePredecessors(soldierCount, childIndex)) {
                SANPAO15_REQUIRE(predecessor.soldierCount == soldierCount);
                SANPAO15_REQUIRE(predecessor.index < denseStateCount(soldierCount));

                const Position parent = positionFromDenseIndex(soldierCount, predecessor.index);
                SANPAO15_REQUIRE(parent.side == opposite(child.side));
                SANPAO15_REQUIRE(popcount25(parent.cannons) == 3);
                SANPAO15_REQUIRE(popcount25(parent.soldiers) == soldierCount);
                SANPAO15_REQUIRE(popcount25(child.soldiers) == soldierCount);
                SANPAO15_REQUIRE((parent.cannons & parent.soldiers) == 0);
                SANPAO15_REQUIRE(successorContainsSameLayerTarget(
                    soldierCount,
                    predecessor.index,
                    childIndex,
                    predecessor.move));
            }
        }
    }
}

SANPAO15_TEST(denseSuccessorPredecessorSymmetryForSameLayerMoves) {
    for (int soldierCount : {0, 1, 2, 3, 4, 15}) {
        for (uint64_t parentIndex : sampleIndexes(soldierCount)) {
            for (const DenseSuccessor& successor : generateDenseSuccessors(soldierCount, parentIndex)) {
                if (successor.kind != DenseSuccessorKind::SameLayer) {
                    continue;
                }
                SANPAO15_REQUIRE(predecessorContainsParent(
                    soldierCount,
                    successor.toIndex,
                    parentIndex));
            }
        }
    }
}

SANPAO15_TEST(densePredecessorsRejectInvalidIndex) {
    sanpao15::test::requireThrows([] {
        (void)generateDensePredecessors(1, denseStateCount(1));
    }, "generateDensePredecessors rejects out-of-range index");
}
