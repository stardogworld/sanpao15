#include "test_common.h"

#include <vector>

#include "sanpao15/combinadic.h"

using namespace sanpao15;

namespace {

uint32_t maskOf(std::initializer_list<int> bits) {
    uint32_t mask = 0;
    for (int bit : bits) {
        mask |= uint32_t{1} << bit;
    }
    return mask;
}

void checkAllCombinations(int n, int k) {
    const uint64_t count = binom(n, k);
    for (uint64_t rank = 0; rank < count; ++rank) {
        const uint32_t mask = unrankCombination(rank, n, k);
        SANPAO15_REQUIRE(rankCombination(mask, n, k) == rank);
    }
}

void checkSampledCombinations(int n, int k) {
    const uint64_t count = binom(n, k);
    std::vector<uint64_t> ranks{0};
    if (count > 1) {
        ranks.push_back(1);
        ranks.push_back(count / 2);
        ranks.push_back(count - 1);
    }
    for (uint64_t rank : ranks) {
        const uint32_t mask = unrankCombination(rank, n, k);
        SANPAO15_REQUIRE(rankCombination(mask, n, k) == rank);
    }
}

}  // namespace

SANPAO15_TEST(binomKnownValues) {
    SANPAO15_REQUIRE(binom(25, 3) == 2300);
    SANPAO15_REQUIRE(binom(22, 15) == 170544);
    SANPAO15_REQUIRE(binom(22, 11) == 705432);
    SANPAO15_REQUIRE(binom(5, 0) == 1);
    SANPAO15_REQUIRE(binom(5, 5) == 1);
}

SANPAO15_TEST(combinationRoundtripSmallExhaustive) {
    for (int k = 0; k <= 5; ++k) {
        checkAllCombinations(5, k);
    }
}

SANPAO15_TEST(combinationRoundtripLargerSamples) {
    checkSampledCombinations(25, 3);
    checkSampledCombinations(22, 0);
    checkSampledCombinations(22, 1);
    checkSampledCombinations(22, 2);
    checkSampledCombinations(22, 11);
    checkSampledCombinations(22, 15);
}

SANPAO15_TEST(combinationColexRankKnownMasks) {
    SANPAO15_REQUIRE(rankCombination(maskOf({0, 1, 2}), 5, 3) == 0);
    SANPAO15_REQUIRE(rankCombination(maskOf({0, 1, 3}), 5, 3) == 1);
    SANPAO15_REQUIRE(rankCombination(maskOf({0, 2, 3}), 5, 3) == 2);
    SANPAO15_REQUIRE(rankCombination(maskOf({1, 2, 3}), 5, 3) == 3);
    SANPAO15_REQUIRE(rankCombination(maskOf({0, 1, 4}), 5, 3) == 4);
}

SANPAO15_TEST(combinationRejectsInvalidInputs) {
    sanpao15::test::requireThrows([] { (void)binom(26, 3); }, "binom rejects n > 25");
    sanpao15::test::requireThrows([] { (void)binom(5, 6); }, "binom rejects k > n");
    sanpao15::test::requireThrows([] { (void)rankCombination(maskOf({0, 1}), 5, 3); }, "rank rejects wrong popcount");
    sanpao15::test::requireThrows([] { (void)rankCombination(uint32_t{1} << 5, 5, 1); }, "rank rejects out-of-range bits");
    sanpao15::test::requireThrows([] { (void)unrankCombination(10, 5, 3); }, "unrank rejects rank C(5,3)");
}
