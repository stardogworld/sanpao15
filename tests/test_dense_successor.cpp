#include "test_common.h"

#include <algorithm>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/notation.h"
#include "sanpao15/rules.h"

using namespace sanpao15;

namespace {

uint32_t maskOf(std::initializer_list<int> bits) {
    uint32_t mask = 0;
    for (int bit : bits) {
        mask = setBit(mask, bit);
    }
    return mask;
}

uint64_t nextRandom(uint64_t& state) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
}

bool samePositionSet(std::vector<Position> lhs, std::vector<Position> rhs) {
    const auto less = [](const Position& a, const Position& b) {
        return packPosition(a) < packPosition(b);
    };
    std::sort(lhs.begin(), lhs.end(), less);
    std::sort(rhs.begin(), rhs.end(), less);
    return lhs == rhs;
}

void requireSuccessorsMatchCore(int soldierCount, uint64_t index) {
    const Position pos = positionFromDenseIndex(soldierCount, index);
    std::vector<Position> expected;
    for (const Move& move : generateLegalMoves(pos)) {
        expected.push_back(applyMove(pos, move));
    }

    std::vector<Position> actual;
    for (const DenseSuccessor& successor : generateDenseSuccessors(soldierCount, index)) {
        SANPAO15_REQUIRE(successor.fromSoldierCount == soldierCount);
        SANPAO15_REQUIRE(successor.fromIndex == index);
        SANPAO15_REQUIRE(successor.toIndex < denseStateCount(successor.toSoldierCount));
        const Position decoded = positionFromDenseIndex(successor.toSoldierCount, successor.toIndex);
        actual.push_back(decoded);
        SANPAO15_REQUIRE(decoded == applyMove(pos, successor.move));
        if (successor.kind == DenseSuccessorKind::SameLayer) {
            SANPAO15_REQUIRE(successor.toSoldierCount == soldierCount);
        } else {
            SANPAO15_REQUIRE(successor.toSoldierCount == soldierCount - 1);
        }
    }
    SANPAO15_REQUIRE(samePositionSet(expected, actual));
}

}  // namespace

SANPAO15_TEST(denseSuccessorsMatchCoreForSampledLayers) {
    uint64_t rng = 0x51554343ull;
    for (int soldierCount : {0, 1, 2, 3, 15}) {
        const uint64_t count = denseStateCount(soldierCount);
        requireSuccessorsMatchCore(soldierCount, 0);
        requireSuccessorsMatchCore(soldierCount, count - 1);
        for (int sample = 0; sample < 40; ++sample) {
            requireSuccessorsMatchCore(soldierCount, nextRandom(rng) % count);
        }
    }
}

SANPAO15_TEST(denseSuccessorsMatchInitialPosition) {
    const Position initial = parsePositionNotation("SSSSS/SSSSS/SSSSS/...../.CCC. c");
    const int soldierCount = popcount25(initial.soldiers);
    const uint64_t index = denseIndex(initial);
    const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
    const std::vector<Move> moves = generateLegalMoves(initial);
    SANPAO15_REQUIRE(successors.size() == moves.size());
    requireSuccessorsMatchCore(soldierCount, index);
}

SANPAO15_TEST(denseSuccessorCaptureTargetsLowerLayer) {
    Position pos;
    pos.cannons = maskOf({20, 21, 22});
    pos.soldiers = maskOf({10, 1, 2});
    pos.side = Side::Cannon;

    const int soldierCount = popcount25(pos.soldiers);
    const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, denseIndex(pos));
    const auto capture = std::find_if(successors.begin(), successors.end(), [](const DenseSuccessor& successor) {
        return successor.kind == DenseSuccessorKind::CaptureToLowerLayer;
    });
    SANPAO15_REQUIRE(capture != successors.end());
    SANPAO15_REQUIRE(capture->toSoldierCount == soldierCount - 1);
    SANPAO15_REQUIRE(capture->toIndex < denseStateCount(soldierCount - 1));
    SANPAO15_REQUIRE(capture->move.capture);
}

SANPAO15_TEST(denseSuccessorK0HasNoCaptureAndIsCannonWinTerminal) {
    const uint64_t count = denseStateCount(0);
    for (uint64_t index : {uint64_t{0}, count / 2, count - 1}) {
        const DenseTerminalInfo terminal = terminalOutcomeForDenseState(0, index);
        SANPAO15_REQUIRE(terminal.terminal);
        SANPAO15_REQUIRE(terminal.outcome == Outcome::CannonWin);
        for (const DenseSuccessor& successor : generateDenseSuccessors(0, index)) {
            SANPAO15_REQUIRE(successor.kind == DenseSuccessorKind::SameLayer);
            SANPAO15_REQUIRE(successor.toSoldierCount == 0);
        }
    }
}

SANPAO15_TEST(denseTerminalDetectsCannonStuckSoldierWin) {
    Position pos;
    pos.cannons = maskOf({0, 1, 5});
    pos.soldiers = maskOf({2, 6, 10});
    pos.side = Side::Cannon;
    SANPAO15_REQUIRE(generateCannonMoves(pos).empty());

    const DenseTerminalInfo terminal = terminalOutcomeForDenseState(popcount25(pos.soldiers), denseIndex(pos));
    SANPAO15_REQUIRE(terminal.terminal);
    SANPAO15_REQUIRE(terminal.outcome == Outcome::SoldierWin);
}

SANPAO15_TEST(denseSuccessorRejectsInvalidIndex) {
    sanpao15::test::requireThrows([] {
        (void)generateDenseSuccessors(1, denseStateCount(1));
    }, "generateDenseSuccessors rejects out-of-range index");
    sanpao15::test::requireThrows([] {
        (void)terminalOutcomeForDenseState(1, denseStateCount(1));
    }, "terminalOutcomeForDenseState rejects out-of-range index");
}

SANPAO15_TEST(denseMoveStatsSmoke) {
    const DenseLayerMoveStats k0 = analyzeDenseLayerMoves(0, 0);
    SANPAO15_REQUIRE(k0.sampledStates == denseStateCount(0));
    SANPAO15_REQUIRE(k0.terminalStates == denseStateCount(0));
    SANPAO15_REQUIRE(k0.captureSuccessors == 0);
    SANPAO15_REQUIRE(k0.sameLayerSuccessors + k0.captureSuccessors == k0.totalSuccessors);

    const DenseLayerMoveStats k1 = analyzeDenseLayerMoves(1, 0);
    SANPAO15_REQUIRE(k1.sampledStates == denseStateCount(1));
    SANPAO15_REQUIRE(k1.sameLayerSuccessors + k1.captureSuccessors == k1.totalSuccessors);
    SANPAO15_REQUIRE(k1.captureSuccessors > 0);

    const DenseLayerMoveStats k2 = analyzeDenseLayerMoves(2, 1000);
    SANPAO15_REQUIRE(k2.sampledStates == 1000);
    SANPAO15_REQUIRE(k2.sameLayerSuccessors + k2.captureSuccessors == k2.totalSuccessors);
}
