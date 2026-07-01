#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "sanpao15/dense_table.h"

namespace sanpao15 {

struct DenseLayerSolveResult {
    int soldierCount = 0;
    uint64_t stateCount = 0;

    uint64_t cannonWin = 0;
    uint64_t soldierWin = 0;
    uint64_t draw = 0;
    uint64_t unknown = 0;

    uint64_t terminalStates = 0;
    uint64_t sameLayerEdges = 0;
    uint64_t captureEdges = 0;

    uint64_t retrogradeResolved = 0;
    uint64_t unresolvedAsDraw = 0;

    uint64_t resolvedByTerminal = 0;
    uint64_t resolvedByLowerLayer = 0;
    uint64_t resolvedByPropagation = 0;
    uint64_t drawAfterQueue = 0;
    uint64_t maxSuccessors = 0;
    uint64_t maxRemaining = 0;
    uint64_t queuePeak = 0;
    uint64_t estimatedMemoryBytes = 0;
    uint64_t predecessorCalls = 0;
    uint64_t generatedPredecessors = 0;
    uint64_t maxPredecessors = 0;
    double initializationSeconds = 0.0;
    double propagationSeconds = 0.0;
    double finalizeSeconds = 0.0;
    double seconds = 0.0;
};

struct LowKTablebaseLayerResult {
    DenseLayerSolveResult solve;
    std::filesystem::path outputFile;
    uint64_t outputBytes = 0;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
};

struct LowKTablebaseSolveOptions {
    int maxK = 0;
    std::filesystem::path outputDir;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
};

struct LowKTablebaseVerifyLayerResult {
    int soldierCount = 0;
    uint64_t stateCount = 0;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
    uint64_t sampledStates = 0;
    uint64_t cannonWin = 0;
    uint64_t soldierWin = 0;
    uint64_t draw = 0;
    uint64_t unknown = 0;
};

struct LowKTablebaseVerifyResult {
    std::filesystem::path inputDir;
    int maxK = 0;
    uint64_t sampleLimit = 0;
    std::vector<LowKTablebaseVerifyLayerResult> layers;
};

DenseLayerSolveResult solveDenseLayerOutcome(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output);
DenseLayerSolveResult solveDenseLayerOutcomeStreaming(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output);
uint8_t checkedStreamingRemainingCount(uint64_t remainingCount);

std::filesystem::path lowKLayerResultPath(const std::filesystem::path& dir, int soldierCount);

std::vector<LowKTablebaseLayerResult> solveLowKTablebase(const LowKTablebaseSolveOptions& options);
std::vector<LowKTablebaseLayerResult> solveLowKTablebaseStreaming(const LowKTablebaseSolveOptions& options);

LowKTablebaseVerifyResult verifyLowKTablebase(
    const std::filesystem::path& dir,
    int maxK,
    uint64_t sampleLimit);

}  // namespace sanpao15
