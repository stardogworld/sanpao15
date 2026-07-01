#include "sanpao15/position.h"

#include <stdexcept>

#include "sanpao15/bitboard.h"

namespace sanpao15 {

Position initialPosition() {
    Position pos;
    for (int square = 0; square <= 14; ++square) {
        pos.soldiers = setBit(pos.soldiers, square);
    }
    pos.cannons = setBit(pos.cannons, 21);
    pos.cannons = setBit(pos.cannons, 22);
    pos.cannons = setBit(pos.cannons, 23);
    pos.side = Side::Cannon;
    return pos;
}

uint64_t packPosition(const Position& pos) {
    const uint64_t cannonMask = static_cast<uint64_t>(pos.cannons & BoardMask);
    const uint64_t soldierMask = static_cast<uint64_t>(pos.soldiers & BoardMask);
    const uint64_t side = static_cast<uint64_t>(pos.side);
    return cannonMask | (soldierMask << 25) | (side << 50);
}

Position unpackPosition(uint64_t key) {
    Position pos;
    pos.cannons = static_cast<uint32_t>(key & BoardMask);
    pos.soldiers = static_cast<uint32_t>((key >> 25) & BoardMask);
    const uint64_t side = (key >> 50) & 1u;
    pos.side = side == 0 ? Side::Cannon : Side::Soldier;
    return pos;
}

bool isValidSquare(int square) {
    return square >= 0 && square < SquareCount;
}

int rowOf(int square) {
    if (!isValidSquare(square)) {
        throw std::out_of_range("square is outside the 5x5 board");
    }
    return square / BoardSize;
}

int colOf(int square) {
    if (!isValidSquare(square)) {
        throw std::out_of_range("square is outside the 5x5 board");
    }
    return square % BoardSize;
}

}  // namespace sanpao15
