#include "sanpao15/rules.h"

#include "sanpao15/bitboard.h"

namespace sanpao15 {

bool cannonHasAnyMove(const Position& pos) {
    return !generateCannonMoves(pos).empty();
}

bool isTerminal(const Position& pos) {
    if (pos.soldiers == 0) {
        return true;
    }
    if (!cannonHasAnyMove(pos)) {
        return true;
    }
    return generateLegalMoves(pos).empty();
}

Outcome terminalOutcome(const Position& pos) {
    if (pos.soldiers == 0) {
        return Outcome::CannonWin;
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
