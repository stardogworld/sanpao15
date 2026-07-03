#include "sanpao15/local_backend.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>

#include "httplib.h"

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/material_target_distance.h"
#include "sanpao15/notation.h"
#include "sanpao15/ruleset.h"

namespace sanpao15 {

namespace {

constexpr std::streamoff HeaderBytes = 44;

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (unsigned char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
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
                if (ch < 0x20) {
                    std::ostringstream out;
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                    escaped += out.str();
                } else {
                    escaped.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return escaped;
}

std::string jsonString(const std::string& text) {
    return "\"" + jsonEscape(text) + "\"";
}

std::string jsonBool(bool value) {
    return value ? "true" : "false";
}

std::string jsonError(const std::string& code, const std::string& message) {
    std::ostringstream out;
    out << "{"
        << "\"ok\":false,"
        << "\"code\":" << jsonString(code) << ","
        << "\"error\":" << jsonString(message)
        << "}";
    return out.str();
}

std::string rulesetHashHex() {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << StandardRulesetHash;
    return out.str();
}

std::string denseEncodingName(DenseResultEncoding encoding) {
    switch (encoding) {
        case DenseResultEncoding::Byte:
            return "byte";
        case DenseResultEncoding::Packed2Bit:
            return "2bit";
    }
    return "unknown";
}

std::string outcomeText(Outcome outcome) {
    switch (outcome) {
        case Outcome::CannonWin:
            return "\\u70ae\\u80dc";
        case Outcome::SoldierWin:
            return "\\u5175\\u80dc";
        case Outcome::Draw:
            return "\\u548c\\u68cb";
        case Outcome::Unknown:
            return "\\u672a\\u77e5";
    }
    return "\\u672a\\u77e5";
}

std::string classificationText(const std::string& classification) {
    if (classification == "winning") {
        return "\\u4fdd\\u6301\\u80dc\\u52bf";
    }
    if (classification == "drawing") {
        return "\\u4fdd\\u6301\\u548c\\u68cb";
    }
    return "\\u8d70\\u5411\\u8d25\\u5c40";
}

std::string moveToString(const Move& move) {
    std::string text = std::to_string(move.from) + "->" + std::to_string(move.to);
    if (move.capture) {
        text += " captures ";
        text += std::to_string(move.capturedSquare);
    }
    return text;
}

std::string layerFileName(int soldierCount) {
    std::ostringstream out;
    out << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15res";
    return out.str();
}

std::string mtdLayerFileName(int soldierCount) {
    std::ostringstream out;
    out << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15mtd";
    return out.str();
}

std::string localMtdStoreModeToString(LocalMtdStoreMode mode) {
    switch (mode) {
        case LocalMtdStoreMode::Ram:
            return "ram";
        case LocalMtdStoreMode::Mmap:
            return "mmap";
    }
    return "mmap";
}

void requireBackendDenseLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

std::string pathString(const std::filesystem::path& path) {
    return path.generic_string();
}

void writeIntArray(std::ostream& out, const std::vector<int>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        out << values[i] << (i + 1 == values.size() ? "" : ",");
    }
}

void writeTablebaseInvalidLayers(std::ostream& out, const std::vector<LocalTablebaseInvalidLayer>& invalidLayers) {
    for (size_t i = 0; i < invalidLayers.size(); ++i) {
        const LocalTablebaseInvalidLayer& invalid = invalidLayers[i];
        out << "{"
            << "\"soldierCount\":" << invalid.soldierCount << ","
            << "\"path\":" << jsonString(pathString(invalid.path)) << ","
            << "\"error\":" << jsonString(invalid.error)
            << "}";
        if (i + 1 != invalidLayers.size()) {
            out << ",";
        }
    }
}

void writeMtdInvalidLayers(std::ostream& out, const std::vector<LocalMtdInvalidLayer>& invalidLayers) {
    for (size_t i = 0; i < invalidLayers.size(); ++i) {
        const LocalMtdInvalidLayer& invalid = invalidLayers[i];
        out << "{"
            << "\"soldierCount\":" << invalid.soldierCount << ","
            << "\"path\":" << jsonString(pathString(invalid.path)) << ","
            << "\"error\":" << jsonString(invalid.error)
            << "}";
        if (i + 1 != invalidLayers.size()) {
            out << ",";
        }
    }
}

bool directoryExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
}

std::filesystem::path parentPathOrCwd(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::filesystem::current_path();
    }
    if (path.has_parent_path()) {
        return path.parent_path();
    }
    return std::filesystem::current_path();
}

uint64_t fileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("failed to read file size: " + path.string());
    }
    return size;
}

int classificationRank(const std::string& classification) {
    if (classification == "winning") {
        return 0;
    }
    if (classification == "drawing") {
        return 1;
    }
    return 2;
}

int recommendationTier(Side sideToMove, Outcome currentOutcome, Outcome successorOutcome) {
    if (currentOutcome == winFor(sideToMove)) {
        if (successorOutcome == winFor(sideToMove)) {
            return 0;
        }
        if (successorOutcome == Outcome::Draw) {
            return 1;
        }
        return 2;
    }
    if (currentOutcome == Outcome::Draw) {
        if (successorOutcome == Outcome::Draw) {
            return 0;
        }
        if (successorOutcome == winFor(sideToMove)) {
            return 1;
        }
        return 2;
    }
    if (currentOutcome == opponentWinFor(sideToMove)) {
        if (successorOutcome == Outcome::Draw) {
            return 0;
        }
        if (successorOutcome == opponentWinFor(sideToMove)) {
            return 1;
        }
        return 2;
    }
    return classificationRank(successorOutcome == winFor(sideToMove) ? "winning" :
                              successorOutcome == Outcome::Draw ? "drawing" : "losing");
}

bool deterministicMoveLess(const DenseTablebaseMoveInfo& lhs, const DenseTablebaseMoveInfo& rhs) {
    const int lhsCaptureRank = lhs.move.capture ? 0 : 1;
    const int rhsCaptureRank = rhs.move.capture ? 0 : 1;
    return std::tie(lhs.move.from, lhs.move.to, lhsCaptureRank, lhs.move.capturedSquare,
                    lhs.successorSoldierCount, lhs.successorIndex) <
           std::tie(rhs.move.from, rhs.move.to, rhsCaptureRank, rhs.move.capturedSquare,
                    rhs.successorSoldierCount, rhs.successorIndex);
}

bool isBestRecommendedMove(
    const DenseTablebaseLookupResult& result,
    const DenseTablebaseMoveInfo& move) {
    if (result.moves.empty()) {
        return false;
    }
    const int tier = recommendationTier(result.position.side, result.outcome, move.successorOutcome);
    for (const DenseTablebaseMoveInfo& candidate : result.moves) {
        if (recommendationTier(result.position.side, result.outcome, candidate.successorOutcome) < tier) {
            return false;
        }
    }
    return true;
}

std::vector<const DenseTablebaseMoveInfo*> recommendedMoves(const DenseTablebaseLookupResult& result) {
    std::vector<const DenseTablebaseMoveInfo*> selected;
    for (const DenseTablebaseMoveInfo& move : result.moves) {
        if (isBestRecommendedMove(result, move)) {
            selected.push_back(&move);
        }
    }
    return selected;
}

bool sameRankScore(const MoveScore& lhs, const MoveScore& rhs) {
    return std::tie(lhs.wdlTier, lhs.primary, lhs.secondary, lhs.tertiary) ==
           std::tie(rhs.wdlTier, rhs.primary, rhs.secondary, rhs.tertiary);
}

