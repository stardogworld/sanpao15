#include "test_common.h"

#include <string>

#include "sanpao15/small_count_array.h"

using namespace sanpao15;

namespace {

void exerciseWidth(SmallCountArray::Width width, uint8_t maxValue) {
    SmallCountArray counts(11, width);
    SANPAO15_REQUIRE(counts.size() == 11);
    SANPAO15_REQUIRE(counts.width() == width);
    counts.set(0, 0);
    counts.set(1, 1);
    counts.set(2, 2);
    counts.set(3, maxValue);
    counts.set(4, static_cast<uint8_t>(maxValue - 1u));
    counts.set(10, maxValue);

    SANPAO15_REQUIRE(counts.get(0) == 0);
    SANPAO15_REQUIRE(counts.get(1) == 1);
    SANPAO15_REQUIRE(counts.get(2) == 2);
    SANPAO15_REQUIRE(counts.get(3) == maxValue);
    SANPAO15_REQUIRE(counts.get(4) == maxValue - 1u);
    SANPAO15_REQUIRE(counts.get(10) == maxValue);

    counts.decrement(3);
    counts.decrementUnchecked(10);
    SANPAO15_REQUIRE(counts.get(3) == maxValue - 1u);
    SANPAO15_REQUIRE(counts.get(10) == maxValue - 1u);
}

}  // namespace

SANPAO15_TEST(smallCountArray4BitSetGetAndDecrement) {
    exerciseWidth(SmallCountArray::Width::Nibble4, 15);
    SmallCountArray counts(5, SmallCountArray::Width::Nibble4);
    SANPAO15_REQUIRE(counts.bytes() == 3);
    counts.set(4, 15);
    SANPAO15_REQUIRE(counts.get(4) == 15);
}

SANPAO15_TEST(smallCountArray6BitSetGetAndDecrement) {
    exerciseWidth(SmallCountArray::Width::Packed6, 63);
    SmallCountArray counts(5, SmallCountArray::Width::Packed6);
    SANPAO15_REQUIRE(counts.bytes() == 4);
    counts.set(0, 63);
    counts.set(1, 62);
    counts.set(2, 61);
    counts.set(3, 60);
    counts.set(4, 59);
    SANPAO15_REQUIRE(counts.get(0) == 63);
    SANPAO15_REQUIRE(counts.get(1) == 62);
    SANPAO15_REQUIRE(counts.get(2) == 61);
    SANPAO15_REQUIRE(counts.get(3) == 60);
    SANPAO15_REQUIRE(counts.get(4) == 59);
}

SANPAO15_TEST(smallCountArray8BitSetGetAndDecrement) {
    exerciseWidth(SmallCountArray::Width::Byte8, 255);
    SmallCountArray counts(3, SmallCountArray::Width::Byte8);
    SANPAO15_REQUIRE(counts.bytes() == 3);
    counts.set(2, 255);
    SANPAO15_REQUIRE(counts.get(2) == 255);
}

SANPAO15_TEST(smallCountArrayRejectsOverflowAndUnderflow) {
    SmallCountArray four(1, SmallCountArray::Width::Nibble4);
    sanpao15::test::requireThrows([&] {
        four.set(0, 16);
    }, "4-bit small count should reject 16");
    sanpao15::test::requireThrows([&] {
        four.decrement(0);
    }, "small count decrement should reject underflow");

    SmallCountArray six(1, SmallCountArray::Width::Packed6);
    sanpao15::test::requireThrows([&] {
        six.set(0, 64);
    }, "6-bit small count should reject 64");
    sanpao15::test::requireThrows([&] {
        (void)six.get(1);
    }, "small count should reject out-of-range get");
}

SANPAO15_TEST(smallCountWidthSelectionUsesSmallestSafeWidth) {
    SANPAO15_REQUIRE(chooseSmallCountWidth(15) == SmallCountArray::Width::Nibble4);
    SANPAO15_REQUIRE(chooseSmallCountWidth(16) == SmallCountArray::Width::Packed6);
    SANPAO15_REQUIRE(chooseSmallCountWidth(63) == SmallCountArray::Width::Packed6);
    SANPAO15_REQUIRE(chooseSmallCountWidth(64) == SmallCountArray::Width::Byte8);
    SANPAO15_REQUIRE(smallCountWidthToString(SmallCountArray::Width::Nibble4) == std::string("4bit"));
    SANPAO15_REQUIRE(smallCountArrayBytes(5, SmallCountArray::Width::Nibble4) == 3);
    SANPAO15_REQUIRE(smallCountArrayBytes(5, SmallCountArray::Width::Packed6) == 4);
    SANPAO15_REQUIRE(smallCountArrayBytes(5, SmallCountArray::Width::Byte8) == 5);
}
