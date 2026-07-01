#pragma once

#include <cstdint>

#include "sanpao15/move.h"

namespace sanpao15 {

constexpr int BoardSize = 5;
constexpr int SquareCount = BoardSize * BoardSize;
constexpr uint32_t BoardMask = (uint32_t{1} << SquareCount) - 1u;

struct Position {
    uint32_t cannons = 0;
    uint32_t soldiers = 0;
    Side side = Side::Cannon;

    friend bool operator==(const Position& lhs, const Position& rhs) = default;
};

Position initialPosition();

uint64_t packPosition(const Position& pos);
Position unpackPosition(uint64_t key);

bool isValidSquare(int square);
int rowOf(int square);
int colOf(int square);

}  // namespace sanpao15
