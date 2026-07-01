#include "test_common.h"

#include <array>
#include <cstdint>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/notation.h"
#include "sanpao15/rules.h"

using namespace sanpao15;

namespace {

uint32_t maskOf(std::initializer_list<int> bits) {
    uint32_t mask = 0;
    for (int bit : bits) {
        mask = setBit(mask, bit);
    }
    return mask;
}

uint64_t nextRandom(uint64_t& state) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
}

void requireDenseRoundtrip(const Position& pos) {
    const int soldierCount = popcount25(pos.soldiers);
    const uint64_t index = denseIndex(pos);
    const Position decoded = positionFromDenseIndex(soldierCount, index);
    SANPAO15_REQUIRE(decoded == pos);
    SANPAO15_REQUIRE(denseIndex(decoded) == index);
}

void smokeLayerRoundtrip(int soldierCount, uint64_t maxChecks) {
    const uint64_t states = denseStateCount(soldierCount);
    const uint64_t step = states <= maxChecks ? 1 : (states / maxChecks);
    uint64_t checked = 0;
    for (uint64_t index = 0; index < states; index += step) {
        const Position pos = positionFromDenseIndex(soldierCount, index);
        SANPAO15_REQUIRE(popcount25(pos.cannons) == 3);
        SANPAO15_REQUIRE(popcount25(pos.soldiers) == soldierCount);
        SANPAO15_REQUIRE((pos.cannons & pos.soldiers) == 0);
        SANPAO15_REQUIRE(denseIndex(pos) == index);
        for (const Move& move : generateLegalMoves(pos)) {
            const Position next = applyMove(pos, move);
            SANPAO15_REQUIRE(popcount25(next.cannons) == 3);
            SANPAO15_REQUIRE(popcount25(next.soldiers) == soldierCount || popcount25(next.soldiers) == soldierCount - 1);
            SANPAO15_REQUIRE((next.cannons & next.soldiers) == 0);
        }
        ++checked;
    }
    SANPAO15_REQUIRE(checked > 0);
}

}  // namespace

SANPAO15_TEST(denseStateCountsMatchTheory) {
    SANPAO15_REQUIRE(denseStateCount(0) == 4600);
    SANPAO15_REQUIRE(denseStateCount(11) == 3244987200ull);
    SANPAO15_REQUIRE(denseStateCount(15) == 784502400ull);
    SANPAO15_REQUIRE(totalDenseStateCount() == 18787540800ull);
    SANPAO15_REQUIRE(totalDenseOutcomeBytes2Bit() == 4696885200ull);
    SANPAO15_REQUIRE(totalDenseOutcomeBytes1Byte() == 18787540800ull);
}

SANPAO15_TEST(denseIndexRoundtripsInitialAndEdgeCases) {
    requireDenseRoundtrip(parsePositionNotation("SSSSS/SSSSS/SSSSS/...../.CCC. c"));

    Position edge;
    edge.cannons = maskOf({0, 23, 24});
    edge.soldiers = maskOf({1, 2, 3, 4, 5, 6, 7});
    edge.side = Side::Soldier;
    requireDenseRoundtrip(edge);

    Position noSoldiers;
    noSoldiers.cannons = maskOf({0, 1, 24});
    noSoldiers.side = Side::Cannon;
    requireDenseRoundtrip(noSoldiers);
}

SANPAO15_TEST(denseIndexRandomRoundtripsAllLayers) {
    uint64_t rng = 0x533135ull;
    for (int soldiers = 0; soldiers <= 15; ++soldiers) {
        const uint64_t states = denseStateCount(soldiers);
        for (int sample = 0; sample < 100; ++sample) {
            const uint64_t index = nextRandom(rng) % states;
            const Position pos = positionFromDenseIndex(soldiers, index);
            SANPAO15_REQUIRE(denseIndex(pos) == index);
        }
    }
}

SANPAO15_TEST(denseIndexFullSpaceSmokeK0K1AndSampleK2) {
    smokeLayerRoundtrip(0, denseStateCount(0));
    smokeLayerRoundtrip(1, denseStateCount(1));
    smokeLayerRoundtrip(2, 20000);
}

SANPAO15_TEST(denseIndexRejectsInvalidPositionsAndIndexes) {
    sanpao15::test::requireThrows([] {
        (void)rankDensePosition(maskOf({0, 1}), maskOf({2}), Side::Cannon);
    }, "wrong cannon count rejected");
    sanpao15::test::requireThrows([] {
        (void)rankDensePosition(maskOf({0, 1, 2}), maskOf({2}), Side::Cannon);
    }, "overlap rejected");
    sanpao15::test::requireThrows([] {
        (void)rankDensePosition(maskOf({0, 1, 2}), maskOf({3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18}), Side::Cannon);
    }, "too many soldiers rejected");
    sanpao15::test::requireThrows([] {
        (void)positionFromDenseIndex(0, denseStateCount(0));
    }, "index out of range rejected");
}
