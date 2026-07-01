#include "test_common.h"

#include "sanpao15/bitboard.h"
#include "sanpao15/rules.h"

using namespace sanpao15;

SANPAO15_TEST(noSoldiersMeansCannonWin) {
    Position pos;
    pos.cannons = setBit(0, 12);
    pos.side = Side::Soldier;

    SANPAO15_REQUIRE(isTerminal(pos));
    SANPAO15_REQUIRE(terminalOutcome(pos) == Outcome::CannonWin);
}

SANPAO15_TEST(cannonWithNoMoveMeansSoldierWin) {
    Position pos;
    pos.cannons = setBit(0, 12);
    pos.soldiers = 0;
    for (int square : {2, 7, 10, 11, 13, 14, 17, 22}) {
        pos.soldiers = setBit(pos.soldiers, square);
    }
    pos.side = Side::Cannon;

    SANPAO15_REQUIRE(isTerminal(pos));
    SANPAO15_REQUIRE(terminalOutcome(pos) == Outcome::SoldierWin);
}

SANPAO15_TEST(nonTerminalPositionIsUnknown) {
    const Position pos = initialPosition();
    SANPAO15_REQUIRE(!isTerminal(pos));
    SANPAO15_REQUIRE(terminalOutcome(pos) == Outcome::Unknown);
}
