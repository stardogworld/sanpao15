#include "test_common.h"

#include <filesystem>
#include <fstream>

#include "sanpao15/bitboard.h"
#include "sanpao15/notation.h"
#include "sanpao15/rules.h"
#include "sanpao15/solver.h"
#include "sanpao15/table.h"

using namespace sanpao15;

namespace {

constexpr uint64_t OldRulesetHash = 0x5331355F76315F01ull;
constexpr std::streamoff TableRulesetHashOffset = 32;
constexpr std::streamoff TableStateCountOffset = 16;
constexpr std::streamoff TableFirstEntryOffset = 40;
constexpr std::streamoff TableEntryBytes = 17;

std::filesystem::path tempTablePath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

bool sameMove(const std::optional<Move>& lhs, const std::optional<Move>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || *lhs == *rhs;
}

uint32_t maskOf(std::initializer_list<int> bits) {
    uint32_t mask = 0;
    for (int bit : bits) {
        mask = setBit(mask, bit);
    }
    return mask;
}

Position oneStepCannonWinPosition() {
    Position pos;
    pos.cannons = setBit(0, 20);
    pos.soldiers = maskOf({1, 2, 3, 10});
    pos.side = Side::Cannon;
    return pos;
}

ResultTable smallResultTable() {
    ResultTable table;
    table.initialKey = 123;
    table.exact = false;
    table.truncated = true;

    StateInfo first;
    first.outcome = Outcome::CannonWin;
    first.distance = 4;
    first.bestMove = Move{21, 16, false, -1};
    table.entries.emplace(123, first);

    StateInfo second;
    second.outcome = Outcome::SoldierWin;
    second.distance = 7;
    second.bestMove = Move{20, 10, true, 10};
    table.entries.emplace(456, second);
    return table;
}

void writeU64LEAt(const std::filesystem::path& path, std::streamoff offset, uint64_t value) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(offset);
    for (int shift = 0; shift < 64; shift += 8) {
        file.put(static_cast<char>((value >> shift) & 0xffu));
    }
}

void writeByteAt(const std::filesystem::path& path, std::streamoff offset, uint8_t value) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(offset);
    file.put(static_cast<char>(value));
}

}  // namespace

SANPAO15_TEST(resultTableRoundtripPreservesEntries) {
    ResultTable table = smallResultTable();

    const std::filesystem::path path = tempTablePath("sanpao15-roundtrip.s15tbl");
    saveResultTable(table, path);
    const ResultTable loaded = loadResultTable(path);
    std::filesystem::remove(path);

    SANPAO15_REQUIRE(loaded.initialKey == table.initialKey);
    SANPAO15_REQUIRE(loaded.exact == table.exact);
    SANPAO15_REQUIRE(loaded.truncated == table.truncated);
    SANPAO15_REQUIRE(loaded.rulesetHash == StandardRulesetHash);
    SANPAO15_REQUIRE(loaded.entries.size() == table.entries.size());

    for (const auto& [key, expected] : table.entries) {
        const StateInfo* actual = findStateInfo(loaded, key);
        SANPAO15_REQUIRE(actual != nullptr);
        SANPAO15_REQUIRE(actual->outcome == expected.outcome);
        SANPAO15_REQUIRE(actual->distance == expected.distance);
        SANPAO15_REQUIRE(sameMove(actual->bestMove, expected.bestMove));
    }
}

SANPAO15_TEST(resultTableRejectsOldRulesetHashByDefault) {
    ResultTable table = smallResultTable();
    const std::filesystem::path path = tempTablePath("sanpao15-old-ruleset.s15tbl");
    saveResultTable(table, path);
    writeU64LEAt(path, TableRulesetHashOffset, OldRulesetHash);

    sanpao15::test::requireThrows([&] {
        (void)loadResultTable(path);
    }, "old ruleset hash should be rejected by default");

    const ResultTable unchecked = loadResultTableUnchecked(path);
    SANPAO15_REQUIRE(unchecked.rulesetHash == OldRulesetHash);
    std::filesystem::remove(path);
}

SANPAO15_TEST(resultTableRejectsTrailingBytes) {
    ResultTable table = smallResultTable();
    const std::filesystem::path path = tempTablePath("sanpao15-trailing.s15tbl");
    saveResultTable(table, path);
    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output.put('x');
    }

    sanpao15::test::requireThrows([&] {
        (void)loadResultTable(path);
    }, "trailing bytes should be rejected");
    std::filesystem::remove(path);
}

SANPAO15_TEST(resultTableRejectsDuplicateKeys) {
    ResultTable table = smallResultTable();
    const std::filesystem::path path = tempTablePath("sanpao15-duplicate-key.s15tbl");
    saveResultTable(table, path);
    writeU64LEAt(path, TableFirstEntryOffset + TableEntryBytes, 123);

    sanpao15::test::requireThrows([&] {
        (void)loadResultTable(path);
    }, "duplicate keys should be rejected");
    std::filesystem::remove(path);
}

