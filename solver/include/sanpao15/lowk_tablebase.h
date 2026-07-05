#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sanpao15/position.h"
#include "sanpao15/dense_successor.h"
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

struct DenseStreamingInitScan {
    DenseTerminalInfo terminal;
    uint64_t successorCount = 0;
    uint64_t sameLayerEdges = 0;
    uint64_t captureEdges = 0;
    uint64_t remainingCount = 0;
    bool resolved = false;
    Outcome resolvedOutcome = Outcome::Unknown;
};

struct DenseLayerProductionSolveOptions {
    int soldierCount = -1;
    std::optional<std::filesystem::path> lowerResultPath;
    std::filesystem::path outputResultPath;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
    bool overwrite = false;
    bool writeStatsJson = true;
};

struct DenseLayerProductionSolveResult {
    DenseLayerSolveResult solve;
    std::filesystem::path outputResultPath;
    std::filesystem::path statsJsonPath;
    uint64_t outputBytes = 0;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
};

struct DenseLayerRangeEntry {
    int soldierCount = -1;
    std::string status;
    std::filesystem::path resultPath;
    std::filesystem::path statsPath;
    bool statsPathMissing = false;
    uint64_t stateCount = 0;
    uint64_t outputBytes = 0;
    double totalSeconds = 0.0;
    uint64_t cannonWin = 0;
    uint64_t soldierWin = 0;
    uint64_t draw = 0;
    uint64_t unknown = 0;
    std::string error;
};

struct DenseLayerRangeSolveOptions {
    int startLayer = -1;
    int endLayer = -1;
    std::filesystem::path outputDir;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
    bool resume = false;
    bool overwrite = false;
    bool cleanTemp = false;
};

struct DenseLayerRangeSolveResult {
    int startLayer = -1;
    int endLayer = -1;
    std::filesystem::path outputDir;
    std::filesystem::path manifestPath;
    std::vector<DenseLayerRangeEntry> layers;
    double totalSeconds = 0.0;
    uint64_t totalOutputBytes = 0;
};

enum class DenseLayerFileStatus {
    Missing,
    Valid,
    Invalid,
};

struct DenseLayerPreflightOptions {
    int startLayer = -1;
    int endLayer = -1;
    std::filesystem::path outputDir;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
    std::optional<std::filesystem::path> outputJsonPath;
};

struct DenseLayerPreflightEntry {
    int soldierCount = -1;
    uint64_t stateCount = 0;
    uint64_t outputBytes2Bit = 0;
    uint64_t outputBytesByte = 0;
    uint64_t selectedOutputBytes = 0;
    uint64_t remainingBytes = 0;
    uint64_t lowerLayerPayloadBytes = 0;
    uint64_t estimatedQueueBytes = 0;
    uint64_t estimatedCoreMemoryBytes = 0;
    uint64_t recommendedMemoryBytes = 0;
    double estimatedSeconds = 0.0;
    DenseLayerFileStatus resultStatus = DenseLayerFileStatus::Missing;
    bool statsJsonPresent = false;
    bool manifestEntryPresent = false;
    bool wouldSkipWithResume = false;
    bool wouldSolve = false;
    bool lowerLayerAvailable = false;
    std::string risk;
    std::optional<std::string> error;
};

struct DenseLayerRangePreflightResult {
    int startLayer = -1;
    int endLayer = -1;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
    std::filesystem::path outputDir;
    std::filesystem::path jsonPath;
    std::vector<DenseLayerPreflightEntry> layers;
    uint64_t totalStateCount = 0;
    uint64_t totalSelectedOutputBytes = 0;
    uint64_t existingValidOutputBytes = 0;
    uint64_t missingOutputBytes = 0;
    uint64_t requiredAdditionalDiskBytes = 0;
    uint64_t availableDiskBytes = 0;
    bool diskSpaceKnown = false;
    bool diskOk = false;
    uint64_t peakEstimatedCoreMemoryBytes = 0;
    uint64_t peakRecommendedMemoryBytes = 0;
    int peakMemoryLayer = -1;
    double estimatedTotalSeconds = 0.0;
    double estimatedRemainingSeconds = 0.0;
    bool canResumeRange = false;
    bool hasInvalidLayers = false;
    bool hasMissingLower = false;
};

