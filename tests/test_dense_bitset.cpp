#include "test_common.h"

#include <cstdint>

#include "sanpao15/dense_bitset.h"

using namespace sanpao15;

SANPAO15_TEST(denseBitsetSetClearAssignAndFill) {
    DenseBitset bits(130);
    SANPAO15_REQUIRE(bits.size() == 130);
    SANPAO15_REQUIRE(bits.bytes() == 24);
    SANPAO15_REQUIRE(!bits.get(0));
    SANPAO15_REQUIRE(!bits.get(129));

    bits.set(0);
    bits.set(64);
    bits.set(129);
    SANPAO15_REQUIRE(bits.get(0));
    SANPAO15_REQUIRE(bits.get(64));
    SANPAO15_REQUIRE(bits.get(129));

    bits.clear(64);
    bits.assign(1, true);
    bits.assign(129, false);
    SANPAO15_REQUIRE(bits.get(1));
    SANPAO15_REQUIRE(!bits.get(64));
    SANPAO15_REQUIRE(!bits.get(129));

    bits.fill(true);
    SANPAO15_REQUIRE(bits.get(0));
    SANPAO15_REQUIRE(bits.get(129));
    SANPAO15_REQUIRE((bits.words().back() >> 2u) == 0);

    bits.fill(false);
    SANPAO15_REQUIRE(!bits.get(0));
    SANPAO15_REQUIRE(!bits.get(129));
}

SANPAO15_TEST(denseBitsetCheckedApiRejectsOutOfRange) {
    DenseBitset bits(1);
    bits.set(0);
    SANPAO15_REQUIRE(bits.get(0));
    sanpao15::test::requireThrows([&] {
        (void)bits.get(1);
    }, "DenseBitset get should reject out-of-range index");
    sanpao15::test::requireThrows([&] {
        bits.set(1);
    }, "DenseBitset set should reject out-of-range index");
    sanpao15::test::requireThrows([&] {
        bits.clear(1);
    }, "DenseBitset clear should reject out-of-range index");
    sanpao15::test::requireThrows([&] {
        bits.assign(1, true);
    }, "DenseBitset assign should reject out-of-range index");
}

SANPAO15_TEST(denseBitsetSupportsUint64IndexBoundaries) {
    DenseBitset bits(65);
    bits.set(64);
    SANPAO15_REQUIRE(bits.get(64));
    SANPAO15_REQUIRE(bits.getUnchecked(64));
}
