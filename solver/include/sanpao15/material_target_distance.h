#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sanpao15/dense_bitset.h"
#include "sanpao15/dense_table.h"
#include "sanpao15/move.h"
#include "sanpao15/position.h"

namespace sanpao15 {

enum class MtdEncoding : uint32_t {
    Packed12Material4Distance8 = 1,
};

struct MtdEntry {
    uint8_t materialTarget = 0;
    uint8_t guaranteeDistance = 0;

    friend bool operator==(const MtdEntry& lhs, const MtdEntry& rhs) = default;
};

class PackedMtdTable12 {
public:
    explicit PackedMtdTable12(uint64_t stateCount = 0, MtdEntry initial = {});

    MtdEntry get(uint64_t index) const;
    void set(uint64_t index, MtdEntry entry);
    MtdEntry getUnchecked(uint64_t index) const;
    void setUnchecked(uint64_t index, MtdEntry entry);

    uint64_t size() const;
    uint64_t bytes() const;
    const std::vector<uint8_t>& payload() const;
    std::vector<uint8_t>& mutablePayload();

private:
    uint64_t stateCount_ = 0;
    std::vector<uint8_t> packed_;
};

struct MtdFileInfo {
    uint32_t version = 0;
    uint64_t rulesetHash = 0;
    int soldierCount = 0;
    uint64_t stateCount = 0;
    MtdEncoding encoding = MtdEncoding::Packed12Material4Distance8;
    uint64_t payloadBytes = 0;
    uint64_t fileSize = 0;
};

struct MtdInspectStats {
    MtdFileInfo info;
    uint32_t threads = 1;
    uint8_t minMaterialTarget = 0;
    uint8_t maxMaterialTarget = 0;
    uint8_t maxExactDistance = 0;
    uint64_t saturatedDistanceCount = 0;
    std::array<uint64_t, 16> materialTargetCounts{};
    std::array<uint64_t, 16> cannonMaxCapturesCounts{};
    std::array<uint64_t, 256> distanceCounts{};
};

struct MtdLayerWriteStats {
    uint64_t stateCount = 0;
    uint64_t outputBytes = 0;
    std::array<uint64_t, 16> materialTargetCounts{};
    std::array<uint64_t, 16> cannonMaxCapturesCounts{};
    std::array<uint64_t, 256> distanceCounts{};
    uint8_t maxExactDistance = 0;
    uint64_t saturatedDistanceCount = 0;
};

struct MtdWdlLayerScanSummary {
    std::array<uint64_t, 4> outcomeCounts{};
    bool hasUnknown = false;
    uint64_t firstUnknown = 0;
};

struct MtdDistanceWork {
    std::vector<uint8_t> values;
    DenseBitset solved;

    explicit MtdDistanceWork(uint64_t stateCount = 0);

    void reset(uint64_t stateCount);
    uint64_t size() const noexcept;
    uint64_t bytes() const noexcept;
    bool isSolved(uint64_t index) const;
    bool isSolvedUnchecked(uint64_t index) const noexcept;
    uint8_t get(uint64_t index) const;
    uint8_t getUnchecked(uint64_t index) const noexcept;
    void set(uint64_t index, uint8_t value);
    void setUnchecked(uint64_t index, uint8_t value) noexcept;
    void setUncheckedAtomic(uint64_t index, uint8_t value) noexcept;
    void markUnsolved(uint64_t index);
    void markUnsolvedUnchecked(uint64_t index) noexcept;
    void fillSolved(uint8_t value);
};

class MtdThresholdStampScratch {
public:
    explicit MtdThresholdStampScratch(uint64_t stateCount = 0);

