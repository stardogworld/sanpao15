#include "sanpao15/lowk_tablebase.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cctype>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

bool bitIsSet(uint32_t mask, int square) {
    return (mask & (uint32_t{1} << square)) != 0;
}

uint32_t bitForSquare(int square) {
    return uint32_t{1} << square;
}

template <typename Fn>
void forEachSetSquare(uint32_t mask, Fn&& fn) {
    mask &= BoardMask;
    while (mask != 0) {
        const int square = static_cast<int>(std::countr_zero(mask));
        fn(square);
        mask &= mask - 1u;
    }
}

template <typename Fn>
void forEachOrthogonalNeighbor(int square, Fn&& fn) {
    const int row = square / BoardSize;
    const int col = square % BoardSize;
    if (row > 0) {
        fn(square - BoardSize);
    }
    if (row + 1 < BoardSize) {
        fn(square + BoardSize);
    }
    if (col > 0) {
        fn(square - 1);
    }
    if (col + 1 < BoardSize) {
        fn(square + 1);
    }
}

template <typename Fn>
void forEachCannonJump(int square, Fn&& fn) {
    const int row = square / BoardSize;
    const int col = square % BoardSize;
    const auto addJump = [&](int dRow, int dCol) {
        const int overRow = row + dRow;
        const int overCol = col + dCol;
        const int landingRow = row + 2 * dRow;
        const int landingCol = col + 2 * dCol;
        if (overRow < 0 || overRow >= BoardSize || overCol < 0 || overCol >= BoardSize) {
            return;
        }
        if (landingRow < 0 || landingRow >= BoardSize || landingCol < 0 || landingCol >= BoardSize) {
            return;
        }
        fn(overRow * BoardSize + overCol, landingRow * BoardSize + landingCol);
    };

    addJump(-1, 0);
    addJump(1, 0);
    addJump(0, -1);
    addJump(0, 1);
}

void requireLowK(int maxK) {
    if (maxK < 0 || maxK > 3) {
        throw std::invalid_argument("low-k tablebase prototype supports K in 0..3");
    }
}

void requireStreamingLowK(int maxK) {
    if (maxK < 0 || maxK > 4) {
        throw std::invalid_argument("streaming low-k tablebase prototype supports K in 0..4");
    }
}

void requireLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 3) {
        throw std::invalid_argument("low-k dense layer solver supports soldier count in 0..3");
    }
}

void requireStreamingLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 4) {
        throw std::invalid_argument("streaming dense layer solver supports soldier count in 0..4");
    }
}

void requireDenseProductionLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("production dense layer solver supports soldier count in 0..15");
    }
    if (denseStateCount(soldierCount) > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::overflow_error("production dense layer solver requires uint32-addressable layers");
    }
}

bool isWinForSide(Outcome outcome, Side side) {
    return outcome == winFor(side);
}

bool isOpponentWinForSide(Outcome outcome, Side side) {
    return outcome == opponentWinFor(side);
}

struct SolveGraph {
    std::vector<std::vector<uint32_t>> predecessors;
    std::vector<uint32_t> remainingNonOpponentWin;
};

uint64_t remainingArrayBytes(uint64_t stateCount) {
    return stateCount * static_cast<uint64_t>(sizeof(uint8_t));
}

void validateLayerSolveInputs(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    const PackedOutcomeTable2Bit& output) {
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (output.size() != stateCount) {
        throw std::invalid_argument("output table size does not match dense layer");
    }
    if (soldierCount > 0 && lowerLayer == nullptr) {
        throw std::invalid_argument("lower-layer table is required for soldierCount > 0");
    }
    if (soldierCount == 0 && lowerLayer != nullptr) {
        throw std::invalid_argument("lower-layer table must be null for soldierCount == 0");
    }
}

void validateProductionLayerSolveInputs(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    const PackedOutcomeTable2Bit& output) {
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (output.size() != stateCount) {
        throw std::invalid_argument("output table size does not match dense production layer");
    }
    if (soldierCount >= MinSoldiersForSoldierSurvival && lowerLayer == nullptr) {
        throw std::invalid_argument("lower-layer table is required for production layers with soldierCount >= 4");
    }
    if (soldierCount < MinSoldiersForSoldierSurvival && lowerLayer != nullptr) {
        throw std::invalid_argument("lower-layer table must be omitted for material-rule production layers");
    }
}

void countOutcome(DenseLayerSolveResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

void addOutcomeCount(DenseLayerSolveResult& result, Outcome outcome, uint64_t count) {
    switch (outcome) {
        case Outcome::Unknown:
            result.unknown += count;
            break;
        case Outcome::CannonWin:
            result.cannonWin += count;
            break;
        case Outcome::SoldierWin:
            result.soldierWin += count;
            break;
        case Outcome::Draw:
            result.draw += count;
            break;
    }
}

void countVerifyOutcome(LowKTablebaseVerifyLayerResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

void countVerifyOutcome(DenseLayerVerifyResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

void noteQueueSize(DenseLayerSolveResult& result, uint64_t queueSize) {
    result.queuePeak = std::max(result.queuePeak, queueSize);
}

bool solveForcedMaterialLayer(
    int soldierCount,
    PackedOutcomeTable2Bit& output,
    DenseLayerSolveResult& result,
    uint64_t estimatedMemoryBytes) {
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(soldierCount);
    if (!material.has_value()) {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < output.size(); ++index) {
        output.set(index, *material);
    }
    const auto finish = std::chrono::steady_clock::now();

    result.terminalStates = result.stateCount;
    result.resolvedByTerminal = result.stateCount;
    addOutcomeCount(result, *material, result.stateCount);
    result.unknown = 0;
    result.estimatedMemoryBytes = estimatedMemoryBytes;
    result.initializationSeconds = std::chrono::duration<double>(finish - start).count();
    result.seconds = result.initializationSeconds;
    return true;
}

LowKTablebaseLayerResult saveSolvedLayer(
    const DenseLayerSolveResult& solve,
    const PackedOutcomeTable2Bit& table,
    const std::filesystem::path& outputDir,
    DenseResultEncoding encoding) {
    const std::filesystem::path outputPath = lowKLayerResultPath(outputDir, solve.soldierCount);
    if (encoding == DenseResultEncoding::Packed2Bit) {
        saveDenseResultTable2Bit(table, solve.soldierCount, outputPath, StandardRulesetHash);
    } else {
        DenseOutcomeTable byteTable(table.size());
        for (uint64_t index = 0; index < table.size(); ++index) {
            byteTable.set(index, table.get(index));
        }
        saveDenseResultTable(byteTable, solve.soldierCount, outputPath, StandardRulesetHash);
    }

    (void)validateDenseResultFile(outputPath, StandardRulesetHash, solve.soldierCount);
    return LowKTablebaseLayerResult{
        solve,
        outputPath,
        std::filesystem::file_size(outputPath),
        encoding,
    };
}

PackedOutcomeTable2Bit loadDenseResultAnyEncoding(
    const std::filesystem::path& path,
    int soldierCount) {
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, soldierCount);
    if (info.encoding == DenseResultEncoding::Packed2Bit) {
        return loadDenseResultTable2Bit(path, StandardRulesetHash, soldierCount);
    }

    const DenseOutcomeTable byteTable = loadDenseResultTableByte(path, StandardRulesetHash, soldierCount);
    PackedOutcomeTable2Bit packed(byteTable.size());
    for (uint64_t index = 0; index < byteTable.size(); ++index) {
        packed.set(index, byteTable.get(index));
    }
    return packed;
}

std::string denseEncodingName(DenseResultEncoding encoding) {
    switch (encoding) {
        case DenseResultEncoding::Byte:
            return "byte";
        case DenseResultEncoding::Packed2Bit:
            return "2bit";
    }
    throw std::invalid_argument("invalid dense result encoding");
}

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string rulesetHashHex() {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << StandardRulesetHash;
    return out.str();
}

std::filesystem::path statsJsonPathForResult(const std::filesystem::path& resultPath) {
    std::filesystem::path stats = resultPath;
    stats.replace_extension(".solve.json");
    return stats;
}

std::filesystem::path tempResultPath(const std::filesystem::path& resultPath) {
    std::filesystem::path temp = resultPath;
    temp += ".tmp";
    return temp;
}

std::filesystem::path rangeManifestPath(const std::filesystem::path& outputDir) {
    return outputDir / "manifest.json";
}

std::filesystem::path rangeLayerResultPath(const std::filesystem::path& outputDir, int soldierCount) {
    return lowKLayerResultPath(outputDir, soldierCount);
}

std::filesystem::path rangeLayerStatsPath(const std::filesystem::path& outputDir, int soldierCount) {
    return statsJsonPathForResult(rangeLayerResultPath(outputDir, soldierCount));
}

void validateOutputTarget(
    const std::filesystem::path& outputPath,
    const std::filesystem::path& statsPath,
    bool overwrite,
    bool writeStatsJson) {
    if (outputPath.empty()) {
        throw std::invalid_argument("--out-res is required for production dense layer solve");
    }
    if (std::filesystem::exists(outputPath) && !overwrite) {
        throw std::runtime_error("output .s15res already exists; pass overwrite to replace it: " + outputPath.string());
    }
    if (writeStatsJson && std::filesystem::exists(statsPath) && !overwrite) {
        throw std::runtime_error("stats .solve.json already exists; pass overwrite to replace it: " + statsPath.string());
    }
}

void requireDenseRangeLayer(int layer) {
    if (layer < 0 || layer > 15) {
        throw std::invalid_argument("dense layer range bounds must be in 0..15");
    }
}

void validateRangeOptions(const DenseLayerRangeSolveOptions& options) {
    requireDenseRangeLayer(options.startLayer);
    requireDenseRangeLayer(options.endLayer);
    if (options.startLayer > options.endLayer) {
        throw std::invalid_argument("--solve-layer-range START must be less than or equal to END");
    }
    if (options.outputDir.empty()) {
        throw std::invalid_argument("--solve-layer-range requires --out-dir DIR");
    }
    if (options.resume && options.overwrite) {
        throw std::invalid_argument("--resume and --overwrite cannot be used together");
    }
}

void validateLowerResultFile(const std::filesystem::path& path, int expectedSoldierCount) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("lower .s15res file does not exist: " + path.string());
    }
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, expectedSoldierCount);
    if (info.soldierCount != expectedSoldierCount) {
        throw std::runtime_error("--lower-res must contain soldierCount K-1");
    }
    if (info.stateCount != denseStateCount(expectedSoldierCount)) {
        throw std::runtime_error("--lower-res state count does not match denseStateCount(K-1)");
    }
    (void)denseEncodingName(info.encoding);
}

