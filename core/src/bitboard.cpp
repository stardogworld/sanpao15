#include "sanpao15/bitboard.h"

#include <bit>
#include <stdexcept>

#include "sanpao15/position.h"

namespace sanpao15 {

namespace {

void requireSquare(int square) {
    if (!isValidSquare(square)) {
        throw std::out_of_range("square is outside the 5x5 board");
    }
}

}  // namespace

int popcount25(uint32_t mask) {
    // Keep only the 25 board bits so accidental high bits never affect counts.
    return static_cast<int>(std::popcount(mask & BoardMask));
}

bool hasBit(uint32_t mask, int square) {
    requireSquare(square);
    // A square is represented by one bit at the same zero-based index.
    return (mask & (uint32_t{1} << square)) != 0;
}

uint32_t setBit(uint32_t mask, int square) {
    requireSquare(square);
    // Set only the requested square bit and preserve all existing board bits.
    return (mask | (uint32_t{1} << square)) & BoardMask;
}

uint32_t clearBit(uint32_t mask, int square) {
    requireSquare(square);
    // Clearing a bit removes a piece from that square while leaving the rest.
    return mask & ~(uint32_t{1} << square) & BoardMask;
}

std::vector<int> squaresInMask(uint32_t mask) {
    std::vector<int> squares;
    for (int square = 0; square < SquareCount; ++square) {
        if ((mask & (uint32_t{1} << square)) != 0) {
            squares.push_back(square);
        }
    }
    return squares;
}

std::vector<int> orthogonalNeighbors(int square) {
    requireSquare(square);

    const int row = rowOf(square);
    const int col = colOf(square);
    std::vector<int> result;
    if (row > 0) {
        result.push_back(square - BoardSize);
    }
    if (row + 1 < BoardSize) {
        result.push_back(square + BoardSize);
    }
    if (col > 0) {
        result.push_back(square - 1);
    }
    if (col + 1 < BoardSize) {
        result.push_back(square + 1);
    }
    return result;
}

std::vector<Jump> cannonJumps(int square) {
    requireSquare(square);

    const int row = rowOf(square);
    const int col = colOf(square);
    std::vector<Jump> result;

    const auto addJump = [&](int dRow, int dCol) {
        const int overRow = row + dRow;
        const int overCol = col + dCol;
        const int landingRow = row + 2 * dRow;
        const int landingCol = col + 2 * dCol;
        if (overRow < 0 || overRow >= BoardSize || overCol < 0 || overCol >= BoardSize) {
            return;
        }
        if (landingRow < 0 || landingRow >= BoardSize || landingCol < 0 || landingCol >= BoardSize) {
            return;
        }
        result.push_back({overRow * BoardSize + overCol, landingRow * BoardSize + landingCol});
    };

    addJump(-1, 0);
    addJump(1, 0);
    addJump(0, -1);
    addJump(0, 1);
    return result;
}

}  // namespace sanpao15
