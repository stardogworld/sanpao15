#include "sanpao15/move.h"

namespace sanpao15 {

std::string sideToString(Side side) {
    return side == Side::Cannon ? "Cannon" : "Soldier";
}

std::string outcomeToString(Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            return "Unknown";
        case Outcome::CannonWin:
            return "CannonWin";
        case Outcome::SoldierWin:
            return "SoldierWin";
        case Outcome::Draw:
            return "Draw";
    }
    return "Unknown";
}

Side opposite(Side side) {
    return side == Side::Cannon ? Side::Soldier : Side::Cannon;
}

Outcome winFor(Side side) {
    return side == Side::Cannon ? Outcome::CannonWin : Outcome::SoldierWin;
}

Outcome opponentWinFor(Side side) {
    return winFor(opposite(side));
}

}  // namespace sanpao15
