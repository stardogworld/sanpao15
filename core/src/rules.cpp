#include "sanpao15/rules.h"

#include "sanpao15/bitboard.h"

namespace sanpao15 {

bool cannonHasAnyMove(const Position& pos) {
    return !generateCannonMoves(pos).empty();
}

bool isTerminal(const Position& pos) {
    if (forcedOutcomeByMaterialRule(popcount25(pos.soldiers)).has_value()) {
        return true;
    }
    if (!cannonHasAnyMove(pos)) {
        return true;
    }
    return generateLegalMoves(pos).empty();
}

Outcome terminalOutcome(const Position& pos) {
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(popcount25(pos.soldiers));
    if (material.has_value()) {
        return *material;
    }
    if (!cannonHasAnyMove(pos)) {
        return Outcome::SoldierWin;
    }
    if (generateLegalMoves(pos).empty()) {
        return opponentWinFor(pos.side);
    }
    return Outcome::Unknown;
}

}  // namespace sanpao15
