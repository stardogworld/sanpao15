#include "test_common.h"

#include "sanpao15/bitboard.h"
#include "sanpao15/position.h"

using namespace sanpao15;

SANPAO15_TEST(initialPositionIsCorrect) {
    const Position pos = initialPosition();

    SANPAO15_REQUIRE(popcount25(pos.soldiers) == 15);
    SANPAO15_REQUIRE(popcount25(pos.cannons) == 3);
    for (int square = 0; square <= 14; ++square) {
        SANPAO15_REQUIRE(hasBit(pos.soldiers, square));
    }
    SANPAO15_REQUIRE(hasBit(pos.cannons, 21));
    SANPAO15_REQUIRE(hasBit(pos.cannons, 22));
    SANPAO15_REQUIRE(hasBit(pos.cannons, 23));
    SANPAO15_REQUIRE(pos.side == Side::Cannon);
}

SANPAO15_TEST(packPositionRoundTrips) {
    Position pos = initialPosition();
    pos.cannons = clearBit(pos.cannons, 22);
    pos.cannons = setBit(pos.cannons, 17);
    pos.soldiers = clearBit(pos.soldiers, 13);
    pos.side = Side::Soldier;

    const Position unpacked = unpackPosition(packPosition(pos));
    SANPAO15_REQUIRE(unpacked == pos);
}

SANPAO15_TEST(neighborsStayOrthogonalAndOnBoard) {
    const auto corner = orthogonalNeighbors(0);
    SANPAO15_REQUIRE(corner.size() == 2);
    SANPAO15_REQUIRE((corner[0] == 5 || corner[1] == 5));
    SANPAO15_REQUIRE((corner[0] == 1 || corner[1] == 1));

    const auto center = orthogonalNeighbors(12);
    SANPAO15_REQUIRE(center.size() == 4);
}
