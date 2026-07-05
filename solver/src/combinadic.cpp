#include "sanpao15/combinadic.h"

#include <bit>
#include <stdexcept>

namespace sanpao15 {

namespace {

void requireCombinationParams(int n, int k) {
    if (n < 0 || n > 25) {
        throw std::invalid_argument("combination n must be in 0..25");
    }
    if (k < 0 || k > n) {
        throw std::invalid_argument("combination k must be in 0..n");
    }
}

uint64_t binomOrZero(int n, int k) {
    if (k < 0 || n < 0 || k > n) {
        return 0;
    }
    return binom(n, k);
}

}  // namespace

uint64_t binom(int n, int k) {
    requireCombinationParams(n, k);
    if (k > n - k) {
        k = n - k;
    }
    uint64_t result = 1;
    for (int i = 1; i <= k; ++i) {
        result = (result * static_cast<uint64_t>(n - k + i)) / static_cast<uint64_t>(i);
    }
    return result;
}

uint64_t rankCombination(uint32_t mask, int n, int k) {
    requireCombinationParams(n, k);
    const uint32_t validMask = n == 32 ? ~uint32_t{0} : ((uint32_t{1} << n) - 1u);
    if ((mask & ~validMask) != 0) {
        throw std::invalid_argument("combination mask contains bits outside n");
    }
    if (std::popcount(mask) != static_cast<unsigned>(k)) {
        throw std::invalid_argument("combination mask popcount does not match k");
    }

    uint64_t rank = 0;
    int chosen = 0;
    for (int bit = 0; bit < n; ++bit) {
        if ((mask & (uint32_t{1} << bit)) == 0) {
            continue;
        }
        ++chosen;
        rank += binomOrZero(bit, chosen);
    }
    return rank;
}

uint32_t unrankCombination(uint64_t rank, int n, int k) {
    requireCombinationParams(n, k);
    if (rank >= binom(n, k)) {
        throw std::out_of_range("combination rank is outside [0, C(n,k))");
    }

    uint32_t mask = 0;
    int nextBitExclusive = n;
    for (int chosen = k; chosen >= 1; --chosen) {
        int bit = nextBitExclusive - 1;
        while (bit >= 0 && binomOrZero(bit, chosen) > rank) {
            --bit;
        }
        if (bit < 0) {
            throw std::logic_error("failed to unrank combination");
        }
        mask |= uint32_t{1} << bit;
        rank -= binomOrZero(bit, chosen);
        nextBitExclusive = bit;
    }
    return mask;
}

}  // namespace sanpao15
