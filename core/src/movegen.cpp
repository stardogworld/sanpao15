#include "sanpao15/rules.h"

#include "sanpao15/bitboard.h"

namespace sanpao15 {

std::vector<Move> generateCannonMoves(const Position& pos) {
    std::vector<Move> moves;
    const uint32_t occupied = pos.cannons | pos.soldiers;

    for (int from : squaresInMask(pos.cannons)) {
        for (int to : orthogonalNeighbors(from)) {
            if (!hasBit(occupied, to)) {
                moves.push_back({from, to, false, -1});
            }
        }

        for (const Jump& jump : cannonJumps(from)) {
            if (!hasBit(occupied, jump.over) && hasBit(pos.soldiers, jump.landing)) {
                moves.push_back({from, jump.landing, true, jump.landing});
            }
        }
    }

    return moves;
}

std::vector<Move> generateSoldierMoves(const Position& pos) {
    std::vector<Move> moves;
    const uint32_t occupied = pos.cannons | pos.soldiers;

    for (int from : squaresInMask(pos.soldiers)) {
        for (int to : orthogonalNeighbors(from)) {
            if (!hasBit(occupied, to)) {
                moves.push_back({from, to, false, -1});
            }
        }
    }

    return moves;
}

std::vector<Move> generateLegalMoves(const Position& pos) {
    return pos.side == Side::Cannon ? generateCannonMoves(pos) : generateSoldierMoves(pos);
}

Position applyMove(const Position& pos, const Move& move) {
    Position next = pos;
    if (pos.side == Side::Cannon) {
        next.cannons = clearBit(next.cannons, move.from);
        next.cannons = setBit(next.cannons, move.to);
        if (move.capture) {
            next.soldiers = clearBit(next.soldiers, move.capturedSquare);
        }
    } else {
        next.soldiers = clearBit(next.soldiers, move.from);
        next.soldiers = setBit(next.soldiers, move.to);
    }
    next.side = opposite(pos.side);
    return next;
}

}  // namespace sanpao15
