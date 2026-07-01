#include "test_common.h"

#include <filesystem>
#include <fstream>

#include "sanpao15/dense_index.h"
#include "sanpao15/dense_table.h"
#include "sanpao15/table.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempResPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

constexpr std::streamoff DenseResultHeaderBytes = 44;

void corruptFirstByte(const std::filesystem::path& path) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.put('X');
}

void writeByteAt(const std::filesystem::path& path, std::streamoff offset, uint8_t value) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(offset);
    file.put(static_cast<char>(value));
}

}  // namespace

SANPAO15_TEST(denseOutcomeTableSetGetKeepsUnknownDistinct) {
    DenseOutcomeTable table(8);
    for (uint64_t index = 0; index < table.size(); ++index) {
        SANPAO15_REQUIRE(table.get(index) == Outcome::Unknown);
    }
    table.set(0, Outcome::Unknown);
    table.set(1, Outcome::CannonWin);
    table.set(2, Outcome::SoldierWin);
    table.set(3, Outcome::Draw);
    SANPAO15_REQUIRE(table.get(0) == Outcome::Unknown);
    SANPAO15_REQUIRE(table.get(1) == Outcome::CannonWin);
    SANPAO15_REQUIRE(table.get(2) == Outcome::SoldierWin);
    SANPAO15_REQUIRE(table.get(3) == Outcome::Draw);
    SANPAO15_REQUIRE(table.bytes() == 8);
}

SANPAO15_TEST(packedOutcomeTableSetGetCrossesByteBoundaries) {
    PackedOutcomeTable2Bit table(10);
    table.set(0, Outcome::CannonWin);
    table.set(3, Outcome::Draw);
    table.set(4, Outcome::SoldierWin);
    table.set(7, Outcome::CannonWin);
    table.set(8, Outcome::Draw);
    table.set(9, Outcome::Unknown);
    SANPAO15_REQUIRE(table.bytes() == 3);
    SANPAO15_REQUIRE(table.get(0) == Outcome::CannonWin);
    SANPAO15_REQUIRE(table.get(3) == Outcome::Draw);
    SANPAO15_REQUIRE(table.get(4) == Outcome::SoldierWin);
    SANPAO15_REQUIRE(table.get(7) == Outcome::CannonWin);
    SANPAO15_REQUIRE(table.get(8) == Outcome::Draw);
    SANPAO15_REQUIRE(table.get(9) == Outcome::Unknown);
}

SANPAO15_TEST(denseResultByteRoundtrip) {
    const int soldiers = 0;
    DenseOutcomeTable table(denseStateCount(soldiers));
    table.set(0, Outcome::CannonWin);
    table.set(1, Outcome::SoldierWin);
    table.set(table.size() - 1, Outcome::Draw);

    const std::filesystem::path path = tempResPath("sanpao15-byte-roundtrip.s15res");
    saveDenseResultTable(table, soldiers, path, StandardRulesetHash);
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, soldiers);
    SANPAO15_REQUIRE(info.encoding == DenseResultEncoding::Byte);
    SANPAO15_REQUIRE(info.stateCount == denseStateCount(soldiers));

    const DenseOutcomeTable loaded = loadDenseResultTableByte(path, StandardRulesetHash, soldiers);
    std::filesystem::remove(path);
    SANPAO15_REQUIRE(loaded.get(0) == Outcome::CannonWin);
    SANPAO15_REQUIRE(loaded.get(1) == Outcome::SoldierWin);
    SANPAO15_REQUIRE(loaded.get(2) == Outcome::Unknown);
    SANPAO15_REQUIRE(loaded.get(loaded.size() - 1) == Outcome::Draw);
}

SANPAO15_TEST(denseResultPackedRoundtrip) {
    const int soldiers = 0;
    PackedOutcomeTable2Bit table(denseStateCount(soldiers));
    table.set(0, Outcome::CannonWin);
    table.set(4, Outcome::SoldierWin);
    table.set(table.size() - 1, Outcome::Draw);

    const std::filesystem::path path = tempResPath("sanpao15-packed-roundtrip.s15res");
    saveDenseResultTable2Bit(table, soldiers, path, StandardRulesetHash);
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, soldiers);
    SANPAO15_REQUIRE(info.encoding == DenseResultEncoding::Packed2Bit);
    SANPAO15_REQUIRE(info.payloadBytes == denseStateCount(soldiers) / 4);

    const PackedOutcomeTable2Bit loaded = loadDenseResultTable2Bit(path, StandardRulesetHash, soldiers);
    std::filesystem::remove(path);
    SANPAO15_REQUIRE(loaded.get(0) == Outcome::CannonWin);
    SANPAO15_REQUIRE(loaded.get(4) == Outcome::SoldierWin);
    SANPAO15_REQUIRE(loaded.get(5) == Outcome::Unknown);
    SANPAO15_REQUIRE(loaded.get(loaded.size() - 1) == Outcome::Draw);
}

SANPAO15_TEST(denseResultRejectsBadMagicAndWrongExpectations) {
    const int soldiers = 0;
    const std::filesystem::path path = tempResPath("sanpao15-bad-magic.s15res");
    createEmptyDenseResultFile(soldiers, path, DenseResultEncoding::Byte, StandardRulesetHash);
    sanpao15::test::requireThrows([&] {
        (void)validateDenseResultFile(path, StandardRulesetHash, 1);
    }, "wrong soldier count rejected");
    sanpao15::test::requireThrows([&] {
        (void)validateDenseResultFile(path, StandardRulesetHash + 1, soldiers);
    }, "wrong ruleset hash rejected");
    corruptFirstByte(path);
    sanpao15::test::requireThrows([&] {
        (void)inspectDenseResultFile(path);
    }, "bad magic rejected");
    std::filesystem::remove(path);
}

SANPAO15_TEST(denseResultValidateRejectsInvalidBytePayload) {
    const int soldiers = 0;
    const std::filesystem::path path = tempResPath("sanpao15-invalid-byte-payload.s15res");
    createEmptyDenseResultFile(soldiers, path, DenseResultEncoding::Byte, StandardRulesetHash);
    writeByteAt(path, DenseResultHeaderBytes, 4);

    const DenseResultFileInfo inspected = inspectDenseResultFile(path);
    SANPAO15_REQUIRE(inspected.encoding == DenseResultEncoding::Byte);
    sanpao15::test::requireThrows([&] {
        (void)validateDenseResultFile(path, StandardRulesetHash, soldiers);
    }, "invalid byte outcome payload should be rejected by validate");
    std::filesystem::remove(path);
}

SANPAO15_TEST(denseResultPackedLayersHaveNoUnusedBits) {
    for (int soldiers = 0; soldiers <= 15; ++soldiers) {
        SANPAO15_REQUIRE(denseStateCount(soldiers) % 4u == 0);
    }
}