std::optional<MtdEntry> queryMoveMtd(
    const MaterialTargetDistanceStore* mtdStore,
    const DenseTablebaseMoveInfo& move) {
    if (mtdStore == nullptr) {
        return std::nullopt;
    }
    return mtdStore->query(move.successorSoldierCount, move.successorIndex);
}

std::string mtdMeaning(Outcome outcome) {
    switch (outcome) {
        case Outcome::CannonWin:
            return "forced-cannon-win-distance";
        case Outcome::SoldierWin:
            return "forced-soldier-win-distance";
        case Outcome::Draw:
            return "draw-material-target-distance";
        case Outcome::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::string mtdText(MtdEntry entry, Outcome outcome, int soldierCount) {
    const int d = entry.guaranteeDistance;
    const int r = entry.materialTarget;
    const bool saturated = entry.guaranteeDistance == MtdSaturatedDistance;
    switch (outcome) {
        case Outcome::CannonWin:
            return saturated
                ? "炮方可保证胜利；距离已饱和为 >=255 ply；目标是吃到少于 4 兵。"
                : "炮方可保证 " + std::to_string(d) + " ply 内胜利；目标是吃到少于 4 兵。";
        case Outcome::SoldierWin:
            return saturated
                ? "兵方可保证围死炮；距离已饱和为 >=255 ply。"
                : "兵方可保证 " + std::to_string(d) + " ply 内围死炮。";
        case Outcome::Draw: {
            const std::string prefix =
                "兵方能保住 " + std::to_string(r) + " 兵；炮方最多还能吃 " +
                std::to_string(std::max(0, soldierCount - r)) + " 兵；";
            if (d == 0) {
                return prefix + "材料目标已达到。";
            }
            if (saturated) {
                return prefix + "达到材料目标的距离已饱和为 >=255 ply。";
            }
            return prefix + "达到材料目标的保证步数为 " + std::to_string(d) + " ply。";
        }
        case Outcome::Unknown:
            return "MTD 含义未知。";
    }
    return "MTD 含义未知。";
}

std::string mtdEntryJson(
    const std::optional<MtdEntry>& entry,
    Outcome outcome,
    int soldierCount,
    const std::string& missingReason) {
    if (!entry.has_value()) {
        return std::string("{\"available\":false,\"reason\":") + jsonString(missingReason) + "}";
    }
    const int materialTarget = entry->materialTarget;
    std::ostringstream out;
    out << "{"
        << "\"available\":true,"
        << "\"materialTarget\":" << materialTarget << ","
        << "\"guaranteeDistance\":" << static_cast<int>(entry->guaranteeDistance) << ","
        << "\"saturated\":" << jsonBool(entry->guaranteeDistance == MtdSaturatedDistance) << ","
        << "\"cannonMaxCaptures\":" << std::max(0, soldierCount - materialTarget) << ","
        << "\"soldierSaved\":" << materialTarget << ","
        << "\"meaning\":" << jsonString(mtdMeaning(outcome)) << ","
        << "\"text\":" << jsonString(mtdText(*entry, outcome, soldierCount))
        << "}";
    return out.str();
}

}  // namespace

MoveScore scoreMoveForRecommendation(
    Side mover,
    Outcome currentOutcome,
    const DenseTablebaseMoveInfo& move,
    const std::optional<MtdEntry>& successorMtd,
    bool useMtd) {
    MoveScore score;
    score.wdlTier = recommendationTier(mover, currentOutcome, move.successorOutcome);
    if (!useMtd || !successorMtd.has_value()) {
        score.usedMtd = false;
        score.description = "WDL-only ranking";
        return score;
    }

    const int64_t d = successorMtd->guaranteeDistance;
    const int64_t r = successorMtd->materialTarget;
    score.usedMtd = true;

    switch (move.successorOutcome) {
        case Outcome::CannonWin:
            if (mover == Side::Cannon) {
                score.primary = d;
                score.secondary = r;
                score.description = "保持炮胜；炮方优先更快获胜，其次吃到更少兵。";
            } else {
                score.primary = -d;
                score.secondary = -r;
                score.description = "败局中延缓炮方胜利；兵方优先拖更久，其次保更多兵。";
            }
            break;
        case Outcome::SoldierWin:
            if (mover == Side::Soldier) {
                score.primary = d;
                score.secondary = -r;
                score.description = "保持兵胜；兵方优先更快围死炮，其次保更多兵。";
            } else {
                score.primary = -d;
                score.secondary = r;
                score.description = "败局中延缓被围死；炮方优先拖更久，其次让最终剩余兵更少。";
            }
            break;
        case Outcome::Draw:
            if (mover == Side::Cannon) {
                score.primary = r;
                score.secondary = d;
                score.description = "保持和棋；炮方优先降低最终剩余兵数，其次更快达到材料目标。";
            } else {
                score.primary = -r;
                score.secondary = -d;
                score.description = "保持和棋；兵方优先保住更多兵，其次尽量延缓炮方达到材料目标。";
            }
            break;
        case Outcome::Unknown:
            score.primary = std::numeric_limits<int64_t>::max() / 4;
            score.description = "successor outcome is Unknown";
            break;
    }
    return score;
}

std::vector<RankedMove> rankRecommendedMoves(
    const DenseTablebaseLookupResult& result,
    const MaterialTargetDistanceStore* mtdStore,
    bool* mtdScoringEnabled,
    std::string* mtdScoringDisabledReason) {
    std::vector<RankedMove> ranked;
    ranked.reserve(result.moves.size());
    int bestWdlTier = std::numeric_limits<int>::max();
    for (const DenseTablebaseMoveInfo& move : result.moves) {
        bestWdlTier = std::min(bestWdlTier, recommendationTier(result.position.side, result.outcome, move.successorOutcome));
    }

    bool useMtd = false;
    std::string disabledReason;
    if (result.moves.empty()) {
        disabledReason = "No legal moves.";
    } else if (mtdStore == nullptr || !mtdStore->status().loaded) {
        disabledReason = "MTD is not loaded; ranking is WDL-only.";
    } else {
        useMtd = true;
        for (const DenseTablebaseMoveInfo& move : result.moves) {
            if (recommendationTier(result.position.side, result.outcome, move.successorOutcome) != bestWdlTier) {
                continue;
            }
            if (!queryMoveMtd(mtdStore, move).has_value()) {
                useMtd = false;
                disabledReason = "MTD data is incomplete for the best WDL tier; ranking is WDL-only.";
                break;
            }
        }
    }
    if (mtdScoringEnabled != nullptr) {
        *mtdScoringEnabled = useMtd;
    }
    if (mtdScoringDisabledReason != nullptr) {
        *mtdScoringDisabledReason = disabledReason;
    }

    for (const DenseTablebaseMoveInfo& move : result.moves) {
        const std::optional<MtdEntry> successorMtd = queryMoveMtd(mtdStore, move);
        MoveScore score = scoreMoveForRecommendation(result.position.side, result.outcome, move, successorMtd, useMtd);
        ranked.push_back(RankedMove{
            move,
            successorMtd,
            score,
            0,
            false,
            score.description,
        });
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedMove& lhs, const RankedMove& rhs) {
        if (std::tie(lhs.score.wdlTier, lhs.score.primary, lhs.score.secondary, lhs.score.tertiary) !=
            std::tie(rhs.score.wdlTier, rhs.score.primary, rhs.score.secondary, rhs.score.tertiary)) {
            return std::tie(lhs.score.wdlTier, lhs.score.primary, lhs.score.secondary, lhs.score.tertiary) <
                   std::tie(rhs.score.wdlTier, rhs.score.primary, rhs.score.secondary, rhs.score.tertiary);
        }
        return deterministicMoveLess(lhs.move, rhs.move);
    });

    int nextRank = 1;
    for (size_t i = 0; i < ranked.size(); ++i) {
        if (i > 0 && !sameRankScore(ranked[i - 1].score, ranked[i].score)) {
            nextRank = static_cast<int>(i + 1);
        }
        ranked[i].rank = nextRank;
    }
    if (!ranked.empty()) {
        const MoveScore bestScore = ranked.front().score;
        for (RankedMove& move : ranked) {
            move.isOptimal = sameRankScore(move.score, bestScore);
        }
    }
    return ranked;
}