uint64_t parseJsonUnsignedField(const std::string& text, const std::string& field, uint64_t fallback) {
    const std::string key = "\"" + field + "\"";
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return fallback;
    }
    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    size_t pos = colon + 1;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    size_t end = pos;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    if (end == pos) {
        return fallback;
    }
    return static_cast<uint64_t>(std::stoull(text.substr(pos, end - pos)));
}

double parseJsonDoubleField(const std::string& text, const std::string& field, double fallback) {
    const std::string key = "\"" + field + "\"";
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return fallback;
    }
    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    size_t pos = colon + 1;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    size_t end = pos;
    while (end < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.' ||
            text[end] == '-' || text[end] == '+' || text[end] == 'e' || text[end] == 'E')) {
        ++end;
    }
    if (end == pos) {
        return fallback;
    }
    return std::stod(text.substr(pos, end - pos));
}

void fillRangeEntryFromStatsJson(DenseLayerRangeEntry& entry) {
    if (!std::filesystem::exists(entry.statsPath)) {
        entry.statsPathMissing = true;
        return;
    }
    std::ifstream input(entry.statsPath);
    if (!input) {
        entry.statsPathMissing = true;
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    entry.cannonWin = parseJsonUnsignedField(text, "cannonWin", entry.cannonWin);
    entry.soldierWin = parseJsonUnsignedField(text, "soldierWin", entry.soldierWin);
    entry.draw = parseJsonUnsignedField(text, "draw", entry.draw);
    entry.unknown = parseJsonUnsignedField(text, "unknown", entry.unknown);
    entry.totalSeconds = parseJsonDoubleField(text, "totalSeconds", entry.totalSeconds);
    entry.outputBytes = parseJsonUnsignedField(text, "outputBytes", entry.outputBytes);
}

struct PreflightStatsSnapshot {
    bool present = false;
    uint64_t stateCount = 0;
    uint64_t queuePeak = 0;
    uint64_t estimatedMemoryBytes = 0;
    double totalSeconds = 0.0;
};

PreflightStatsSnapshot readPreflightStatsSnapshot(
    const std::filesystem::path& path,
    uint64_t fallbackStateCount) {
    PreflightStatsSnapshot stats;
    if (!std::filesystem::exists(path)) {
        return stats;
    }
    stats.present = true;
    std::ifstream input(path);
    if (!input) {
        return stats;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    stats.stateCount = parseJsonUnsignedField(text, "stateCount", 0);
    if (stats.stateCount == 0) {
        stats.stateCount = parseJsonUnsignedField(text, "states", fallbackStateCount);
    }
    stats.queuePeak = parseJsonUnsignedField(text, "queuePeak", 0);
    stats.estimatedMemoryBytes = parseJsonUnsignedField(text, "estimatedMemoryBytes", 0);
    stats.totalSeconds = parseJsonDoubleField(text, "totalSeconds", 0.0);
    return stats;
}

struct PreflightEstimator {
    double safetyQueueRatio = 0.50;
    double secondsPerState = 0.0;
};

double knownDenseSecondsPerState() {
    const double k4 = 68.8957879 / 33649000.0;
    const double k5 = 268.8983137 / 121136400.0;
    const double k6 = 568.0760603 / 343219800.0;
    return std::max({k4, k5, k6});
}

PreflightEstimator buildPreflightEstimator(const std::filesystem::path& outputDir) {
    double observedQueueRatio = 0.40;
    double secondsPerState = knownDenseSecondsPerState();

    for (int k = 0; k <= 15; ++k) {
        const std::filesystem::path resultPath = rangeLayerResultPath(outputDir, k);
        const std::filesystem::path statsPath = rangeLayerStatsPath(outputDir, k);
        try {
            if (!std::filesystem::exists(resultPath)) {
                continue;
            }
            (void)validateDenseResultFile(resultPath, StandardRulesetHash, k);
            const PreflightStatsSnapshot stats = readPreflightStatsSnapshot(statsPath, denseStateCount(k));
            if (stats.present && stats.stateCount != 0) {
                if (stats.queuePeak != 0) {
                    observedQueueRatio =
                        std::max(observedQueueRatio, static_cast<double>(stats.queuePeak) / static_cast<double>(stats.stateCount));
                }
                if (stats.totalSeconds > 0.0) {
                    secondsPerState =
                        std::max(secondsPerState, stats.totalSeconds / static_cast<double>(stats.stateCount));
                }
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    PreflightEstimator estimator;
    estimator.safetyQueueRatio = std::max(observedQueueRatio * 1.25, 0.40);
    estimator.secondsPerState = secondsPerState;
    return estimator;
}

void validatePreflightOptions(const DenseLayerPreflightOptions& options) {
    requireDenseRangeLayer(options.startLayer);
    requireDenseRangeLayer(options.endLayer);
    if (options.startLayer > options.endLayer) {
        throw std::invalid_argument("--preflight-layer-range START must be less than or equal to END");
    }
    if (options.outputDir.empty()) {
        throw std::invalid_argument("--preflight-layer-range requires --out-dir DIR");
    }
}

uint64_t ceilMultiply(uint64_t value, double multiplier) {
    const long double scaled = static_cast<long double>(value) * static_cast<long double>(multiplier);
    if (scaled > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
        throw std::overflow_error("preflight estimate exceeds uint64_t");
    }
    return static_cast<uint64_t>(std::ceil(scaled));
}

uint64_t ceilRatio(uint64_t value, uint64_t numerator, uint64_t denominator) {
    if (denominator == 0) {
        throw std::invalid_argument("preflight ratio denominator must be non-zero");
    }
    if (value > (std::numeric_limits<uint64_t>::max() - denominator + 1u) / numerator) {
        return ceilMultiply(value, static_cast<double>(numerator) / static_cast<double>(denominator));
    }
    return (value * numerator + denominator - 1u) / denominator;
}

std::string riskForRecommendedMemory(uint64_t recommendedMemoryBytes) {
    constexpr uint64_t GiB = 1024ull * 1024ull * 1024ull;
    if (recommendedMemoryBytes >= 64ull * GiB) {
        return "high";
    }
    if (recommendedMemoryBytes >= 32ull * GiB) {
        return "medium";
    }
    return "low";
}

struct PreflightFileStatus {
    DenseLayerFileStatus status = DenseLayerFileStatus::Missing;
    DenseResultFileInfo info;
    std::string error;
};

PreflightFileStatus inspectPreflightResultFile(
    const std::filesystem::path& outputDir,
    int soldierCount) {
    PreflightFileStatus status;
    const std::filesystem::path path = rangeLayerResultPath(outputDir, soldierCount);
    if (!std::filesystem::exists(path)) {
        status.status = DenseLayerFileStatus::Missing;
        return status;
    }
    try {
        status.info = validateDenseResultFile(path, StandardRulesetHash, soldierCount);
        status.status = DenseLayerFileStatus::Valid;
    } catch (const std::exception& error) {
        status.status = DenseLayerFileStatus::Invalid;
        status.error = error.what();
    }
    return status;
}

std::string readTextIfExists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool manifestMentionsLayer(const std::string& manifest, int soldierCount) {
    if (manifest.empty()) {
        return false;
    }
    return manifest.find(rangeLayerResultPath({}, soldierCount).filename().generic_string()) != std::string::npos;
}

std::filesystem::path nearestExistingPathForSpace(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path probe = std::filesystem::absolute(path.empty() ? std::filesystem::path(".") : path, ec);
    if (ec) {
        probe = path.empty() ? std::filesystem::path(".") : path;
    }
    while (!probe.empty() && !std::filesystem::exists(probe, ec)) {
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    if (probe.empty()) {
        probe = ".";
    }
    return probe;
}

void fillPreflightDiskSpace(DenseLayerRangePreflightResult& result) {
    std::error_code ec;
    const std::filesystem::path probe = nearestExistingPathForSpace(result.outputDir);
    const std::filesystem::space_info space = std::filesystem::space(probe, ec);
    if (ec) {
        result.diskSpaceKnown = false;
        result.diskOk = false;
        result.availableDiskBytes = 0;
        return;
    }
    result.diskSpaceKnown = true;
    result.availableDiskBytes = space.available;
    result.diskOk = result.availableDiskBytes >= result.requiredAdditionalDiskBytes;
}

std::string statusAction(const DenseLayerPreflightEntry& entry) {
    if (entry.resultStatus == DenseLayerFileStatus::Valid) {
        return "skip";
    }
    if (entry.resultStatus == DenseLayerFileStatus::Missing) {
        return entry.lowerLayerAvailable || entry.soldierCount < MinSoldiersForSoldierSurvival ? "solve" : "blocked";
    }
    return "error";
}

void writePreflightJson(const DenseLayerRangePreflightResult& result) {
    const std::filesystem::path parent = result.jsonPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const std::filesystem::path tempPath = result.jsonPath.string() + ".tmp";
    std::ofstream out(tempPath);
    if (!out) {
        throw std::runtime_error("failed to open preflight JSON for writing: " + tempPath.string());
    }

    out << "{\n";
    out << "  \"format\": \"sanpao15-layer-range-preflight\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"rulesetName\": \"" << RulesetName << "\",\n";
    out << "  \"rulesetHash\": \"" << rulesetHashHex() << "\",\n";
    out << "  \"encoding\": \"" << denseEncodingName(result.encoding) << "\",\n";
    out << "  \"startLayer\": " << result.startLayer << ",\n";
    out << "  \"endLayer\": " << result.endLayer << ",\n";
    out << "  \"outputDir\": \"" << jsonEscape(result.outputDir.string()) << "\",\n";
    out << "  \"canResumeRange\": " << (result.canResumeRange ? "true" : "false") << ",\n";
    out << "  \"hasInvalidLayers\": " << (result.hasInvalidLayers ? "true" : "false") << ",\n";
    out << "  \"hasMissingLower\": " << (result.hasMissingLower ? "true" : "false") << ",\n";
    out << "  \"disk\": {\n";
    out << "    \"spaceKnown\": " << (result.diskSpaceKnown ? "true" : "false") << ",\n";
    out << "    \"availableBytes\": " << result.availableDiskBytes << ",\n";
    out << "    \"requiredAdditionalBytes\": " << result.requiredAdditionalDiskBytes << ",\n";
    out << "    \"ok\": " << (result.diskOk ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"totals\": {\n";
    out << "    \"totalStateCount\": " << result.totalStateCount << ",\n";
    out << "    \"totalSelectedOutputBytes\": " << result.totalSelectedOutputBytes << ",\n";
    out << "    \"existingValidOutputBytes\": " << result.existingValidOutputBytes << ",\n";
    out << "    \"missingOutputBytes\": " << result.missingOutputBytes << ",\n";
    out << "    \"peakEstimatedCoreMemoryBytes\": " << result.peakEstimatedCoreMemoryBytes << ",\n";
    out << "    \"peakRecommendedMemoryBytes\": " << result.peakRecommendedMemoryBytes << ",\n";
    out << "    \"peakMemoryLayer\": " << result.peakMemoryLayer << ",\n";
    out << "    \"estimatedTotalSeconds\": " << std::setprecision(12) << result.estimatedTotalSeconds << ",\n";
    out << "    \"estimatedRemainingSeconds\": " << std::setprecision(12) << result.estimatedRemainingSeconds << "\n";
    out << "  },\n";
    out << "  \"layers\": [\n";
    for (size_t i = 0; i < result.layers.size(); ++i) {
        const DenseLayerPreflightEntry& layer = result.layers[i];
        out << "    {\n";
        out << "      \"soldierCount\": " << layer.soldierCount << ",\n";
        out << "      \"stateCount\": " << layer.stateCount << ",\n";
        out << "      \"outputBytes2Bit\": " << layer.outputBytes2Bit << ",\n";
        out << "      \"outputBytesByte\": " << layer.outputBytesByte << ",\n";
        out << "      \"selectedOutputBytes\": " << layer.selectedOutputBytes << ",\n";
        out << "      \"remainingBytes\": " << layer.remainingBytes << ",\n";
        out << "      \"lowerLayerPayloadBytes\": " << layer.lowerLayerPayloadBytes << ",\n";
        out << "      \"estimatedQueueBytes\": " << layer.estimatedQueueBytes << ",\n";
        out << "      \"estimatedCoreMemoryBytes\": " << layer.estimatedCoreMemoryBytes << ",\n";
        out << "      \"recommendedMemoryBytes\": " << layer.recommendedMemoryBytes << ",\n";
        out << "      \"estimatedSeconds\": " << std::setprecision(12) << layer.estimatedSeconds << ",\n";
        out << "      \"resultStatus\": \"" << denseLayerFileStatusToString(layer.resultStatus) << "\",\n";
        out << "      \"action\": \"" << statusAction(layer) << "\",\n";
        out << "      \"statsJsonPresent\": " << (layer.statsJsonPresent ? "true" : "false") << ",\n";
        out << "      \"manifestEntryPresent\": " << (layer.manifestEntryPresent ? "true" : "false") << ",\n";
        out << "      \"wouldSkipWithResume\": " << (layer.wouldSkipWithResume ? "true" : "false") << ",\n";
        out << "      \"wouldSolve\": " << (layer.wouldSolve ? "true" : "false") << ",\n";
        out << "      \"lowerLayerAvailable\": " << (layer.lowerLayerAvailable ? "true" : "false") << ",\n";
        out << "      \"risk\": \"" << jsonEscape(layer.risk) << "\"";
        if (layer.error.has_value()) {
            out << ",\n";
            out << "      \"error\": \"" << jsonEscape(*layer.error) << "\"\n";
        } else {
            out << "\n";
        }
        out << "    }" << (i + 1 == result.layers.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    if (!out) {
        throw std::runtime_error("failed to write preflight JSON: " + tempPath.string());
    }
    out.close();
    if (!out) {
        throw std::runtime_error("failed to close preflight JSON: " + tempPath.string());
    }
    if (std::filesystem::exists(result.jsonPath)) {
        std::filesystem::remove(result.jsonPath);
    }
    std::filesystem::rename(tempPath, result.jsonPath);
}

std::string manifestRelativePath(const std::filesystem::path& outputDir, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(path, outputDir, ec);
    if (ec) {
        relative = path.filename();
    }
    return relative.generic_string();
}

void writeRangeManifest(const DenseLayerRangeSolveResult& result, DenseResultEncoding encoding) {
    const std::filesystem::path parent = result.manifestPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const std::filesystem::path tempPath = result.manifestPath.string() + ".tmp";
    std::ofstream out(tempPath);
    if (!out) {
        throw std::runtime_error("failed to open range manifest for writing: " + tempPath.string());
    }
    out << "{\n";
    out << "  \"format\": \"sanpao15-layer-range-manifest\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"rulesetName\": \"" << RulesetName << "\",\n";
    out << "  \"rulesetHash\": \"" << rulesetHashHex() << "\",\n";
    out << "  \"encoding\": \"" << denseEncodingName(encoding) << "\",\n";
    out << "  \"startLayer\": " << result.startLayer << ",\n";
    out << "  \"endLayer\": " << result.endLayer << ",\n";
    out << "  \"layers\": [\n";
    for (size_t i = 0; i < result.layers.size(); ++i) {
        const DenseLayerRangeEntry& layer = result.layers[i];
        out << "    {\n";
        out << "      \"soldierCount\": " << layer.soldierCount << ",\n";
        out << "      \"status\": \"" << jsonEscape(layer.status) << "\",\n";
        out << "      \"resultPath\": \"" << jsonEscape(manifestRelativePath(result.outputDir, layer.resultPath)) << "\",\n";
        out << "      \"statsPath\": \"" << jsonEscape(manifestRelativePath(result.outputDir, layer.statsPath)) << "\",\n";
        out << "      \"statsPathMissing\": " << (layer.statsPathMissing ? "true" : "false") << ",\n";
        out << "      \"stateCount\": " << layer.stateCount << ",\n";
        out << "      \"outputBytes\": " << layer.outputBytes << ",\n";
        out << "      \"totalSeconds\": " << std::setprecision(12) << layer.totalSeconds << ",\n";
        out << "      \"cannonWin\": " << layer.cannonWin << ",\n";
        out << "      \"soldierWin\": " << layer.soldierWin << ",\n";
        out << "      \"draw\": " << layer.draw << ",\n";
        out << "      \"unknown\": " << layer.unknown;
        if (!layer.error.empty()) {
            out << ",\n";
            out << "      \"error\": \"" << jsonEscape(layer.error) << "\"\n";
        } else {
            out << "\n";
        }
        out << "    }" << (i + 1 == result.layers.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"totalSeconds\": " << std::setprecision(12) << result.totalSeconds << ",\n";
    out << "  \"totalOutputBytes\": " << result.totalOutputBytes << "\n";
    out << "}\n";
    if (!out) {
        throw std::runtime_error("failed to write range manifest: " + tempPath.string());
    }
    out.close();
    if (!out) {
        throw std::runtime_error("failed to close range manifest: " + tempPath.string());
    }
    if (std::filesystem::exists(result.manifestPath)) {
        std::filesystem::remove(result.manifestPath);
    }
    std::filesystem::rename(tempPath, result.manifestPath);
}

void saveDenseResultAnyEncoding(
    const PackedOutcomeTable2Bit& table,
    int soldierCount,
    const std::filesystem::path& path,
    DenseResultEncoding encoding) {
    if (encoding == DenseResultEncoding::Packed2Bit) {
        saveDenseResultTable2Bit(table, soldierCount, path, StandardRulesetHash);
        return;
    }

    DenseOutcomeTable byteTable(table.size());
    for (uint64_t index = 0; index < table.size(); ++index) {
        byteTable.set(index, table.getUnchecked(index));
    }
    saveDenseResultTable(byteTable, soldierCount, path, StandardRulesetHash);
}

void writeLayerSolveStatsJson(
    const DenseLayerProductionSolveOptions& options,
    const DenseLayerProductionSolveResult& result) {
    const std::filesystem::path parent = result.statsJsonPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const std::filesystem::path tempPath = result.statsJsonPath.string() + ".tmp";
    std::ofstream out(tempPath);
    if (!out) {
        throw std::runtime_error("failed to open stats .solve.json for writing: " + tempPath.string());
    }
    const DenseLayerSolveResult& solve = result.solve;
    out << "{\n";
    out << "  \"format\": \"sanpao15-layer-solve-stats\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"rulesetName\": \"" << RulesetName << "\",\n";
    out << "  \"rulesetHash\": \"" << rulesetHashHex() << "\",\n";
    out << "  \"soldierCount\": " << solve.soldierCount << ",\n";
    out << "  \"stateCount\": " << solve.stateCount << ",\n";
    out << "  \"encoding\": \"" << denseEncodingName(result.encoding) << "\",\n";
    out << "  \"outputResultPath\": \"" << jsonEscape(result.outputResultPath.string()) << "\",\n";
    if (options.lowerResultPath.has_value()) {
        out << "  \"lowerResultPath\": \"" << jsonEscape(options.lowerResultPath->string()) << "\",\n";
    } else {
        out << "  \"lowerResultPath\": null,\n";
    }
    out << "  \"states\": " << solve.stateCount << ",\n";
    out << "  \"cannonWin\": " << solve.cannonWin << ",\n";
    out << "  \"soldierWin\": " << solve.soldierWin << ",\n";
    out << "  \"draw\": " << solve.draw << ",\n";
    out << "  \"unknown\": " << solve.unknown << ",\n";
    out << "  \"terminalStates\": " << solve.terminalStates << ",\n";
    out << "  \"sameLayerEdges\": " << solve.sameLayerEdges << ",\n";
    out << "  \"captureEdges\": " << solve.captureEdges << ",\n";
    out << "  \"retrogradeResolved\": " << solve.retrogradeResolved << ",\n";
    out << "  \"unresolvedAsDraw\": " << solve.unresolvedAsDraw << ",\n";
    out << "  \"resolvedByTerminal\": " << solve.resolvedByTerminal << ",\n";
    out << "  \"resolvedByLowerLayer\": " << solve.resolvedByLowerLayer << ",\n";
    out << "  \"resolvedByPropagation\": " << solve.resolvedByPropagation << ",\n";
    out << "  \"drawAfterQueue\": " << solve.drawAfterQueue << ",\n";
    out << "  \"maxRemaining\": " << solve.maxRemaining << ",\n";
    out << "  \"maxSuccessors\": " << solve.maxSuccessors << ",\n";
    out << "  \"queuePeak\": " << solve.queuePeak << ",\n";
    out << "  \"predecessorCalls\": " << solve.predecessorCalls << ",\n";
    out << "  \"generatedPredecessors\": " << solve.generatedPredecessors << ",\n";
    out << "  \"maxPredecessors\": " << solve.maxPredecessors << ",\n";
    out << "  \"initializationSeconds\": " << std::setprecision(12) << solve.initializationSeconds << ",\n";
    out << "  \"propagationSeconds\": " << std::setprecision(12) << solve.propagationSeconds << ",\n";
    out << "  \"finalizeSeconds\": " << std::setprecision(12) << solve.finalizeSeconds << ",\n";
    out << "  \"totalSeconds\": " << std::setprecision(12) << solve.seconds << ",\n";
    out << "  \"outputBytes\": " << result.outputBytes << ",\n";
    out << "  \"estimatedMemoryBytes\": " << solve.estimatedMemoryBytes << "\n";
    out << "}\n";
    if (!out) {
        throw std::runtime_error("failed to write stats .solve.json: " + tempPath.string());
    }
    out.close();
    if (!out) {
        throw std::runtime_error("failed to close stats .solve.json: " + tempPath.string());
    }
    if (std::filesystem::exists(result.statsJsonPath)) {
        std::filesystem::remove(result.statsJsonPath);
    }
    std::filesystem::rename(tempPath, result.statsJsonPath);
}

Outcome successorOutcome(
    const DenseSuccessor& successor,
    const PackedOutcomeTable2Bit& sameLayer,
    const PackedOutcomeTable2Bit* lowerLayer) {
    if (successor.kind == DenseSuccessorKind::SameLayer) {
        return sameLayer.get(successor.toIndex);
    }
    if (lowerLayer == nullptr) {
        throw std::logic_error("capture successor requires lower-layer outcome table");
    }
    return lowerLayer->get(successor.toIndex);
}

void verifySolvedState(
    int soldierCount,
    uint64_t index,
    const PackedOutcomeTable2Bit& table,
    const PackedOutcomeTable2Bit* lowerLayer) {
    const Position pos = positionFromDenseIndex(soldierCount, index);
    const Outcome outcome = table.get(index);
    if (outcome == Outcome::Unknown) {
        throw std::runtime_error("low-k verifier found Unknown outcome");
    }

    const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
    if (terminal.terminal) {
        if (outcome != terminal.outcome) {
            throw std::runtime_error("low-k verifier found terminal outcome mismatch");
        }
        return;
    }

    const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
    if (successors.empty()) {
        if (outcome != opponentWinFor(pos.side)) {
            throw std::runtime_error("low-k verifier found no-move non-terminal mismatch");
        }
        return;
    }

    bool hasCurrentSideWin = false;
    bool hasDraw = false;
    for (const DenseSuccessor& successor : successors) {
        const Outcome child = successorOutcome(successor, table, lowerLayer);
        if (child == Outcome::Unknown) {
            throw std::runtime_error("low-k verifier found Unknown successor outcome");
        }
        if (isWinForSide(child, pos.side)) {
            hasCurrentSideWin = true;
        }
        if (child == Outcome::Draw) {
            hasDraw = true;
        }
    }

    if (isWinForSide(outcome, pos.side) && !hasCurrentSideWin) {
        throw std::runtime_error("low-k verifier found side-win state without winning successor");
    }
    if (isOpponentWinForSide(outcome, pos.side) && (hasCurrentSideWin || hasDraw)) {
        throw std::runtime_error("low-k verifier found opponent-win state with winning successor");
    }
    if (outcome == Outcome::Draw && (hasCurrentSideWin || !hasDraw)) {
        throw std::runtime_error("low-k verifier found invalid draw state");
    }
}

}  // namespace

uint8_t checkedStreamingRemainingCount(uint64_t remainingCount) {
    if (remainingCount > std::numeric_limits<uint8_t>::max()) {
        throw std::overflow_error("streaming remaining counter exceeds uint8_t");
    }
    return static_cast<uint8_t>(remainingCount);
}

DenseStreamingInitScan scanDenseStateForStreamingInitialization(
    int soldierCount,
    uint64_t index,
    const Position& pos,
    const PackedOutcomeTable2Bit* lowerLayer) {
    DenseStreamingInitScan scan;
    const std::optional<Outcome> material = forcedOutcomeByMaterialRule(soldierCount);
    if (material.has_value()) {
        scan.terminal = DenseTerminalInfo{true, *material};
        return scan;
    }
    if (soldierCount > 0 && lowerLayer == nullptr) {
        throw std::invalid_argument("lower-layer table is required for streaming initialization scan");
    }
    if (popcount25(pos.cannons) != 3 || popcount25(pos.soldiers) != soldierCount ||
        (pos.cannons & pos.soldiers) != 0) {
        throw std::invalid_argument("position does not match streaming initialization layer");
    }

    const uint32_t occupied = pos.cannons | pos.soldiers;
    bool cannonCanMove = false;
    if (pos.side == Side::Cannon) {
        forEachSetSquare(pos.cannons, [&](int from) {
            forEachOrthogonalNeighbor(from, [&](int to) {
                if (bitIsSet(occupied, to)) {
                    return;
                }
                cannonCanMove = true;
                ++scan.successorCount;
                ++scan.sameLayerEdges;
                if (!scan.resolved) {
                    ++scan.remainingCount;
                }
            });
            forEachCannonJump(from, [&](int over, int landing) {
                if (bitIsSet(occupied, over) || !bitIsSet(pos.soldiers, landing)) {
                    return;
                }
                cannonCanMove = true;
                ++scan.successorCount;
                ++scan.captureEdges;

                Position next = pos;
                next.cannons = (next.cannons & ~bitForSquare(from)) | bitForSquare(landing);
                next.soldiers &= ~bitForSquare(landing);
                next.side = Side::Soldier;
                const uint64_t toIndex = denseIndex(next);
                const Outcome child = lowerLayer->getUnchecked(toIndex);
                if (isWinForSide(child, pos.side) && !scan.resolved) {
                    scan.resolved = true;
                    scan.resolvedOutcome = child;
                } else if (child == Outcome::Draw && !scan.resolved) {
                    ++scan.remainingCount;
                }
            });
        });
        if (!cannonCanMove) {
            scan.terminal = DenseTerminalInfo{true, Outcome::SoldierWin};
        }
        return scan;
    }

    forEachSetSquare(pos.cannons, [&](int from) {
        if (cannonCanMove) {
            return;
        }
        forEachOrthogonalNeighbor(from, [&](int to) {
            if (!cannonCanMove && !bitIsSet(occupied, to)) {
                cannonCanMove = true;
            }
        });
        forEachCannonJump(from, [&](int over, int landing) {
            if (!cannonCanMove && !bitIsSet(occupied, over) && bitIsSet(pos.soldiers, landing)) {
                cannonCanMove = true;
            }
        });
    });
    if (!cannonCanMove) {
        scan.terminal = DenseTerminalInfo{true, Outcome::SoldierWin};
        return scan;
    }

    forEachSetSquare(pos.soldiers, [&](int from) {
        forEachOrthogonalNeighbor(from, [&](int to) {
            if (bitIsSet(occupied, to)) {
                return;
            }
            ++scan.successorCount;
            ++scan.sameLayerEdges;
            if (!scan.resolved) {
                ++scan.remainingCount;
            }
        });
    });
    if (scan.successorCount == 0) {
        scan.terminal = DenseTerminalInfo{true, Outcome::CannonWin};
    }
    (void)index;
    return scan;
}

DenseLayerSolveResult solveDenseLayerOutcome(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output) {
    requireLayer(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    validateLayerSolveInputs(soldierCount, lowerLayer, output);
    resetOutcomeTable(output);
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::overflow_error("low-k prototype predecessor graph requires 32-bit state ids");
    }

    const auto start = std::chrono::steady_clock::now();
    DenseLayerSolveResult result;
    result.soldierCount = soldierCount;
    result.stateCount = stateCount;
    if (solveForcedMaterialLayer(
            soldierCount,
            output,
            result,
            output.bytes() + stateCount * static_cast<uint64_t>(sizeof(uint32_t)))) {
        return result;
    }

    SolveGraph graph;
    graph.predecessors.resize(static_cast<size_t>(stateCount));
    graph.remainingNonOpponentWin.assign(static_cast<size_t>(stateCount), 0);

    std::deque<uint32_t> queue;

    for (uint64_t index = 0; index < stateCount; ++index) {
        const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
        if (terminal.terminal) {
            output.set(index, terminal.outcome);
            ++result.terminalStates;
            queue.push_back(static_cast<uint32_t>(index));
            ++result.resolvedByTerminal;
            noteQueueSize(result, queue.size());
            continue;
        }

        const Position pos = positionFromDenseIndex(soldierCount, index);
        const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
        bool resolved = false;
        uint32_t remaining = 0;
        for (const DenseSuccessor& successor : successors) {
            if (successor.kind == DenseSuccessorKind::SameLayer) {
                ++result.sameLayerEdges;
                graph.predecessors[static_cast<size_t>(successor.toIndex)].push_back(static_cast<uint32_t>(index));
                if (!resolved) {
                    ++remaining;
                    result.maxRemaining = std::max<uint64_t>(result.maxRemaining, remaining);
                }
                continue;
            }

            ++result.captureEdges;
            const Outcome child = lowerLayer->get(successor.toIndex);
            if (isWinForSide(child, pos.side) && !resolved) {
                output.set(index, child);
                queue.push_back(static_cast<uint32_t>(index));
                ++result.resolvedByLowerLayer;
                noteQueueSize(result, queue.size());
                resolved = true;
            }
            if (child == Outcome::Draw && !resolved) {
                ++remaining;
                result.maxRemaining = std::max<uint64_t>(result.maxRemaining, remaining);
            }
        }

        if (!resolved) {
            graph.remainingNonOpponentWin[static_cast<size_t>(index)] = remaining;
            if (remaining == 0) {
                output.set(index, opponentWinFor(pos.side));
                queue.push_back(static_cast<uint32_t>(index));
                ++result.resolvedByLowerLayer;
                noteQueueSize(result, queue.size());
            }
        }
    }

    while (!queue.empty()) {
        const uint32_t childIndex = queue.front();
        queue.pop_front();
        const Outcome childOutcome = output.get(childIndex);
        if (childOutcome == Outcome::Unknown || childOutcome == Outcome::Draw) {
            continue;
        }

        for (uint32_t parentIndex : graph.predecessors[childIndex]) {
            if (output.get(parentIndex) != Outcome::Unknown) {
                continue;
            }
            const Position parent = positionFromDenseIndex(soldierCount, parentIndex);
            if (isWinForSide(childOutcome, parent.side)) {
                output.set(parentIndex, childOutcome);
                ++result.retrogradeResolved;
                ++result.resolvedByPropagation;
                queue.push_back(parentIndex);
                noteQueueSize(result, queue.size());
                continue;
            }
            if (isOpponentWinForSide(childOutcome, parent.side)) {
                uint32_t& remaining = graph.remainingNonOpponentWin[parentIndex];
                if (remaining == 0) {
                    throw std::logic_error("remainingNonOpponentWin underflow");
                }
                --remaining;
                if (remaining == 0) {
                    output.set(parentIndex, opponentWinFor(parent.side));
                    ++result.retrogradeResolved;
                    ++result.resolvedByPropagation;
                    queue.push_back(parentIndex);
                    noteQueueSize(result, queue.size());
                }
            }
        }
    }

    for (uint64_t index = 0; index < stateCount; ++index) {
        Outcome outcome = output.get(index);
        if (outcome == Outcome::Unknown) {
            output.set(index, Outcome::Draw);
            outcome = Outcome::Draw;
            ++result.unresolvedAsDraw;
            ++result.drawAfterQueue;
        }
        countOutcome(result, outcome);
    }

    result.unknown = 0;
    result.estimatedMemoryBytes =
        output.bytes() + stateCount * static_cast<uint64_t>(sizeof(uint32_t)) +
        stateCount * static_cast<uint64_t>(sizeof(std::vector<uint32_t>));
    const auto finish = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(finish - start).count();
    return result;
}

DenseLayerSolveResult solveDenseLayerOutcomeStreaming(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output) {
    requireStreamingLayer(soldierCount);
    validateLayerSolveInputs(soldierCount, lowerLayer, output);
    resetOutcomeTable(output);
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("streaming low-k prototype requires size_t-addressable layers");
    }

    const auto start = std::chrono::steady_clock::now();
    DenseLayerSolveResult result;
    result.soldierCount = soldierCount;
    result.stateCount = stateCount;
    if (solveForcedMaterialLayer(
            soldierCount,
            output,
            result,
            output.bytes() + remainingArrayBytes(stateCount))) {
        return result;
    }

    std::vector<uint8_t> remaining(static_cast<size_t>(stateCount), 0);
    std::vector<uint64_t> queue;
    size_t queueHead = 0;
    std::vector<uint64_t> predecessorIndices;
    predecessorIndices.reserve(32);

    const auto initStart = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < stateCount; ++index) {
        const Position pos = positionFromDenseIndex(soldierCount, index);
        const DenseStreamingInitScan scan =
            scanDenseStateForStreamingInitialization(soldierCount, index, pos, lowerLayer);
        if (scan.terminal.terminal) {
            output.setUnchecked(index, scan.terminal.outcome);
            ++result.terminalStates;
            ++result.resolvedByTerminal;
            queue.push_back(index);
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
            continue;
        }

        result.maxSuccessors = std::max(result.maxSuccessors, scan.successorCount);
        result.sameLayerEdges += scan.sameLayerEdges;
        result.captureEdges += scan.captureEdges;
        if (scan.resolved) {
            output.setUnchecked(index, scan.resolvedOutcome);
            ++result.resolvedByLowerLayer;
            queue.push_back(index);
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
            continue;
        }

        remaining[static_cast<size_t>(index)] = checkedStreamingRemainingCount(scan.remainingCount);
        result.maxRemaining = std::max(result.maxRemaining, scan.remainingCount);
        if (scan.remainingCount == 0) {
            output.setUnchecked(index, opponentWinFor(pos.side));
                ++result.resolvedByLowerLayer;
                queue.push_back(index);
                noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
        }
    }
    const auto initFinish = std::chrono::steady_clock::now();
    result.initializationSeconds = std::chrono::duration<double>(initFinish - initStart).count();

    const auto propagationStart = std::chrono::steady_clock::now();
    if (result.terminalStates == stateCount) {
        queue.clear();
        queueHead = 0;
    }
    while (queueHead < queue.size()) {
        const uint64_t childIndex = queue[queueHead++];
        const Outcome childOutcome = output.getUnchecked(childIndex);
        if (childOutcome == Outcome::Unknown || childOutcome == Outcome::Draw) {
            continue;
        }

        const Position child = positionFromDenseIndex(soldierCount, childIndex);
        const Side parentSide = opposite(child.side);
        generateDensePredecessorIndicesFromPosition(
            soldierCount,
            childIndex,
            child,
            predecessorIndices);
        ++result.predecessorCalls;
        result.generatedPredecessors += static_cast<uint64_t>(predecessorIndices.size());
        result.maxPredecessors = std::max<uint64_t>(result.maxPredecessors, predecessorIndices.size());

        for (uint64_t parentIndex : predecessorIndices) {
            if (output.getUnchecked(parentIndex) != Outcome::Unknown) {
                continue;
            }
            if (isWinForSide(childOutcome, parentSide)) {
                output.setUnchecked(parentIndex, childOutcome);
                ++result.retrogradeResolved;
                ++result.resolvedByPropagation;
                queue.push_back(parentIndex);
                noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
                continue;
            }
            if (isOpponentWinForSide(childOutcome, parentSide)) {
                uint8_t& remainingCount = remaining[static_cast<size_t>(parentIndex)];
                if (remainingCount == 0) {
                    throw std::logic_error("streaming remaining counter underflow");
                }
                --remainingCount;
                if (remainingCount == 0) {
                    output.setUnchecked(parentIndex, opponentWinFor(parentSide));
                    ++result.retrogradeResolved;
                    ++result.resolvedByPropagation;
                    queue.push_back(parentIndex);
                    noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
                }
            }
        }
    }
    const auto propagationFinish = std::chrono::steady_clock::now();
    result.propagationSeconds = std::chrono::duration<double>(propagationFinish - propagationStart).count();

    const auto finalizeStart = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < stateCount; ++index) {
        Outcome outcome = output.getUnchecked(index);
        if (outcome == Outcome::Unknown) {
            output.setUnchecked(index, Outcome::Draw);
            outcome = Outcome::Draw;
            ++result.unresolvedAsDraw;
            ++result.drawAfterQueue;
        }
        countOutcome(result, outcome);
    }
    result.unknown = 0;
    result.estimatedMemoryBytes = output.bytes() + remainingArrayBytes(stateCount);
    const auto finalizeFinish = std::chrono::steady_clock::now();
    result.finalizeSeconds = std::chrono::duration<double>(finalizeFinish - finalizeStart).count();
    result.seconds = std::chrono::duration<double>(finalizeFinish - start).count();
    return result;
}

DenseLayerSolveResult solveDenseLayerOutcomeStreamingProduction(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output) {
    requireDenseProductionLayer(soldierCount);
    validateProductionLayerSolveInputs(soldierCount, lowerLayer, output);
    resetOutcomeTable(output);
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("production dense layer solver requires size_t-addressable layers");
    }

    const auto start = std::chrono::steady_clock::now();
    DenseLayerSolveResult result;
    result.soldierCount = soldierCount;
    result.stateCount = stateCount;
    if (solveForcedMaterialLayer(
            soldierCount,
            output,
            result,
            output.bytes() + remainingArrayBytes(stateCount))) {
        return result;
    }

    std::vector<uint8_t> remaining(static_cast<size_t>(stateCount), 0);
    std::vector<uint32_t> queue;
    size_t queueHead = 0;
    std::vector<uint32_t> predecessorIndices;
    predecessorIndices.reserve(32);

    const auto initStart = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < stateCount; ++index) {
        const Position pos = positionFromDenseIndex(soldierCount, index);
        const DenseStreamingInitScan scan =
            scanDenseStateForStreamingInitialization(soldierCount, index, pos, lowerLayer);
        if (scan.terminal.terminal) {
            output.setUnchecked(index, scan.terminal.outcome);
            ++result.terminalStates;
            ++result.resolvedByTerminal;
            queue.push_back(checkedDenseIndex32(index));
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
            continue;
        }

        result.maxSuccessors = std::max(result.maxSuccessors, scan.successorCount);
        result.sameLayerEdges += scan.sameLayerEdges;
        result.captureEdges += scan.captureEdges;
        if (scan.resolved) {
            output.setUnchecked(index, scan.resolvedOutcome);
            ++result.resolvedByLowerLayer;
            queue.push_back(checkedDenseIndex32(index));
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
            continue;
        }

        remaining[static_cast<size_t>(index)] = checkedStreamingRemainingCount(scan.remainingCount);
        result.maxRemaining = std::max(result.maxRemaining, scan.remainingCount);
        if (scan.remainingCount == 0) {
            output.setUnchecked(index, opponentWinFor(pos.side));
            ++result.resolvedByLowerLayer;
            queue.push_back(checkedDenseIndex32(index));
            noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
        }
    }
    const auto initFinish = std::chrono::steady_clock::now();
    result.initializationSeconds = std::chrono::duration<double>(initFinish - initStart).count();

    const auto propagationStart = std::chrono::steady_clock::now();
    if (result.terminalStates == stateCount) {
        queue.clear();
        queueHead = 0;
    }
    while (queueHead < queue.size()) {
        const uint32_t childIndex = queue[queueHead++];
        const Outcome childOutcome = output.getUnchecked(childIndex);
        if (childOutcome == Outcome::Unknown || childOutcome == Outcome::Draw) {
            continue;
        }

        const Position child = positionFromDenseIndex(soldierCount, childIndex);
        const Side parentSide = opposite(child.side);
        generateDensePredecessorIndicesFromPosition(
            soldierCount,
            childIndex,
            child,
            predecessorIndices);
        ++result.predecessorCalls;
        result.generatedPredecessors += static_cast<uint64_t>(predecessorIndices.size());
        result.maxPredecessors = std::max<uint64_t>(result.maxPredecessors, predecessorIndices.size());

        for (uint32_t parentIndex : predecessorIndices) {
            if (output.getUnchecked(parentIndex) != Outcome::Unknown) {
                continue;
            }
            if (isWinForSide(childOutcome, parentSide)) {
                output.setUnchecked(parentIndex, childOutcome);
                ++result.retrogradeResolved;
                ++result.resolvedByPropagation;
                queue.push_back(parentIndex);
                noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
                continue;
            }
            if (isOpponentWinForSide(childOutcome, parentSide)) {
                uint8_t& remainingCount = remaining[static_cast<size_t>(parentIndex)];
                if (remainingCount == 0) {
                    throw std::logic_error("streaming remaining counter underflow");
                }
                --remainingCount;
                if (remainingCount == 0) {
                    output.setUnchecked(parentIndex, opponentWinFor(parentSide));
                    ++result.retrogradeResolved;
                    ++result.resolvedByPropagation;
                    queue.push_back(parentIndex);
                    noteQueueSize(result, static_cast<uint64_t>(queue.size() - queueHead));
                }
            }
        }
    }
    const auto propagationFinish = std::chrono::steady_clock::now();
    result.propagationSeconds = std::chrono::duration<double>(propagationFinish - propagationStart).count();

    const auto finalizeStart = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < stateCount; ++index) {
        Outcome outcome = output.getUnchecked(index);
        if (outcome == Outcome::Unknown) {
            output.setUnchecked(index, Outcome::Draw);
            outcome = Outcome::Draw;
            ++result.unresolvedAsDraw;
            ++result.drawAfterQueue;
        }
        countOutcome(result, outcome);
    }
    result.unknown = 0;
    result.estimatedMemoryBytes =
        output.bytes() + remainingArrayBytes(stateCount) +
        result.queuePeak * static_cast<uint64_t>(sizeof(uint32_t)) +
        32u * static_cast<uint64_t>(sizeof(uint32_t));
    const auto finalizeFinish = std::chrono::steady_clock::now();
    result.finalizeSeconds = std::chrono::duration<double>(finalizeFinish - finalizeStart).count();
    result.seconds = std::chrono::duration<double>(finalizeFinish - start).count();
    return result;
}

std::filesystem::path lowKLayerResultPath(const std::filesystem::path& dir, int soldierCount) {
    std::ostringstream name;
    name << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15res";
    return dir / name.str();
}

std::vector<LowKTablebaseLayerResult> solveLowKTablebase(const LowKTablebaseSolveOptions& options) {
    requireLowK(options.maxK);
    if (options.outputDir.empty()) {
        throw std::invalid_argument("low-k output directory is required");
    }
    std::filesystem::create_directories(options.outputDir);

    std::vector<LowKTablebaseLayerResult> results;
    std::vector<PackedOutcomeTable2Bit> solvedLayers;
    solvedLayers.reserve(static_cast<size_t>(options.maxK + 1));

    for (int k = 0; k <= options.maxK; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solvedLayers[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult solve = solveDenseLayerOutcome(k, lower, table);

        results.push_back(saveSolvedLayer(solve, table, options.outputDir, options.encoding));
        solvedLayers.push_back(std::move(table));
    }
    return results;
}

std::vector<LowKTablebaseLayerResult> solveLowKTablebaseStreaming(const LowKTablebaseSolveOptions& options) {
    requireStreamingLowK(options.maxK);
    if (options.outputDir.empty()) {
        throw std::invalid_argument("streaming low-k output directory is required");
    }
    std::filesystem::create_directories(options.outputDir);

    std::vector<LowKTablebaseLayerResult> results;
    std::vector<PackedOutcomeTable2Bit> solvedLayers;
    solvedLayers.reserve(static_cast<size_t>(options.maxK + 1));

    for (int k = 0; k <= options.maxK; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solvedLayers[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult solve = solveDenseLayerOutcomeStreaming(k, lower, table);

        results.push_back(saveSolvedLayer(solve, table, options.outputDir, options.encoding));
        solvedLayers.push_back(std::move(table));
    }
    return results;
}

DenseLayerProductionSolveResult solveDenseLayerProduction(
    const DenseLayerProductionSolveOptions& options) {
    requireDenseProductionLayer(options.soldierCount);
    if (options.outputResultPath.empty()) {
        throw std::invalid_argument("--out-res is required for production dense layer solve");
    }
    const std::filesystem::path statsPath = statsJsonPathForResult(options.outputResultPath);
    validateOutputTarget(
        options.outputResultPath,
        statsPath,
        options.overwrite,
        options.writeStatsJson);

    std::optional<PackedOutcomeTable2Bit> lowerStorage;
    if (options.soldierCount < MinSoldiersForSoldierSurvival) {
        if (options.lowerResultPath.has_value()) {
            throw std::invalid_argument("--lower-res must be omitted for material-rule layers K=0..3");
        }
    } else {
        if (!options.lowerResultPath.has_value()) {
            throw std::invalid_argument("--lower-res is required for --solve-layer K when K >= 4");
        }
        validateLowerResultFile(*options.lowerResultPath, options.soldierCount - 1);
        lowerStorage = loadDenseResultAnyEncoding(*options.lowerResultPath, options.soldierCount - 1);
    }

    const std::filesystem::path parent = options.outputResultPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    if (options.writeStatsJson) {
        const std::filesystem::path statsParent = statsPath.parent_path();
        if (!statsParent.empty()) {
            std::filesystem::create_directories(statsParent);
        }
    }

    const PackedOutcomeTable2Bit* lower = lowerStorage.has_value() ? &*lowerStorage : nullptr;
    PackedOutcomeTable2Bit table(denseStateCount(options.soldierCount));
    const DenseLayerSolveResult solve =
        solveDenseLayerOutcomeStreamingProduction(options.soldierCount, lower, table);

    const std::filesystem::path tmpPath = tempResultPath(options.outputResultPath);
    if (std::filesystem::exists(tmpPath)) {
        std::filesystem::remove(tmpPath);
    }
    saveDenseResultAnyEncoding(table, options.soldierCount, tmpPath, options.encoding);
    (void)validateDenseResultFile(tmpPath, StandardRulesetHash, options.soldierCount);

    if (std::filesystem::exists(options.outputResultPath)) {
        std::filesystem::remove(options.outputResultPath);
    }
    std::filesystem::rename(tmpPath, options.outputResultPath);

    DenseLayerProductionSolveResult result;
    result.solve = solve;
    result.outputResultPath = options.outputResultPath;
    result.statsJsonPath = statsPath;
    result.outputBytes = std::filesystem::file_size(options.outputResultPath);
    result.encoding = options.encoding;
    if (options.writeStatsJson) {
        writeLayerSolveStatsJson(options, result);
    }
    return result;
}

DenseLayerRangeSolveResult solveDenseLayerRange(
    const DenseLayerRangeSolveOptions& options) {
    validateRangeOptions(options);
    std::filesystem::create_directories(options.outputDir);

    DenseLayerRangeSolveResult result;
    result.startLayer = options.startLayer;
    result.endLayer = options.endLayer;
    result.outputDir = options.outputDir;
    result.manifestPath = rangeManifestPath(options.outputDir);

    if (options.cleanTemp) {
        for (int k = options.startLayer; k <= options.endLayer; ++k) {
            const std::filesystem::path tmp = tempResultPath(rangeLayerResultPath(options.outputDir, k));
            if (std::filesystem::exists(tmp)) {
                std::filesystem::remove(tmp);
            }
        }
    } else {
        for (int k = options.startLayer; k <= options.endLayer; ++k) {
            const std::filesystem::path tmp = tempResultPath(rangeLayerResultPath(options.outputDir, k));
            if (std::filesystem::exists(tmp)) {
                throw std::runtime_error("stale temporary .s15res exists; pass --clean-temp or remove it: " + tmp.string());
            }
        }
    }

    const auto rangeStart = std::chrono::steady_clock::now();
    if (options.startLayer >= MinSoldiersForSoldierSurvival) {
        validateLowerResultFile(rangeLayerResultPath(options.outputDir, options.startLayer - 1), options.startLayer - 1);
    }

    for (int k = options.startLayer; k <= options.endLayer; ++k) {
        DenseLayerRangeEntry entry;
        entry.soldierCount = k;
        entry.resultPath = rangeLayerResultPath(options.outputDir, k);
        entry.statsPath = rangeLayerStatsPath(options.outputDir, k);
        entry.stateCount = denseStateCount(k);

        try {
            if (options.resume && std::filesystem::exists(entry.resultPath)) {
                const DenseResultFileInfo info = validateDenseResultFile(entry.resultPath, StandardRulesetHash, k);
                entry.status = "skipped";
                entry.stateCount = info.stateCount;
                entry.outputBytes = std::filesystem::file_size(entry.resultPath);
                fillRangeEntryFromStatsJson(entry);
                result.totalOutputBytes += entry.outputBytes;
                result.layers.push_back(entry);
                const auto now = std::chrono::steady_clock::now();
                result.totalSeconds = std::chrono::duration<double>(now - rangeStart).count();
                writeRangeManifest(result, options.encoding);
                continue;
            }

            if (std::filesystem::exists(entry.resultPath) && !options.overwrite) {
                throw std::runtime_error("range output layer already exists; pass --resume or --overwrite: " + entry.resultPath.string());
            }

            std::optional<std::filesystem::path> lowerPath;
            if (k >= MinSoldiersForSoldierSurvival) {
                lowerPath = rangeLayerResultPath(options.outputDir, k - 1);
                validateLowerResultFile(*lowerPath, k - 1);
            }

            DenseLayerProductionSolveOptions layerOptions;
            layerOptions.soldierCount = k;
            layerOptions.lowerResultPath = lowerPath;
            layerOptions.outputResultPath = entry.resultPath;
            layerOptions.encoding = options.encoding;
            layerOptions.overwrite = options.overwrite;
            layerOptions.writeStatsJson = true;
            const DenseLayerProductionSolveResult solved = solveDenseLayerProduction(layerOptions);

            entry.status = "completed";
            entry.stateCount = solved.solve.stateCount;
            entry.outputBytes = solved.outputBytes;
            entry.totalSeconds = solved.solve.seconds;
            entry.cannonWin = solved.solve.cannonWin;
            entry.soldierWin = solved.solve.soldierWin;
            entry.draw = solved.solve.draw;
            entry.unknown = solved.solve.unknown;
            result.totalOutputBytes += entry.outputBytes;
            result.layers.push_back(entry);
            const auto now = std::chrono::steady_clock::now();
            result.totalSeconds = std::chrono::duration<double>(now - rangeStart).count();
            writeRangeManifest(result, options.encoding);
        } catch (const std::exception& error) {
            entry.status = "failed";
            entry.error = error.what();
            if (std::filesystem::exists(entry.resultPath)) {
                std::error_code ec;
                entry.outputBytes = std::filesystem::file_size(entry.resultPath, ec);
            }
            result.layers.push_back(entry);
            const auto failedFinish = std::chrono::steady_clock::now();
            result.totalSeconds = std::chrono::duration<double>(failedFinish - rangeStart).count();
            writeRangeManifest(result, options.encoding);
            throw;
        }
    }

    const auto rangeFinish = std::chrono::steady_clock::now();
    result.totalSeconds = std::chrono::duration<double>(rangeFinish - rangeStart).count();
    writeRangeManifest(result, options.encoding);
    return result;
}

const char* denseLayerFileStatusToString(DenseLayerFileStatus status) {
    switch (status) {
        case DenseLayerFileStatus::Missing:
            return "missing";
        case DenseLayerFileStatus::Valid:
            return "valid";
        case DenseLayerFileStatus::Invalid:
            return "invalid";
    }
    return "unknown";
}

DenseLayerRangePreflightResult preflightDenseLayerRange(
    const DenseLayerPreflightOptions& options) {
    validatePreflightOptions(options);

    DenseLayerRangePreflightResult result;
    result.startLayer = options.startLayer;
    result.endLayer = options.endLayer;
    result.encoding = options.encoding;
    result.outputDir = options.outputDir;
    result.jsonPath = options.outputJsonPath.value_or(options.outputDir / "preflight.json");

    const PreflightEstimator estimator = buildPreflightEstimator(options.outputDir);
    const std::string manifestText = readTextIfExists(rangeManifestPath(options.outputDir));

    std::array<PreflightFileStatus, 16> statuses{};
    std::array<bool, 16> availableAfterPlan{};
    for (int k = 0; k <= 15; ++k) {
        statuses[static_cast<size_t>(k)] = inspectPreflightResultFile(options.outputDir, k);
        availableAfterPlan[static_cast<size_t>(k)] =
            statuses[static_cast<size_t>(k)].status == DenseLayerFileStatus::Valid;
    }

    uint64_t largestMissingLayerOutputBytes = 0;
    for (int k = options.startLayer; k <= options.endLayer; ++k) {
        const size_t index = static_cast<size_t>(k);
        const uint64_t stateCount = denseStateCount(k);
        const uint64_t selectedOutputBytes = denseResultPayloadBytes(stateCount, options.encoding);

        DenseLayerPreflightEntry entry;
        entry.soldierCount = k;
        entry.stateCount = stateCount;
        entry.outputBytes2Bit = denseResultPayloadBytes(stateCount, DenseResultEncoding::Packed2Bit);
        entry.outputBytesByte = denseResultPayloadBytes(stateCount, DenseResultEncoding::Byte);
        entry.selectedOutputBytes = selectedOutputBytes;
        entry.remainingBytes = stateCount;
        entry.resultStatus = statuses[index].status;
        entry.statsJsonPresent = readPreflightStatsSnapshot(rangeLayerStatsPath(options.outputDir, k), stateCount).present;
        entry.manifestEntryPresent = manifestMentionsLayer(manifestText, k);

        if (k >= MinSoldiersForSoldierSurvival) {
            const size_t lowerIndex = static_cast<size_t>(k - 1);
            if (statuses[lowerIndex].status == DenseLayerFileStatus::Valid) {
                entry.lowerLayerPayloadBytes = statuses[lowerIndex].info.payloadBytes;
            } else {
                entry.lowerLayerPayloadBytes = denseResultPayloadBytes(denseStateCount(k - 1), options.encoding);
            }
            entry.lowerLayerAvailable = availableAfterPlan[lowerIndex];
        } else {
            entry.lowerLayerPayloadBytes = 0;
            entry.lowerLayerAvailable = true;
        }

        if (entry.resultStatus == DenseLayerFileStatus::Valid) {
            entry.wouldSkipWithResume = true;
        } else if (entry.resultStatus == DenseLayerFileStatus::Missing) {
            entry.wouldSolve = true;
        } else {
            result.hasInvalidLayers = true;
            entry.error = statuses[index].error.empty() ? "invalid existing result file" : statuses[index].error;
        }

        if (k >= MinSoldiersForSoldierSurvival && !entry.lowerLayerAvailable) {
            result.hasMissingLower = true;
            if (statuses[static_cast<size_t>(k - 1)].status == DenseLayerFileStatus::Invalid) {
                result.hasInvalidLayers = true;
            }
            if (!entry.error.has_value()) {
                std::ostringstream message;
                message << "missing required lower layer-" << std::setw(2) << std::setfill('0') << (k - 1)
                        << ".s15res";
                entry.error = message.str();
            }
        }

        const uint64_t estimatedQueueEntries = ceilMultiply(stateCount, estimator.safetyQueueRatio);
        if (estimatedQueueEntries > std::numeric_limits<uint64_t>::max() / sizeof(uint32_t)) {
            throw std::overflow_error("preflight queue byte estimate exceeds uint64_t");
        }
        entry.estimatedQueueBytes = estimatedQueueEntries * static_cast<uint64_t>(sizeof(uint32_t));
        entry.estimatedCoreMemoryBytes =
            entry.selectedOutputBytes +
            entry.remainingBytes +
            entry.lowerLayerPayloadBytes +
            entry.estimatedQueueBytes;
        entry.recommendedMemoryBytes = ceilRatio(entry.estimatedCoreMemoryBytes, 3, 2);
        entry.estimatedSeconds =
            static_cast<double>(stateCount) * estimator.secondsPerState * 1.25;
        entry.risk = riskForRecommendedMemory(entry.recommendedMemoryBytes);

        result.totalStateCount += entry.stateCount;
        result.totalSelectedOutputBytes += entry.selectedOutputBytes;
        result.estimatedTotalSeconds += entry.estimatedSeconds;
        if (entry.resultStatus == DenseLayerFileStatus::Valid) {
            result.existingValidOutputBytes += entry.selectedOutputBytes;
        } else if (entry.resultStatus == DenseLayerFileStatus::Missing) {
            result.missingOutputBytes += entry.selectedOutputBytes;
            largestMissingLayerOutputBytes = std::max(largestMissingLayerOutputBytes, entry.selectedOutputBytes);
            result.estimatedRemainingSeconds += entry.estimatedSeconds;
        }
        if (entry.estimatedCoreMemoryBytes > result.peakEstimatedCoreMemoryBytes) {
            result.peakEstimatedCoreMemoryBytes = entry.estimatedCoreMemoryBytes;
            result.peakRecommendedMemoryBytes = entry.recommendedMemoryBytes;
            result.peakMemoryLayer = k;
        }

        availableAfterPlan[index] =
            entry.resultStatus == DenseLayerFileStatus::Valid ||
            (entry.resultStatus == DenseLayerFileStatus::Missing && entry.wouldSolve && entry.lowerLayerAvailable);

        result.layers.push_back(entry);
    }

    const uint64_t diskWithWriteSlack = ceilMultiply(result.missingOutputBytes, 1.2);
    if (diskWithWriteSlack > std::numeric_limits<uint64_t>::max() - largestMissingLayerOutputBytes) {
        throw std::overflow_error("preflight disk estimate exceeds uint64_t");
    }
    result.requiredAdditionalDiskBytes = diskWithWriteSlack + largestMissingLayerOutputBytes;
    fillPreflightDiskSpace(result);
    result.canResumeRange =
        !result.hasInvalidLayers &&
        !result.hasMissingLower &&
        (!result.diskSpaceKnown || result.diskOk);

    writePreflightJson(result);
    return result;
}

LowKTablebaseVerifyResult verifyLowKTablebase(
    const std::filesystem::path& dir,
    int maxK,
    uint64_t sampleLimit) {
    requireStreamingLowK(maxK);
    LowKTablebaseVerifyResult result;
    result.inputDir = dir;
    result.maxK = maxK;
    result.sampleLimit = sampleLimit;

    std::vector<PackedOutcomeTable2Bit> tables;
    tables.reserve(static_cast<size_t>(maxK + 1));
    for (int k = 0; k <= maxK; ++k) {
        const std::filesystem::path path = lowKLayerResultPath(dir, k);
        const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, k);
        PackedOutcomeTable2Bit table = loadDenseResultAnyEncoding(path, k);

        LowKTablebaseVerifyLayerResult layer;
        layer.soldierCount = k;
        layer.stateCount = table.size();
        layer.encoding = info.encoding;

        for (uint64_t index = 0; index < table.size(); ++index) {
            countVerifyOutcome(layer, table.get(index));
        }

        if (soldiersAreBelowSurvivalLimit(k) && layer.cannonWin != layer.stateCount) {
            throw std::runtime_error("low-k verifier expected every material-rule layer state to be CannonWin");
        }
        if (layer.unknown != 0) {
            throw std::runtime_error("low-k verifier found Unknown outcomes");
        }

        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &tables[static_cast<size_t>(k - 1)];
        if (sampleLimit == 0 || sampleLimit >= table.size()) {
            layer.sampledStates = table.size();
            for (uint64_t index = 0; index < table.size(); ++index) {
                verifySolvedState(k, index, table, lower);
            }
        } else {
            layer.sampledStates = sampleLimit;
            uint64_t rng = 0x6C6F776B74626C75ull ^ table.size();
            for (uint64_t i = 0; i < sampleLimit; ++i) {
                rng = rng * 6364136223846793005ull + 1442695040888963407ull;
                verifySolvedState(k, rng % table.size(), table, lower);
            }
        }

        result.layers.push_back(layer);
        tables.push_back(std::move(table));
    }

    return result;
}

DenseLayerVerifyResult verifyDenseLayerResult(const DenseLayerVerifyOptions& options) {
    if (options.resultPath.empty()) {
        throw std::invalid_argument("--verify-layer requires a .s15res file path");
    }
    const DenseResultFileInfo info = validateDenseResultFile(options.resultPath, StandardRulesetHash);
    requireDenseProductionLayer(info.soldierCount);
    PackedOutcomeTable2Bit table = loadDenseResultAnyEncoding(options.resultPath, info.soldierCount);

    std::optional<PackedOutcomeTable2Bit> lowerStorage;
    if (info.soldierCount < MinSoldiersForSoldierSurvival) {
        if (options.lowerResultPath.has_value()) {
            throw std::invalid_argument("--lower-res must be omitted when verifying material-rule layers K=0..3");
        }
    } else {
        if (!options.lowerResultPath.has_value()) {
            throw std::invalid_argument("--lower-res is required for --verify-layer when K >= 4");
        }
        validateLowerResultFile(*options.lowerResultPath, info.soldierCount - 1);
        lowerStorage = loadDenseResultAnyEncoding(*options.lowerResultPath, info.soldierCount - 1);
    }

    DenseLayerVerifyResult result;
    result.soldierCount = info.soldierCount;
    result.stateCount = info.stateCount;
    result.encoding = info.encoding;
    for (uint64_t index = 0; index < table.size(); ++index) {
        countVerifyOutcome(result, table.getUnchecked(index));
    }
    if (soldiersAreBelowSurvivalLimit(info.soldierCount) && result.cannonWin != result.stateCount) {
        throw std::runtime_error("single-layer verifier expected every material-rule state to be CannonWin");
    }
    if (result.unknown != 0) {
        throw std::runtime_error("single-layer verifier found Unknown outcomes");
    }

    const PackedOutcomeTable2Bit* lower = lowerStorage.has_value() ? &*lowerStorage : nullptr;
    if (options.sampleLimit == 0 || options.sampleLimit >= table.size()) {
        result.sampledStates = table.size();
        for (uint64_t index = 0; index < table.size(); ++index) {
            verifySolvedState(info.soldierCount, index, table, lower);
        }
    } else {
        result.sampledStates = options.sampleLimit;
        uint64_t rng = 0x4C41594552564552ull ^ table.size();
        for (uint64_t i = 0; i < options.sampleLimit; ++i) {
            rng = rng * 6364136223846793005ull + 1442695040888963407ull;
            verifySolvedState(info.soldierCount, rng % table.size(), table, lower);
        }
    }

    return result;
}

}  // namespace sanpao15