struct DenseLayerVerifyOptions {
    std::filesystem::path resultPath;
    std::optional<std::filesystem::path> lowerResultPath;
    uint64_t sampleLimit = 10000;
};

struct DenseLayerVerifyResult {
    int soldierCount = -1;
    uint64_t stateCount = 0;
    uint64_t sampledStates = 0;
    uint64_t cannonWin = 0;
    uint64_t soldierWin = 0;
    uint64_t draw = 0;
    uint64_t unknown = 0;
    DenseResultEncoding encoding = DenseResultEncoding::Packed2Bit;
};

struct DenseTablebaseLookupOptions {
    std::filesystem::path tablebaseDir;
    Position position;
    bool includeMoves = false;
};

struct DenseTablebaseMoveInfo {
    Move move;
    Position successor;
    uint64_t successorIndex = 0;
    int successorSoldierCount = 0;
    Outcome successorOutcome = Outcome::Unknown;
    std::string classification;
};

struct DenseTablebaseLookupResult {
    Position position;
    int soldierCount = 0;
    uint64_t denseIndex = 0;
    Outcome outcome = Outcome::Unknown;
    bool terminal = false;
    std::string terminalReason;
    std::vector<DenseTablebaseMoveInfo> moves;
};

struct WdlLineExplorerOptions {
    std::filesystem::path tablebaseDir;
    Position start;
    int maxPlies = 100;
    bool includeAlternatives = true;
};

struct WdlAlternativeMove {
    Move move;
    Outcome successorOutcome = Outcome::Unknown;
    std::string classification;
};

struct WdlLinePly {
    int ply = 0;
    Position position;
    int soldierCount = 0;
    uint64_t denseIndex = 0;
    Outcome outcome = Outcome::Unknown;
    Side sideToMove = Side::Cannon;

    Move chosenMove;
    Position successor;
    Outcome successorOutcome = Outcome::Unknown;
    std::string chosenClassification;

    std::vector<WdlAlternativeMove> alternatives;
};

struct WdlLineExplorerResult {
    Position start;
    Outcome startOutcome = Outcome::Unknown;
    std::vector<WdlLinePly> plies;
    std::string stopReason;
    std::optional<int> cycleStartPly;
    std::optional<std::string> error;
};

DenseLayerSolveResult solveDenseLayerOutcome(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output);
DenseLayerSolveResult solveDenseLayerOutcomeStreaming(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output);
DenseLayerSolveResult solveDenseLayerOutcomeStreamingProduction(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output);
uint8_t checkedStreamingRemainingCount(uint64_t remainingCount);
DenseStreamingInitScan scanDenseStateForStreamingInitialization(
    int soldierCount,
    uint64_t index,
    const Position& pos,
    const PackedOutcomeTable2Bit* lowerLayer);

std::filesystem::path lowKLayerResultPath(const std::filesystem::path& dir, int soldierCount);

std::vector<LowKTablebaseLayerResult> solveLowKTablebase(const LowKTablebaseSolveOptions& options);
std::vector<LowKTablebaseLayerResult> solveLowKTablebaseStreaming(const LowKTablebaseSolveOptions& options);
DenseLayerProductionSolveResult solveDenseLayerProduction(
    const DenseLayerProductionSolveOptions& options);
DenseLayerRangeSolveResult solveDenseLayerRange(
    const DenseLayerRangeSolveOptions& options);
DenseLayerRangePreflightResult preflightDenseLayerRange(
    const DenseLayerPreflightOptions& options);
const char* denseLayerFileStatusToString(DenseLayerFileStatus status);

LowKTablebaseVerifyResult verifyLowKTablebase(
    const std::filesystem::path& dir,
    int maxK,
    uint64_t sampleLimit);
DenseLayerVerifyResult verifyDenseLayerResult(const DenseLayerVerifyOptions& options);

Outcome lookupDenseTablebaseOutcomeAt(
    const std::filesystem::path& tablebaseDir,
    const Position& position);
DenseTablebaseLookupResult lookupDenseTablebasePosition(
    const DenseTablebaseLookupOptions& options);
WdlLineExplorerResult exploreDenseTablebaseWdlLine(
    const WdlLineExplorerOptions& options);

}  // namespace sanpao15