namespace {

std::string moveJson(const DenseTablebaseMoveInfo& move) {
    std::ostringstream out;
    out << "{"
        << "\"move\":" << jsonString(moveToString(move.move)) << ","
        << "\"from\":" << move.move.from << ","
        << "\"to\":" << move.move.to << ","
        << "\"capture\":" << jsonBool(move.move.capture) << ","
        << "\"capturedSquare\":" << move.move.capturedSquare << ","
        << "\"successorPosition\":" << jsonString(positionToNotation(move.successor)) << ","
        << "\"successorSoldierCount\":" << move.successorSoldierCount << ","
        << "\"successorIndex\":" << jsonString(std::to_string(move.successorIndex)) << ","
        << "\"successorOutcome\":" << jsonString(outcomeToString(move.successorOutcome)) << ","
        << "\"successorOutcomeText\":\"" << outcomeText(move.successorOutcome) << "\","
        << "\"classification\":" << jsonString(move.classification) << ","
        << "\"classificationText\":\"" << classificationText(move.classification) << "\""
        << "}";
    return out.str();
}

std::string scoreJson(const MoveScore& score) {
    std::ostringstream out;
    out << "{"
        << "\"wdlTier\":" << score.wdlTier << ","
        << "\"usedMtd\":" << jsonBool(score.usedMtd) << ","
        << "\"primary\":" << score.primary << ","
        << "\"secondary\":" << score.secondary << ","
        << "\"tertiary\":" << score.tertiary << ","
        << "\"description\":" << jsonString(score.description)
        << "}";
    return out.str();
}

std::string rankedMoveJson(const RankedMove& ranked, int currentSoldierCount) {
    const DenseTablebaseMoveInfo& move = ranked.move;
    std::ostringstream out;
    out << "{"
        << "\"move\":" << jsonString(moveToString(move.move)) << ","
        << "\"from\":" << move.move.from << ","
        << "\"to\":" << move.move.to << ","
        << "\"capture\":" << jsonBool(move.move.capture) << ","
        << "\"capturedSquare\":" << move.move.capturedSquare << ","
        << "\"successorPosition\":" << jsonString(positionToNotation(move.successor)) << ","
        << "\"successorSoldierCount\":" << move.successorSoldierCount << ","
        << "\"successorIndex\":" << jsonString(std::to_string(move.successorIndex)) << ","
        << "\"successorOutcome\":" << jsonString(outcomeToString(move.successorOutcome)) << ","
        << "\"successorOutcomeText\":\"" << outcomeText(move.successorOutcome) << "\","
        << "\"classification\":" << jsonString(move.classification) << ","
        << "\"classificationText\":\"" << classificationText(move.classification) << "\","
        << "\"rank\":" << ranked.rank << ","
        << "\"isOptimal\":" << jsonBool(ranked.isOptimal) << ","
        << "\"reason\":" << jsonString(ranked.reason.empty() ? ranked.score.description : ranked.reason) << ","
        << "\"successorMtd\":"
        << mtdEntryJson(
               ranked.successorMtd,
               move.successorOutcome,
               move.successorSoldierCount,
               "MTD layer is not loaded")
        << ",\"score\":" << scoreJson(ranked.score)
        << "}";
    (void)currentSoldierCount;
    return out.str();
}

std::string lookupJson(const DenseTablebaseLookupResult& result, const MaterialTargetDistanceStore* mtdStore) {
    const std::optional<MtdEntry> currentMtd = mtdStore == nullptr ? std::nullopt : mtdStore->query(result.position);
    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"position\":" << jsonString(positionToNotation(result.position)) << ","
        << "\"soldierCount\":" << result.soldierCount << ","
        << "\"denseIndex\":" << jsonString(std::to_string(result.denseIndex)) << ","
        << "\"outcome\":" << jsonString(outcomeToString(result.outcome)) << ","
        << "\"outcomeText\":\"" << outcomeText(result.outcome) << "\","
        << "\"terminal\":" << jsonBool(result.terminal) << ","
        << "\"terminalReason\":" << jsonString(result.terminalReason) << ","
        << "\"mtdAvailable\":" << jsonBool(currentMtd.has_value()) << ","
        << "\"currentMtd\":"
        << mtdEntryJson(currentMtd, result.outcome, result.soldierCount, "MTD layer is not loaded")
        << "}";
    return out.str();
}

std::string lookupJson(const DenseTablebaseLookupResult& result) {
    return lookupJson(result, nullptr);
}

std::string recommendationJson(const DenseTablebaseLookupResult& result, const MaterialTargetDistanceStore* mtdStore) {
    bool mtdScoringEnabled = false;
    std::string disabledReason;
    const std::vector<RankedMove> ranked = rankRecommendedMoves(result, mtdStore, &mtdScoringEnabled, &disabledReason);
    const std::optional<MtdEntry> currentMtd = mtdStore == nullptr ? std::nullopt : mtdStore->query(result.position);
    size_t optimalMoveCount = 0;
    for (const RankedMove& move : ranked) {
        if (move.isOptimal) {
            ++optimalMoveCount;
        }
    }
    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"position\":" << jsonString(positionToNotation(result.position)) << ","
        << "\"outcome\":" << jsonString(outcomeToString(result.outcome)) << ","
        << "\"outcomeText\":\"" << outcomeText(result.outcome) << "\","
        << "\"soldierCount\":" << result.soldierCount << ","
        << "\"denseIndex\":" << jsonString(std::to_string(result.denseIndex)) << ","
        << "\"legalMoveCount\":" << result.moves.size() << ","
        << "\"mtdAvailable\":" << jsonBool(currentMtd.has_value()) << ","
        << "\"mtdScoringEnabled\":" << jsonBool(mtdScoringEnabled) << ","
        << "\"mtdScoringDisabledReason\":";
    if (mtdScoringEnabled || disabledReason.empty()) {
        out << "null";
    } else {
        out << jsonString(disabledReason);
    }
    out << ",\"recommendationPolicy\":" << jsonString(mtdScoringEnabled ? "mtd" : "wdl") << ","
        << "\"currentMtd\":"
        << mtdEntryJson(currentMtd, result.outcome, result.soldierCount, "MTD layer is not loaded")
        << ",\"bestMove\":";
    if (ranked.empty()) {
        out << "null";
    } else {
        out << rankedMoveJson(ranked.front(), result.soldierCount);
    }
    out << ",\"optimalMoveCount\":" << optimalMoveCount << ","
        << "\"recommendedMoves\":[";
    size_t writtenRecommended = 0;
    for (const RankedMove& move : ranked) {
        if (!move.isOptimal) {
            continue;
        }
        if (writtenRecommended != 0) {
            out << ",";
        }
        out << rankedMoveJson(move, result.soldierCount);
        ++writtenRecommended;
    }
    out << "],\"moves\":[";
    for (size_t i = 0; i < ranked.size(); ++i) {
        out << rankedMoveJson(ranked[i], result.soldierCount);
        if (i + 1 != ranked.size()) {
            out << ",";
        }
    }
    out << "]}";
    return out.str();
}

std::string recommendationJson(const DenseTablebaseLookupResult& result) {
    return recommendationJson(result, nullptr);
}

std::string sideEntryJson(Side side, const DenseTablebaseLookupResult& result, const MaterialTargetDistanceStore* mtdStore) {
    const std::vector<const DenseTablebaseMoveInfo*> recommended = recommendedMoves(result);
    size_t losing = 0;
    for (const DenseTablebaseMoveInfo& move : result.moves) {
        if (move.classification == "losing") {
            ++losing;
        }
    }
    std::ostringstream out;
    out << "{"
        << "\"side\":" << jsonString(side == Side::Cannon ? "cannon" : "soldier") << ","
        << "\"outcome\":" << jsonString(outcomeToString(result.outcome)) << ","
        << "\"outcomeText\":\"" << outcomeText(result.outcome) << "\","
        << "\"bestMove\":" << (recommended.empty() ? "null" : jsonString(moveToString(recommended.front()->move))) << ","
        << "\"bestMoveCount\":" << recommended.size() << ","
        << "\"losingMoveCount\":" << losing << ","
        << "\"summary\":" << recommendationJson(result, mtdStore)
        << "}";
    return out.str();
}

std::string compareSidesJson(const DenseTablebaseStore& store, Position position, const MaterialTargetDistanceStore* mtdStore) {
    Position cannon = position;
    cannon.side = Side::Cannon;
    Position soldier = position;
    soldier.side = Side::Soldier;
    const DenseTablebaseLookupResult cannonResult = store.query(cannon, true);
    const DenseTablebaseLookupResult soldierResult = store.query(soldier, true);
    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"cannon\":" << sideEntryJson(Side::Cannon, cannonResult, mtdStore) << ","
        << "\"soldier\":" << sideEntryJson(Side::Soldier, soldierResult, mtdStore)
        << "}";
    return out.str();
}

std::string compareSidesJson(const DenseTablebaseStore& store, Position position) {
    return compareSidesJson(store, position, nullptr);
}

std::string alternativeJson(const WdlAlternativeMove& alternative) {
    std::ostringstream out;
    out << "{"
        << "\"move\":" << jsonString(moveToString(alternative.move)) << ","
        << "\"from\":" << alternative.move.from << ","
        << "\"to\":" << alternative.move.to << ","
        << "\"capture\":" << jsonBool(alternative.move.capture) << ","
        << "\"capturedSquare\":" << alternative.move.capturedSquare << ","
        << "\"successorOutcome\":" << jsonString(outcomeToString(alternative.successorOutcome)) << ","
        << "\"classification\":" << jsonString(alternative.classification)
        << "}";
    return out.str();
}

std::string linePlyJson(const WdlLinePly& ply) {
    std::ostringstream out;
    out << "{"
        << "\"ply\":" << ply.ply << ","
        << "\"position\":" << jsonString(positionToNotation(ply.position)) << ","
        << "\"sideToMove\":" << jsonString(ply.sideToMove == Side::Cannon ? "cannon" : "soldier") << ","
        << "\"outcome\":" << jsonString(outcomeToString(ply.outcome)) << ","
        << "\"soldierCount\":" << ply.soldierCount << ","
        << "\"denseIndex\":" << jsonString(std::to_string(ply.denseIndex)) << ","
        << "\"chosenMove\":" << jsonString(moveToString(ply.chosenMove)) << ","
        << "\"from\":" << ply.chosenMove.from << ","
        << "\"to\":" << ply.chosenMove.to << ","
        << "\"capture\":" << jsonBool(ply.chosenMove.capture) << ","
        << "\"capturedSquare\":" << ply.chosenMove.capturedSquare << ","
        << "\"successorPosition\":" << jsonString(positionToNotation(ply.successor)) << ","
        << "\"successorOutcome\":" << jsonString(outcomeToString(ply.successorOutcome)) << ","
        << "\"classification\":" << jsonString(ply.chosenClassification) << ","
        << "\"alternatives\":[";
    for (size_t i = 0; i < ply.alternatives.size(); ++i) {
        out << alternativeJson(ply.alternatives[i]);
        if (i + 1 != ply.alternatives.size()) {
            out << ",";
        }
    }
    out << "]}";
    return out.str();
}

std::string lineExplorerJson(const WdlLineExplorerResult& result) {
    std::ostringstream out;
    out << "{"
        << "\"ok\":" << jsonBool(!result.error.has_value()) << ","
        << "\"start\":" << jsonString(positionToNotation(result.start)) << ","
        << "\"startOutcome\":" << jsonString(outcomeToString(result.startOutcome)) << ","
        << "\"stopReason\":" << jsonString(result.stopReason) << ","
        << "\"cycleStartPly\":";
    if (result.cycleStartPly.has_value()) {
        out << *result.cycleStartPly;
    } else {
        out << "null";
    }
    if (result.error.has_value()) {
        out << ",\"error\":" << jsonString(*result.error);
    }
    out << ",\"plies\":[";
    for (size_t i = 0; i < result.plies.size(); ++i) {
        out << linePlyJson(result.plies[i]);
        if (i + 1 != result.plies.size()) {
            out << ",";
        }
    }
    out << "]}";
    return out.str();
}

class JsonObjectParser {
public:
    explicit JsonObjectParser(std::string_view text) : text_(text) {}