    void reset(uint64_t stateCount);
    void nextRound();
    bool isTrue(uint64_t index, bool materialAssigned) const;
    bool mark(uint64_t index, bool materialAssigned);
    uint8_t currentStamp() const;
    const std::vector<uint8_t>& stamps() const;

private:
    std::vector<uint8_t> stamps_;
    uint8_t currentStamp_ = 0;
};

struct MtdLayerSolveResult {
    int soldierCount = 0;
    uint32_t threads = 1;
    uint64_t stateCount = 0;
    uint64_t outputBytes = 0;
    std::filesystem::path outputPath;
    std::filesystem::path statsPath;
    std::vector<std::string> parallelStages;
    std::array<uint64_t, 4> outcomeCounts{};
    std::array<uint64_t, 16> materialTargetCounts{};
    std::array<uint64_t, 16> cannonMaxCapturesCounts{};
    std::array<uint64_t, 256> distanceCounts{};
    uint8_t maxExactDistance = 0;
    uint64_t saturatedDistanceCount = 0;
    double stageMaterialSeconds = 0.0;
    double stageDistanceSeconds = 0.0;
    double totalSeconds = 0.0;
    uint64_t estimatedMemoryBytes = 0;
    uint64_t queuePeak = 0;
    uint64_t materialIterations = 0;
    uint64_t distanceIterations = 0;
};

struct MtdLayerSolveOptions {
    int soldierCount = -1;
    std::filesystem::path wdlDir;
    std::filesystem::path mtdDir;
    bool overwrite = false;
    bool writeStatsJson = true;
    uint32_t threads = 1;
};

struct MtdRangeSolveOptions {
    int startLayer = -1;
    int endLayer = -1;
    std::filesystem::path wdlDir;
    std::filesystem::path mtdDir;
    bool overwrite = false;
    bool resume = false;
    uint32_t threads = 1;
};

struct MtdRangeSolveResult {
    int startLayer = -1;
    int endLayer = -1;
    std::filesystem::path wdlDir;
    std::filesystem::path mtdDir;
    std::vector<MtdLayerSolveResult> layers;
    double totalSeconds = 0.0;
    uint64_t totalOutputBytes = 0;
};

struct MtdLayerVerifyOptions {
    int soldierCount = -1;
    std::filesystem::path wdlDir;
    std::filesystem::path mtdDir;
    uint64_t sampleLimit = 10000;
    uint32_t threads = 1;
};

struct MtdLayerVerifyResult {
    int soldierCount = -1;
    uint32_t threads = 1;
    uint64_t stateCount = 0;
    uint64_t sampledStates = 0;
    uint64_t checkedTransitions = 0;
    uint64_t materialTargetKDistanceZero = 0;
    uint8_t maxExactDistance = 0;
    uint64_t saturatedDistanceCount = 0;
};

struct MtdMoveInfo {
    Move move;
    Position successor;
    int successorSoldierCount = 0;
    uint64_t successorIndex = 0;
    Outcome successorOutcome = Outcome::Unknown;
    MtdEntry successorMtd;
    bool wdlPreserving = false;
    bool materialOptimal = false;
    bool distanceOptimal = false;
};

struct MtdQueryOptions {
    std::filesystem::path mtdDir;
    std::filesystem::path wdlDir;
    Position position;
    bool includeMoves = false;
};

struct MtdQueryResult {
    Position position;
    int soldierCount = 0;
    uint64_t denseIndex = 0;
    Outcome outcome = Outcome::Unknown;
    MtdEntry mtd;
    int cannonMaxCaptures = 0;
    int soldierSaved = 0;
    std::vector<MtdMoveInfo> moves;
};

constexpr uint8_t MtdSaturatedDistance = 255;

uint16_t encodeMtdEntry(MtdEntry entry);
MtdEntry decodeMtdEntry(uint16_t encoded);
uint8_t saturatedAdd1(uint8_t distance);
uint64_t mtdPayloadBytes(uint64_t stateCount);
int firstMtdDrawMaterialThreshold();
uint64_t mtdDrawMaterialThresholdRounds(int soldierCount);
MtdEntry mtdEntryFromWorkArrays(
    const std::vector<uint8_t>& material,
    const MtdDistanceWork& distance,
    uint64_t index);
MtdWdlLayerScanSummary scanSolvedWdlLayer(
    const PackedOutcomeTable2Bit& table,
    int soldierCount,
    const char* label,
    uint32_t threads);

std::filesystem::path mtdLayerPath(const std::filesystem::path& dir, int soldierCount);
std::filesystem::path mtdLayerStatsPath(const std::filesystem::path& dir, int soldierCount);

void saveMtdTable(
    const PackedMtdTable12& table,
    int soldierCount,
    const std::filesystem::path& path,
    uint64_t rulesetHash);
MtdLayerWriteStats writeMtdTableFromArrays(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint8_t>& material,
    const MtdDistanceWork& distance,
    uint64_t rulesetHash);
PackedMtdTable12 loadMtdTable(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount);
MtdFileInfo inspectMtdFile(const std::filesystem::path& path);
MtdFileInfo validateMtdHeaderOnly(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount = -1);
void validateMtdPayload(const PackedMtdTable12& table, int soldierCount);
MtdFileInfo validateMtdFileFull(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount = -1);
MtdFileInfo validateMtdFile(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount = -1);
MtdInspectStats inspectMtdTable(const std::filesystem::path& path, uint32_t threads = 1);
MtdEntry lookupMtdEntryAt(
    const std::filesystem::path& mtdDir,
    int soldierCount,
    uint64_t denseIndex);
MtdEntry lookupMtdEntryForPosition(
    const std::filesystem::path& mtdDir,
    const Position& position);

MtdLayerSolveResult solveMtdLayer(const MtdLayerSolveOptions& options);
MtdRangeSolveResult solveMtdRange(const MtdRangeSolveOptions& options);
MtdLayerVerifyResult verifyMtdLayer(const MtdLayerVerifyOptions& options);
MtdQueryResult queryMtd(const MtdQueryOptions& options);

const char* mtdEncodingToString(MtdEncoding encoding);
std::string mtdDistanceToString(uint8_t distance);

}  // namespace sanpao15
