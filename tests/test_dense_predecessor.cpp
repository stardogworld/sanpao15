#include "test_common.h"

#include <algorithm>
#include <tuple>
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

std::vector<uint64_t> largerSampleIndexes(int soldierCount) {
    const uint64_t count = denseStateCount(soldierCount);
    std::vector<uint64_t> indexes{0, count / 3, count / 2, count - 1};
    uint64_t rng = 0x4641535450524544ull ^ static_cast<uint64_t>(soldierCount);
    for (int sample = 0; sample < 64; ++sample) {
        indexes.push_back(nextRandom(rng) % count);
    }
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    return indexes;
}

std::vector<std::tuple<uint64_t, int, int, bool, int>> predecessorSignature(std::vector<DensePredecessor> predecessors) {
    std::vector<std::tuple<uint64_t, int, int, bool, int>> signature;
    signature.reserve(predecessors.size());
    for (const DensePredecessor& predecessor : predecessors) {
        signature.emplace_back(
            predecessor.index,
            predecessor.move.from,
            predecessor.move.to,
            predecessor.move.capture,
            predecessor.move.capturedSquare);
    }
    std::sort(signature.begin(), signature.end());
    signature.erase(std::unique(signature.begin(), signature.end()), signature.end());
    return signature;
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

std::vector<uint64_t> sortedUniquePredecessorIndexes(const std::vector<DensePredecessor>& predecessors) {
    std::vector<uint64_t> indexes;
    indexes.reserve(predecessors.size());
    for (const DensePredecessor& predecessor : predecessors) {
        indexes.push_back(predecessor.index);
    }
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    return indexes;
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

SANPAO15_TEST(densePredecessorFastMatchesCheckedForSampledLayers) {
    for (int soldierCount : {0, 1, 2, 3, 4, 15}) {
        for (uint64_t childIndex : largerSampleIndexes(soldierCount)) {
            const auto checked = predecessorSignature(generateDensePredecessors(
                soldierCount,
                childIndex,
                DensePredecessorValidation::Checked));
            const auto fast = predecessorSignature(generateDensePredecessors(
                soldierCount,
                childIndex,
                DensePredecessorValidation::None));
            SANPAO15_REQUIRE(fast == checked);
        }
    }
}

SANPAO15_TEST(densePredecessorScratchMatchesVectorApiForSampledLayers) {
    std::vector<DensePredecessor> scratch;
    scratch.reserve(32);
    for (int soldierCount : {0, 1, 2, 3, 4, 15}) {
        for (uint64_t childIndex : largerSampleIndexes(soldierCount)) {
            const auto expected = predecessorSignature(generateDensePredecessors(
                soldierCount,
                childIndex,
                DensePredecessorValidation::None));

            generateDensePredecessors(
                soldierCount,
                childIndex,
                DensePredecessorValidation::None,
                scratch);
            SANPAO15_REQUIRE(predecessorSignature(scratch) == expected);

            const Position child = positionFromDenseIndex(soldierCount, childIndex);
            generateDensePredecessorsFromPosition(
                soldierCount,
                childIndex,
                child,
                DensePredecessorValidation::None,
                scratch);
            SANPAO15_REQUIRE(predecessorSignature(scratch) == expected);
        }
    }
}

SANPAO15_TEST(densePredecessorScratchClearsOldContents) {
    std::vector<DensePredecessor> scratch{
        DensePredecessor{15, denseStateCount(15) - 1, Move{24, 0, true, 0}},
        DensePredecessor{7, 123, Move{1, 2, false, -1}},
    };
    const int soldierCount = 4;
    const uint64_t childIndex = denseStateCount(soldierCount) / 2;
    const auto expected = predecessorSignature(generateDensePredecessors(
        soldierCount,
        childIndex,
        DensePredecessorValidation::None));

    generateDensePredecessors(
        soldierCount,
        childIndex,
        DensePredecessorValidation::None,
        scratch);
    SANPAO15_REQUIRE(predecessorSignature(scratch) == expected);
}

SANPAO15_TEST(densePredecessorIndexOnlyMatchesFastApiForSampledLayers) {
    std::vector<DensePredecessor> predecessors;
    std::vector<uint64_t> indexOnly;
    for (int soldierCount : {0, 1, 2, 3, 4, 15}) {
        for (uint64_t childIndex : largerSampleIndexes(soldierCount)) {
            const Position child = positionFromDenseIndex(soldierCount, childIndex);
            generateDensePredecessorsFromPosition(
                soldierCount,
                childIndex,
                child,
                DensePredecessorValidation::None,
                predecessors);
            generateDensePredecessorIndicesFromPosition(soldierCount, childIndex, child, indexOnly);

            std::vector<uint64_t> sortedIndexOnly = indexOnly;
            std::sort(sortedIndexOnly.begin(), sortedIndexOnly.end());
            SANPAO15_REQUIRE(
                std::adjacent_find(sortedIndexOnly.begin(), sortedIndexOnly.end()) == sortedIndexOnly.end());
            SANPAO15_REQUIRE(sortedIndexOnly == sortedUniquePredecessorIndexes(predecessors));
        }
    }
}

SANPAO15_TEST(densePredecessorsRejectInvalidIndex) {
    sanpao15::test::requireThrows([] {
        (void)generateDensePredecessors(1, denseStateCount(1));
    }, "generateDensePredecessors rejects out-of-range index");
}
