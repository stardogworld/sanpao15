#include "test_common.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "sanpao15/parallel.h"

using namespace sanpao15;

SANPAO15_TEST(normalizeThreadCountHandlesCommonCases) {
    SANPAO15_REQUIRE(normalizeThreadCount(1, 100) == 1);
    SANPAO15_REQUIRE(normalizeThreadCount(4, 0) == 1);
    SANPAO15_REQUIRE(normalizeThreadCount(8, 3) == 3);
    SANPAO15_REQUIRE(normalizeThreadCount(300, 1000) == 256);
    SANPAO15_REQUIRE(normalizeThreadCount(0, 1) >= 1);
}

SANPAO15_TEST(parallelForRangesCoversEveryIndexOnce) {
    std::vector<uint8_t> seen(100, 0);
    parallelForRanges(100, 7, [&](uint64_t begin, uint64_t end, uint32_t) {
        for (uint64_t index = begin; index < end; ++index) {
            ++seen[static_cast<size_t>(index)];
        }
    });
    for (uint8_t count : seen) {
        SANPAO15_REQUIRE(count == 1);
    }
}

SANPAO15_TEST(parallelForRangesUsesSingleThreadSynchronously) {
    bool called = false;
    parallelForRanges(9, 1, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
        called = true;
        SANPAO15_REQUIRE(begin == 0);
        SANPAO15_REQUIRE(end == 9);
        SANPAO15_REQUIRE(threadId == 0);
    });
    SANPAO15_REQUIRE(called);
}

SANPAO15_TEST(parallelForRangesPropagatesExceptions) {
    sanpao15::test::requireThrows([&] {
        parallelForRanges(16, 4, [&](uint64_t begin, uint64_t end, uint32_t) {
            for (uint64_t index = begin; index < end; ++index) {
                if (index == 7) {
                    throw std::runtime_error("boom");
                }
            }
        });
    }, "parallelForRanges should rethrow worker exceptions");
}