    std::map<std::string, std::string> parseFlatObject() {
        skipWhitespace();
        consume('{');
        std::map<std::string, std::string> fields;
        skipWhitespace();
        if (peek() == '}') {
            ++offset_;
            return fields;
        }
        while (true) {
            const std::string key = parseString();
            skipWhitespace();
            consume(':');
            skipWhitespace();
            if (peek() == '"') {
                fields[key] = parseString();
            } else {
                fields[key] = parseNumberToken();
            }
            skipWhitespace();
            if (peek() == '}') {
                ++offset_;
                break;
            }
            consume(',');
        }
        skipWhitespace();
        if (offset_ != text_.size()) {
            throw std::runtime_error("unexpected trailing JSON data");
        }
        return fields;
    }

private:
    void skipWhitespace() {
        while (offset_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[offset_]))) {
            ++offset_;
        }
    }

    char peek() const {
        if (offset_ >= text_.size()) {
            throw std::runtime_error("unexpected end of JSON");
        }
        return text_[offset_];
    }

    void consume(char expected) {
        if (peek() != expected) {
            throw std::runtime_error("invalid JSON object");
        }
        ++offset_;
    }

    std::string parseString() {
        consume('"');
        std::string value;
        while (offset_ < text_.size()) {
            const char ch = text_[offset_++];
            if (ch == '"') {
                return value;
            }
            if (ch == '\\') {
                if (offset_ >= text_.size()) {
                    throw std::runtime_error("invalid JSON escape");
                }
                const char escaped = text_[offset_++];
                switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        value.push_back(escaped);
                        break;
                    case 'b':
                        value.push_back('\b');
                        break;
                    case 'f':
                        value.push_back('\f');
                        break;
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    default:
                        throw std::runtime_error("unsupported JSON escape");
                }
            } else {
                value.push_back(ch);
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    std::string parseNumberToken() {
        const size_t start = offset_;
        if (peek() == '-') {
            ++offset_;
        }
        while (offset_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[offset_]))) {
            ++offset_;
        }
        if (start == offset_ || (text_[start] == '-' && start + 1 == offset_)) {
            throw std::runtime_error("expected JSON string or integer");
        }
        return std::string(text_.substr(start, offset_ - start));
    }

    std::string_view text_;
    size_t offset_ = 0;
};

std::map<std::string, std::string> parseRequestJsonObject(const std::string& body) {
    return JsonObjectParser(body).parseFlatObject();
}

