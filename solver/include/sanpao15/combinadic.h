#pragma once

#include <cstdint>

namespace sanpao15 {

uint64_t binom(int n, int k);

// Colexicographic combination rank. For set bits b_0 < ... < b_(k-1),
// rank = sum_i C(b_i, i + 1).
uint64_t rankCombination(uint32_t mask, int n, int k);
uint32_t unrankCombination(uint64_t rank, int n, int k);

}  // namespace sanpao15
