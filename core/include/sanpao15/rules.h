#pragma once

#include <vector>

#include "sanpao15/move.h"
#include "sanpao15/position.h"
#include "sanpao15/ruleset.h"

namespace sanpao15 {

std::vector<Move> generateCannonMoves(const Position& pos);
std::vector<Move> generateSoldierMoves(const Position& pos);
std::vector<Move> generateLegalMoves(const Position& pos);

Position applyMove(const Position& pos, const Move& move);

bool cannonHasAnyMove(const Position& pos);
bool isTerminal(const Position& pos);
Outcome terminalOutcome(const Position& pos);

}  // namespace sanpao15