std::string requireField(const std::map<std::string, std::string>& fields, const std::string& name) {
    const auto it = fields.find(name);
    if (it == fields.end() || it->second.empty()) {
        throw std::runtime_error("missing required JSON field: " + name);
    }
    return it->second;
}

int parseMaxPlies(const std::map<std::string, std::string>& fields) {
    const auto it = fields.find("maxPlies");
    if (it == fields.end()) {
        return 100;
    }
    const int value = std::stoi(it->second);
    if (value <= 0 || value > 1000000) {
        throw std::runtime_error("maxPlies must be in 1..1000000");
    }
    return value;
}

Position positionFromRequest(const std::string& body) {
    const auto fields = parseRequestJsonObject(body);
    return parsePositionNotation(requireField(fields, "position"));
}

std::string errorCodeForException(const std::exception& error) {
    const std::string message = error.what();
    if (message.find("JSON") != std::string::npos ||
        message.find("missing required JSON field") != std::string::npos ||
        message.find("maxPlies") != std::string::npos) {
        return "bad_request";
    }
    if (message.find("notation") != std::string::npos ||
        message.find("board") != std::string::npos ||
        message.find("side") != std::string::npos ||
        message.find("cannons") != std::string::npos ||
        message.find("soldiers") != std::string::npos ||
        message.find("Dense tablebase") != std::string::npos ||
        message.find("overlap") != std::string::npos) {
        return "invalid_position";
    }
    if (message.find("missing dense tablebase layer") != std::string::npos) {
        return "missing_layer";
    }
    if (message.find("ruleset hash mismatch") != std::string::npos) {
        return "ruleset_mismatch";
    }
    if (message.find("tablebase") != std::string::npos) {
        return "missing_tablebase";
    }
    return "lookup_error";
}

LocalHttpResponse apiError(int status, const std::string& code, const std::string& message) {
    return LocalHttpResponse{status, "application/json; charset=utf-8", jsonError(code, message)};
}

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open static file");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string contentTypeForPath(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".html") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".js") {
        return "text/javascript; charset=utf-8";
    }
    if (ext == ".css") {
        return "text/css; charset=utf-8";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".ico") {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

bool pathStartsWith(const std::filesystem::path& child, const std::filesystem::path& root) {
    auto childIt = child.begin();
    auto rootIt = root.begin();
    for (; rootIt != root.end(); ++rootIt, ++childIt) {
        if (childIt == child.end() || *childIt != *rootIt) {
            return false;
        }
    }
    return true;
}

std::optional<std::filesystem::path> resolveStaticPath(
    const std::filesystem::path& uiDir,
    const std::string& requestPath) {
    if (!isSafeStaticRequestPath(requestPath)) {
        return std::nullopt;
    }
    std::string relative = requestPath;
    if (relative.empty() || relative == "/") {
        relative = "/index.html";
    }
    if (relative.front() == '/') {
        relative.erase(relative.begin());
    }

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(uiDir, ec);
    if (ec) {
        return std::nullopt;
    }
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec || !pathStartsWith(candidate, root)) {
        return std::nullopt;
    }
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
        return candidate;
    }
    const std::filesystem::path index = root / "index.html";
    if (std::filesystem::exists(index, ec) && std::filesystem::is_regular_file(index, ec)) {
        return index;
    }
    return std::nullopt;
}

void openBrowser(const std::string& url) {
#ifdef _WIN32
    const std::string command = "start \"\" \"" + url + "\"";
#elif __APPLE__
    const std::string command = "open \"" + url + "\"";
#else
    const std::string command = "xdg-open \"" + url + "\"";
#endif
    (void)std::system(command.c_str());
}

}  // namespace

DenseTablebaseStore DenseTablebaseStore::missing(std::filesystem::path tablebaseDir, std::string error) {
    DenseTablebaseStore store;
    store.status_.tablebaseDir = std::move(tablebaseDir);
    store.status_.tablebaseLoaded = false;
    store.status_.complete = false;
    store.status_.error = std::move(error);
    store.status_.code = "missing_tablebase";
    for (int k = 0; k <= 15; ++k) {
        store.status_.missingLayers.push_back(k);
    }
    return store;
}

DenseTablebaseStore DenseTablebaseStore::open(const std::filesystem::path& tablebaseDir) {
    DenseTablebaseStore store;
    store.status_.tablebaseDir = tablebaseDir;
    if (!directoryExists(tablebaseDir)) {
        return missing(tablebaseDir, "No tablebase directory was found. Pass --tablebase-dir or set SANPAO15_TABLEBASE_DIR.");
    }

    std::optional<std::string> encoding;
    for (int k = 0; k <= 15; ++k) {
        const std::filesystem::path layerPath = tablebaseDir / layerFileName(k);
        if (!std::filesystem::exists(layerPath)) {
            store.status_.missingLayers.push_back(k);
            continue;
        }
        try {
            const DenseResultFileInfo info = inspectDenseResultFile(layerPath);
            if (info.rulesetHash != StandardRulesetHash) {
                throw std::runtime_error(".s15res ruleset hash mismatch");
            }
            if (info.soldierCount != k) {
                throw std::runtime_error(".s15res soldier count mismatch");
            }
            if (info.stateCount != denseStateCount(k)) {
                throw std::runtime_error(".s15res state count does not match soldier-count layer");
            }
            if (info.payloadBytes != denseResultPayloadBytes(info.stateCount, info.encoding)) {
                throw std::runtime_error(".s15res payload byte count does not match encoding");
            }
            store.layers_[static_cast<size_t>(k)] = Layer{layerPath, info};
            store.status_.loadedLayers.push_back(k);
            const std::string layerEncoding = denseEncodingName(info.encoding);
            if (!encoding.has_value()) {
                encoding = layerEncoding;
            } else if (*encoding != layerEncoding) {
                encoding = "mixed";
            }
        } catch (const std::exception& error) {
            store.status_.invalidLayers.push_back(LocalTablebaseInvalidLayer{k, layerPath, error.what()});
        }
    }

    store.status_.encoding = encoding.value_or("unknown");
    store.status_.tablebaseLoaded = !store.status_.loadedLayers.empty() && store.status_.invalidLayers.empty();
    store.status_.complete = store.status_.tablebaseLoaded && store.status_.loadedLayers.size() == 16 &&
                             store.status_.missingLayers.empty();
    if (!store.status_.tablebaseLoaded) {
        store.status_.code = "missing_tablebase";
        store.status_.error = "No valid .s15res layers were loaded from tablebase directory.";
    } else if (!store.status_.complete) {
        store.status_.code = "missing_layer";
        store.status_.error = "Tablebase directory is partial; missing one or more layer-NN.s15res files.";
    }
    return store;
}

MaterialTargetDistanceStore MaterialTargetDistanceStore::missing(
    std::filesystem::path mtdDir,
    std::string error,
    LocalMtdStoreMode mode) {
    MaterialTargetDistanceStore store;
    store.status_.loaded = false;
    store.status_.complete = false;
    store.status_.mtdDir = std::move(mtdDir);
    store.status_.store = localMtdStoreModeToString(mode);
    store.status_.error = std::move(error);
    store.status_.code = "missing_mtd";
    for (int k = 0; k <= 15; ++k) {
        store.status_.missingLayers.push_back(k);
    }
    return store;
}

