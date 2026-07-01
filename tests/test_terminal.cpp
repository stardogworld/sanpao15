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

SANPAO15_TEST(materialRuleForcesCannonWinBelowFourSoldiers) {
    for (int soldierCount : {0, 1, 2, 3}) {
        SANPAO15_REQUIRE(soldiersAreBelowSurvivalLimit(soldierCount));
        const std::optional<Outcome> forced = forcedOutcomeByMaterialRule(soldierCount);
        SANPAO15_REQUIRE(forced.has_value());
        SANPAO15_REQUIRE(*forced == Outcome::CannonWin);
    }
    SANPAO15_REQUIRE(!soldiersAreBelowSurvivalLimit(4));
    SANPAO15_REQUIRE(!forcedOutcomeByMaterialRule(4).has_value());
}

SANPAO15_TEST(materialRuleTakesPriorityOverCannonStuck) {
    Position pos;
    pos.cannons = setBit(setBit(0, 0), 1);
    pos.cannons = setBit(pos.cannons, 5);
    for (int square : {2, 6, 10}) {
        pos.soldiers = setBit(pos.soldiers, square);
    }
    pos.side = Side::Cannon;

    SANPAO15_REQUIRE(generateCannonMoves(pos).empty());
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
