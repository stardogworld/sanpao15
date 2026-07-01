#pragma once

#include <string>

namespace sanpao15 {

enum class Side {
    Cannon = 0,
    Soldier = 1,
};

enum class Outcome {
    Unknown = 0,
    CannonWin = 1,
    SoldierWin = 2,
    Draw = 3,
};

struct Move {
    int from = -1;
    int to = -1;
    bool capture = false;
    int capturedSquare = -1;

    friend bool operator==(const Move& lhs, const Move& rhs) = default;
};

std::string sideToString(Side side);
std::string outcomeToString(Outcome outcome);
Side opposite(Side side);
Outcome winFor(Side side);
Outcome opponentWinFor(Side side);

}  // namespace sanpao15
