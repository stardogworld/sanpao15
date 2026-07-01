#pragma once

#include <cstdint>
#include <vector>

#include "sanpao15/move.h"
#include "sanpao15/position.h"

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

    friend bool operator==(const DenseSuccessor& lhs, const DenseSuccessor& rhs) = default;
};

struct DensePredecessor {
    int soldierCount = 0;
    uint64_t index = 0;
    Move move;

    friend bool operator==(const DensePredecessor& lhs, const DensePredecessor& rhs) = default;
};

enum class DensePredecessorValidation {
    None,
    Checked,
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
std::vector<DenseSuccessor> generateDenseSuccessorsFromPosition(
    int soldierCount,
    uint64_t denseIndex,
    const Position& pos);
void generateDenseSuccessorsFromPosition(
    int soldierCount,
    uint64_t denseIndex,
    const Position& pos,
    std::vector<DenseSuccessor>& out);
std::vector<DensePredecessor> generateDensePredecessors(int soldierCount, uint64_t childIndex);
std::vector<DensePredecessor> generateDensePredecessors(
    int soldierCount,
    uint64_t childIndex,
    DensePredecessorValidation validation);
void generateDensePredecessors(
    int soldierCount,
    uint64_t childIndex,
    DensePredecessorValidation validation,
    std::vector<DensePredecessor>& out);
void generateDensePredecessorsFromPosition(
    int soldierCount,
    uint64_t childIndex,
    const Position& child,
    DensePredecessorValidation validation,
    std::vector<DensePredecessor>& out);
void generateDensePredecessorIndicesFromPosition(
    int soldierCount,
    uint64_t childIndex,
    const Position& child,
    std::vector<uint64_t>& out);
DenseTerminalInfo terminalOutcomeForDenseState(int soldierCount, uint64_t denseIndex);
DenseTerminalInfo terminalOutcomeForPosition(const Position& pos);
DenseTerminalInfo terminalOutcomeForPositionWithSuccessors(
    const Position& pos,
    const std::vector<DenseSuccessor>& legalSuccessors);
DenseLayerMoveStats analyzeDenseLayerMoves(int soldierCount, uint64_t sampleLimit = 0);

const char* denseSuccessorKindToString(DenseSuccessorKind kind);

}  // namespace sanpao15
