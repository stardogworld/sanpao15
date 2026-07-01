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

}  // namespace

SANPAO15_TEST(resultTableRoundtripPreservesEntries) {
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