MaterialTargetDistanceStore MaterialTargetDistanceStore::open(
    const std::filesystem::path& mtdDir,
    LocalMtdStoreMode mode) {
    MaterialTargetDistanceStore store;
    store.status_.mtdDir = mtdDir;
    store.status_.store = localMtdStoreModeToString(mode);
    store.status_.semanticVersion = 2;
    store.status_.encoding = mtdEncodingToString(MtdEncoding::Packed12Material4Distance8);
    if (!directoryExists(mtdDir)) {
        return missing(mtdDir, "No MTD directory was found. Recommendations are WDL-only.", mode);
    }

    for (int k = 0; k <= 15; ++k) {
        const std::filesystem::path layerPath = mtdDir / mtdLayerFileName(k);
        if (!std::filesystem::exists(layerPath)) {
            store.status_.missingLayers.push_back(k);
            continue;
        }
        try {
            const MtdFileInfo info = inspectMtdFile(layerPath);
            if (info.version != 2) {
                throw std::runtime_error(".s15mtd semantic version must be 2");
            }
            if (info.rulesetHash != StandardRulesetHash) {
                throw std::runtime_error(".s15mtd ruleset hash mismatch");
            }
            if (info.soldierCount != k) {
                throw std::runtime_error(".s15mtd soldier count mismatch");
            }
            if (info.stateCount != denseStateCount(k)) {
                throw std::runtime_error(".s15mtd state count does not match soldier-count layer");
            }
            if (info.payloadBytes != mtdPayloadBytes(info.stateCount)) {
                throw std::runtime_error(".s15mtd payload byte count does not match encoding");
            }
            if (info.encoding != MtdEncoding::Packed12Material4Distance8) {
                throw std::runtime_error(".s15mtd encoding must be packed12-material4-distance8");
            }

            Layer layer;
            layer.path = layerPath;
            layer.info = info;
            if (mode == LocalMtdStoreMode::Ram) {
                layer.ram.emplace(loadMtdTable(layerPath, StandardRulesetHash, k));
            } else {
                layer.mmap.emplace(layerPath, StandardRulesetHash, k);
            }
            store.layers_[static_cast<size_t>(k)].emplace(std::move(layer));
            store.status_.loadedLayers.push_back(k);
        } catch (const std::exception& error) {
            store.status_.invalidLayers.push_back(LocalMtdInvalidLayer{k, layerPath, error.what()});
        }
    }

    store.status_.loaded = !store.status_.loadedLayers.empty() && store.status_.invalidLayers.empty();
    store.status_.complete = store.status_.loaded && store.status_.loadedLayers.size() == 16 &&
                             store.status_.missingLayers.empty() && store.status_.invalidLayers.empty();
    if (!store.status_.invalidLayers.empty()) {
        store.status_.code = "invalid_mtd";
        store.status_.error = "One or more .s15mtd layers failed validation.";
    } else if (store.status_.loadedLayers.empty()) {
        store.status_.code = "missing_mtd";
        store.status_.error = "No valid .s15mtd layers were loaded from MTD directory.";
    } else if (!store.status_.complete) {
        store.status_.code = "missing_mtd_layer";
        store.status_.error = "MTD directory is partial; missing one or more layer-NN.s15mtd files.";
    }
    return store;
}

const LocalMtdStatus& MaterialTargetDistanceStore::status() const noexcept {
    return status_;
}

bool MaterialTargetDistanceStore::hasLayer(int soldierCount) const {
    if (soldierCount < 0 || soldierCount > 15) {
        return false;
    }
    return layers_.at(static_cast<size_t>(soldierCount)).has_value();
}

std::optional<MtdEntry> MaterialTargetDistanceStore::query(int soldierCount, uint64_t denseIndex) const {
    requireBackendDenseLayer(soldierCount);
    const auto& layer = layers_.at(static_cast<size_t>(soldierCount));
    if (!layer.has_value()) {
        return std::nullopt;
    }
    if (denseIndex >= layer->info.stateCount) {
        throw std::out_of_range("MTD dense index is outside the layer state count");
    }
    if (layer->ram.has_value()) {
        return layer->ram->view().getUnchecked(denseIndex);
    }
    if (layer->mmap.has_value()) {
        return layer->mmap->view().getUnchecked(denseIndex);
    }
    throw std::runtime_error("MTD layer has no loaded storage");
}

std::optional<MtdEntry> MaterialTargetDistanceStore::query(const Position& position) const {
    const int soldierCount = popcount25(position.soldiers);
    requireBackendDenseLayer(soldierCount);
    return query(soldierCount, denseIndex(position));
}

const LocalTablebaseStatus& DenseTablebaseStore::status() const {
    return status_;
}

Outcome DenseTablebaseStore::queryOutcome(const Position& position) const {
    if (!status_.tablebaseLoaded) {
        throw std::runtime_error(status_.error.empty() ? "tablebase is not loaded" : status_.error);
    }
    const int soldierCount = popcount25(position.soldiers);
    requireBackendDenseLayer(soldierCount);
    const auto& layer = layers_.at(static_cast<size_t>(soldierCount));
    if (!layer.has_value()) {
        throw std::runtime_error("missing dense tablebase layer: " + layerFileName(soldierCount));
    }
    const uint64_t index = denseIndex(position);
    if (index >= layer->info.stateCount) {
        throw std::out_of_range("position dense index is outside the layer state count");
    }

    uint64_t payloadIndex = 0;
    if (layer->info.encoding == DenseResultEncoding::Byte) {
        payloadIndex = index;
    } else {
        payloadIndex = index / 4u;
    }
    if (payloadIndex >= layer->info.payloadBytes) {
        throw std::runtime_error(".s15res payload index is outside payload size: " + layer->path.string());
    }
    const uint64_t totalSize = fileSize(layer->path);
    if (HeaderBytes + static_cast<std::streamoff>(payloadIndex) >= static_cast<std::streamoff>(totalSize)) {
        throw std::runtime_error(".s15res lookup offset is outside file size: " + layer->path.string());
    }

    std::ifstream input(layer->path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open .s15res for random lookup: " + layer->path.string());
    }
    input.seekg(HeaderBytes + static_cast<std::streamoff>(payloadIndex));
    const int byte = input.get();
    if (byte == std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected end of .s15res during random lookup: " + layer->path.string());
    }
    if (layer->info.encoding == DenseResultEncoding::Byte) {
        return decodeOutcome(static_cast<uint8_t>(byte));
    }
    const int shift = static_cast<int>((index % 4u) * 2u);
    return decodeOutcome(static_cast<uint8_t>((static_cast<uint8_t>(byte) >> shift) & 0x3u));
}

DenseTablebaseLookupResult DenseTablebaseStore::query(const Position& position, bool includeMoves) const {
    DenseTablebaseLookupResult result;
    result.position = position;
    result.soldierCount = popcount25(position.soldiers);
    requireBackendDenseLayer(result.soldierCount);
    result.denseIndex = denseIndex(position);
    result.outcome = queryOutcome(position);

    std::vector<DenseSuccessor> successors =
        generateDenseSuccessorsFromPosition(result.soldierCount, result.denseIndex, position);
    const DenseTerminalInfo terminal = terminalOutcomeForPositionWithSuccessors(position, successors);
    result.terminal = terminal.terminal;
    if (terminal.terminal) {
        const std::optional<Outcome> material = forcedOutcomeByMaterialRule(result.soldierCount);
        result.terminalReason = material.has_value() ? RulesetSummary : "rules-terminal";
    }

    if (!includeMoves) {
        return result;
    }

    result.moves.reserve(successors.size());
    for (const DenseSuccessor& successor : successors) {
        const Position successorPosition = positionFromDenseIndex(successor.toSoldierCount, successor.toIndex);
        const Outcome successorOutcome = queryOutcome(successorPosition);
        result.moves.push_back(DenseTablebaseMoveInfo{
            successor.move,
            successorPosition,
            successor.toIndex,
            successor.toSoldierCount,
            successorOutcome,
            successorOutcome == winFor(position.side) ? "winning" :
                successorOutcome == Outcome::Draw ? "drawing" : "losing",
        });
    }
    std::sort(result.moves.begin(), result.moves.end(), [](const DenseTablebaseMoveInfo& lhs, const DenseTablebaseMoveInfo& rhs) {
        const int lhsRank = classificationRank(lhs.classification);
        const int rhsRank = classificationRank(rhs.classification);
        if (lhsRank != rhsRank) {
            return lhsRank < rhsRank;
        }
        return std::tie(lhs.move.from, lhs.move.to, lhs.move.capture, lhs.move.capturedSquare,
                        lhs.successorSoldierCount, lhs.successorIndex) <
               std::tie(rhs.move.from, rhs.move.to, rhs.move.capture, rhs.move.capturedSquare,
                        rhs.successorSoldierCount, rhs.successorIndex);
    });
    return result;
}

