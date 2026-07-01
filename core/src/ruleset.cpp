#include "sanpao15/ruleset.h"

#include <stdexcept>

namespace sanpao15 {

namespace {

void validateSoldierCount(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

}  // namespace

bool soldiersAreBelowSurvivalLimit(int soldierCount) {
    validateSoldierCount(soldierCount);
    return soldierCount < MinSoldiersForSoldierSurvival;
}

std::optional<Outcome> forcedOutcomeByMaterialRule(int soldierCount) {
    if (soldiersAreBelowSurvivalLimit(soldierCount)) {
        return Outcome::CannonWin;
    }
    return std::nullopt;
}

}  // namespace sanpao15
