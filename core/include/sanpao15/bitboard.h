#pragma once

#include <cstdint>
#include <vector>

namespace sanpao15 {

struct Jump {
    int over;
    int landing;
};

int popcount25(uint32_t mask);
bool hasBit(uint32_t mask, int square);
uint32_t setBit(uint32_t mask, int square);
uint32_t clearBit(uint32_t mask, int square);

std::vector<int> squaresInMask(uint32_t mask);
std::vector<int> orthogonalNeighbors(int square);
std::vector<Jump> cannonJumps(int square);

}  // namespace sanpao15