WdlLineExplorerResult DenseTablebaseStore::explore(const Position& start, int maxPlies) const {
    WdlLineExplorerOptions options;
    options.tablebaseDir = status_.tablebaseDir;
    options.start = start;
    options.maxPlies = maxPlies;
    options.includeAlternatives = true;

    WdlLineExplorerResult result;
    result.start = start;
    if (maxPlies < 0) {
        result.stopReason = "lookupError";
        result.error = "maxPlies must be non-negative";
        return result;
    }

    Position current = start;
    std::map<uint64_t, int> visited;
    visited.emplace(packPosition(current), 0);

    for (int ply = 0; ply < maxPlies; ++ply) {
        DenseTablebaseLookupResult lookup;
        try {
            lookup = query(current, true);
        } catch (const std::exception& error) {
            result.stopReason = errorCodeForException(error) == "missing_layer" ? "missingTablebase" : "lookupError";
            result.error = error.what();
            if (result.plies.empty()) {
                result.startOutcome = Outcome::Unknown;
            }
            return result;
        }
        if (ply == 0) {
            result.startOutcome = lookup.outcome;
        }
        if (lookup.terminal) {
            result.stopReason = "terminal";
            return result;
        }
        if (lookup.moves.empty()) {
            result.stopReason = "noLegalMoves";
            return result;
        }
        std::sort(lookup.moves.begin(), lookup.moves.end(), [&lookup](const DenseTablebaseMoveInfo& lhs, const DenseTablebaseMoveInfo& rhs) {
            const int lhsTier = recommendationTier(lookup.position.side, lookup.outcome, lhs.successorOutcome);
            const int rhsTier = recommendationTier(lookup.position.side, lookup.outcome, rhs.successorOutcome);
            if (lhsTier != rhsTier) {
                return lhsTier < rhsTier;
            }
            if (lhs.move.capture != rhs.move.capture) {
                return lhs.move.capture;
            }
            return std::tie(lhs.move.from, lhs.move.to, lhs.move.capture, lhs.move.capturedSquare,
                            lhs.successorSoldierCount, lhs.successorIndex) <
                   std::tie(rhs.move.from, rhs.move.to, rhs.move.capture, rhs.move.capturedSquare,
                            rhs.successorSoldierCount, rhs.successorIndex);
        });
        const DenseTablebaseMoveInfo& chosen = lookup.moves.front();
        WdlLinePly linePly;
        linePly.ply = ply;
        linePly.position = current;
        linePly.soldierCount = lookup.soldierCount;
        linePly.denseIndex = lookup.denseIndex;
        linePly.outcome = lookup.outcome;
        linePly.sideToMove = current.side;
        linePly.chosenMove = chosen.move;
        linePly.successor = chosen.successor;
        linePly.successorOutcome = chosen.successorOutcome;
        linePly.chosenClassification = chosen.classification;
        linePly.alternatives.reserve(lookup.moves.size());
        for (const DenseTablebaseMoveInfo& move : lookup.moves) {
            linePly.alternatives.push_back(WdlAlternativeMove{move.move, move.successorOutcome, move.classification});
        }
        current = chosen.successor;
        result.plies.push_back(std::move(linePly));
        const uint64_t key = packPosition(current);
        const auto [it, inserted] = visited.emplace(key, ply + 1);
        if (!inserted) {
            result.stopReason = "cycle";
            result.cycleStartPly = it->second;
            return result;
        }
    }

    result.stopReason = "maxPlies";
    if (result.plies.empty()) {
        try {
            result.startOutcome = queryOutcome(start);
        } catch (const std::exception& error) {
            result.stopReason = "lookupError";
            result.error = error.what();
        }
    }
    return result;
}

