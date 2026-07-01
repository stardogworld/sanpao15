#include "test_common.h"

#include <algorithm>

#include "sanpao15/bitboard.h"
#include "sanpao15/rules.h"

using namespace sanpao15;

namespace {

bool containsMove(const std::vector<Move>& moves, int from, int to, bool capture = false) {
    return std::any_of(moves.begin(), moves.end(), [&](const Move& move) {
        return move.from == from && move.to == to && move.capture == capture;
    });
}

}  // namespace

SANPAO15_TEST(cannonMovesOnlyToOrthogonalEmptySquares) {
    Position pos;
    pos.cannons = setBit(0, 12);
    pos.soldiers = setBit(0, 7);
    pos.side = Side::Cannon;

    const auto moves = generateCannonMoves(pos);
    SANPAO15_REQUIRE(containsMove(moves, 12, 17));
    SANPAO15_REQUIRE(containsMove(moves, 12, 11));
    SANPAO15_REQUIRE(containsMove(moves, 12, 13));
    SANPAO15_REQUIRE(!containsMove(moves, 12, 7));
}

SANPAO15_TEST(cannonCaptureRequiresEmptyScreenAndSoldierLanding) {
    Position legal;
    legal.cannons = setBit(0, 20);
    legal.soldiers = setBit(0, 10);
    legal.side = Side::Cannon;
    SANPAO15_REQUIRE(containsMove(generateCannonMoves(legal), 20, 10, true));

    Position blockedBySoldier;
    blockedBySoldier.cannons = setBit(0, 20);
    blockedBySoldier.soldiers = setBit(setBit(0, 15), 10);
    blockedBySoldier.side = Side::Cannon;
    SANPAO15_REQUIRE(!containsMove(generateCannonMoves(blockedBySoldier), 20, 10, true));

    Position emptyLanding;
    emptyLanding.cannons = setBit(0, 20);
    emptyLanding.side = Side::Cannon;
    SANPAO15_REQUIRE(!containsMove(generateCannonMoves(emptyLanding), 20, 10, true));
}

SANPAO15_TEST(cannonCaptureMovesToSoldierSquareAndRemovesSoldier) {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.soldiers = setBit(0, 10);
    pos.side = Side::Cannon;

    const Move capture{20, 10, true, 10};
    const Position next = applyMove(pos, capture);
    SANPAO15_REQUIRE(hasBit(next.cannons, 10));
    SANPAO15_REQUIRE(!hasBit(next.cannons, 20));
    SANPAO15_REQUIRE(!hasBit(next.soldiers, 10));
    SANPAO15_REQUIRE(next.side == Side::Soldier);
}

SANPAO15_TEST(soldiersMoveOneOrthogonalStepToEmptySquaresOnly) {
    Position pos;
    pos.soldiers = setBit(0, 12);
    pos.cannons = setBit(0, 7);
    pos.side = Side::Soldier;

    const auto moves = generateSoldierMoves(pos);
    SANPAO15_REQUIRE(containsMove(moves, 12, 17));
    SANPAO15_REQUIRE(containsMove(moves, 12, 11));
    SANPAO15_REQUIRE(containsMove(moves, 12, 13));
    SANPAO15_REQUIRE(!containsMove(moves, 12, 7));
    SANPAO15_REQUIRE(!containsMove(moves, 12, 6));
}
