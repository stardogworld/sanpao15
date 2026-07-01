#include "sanpao15/ruleset.h"

namespace sanpao15 {

bool soldiersAreBelowSurvivalLimit(int soldierCount) {
    return soldierCount < MinSoldiersForSoldierSurvival;
}

std::optional<Outcome> forcedOutcomeByMaterialRule(int soldierCount) {
    if (soldiersAreBelowSurvivalLimit(soldierCount)) {
        return Outcome::CannonWin;
    }
    return std::nullopt;
}

}  // namespace sanpao15
