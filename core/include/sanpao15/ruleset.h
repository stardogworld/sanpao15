#pragma once

#include <cstdint>
#include <optional>

#include "sanpao15/move.h"

namespace sanpao15 {

constexpr int MinSoldiersForSoldierSurvival = 4;
constexpr const char* RulesetName = "sanpao15-min-four-soldiers";
constexpr const char* RulesetSummary = "soldier-count-below-four-is-cannon-win";

// Fixed identifier for the current rules:
// 5x5, soldiers 0..14, cannons 21/22/23, cannon first,
// orthogonal moves, cannon-empty-soldier capture,
// and soldierCount < 4 is an immediate CannonWin.
constexpr uint64_t StandardRulesetHash = 0x5331355F76325F04ull;

bool soldiersAreBelowSurvivalLimit(int soldierCount);
std::optional<Outcome> forcedOutcomeByMaterialRule(int soldierCount);

}  // namespace sanpao15
