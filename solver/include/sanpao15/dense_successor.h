#pragma once

#include <cstdint>
#include <vector>

#include "sanpao15/move.h"

namespace sanpao15 {

enum class DenseSuccessorKind {
    SameLayer,
    CaptureToLowerLayer,
};

struct DenseSuccessor {
    DenseSuccessorKind kind = DenseSuccessorKind::SameLayer;
    int fromSoldierCount = 0;
    int toSoldierCount = 0;
    uint64_t fromIndex = 0;
    uint64_t toIndex = 0;
    Move move;
};

struct DenseTerminalInfo {
    bool terminal = false;
    Outcome outcome = Outcome::Unknown;
};

struct DenseLayerMoveStats {
    int soldierCount = 0;
    uint64_t sampledStates = 0;
    uint64_t terminalStates = 0;
    uint64_t totalSuccessors = 0;
    uint64_t sameLayerSuccessors = 0;
    uint64_t captureSuccessors = 0;
    uint64_t maxSuccessors = 0;
};

std::vector<DenseSuccessor> generateDenseSuccessors(int soldierCount, uint64_t denseIndex);
DenseTerminalInfo terminalOutcomeForDenseState(int soldierCount, uint64_t denseIndex);
DenseLayerMoveStats analyzeDenseLayerMoves(int soldierCount, uint64_t sampleLimit = 0);

const char* denseSuccessorKindToString(DenseSuccessorKind kind);

}  // namespace sanpao15
