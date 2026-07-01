#include "test_common.h"

#include "sanpao15/notation.h"

using namespace sanpao15;

SANPAO15_TEST(initialNotationMatchesSpec) {
    const Position pos = initialPosition();
    SANPAO15_REQUIRE(positionToNotation(pos) == "SSSSS/SSSSS/SSSSS/...../.CCC. c");
}

SANPAO15_TEST(parseNotationRoundTrips) {
    const Position pos = parsePositionNotation("SSSSS/SSSSS/SSSSS/...../.CCC. c");
    SANPAO15_REQUIRE(pos == initialPosition());
}

SANPAO15_TEST(invalidNotationThrows) {
    sanpao15::test::requireThrows(
        [] { (void)parsePositionNotation("SSSSS/SSSSS/SSSSS/...../.CCC. x"); },
        "invalid side should throw");
    sanpao15::test::requireThrows(
        [] { (void)parsePositionNotation("SSSSS/SSSSS/SSSSS/...../.CCZ. c"); },
        "invalid board character should throw");
}