SANPAO15_TEST(resultTableRejectsInvalidMoveSquare) {
    ResultTable table = smallResultTable();
    const std::filesystem::path path = tempTablePath("sanpao15-invalid-move.s15tbl");
    saveResultTable(table, path);
    writeByteAt(path, TableFirstEntryOffset + 8 + 1 + 4, 25);

    sanpao15::test::requireThrows([&] {
        (void)loadResultTable(path);
    }, "invalid move square should be rejected");
    std::filesystem::remove(path);
}

SANPAO15_TEST(resultTableRejectsUnknownEntryFlag) {
    ResultTable table = smallResultTable();
    const std::filesystem::path path = tempTablePath("sanpao15-unknown-flag.s15tbl");
    saveResultTable(table, path);
    writeByteAt(path, TableFirstEntryOffset + 15, 0x80);

    sanpao15::test::requireThrows([&] {
        (void)loadResultTable(path);
    }, "unknown entry flag should be rejected");
    std::filesystem::remove(path);
}

SANPAO15_TEST(resultTableRejectsEntryCountMismatch) {
    ResultTable table = smallResultTable();
    const std::filesystem::path path = tempTablePath("sanpao15-entry-count.s15tbl");
    saveResultTable(table, path);
    writeU64LEAt(path, TableStateCountOffset, 3);

    sanpao15::test::requireThrows([&] {
        (void)loadResultTable(path);
    }, "entry count mismatch should be rejected");
    std::filesystem::remove(path);
}

SANPAO15_TEST(truncatedTableKeepsUnknownStates) {
    const SolveResult result = solveFromInitial(SolveOptions{1});
    SANPAO15_REQUIRE(result.table.truncated);
    SANPAO15_REQUIRE(!result.table.exact);
    const StateInfo* initial = findStateInfo(result.table, packPosition(initialPosition()));
    SANPAO15_REQUIRE(initial != nullptr);
    SANPAO15_REQUIRE(initial->outcome == Outcome::Unknown);
    SANPAO15_REQUIRE(initial->distance == -1);
    SANPAO15_REQUIRE(!initial->bestMove.has_value());
}

SANPAO15_TEST(analyzePositionFromTableMarksBestMove) {
    const Position pos = oneStepCannonWinPosition();
    const Move best{20, 10, true, 10};
    const Position child = applyMove(pos, best);

    ResultTable table;
    table.initialKey = packPosition(pos);
    table.exact = true;
    table.truncated = false;

    StateInfo current;
    current.outcome = Outcome::CannonWin;
    current.distance = 1;
    current.bestMove = best;
    table.entries.emplace(packPosition(pos), current);

    StateInfo terminal;
    terminal.outcome = Outcome::CannonWin;
    terminal.distance = 0;
    table.entries.emplace(packPosition(child), terminal);

    const Analysis analysis = analyzePositionFromTable(pos, table);
    SANPAO15_REQUIRE(analysis.foundInTable);
    SANPAO15_REQUIRE(analysis.tableExact);
    SANPAO15_REQUIRE(analysis.outcome == Outcome::CannonWin);
    SANPAO15_REQUIRE(analysis.distance == 1);
    SANPAO15_REQUIRE(analysis.bestMove.has_value());
    SANPAO15_REQUIRE(*analysis.bestMove == best);
    SANPAO15_REQUIRE(!analysis.legalMoves.empty());

    bool sawBest = false;
    for (const MoveAnalysis& item : analysis.legalMoves) {
        if (item.move == best) {
            sawBest = true;
            SANPAO15_REQUIRE(item.resultingOutcome == Outcome::CannonWin);
            SANPAO15_REQUIRE(item.distance == 0);
            SANPAO15_REQUIRE(item.isBest);
        }
    }
    SANPAO15_REQUIRE(sawBest);
}

SANPAO15_TEST(analyzePositionFromTableRejectsRulesetMismatch) {
    ResultTable table = smallResultTable();
    table.rulesetHash = OldRulesetHash;

    sanpao15::test::requireThrows([&] {
        (void)analyzePositionFromTable(oneStepCannonWinPosition(), table);
    }, "analyzePositionFromTable should reject mismatched ruleset hash");
}

SANPAO15_TEST(oneStepCannonWinHasDistanceAndBestMove) {
    const Position pos = oneStepCannonWinPosition();
    const Analysis analysis = analyzePosition(pos, SolveOptions{0});
    SANPAO15_REQUIRE(analysis.foundInTable);
    SANPAO15_REQUIRE(analysis.outcome == Outcome::CannonWin);
    SANPAO15_REQUIRE(analysis.distance == 1);
    SANPAO15_REQUIRE(analysis.bestMove.has_value());
    SANPAO15_REQUIRE(analysis.bestMove->from == 20);
    SANPAO15_REQUIRE(analysis.bestMove->to == 10);
    SANPAO15_REQUIRE(analysis.bestMove->capture);
    SANPAO15_REQUIRE(analysis.bestMove->capturedSquare == 10);
}

SANPAO15_TEST(initialNotationAndInitialPositionHaveSameKey) {
    const Position parsed = parsePositionNotation("SSSSS/SSSSS/SSSSS/...../.CCC. c");
    SANPAO15_REQUIRE(packPosition(parsed) == packPosition(initialPosition()));
}