std::optional<std::filesystem::path> findDefaultTablebaseDir(
    const std::filesystem::path& cwd,
    const std::filesystem::path& executableDir) {
    if (const char* env = std::getenv("SANPAO15_TABLEBASE_DIR")) {
        std::filesystem::path envPath(env);
        if (directoryExists(envPath)) {
            return envPath;
        }
    }
    for (const std::filesystem::path& path : {
             cwd / "build" / "prod-layers",
             cwd / "prod-layers",
             executableDir / "tablebase",
             executableDir / "prod-layers",
         }) {
        if (directoryExists(path)) {
            return path;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findDefaultUiDir(
    const std::filesystem::path& cwd,
    const std::filesystem::path& executableDir) {
    for (const std::filesystem::path& path : {
             cwd / "ui" / "dist",
             executableDir / "ui" / "dist",
             executableDir / "dist",
         }) {
        if (directoryExists(path)) {
            return path;
        }
    }
    return std::nullopt;
}

bool isSafeStaticRequestPath(const std::string& requestPath) {
    if (requestPath.empty() || requestPath.front() != '/') {
        return false;
    }
    if (requestPath.find('\\') != std::string::npos ||
        requestPath.find('\0') != std::string::npos ||
        requestPath.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

std::string localTablebaseStatusJson(const LocalTablebaseStatus& status) {
    std::ostringstream out;
    out << "{"
        << "\"ok\":" << jsonBool(status.tablebaseLoaded && status.invalidLayers.empty()) << ","
        << "\"mode\":\"local-backend\","
        << "\"tablebaseLoaded\":" << jsonBool(status.tablebaseLoaded) << ","
        << "\"tablebaseDir\":" << jsonString(pathString(status.tablebaseDir)) << ","
        << "\"rulesetName\":" << jsonString(RulesetName) << ","
        << "\"rulesetHash\":" << jsonString(rulesetHashHex()) << ","
        << "\"complete\":" << jsonBool(status.complete) << ","
        << "\"loadedLayers\":[";
    writeIntArray(out, status.loadedLayers);
    out << "],\"missingLayers\":[";
    writeIntArray(out, status.missingLayers);
    out << "],\"invalidLayers\":[";
    writeTablebaseInvalidLayers(out, status.invalidLayers);
    out << "],"
        << "\"encoding\":" << jsonString(status.encoding) << ","
        << "\"readMode\":\"backend-random-read\"";
    if (!status.error.empty()) {
        out << ",\"code\":" << jsonString(status.code.empty() ? "missing_tablebase" : status.code)
            << ",\"error\":" << jsonString(status.error);
    }
    out << "}";
    return out.str();
}

std::string localMtdStatusJson(const LocalMtdStatus& status) {
    std::ostringstream out;
    out << "{"
        << "\"loaded\":" << jsonBool(status.loaded) << ","
        << "\"complete\":" << jsonBool(status.complete) << ","
        << "\"dir\":" << jsonString(pathString(status.mtdDir)) << ","
        << "\"store\":" << jsonString(status.store) << ","
        << "\"semanticVersion\":" << status.semanticVersion << ","
        << "\"encoding\":" << jsonString(status.encoding) << ","
        << "\"loadedLayers\":[";
    writeIntArray(out, status.loadedLayers);
    out << "],\"missingLayers\":[";
    writeIntArray(out, status.missingLayers);
    out << "],\"invalidLayers\":[";
    writeMtdInvalidLayers(out, status.invalidLayers);
    out << "]";
    if (!status.code.empty()) {
        out << ",\"code\":" << jsonString(status.code);
    }
    if (!status.error.empty()) {
        out << ",\"error\":" << jsonString(status.error);
    }
    out << "}";
    return out.str();
}

std::string localCombinedStatusJson(
    const LocalTablebaseStatus& tablebaseStatus,
    const LocalMtdStatus& mtdStatus) {
    std::string base = localTablebaseStatusJson(tablebaseStatus);
    if (!base.empty() && base.back() == '}') {
        base.pop_back();
    }
    base += ",\"mtd\":";
    base += localMtdStatusJson(mtdStatus);
    base += "}";
    return base;
}

LocalHttpResponse handleLocalApiRequest(
    const DenseTablebaseStore& store,
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const MaterialTargetDistanceStore* mtdStore) {
    try {
        if (method == "GET" && path == "/api/status") {
            const std::string statusJson = mtdStore == nullptr
                ? localTablebaseStatusJson(store.status())
                : localCombinedStatusJson(store.status(), mtdStore->status());
            return LocalHttpResponse{200, "application/json; charset=utf-8", statusJson};
        }
        if (method != "POST") {
            return apiError(405, "bad_request", "unsupported method");
        }
        if (!store.status().tablebaseLoaded) {
            return apiError(503, "missing_tablebase", store.status().error);
        }
        if (path == "/api/query") {
            const Position position = positionFromRequest(body);
            return LocalHttpResponse{200, "application/json; charset=utf-8", lookupJson(store.query(position, false), mtdStore)};
        }
        if (path == "/api/recommend") {
            const Position position = positionFromRequest(body);
            return LocalHttpResponse{200, "application/json; charset=utf-8", recommendationJson(store.query(position, true), mtdStore)};
        }
        if (path == "/api/compare-sides") {
            const Position position = positionFromRequest(body);
            return LocalHttpResponse{200, "application/json; charset=utf-8", compareSidesJson(store, position, mtdStore)};
        }
        if (path == "/api/explore") {
            const auto fields = parseRequestJsonObject(body);
            const Position position = parsePositionNotation(requireField(fields, "position"));
            const int maxPlies = parseMaxPlies(fields);
            return LocalHttpResponse{200, "application/json; charset=utf-8", lineExplorerJson(store.explore(position, maxPlies))};
        }
        return apiError(404, "bad_request", "unknown API endpoint");
    } catch (const std::exception& error) {
        const std::string code = errorCodeForException(error);
        const int status = code == "bad_request" || code == "invalid_position" ? 400 :
                           code == "missing_tablebase" || code == "missing_layer" ? 503 : 500;
        return apiError(status, code, error.what());
    }
}

int serveLocalTablebaseBackend(const LocalBackendOptions& options) {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path executableDir = parentPathOrCwd(options.executablePath);
    const std::optional<std::filesystem::path> tablebaseDir =
        options.tablebaseDir.has_value()
            ? options.tablebaseDir
            : findDefaultTablebaseDir(cwd, executableDir);
    DenseTablebaseStore store = tablebaseDir.has_value()
        ? DenseTablebaseStore::open(*tablebaseDir)
        : DenseTablebaseStore::missing({}, "No tablebase directory was found. Pass --tablebase-dir or set SANPAO15_TABLEBASE_DIR.");
    MaterialTargetDistanceStore mtdStore = options.mtdDir.has_value()
        ? MaterialTargetDistanceStore::open(*options.mtdDir, options.mtdStore)
        : MaterialTargetDistanceStore::missing(
              {},
              "No MTD directory was provided. Recommendations are WDL-only.",
              options.mtdStore);

    const std::optional<std::filesystem::path> uiDir =
        options.uiDir.has_value() ? options.uiDir : findDefaultUiDir(cwd, executableDir);

    httplib::Server server;
    server.Get("/api/status", [&](const httplib::Request&, httplib::Response& response) {
        const LocalHttpResponse api = handleLocalApiRequest(store, "GET", "/api/status", {}, &mtdStore);
        response.status = api.status;
        response.set_content(api.body, api.contentType);
    });
    const auto postHandler = [&](const httplib::Request& request, httplib::Response& response) {
        const LocalHttpResponse api = handleLocalApiRequest(store, "POST", request.path, request.body, &mtdStore);
        response.status = api.status;
        response.set_content(api.body, api.contentType);
    };
    server.Post("/api/query", postHandler);
    server.Post("/api/recommend", postHandler);
    server.Post("/api/compare-sides", postHandler);
    server.Post("/api/explore", postHandler);
    server.Get(R"(/.*)", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.path.rfind("/api/", 0) == 0) {
            response.status = 404;
            response.set_content(jsonError("bad_request", "unknown API endpoint"), "application/json; charset=utf-8");
            return;
        }
        if (!uiDir.has_value()) {
            response.status = 200;
            response.set_content("UI dist not found. Run cd ui; npm run build or pass --ui-dir.", "text/plain; charset=utf-8");
            return;
        }
        const std::optional<std::filesystem::path> file = resolveStaticPath(*uiDir, request.path);
        if (!file.has_value()) {
            response.status = 403;
            response.set_content("Forbidden", "text/plain; charset=utf-8");
            return;
        }
        try {
            response.status = 200;
            response.set_content(readFileBytes(*file), contentTypeForPath(*file));
        } catch (const std::exception& error) {
            response.status = 404;
            response.set_content(error.what(), "text/plain; charset=utf-8");
        }
    });

    const std::string url = "http://" + options.host + ":" + std::to_string(options.port);
    std::cout << "Sanpao15 local backend\n";
    std::cout << "Listening: " << url << "\n";
    std::cout << "Host: " << options.host << "\n";
    std::cout << "Port: " << options.port << "\n";
    std::cout << "Tablebase loaded: " << (store.status().tablebaseLoaded ? "yes" : "no") << "\n";
    std::cout << "Tablebase dir: " << pathString(store.status().tablebaseDir) << "\n";
    std::cout << "Loaded layers: " << store.status().loadedLayers.size() << "/16\n";
    if (!store.status().error.empty()) {
        std::cout << "Tablebase note: " << store.status().error << "\n";
    }
    std::cout << "MTD loaded: " << (mtdStore.status().loaded ? "yes" : "no") << "\n";
    std::cout << "MTD dir: " << pathString(mtdStore.status().mtdDir) << "\n";
    std::cout << "MTD store: " << mtdStore.status().store << "\n";
    std::cout << "MTD loaded layers: " << mtdStore.status().loadedLayers.size() << "/16\n";
    if (!mtdStore.status().error.empty()) {
        std::cout << "MTD note: " << mtdStore.status().error << "\n";
    }
    if (uiDir.has_value()) {
        std::cout << "UI dir: " << pathString(*uiDir) << "\n";
    } else {
        std::cout << "UI dir: not found; API-only fallback page will be served.\n";
    }
    if (options.openBrowser) {
        openBrowser(url);
    }
    if (!server.listen(options.host, options.port)) {
        std::cerr << "Failed to listen on " << options.host << ":" << options.port << "\n";
        return 1;
    }
    return 0;
}

}  // namespace sanpao15
