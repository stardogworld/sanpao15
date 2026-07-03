#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sanpao15/lowk_tablebase.h"
#include "sanpao15/material_target_distance.h"

namespace sanpao15 {

struct LocalTablebaseInvalidLayer {
    int soldierCount = -1;
    std::filesystem::path path;
    std::string error;
};

struct LocalTablebaseStatus {
    bool tablebaseLoaded = false;
    bool complete = false;
    std::filesystem::path tablebaseDir;
    std::vector<int> loadedLayers;
    std::vector<int> missingLayers;
    std::vector<LocalTablebaseInvalidLayer> invalidLayers;
    std::string encoding = "unknown";
    std::string error;
    std::string code;
};

class DenseTablebaseStore {
public:
    static DenseTablebaseStore open(const std::filesystem::path& tablebaseDir);
    static DenseTablebaseStore missing(std::filesystem::path tablebaseDir, std::string error);

    const LocalTablebaseStatus& status() const;
    Outcome queryOutcome(const Position& position) const;
    DenseTablebaseLookupResult query(const Position& position, bool includeMoves) const;
    WdlLineExplorerResult explore(const Position& start, int maxPlies) const;

private:
    struct Layer {
        std::filesystem::path path;
        DenseResultFileInfo info;
    };

    LocalTablebaseStatus status_;
    std::vector<std::optional<Layer>> layers_{16};
};

enum class LocalMtdStoreMode {
    Ram,
    Mmap,
};

struct LocalMtdInvalidLayer {
    int soldierCount = -1;
    std::filesystem::path path;
    std::string error;
};

struct LocalMtdStatus {
    bool loaded = false;
    bool complete = false;
    std::filesystem::path mtdDir;
    std::string store = "mmap";
    int semanticVersion = 2;
    std::string encoding = "packed12-material4-distance8";
    std::vector<int> loadedLayers;
    std::vector<int> missingLayers;
    std::vector<LocalMtdInvalidLayer> invalidLayers;
    std::string error;
    std::string code;
};

class MaterialTargetDistanceStore {
public:
    static MaterialTargetDistanceStore missing(
        std::filesystem::path mtdDir,
        std::string error,
        LocalMtdStoreMode mode);
    static MaterialTargetDistanceStore open(
        const std::filesystem::path& mtdDir,
        LocalMtdStoreMode mode);

    const LocalMtdStatus& status() const noexcept;
    bool hasLayer(int soldierCount) const;
    std::optional<MtdEntry> query(int soldierCount, uint64_t denseIndex) const;
    std::optional<MtdEntry> query(const Position& position) const;

private:
    struct Layer {
        std::filesystem::path path;
        MtdFileInfo info;
        std::optional<PackedMtdTable12> ram;
        std::optional<MappedMtdTable12> mmap;
    };

    LocalMtdStatus status_;
    std::vector<std::optional<Layer>> layers_{16};
};

struct MoveScore {
    int wdlTier = 0;
    bool usedMtd = false;
    int64_t primary = 0;
    int64_t secondary = 0;
    int64_t tertiary = 0;
    std::string description;
};

struct RankedMove {
    DenseTablebaseMoveInfo move;
    std::optional<MtdEntry> successorMtd;
    MoveScore score;
    int rank = 0;
    bool isOptimal = false;
    std::string reason;
};

MoveScore scoreMoveForRecommendation(
    Side mover,
    Outcome currentOutcome,
    const DenseTablebaseMoveInfo& move,
    const std::optional<MtdEntry>& successorMtd,
    bool useMtd);

std::vector<RankedMove> rankRecommendedMoves(
    const DenseTablebaseLookupResult& result,
    const MaterialTargetDistanceStore* mtdStore,
    bool* mtdScoringEnabled,
    std::string* mtdScoringDisabledReason);

struct LocalBackendOptions {
    std::string host = "127.0.0.1";
    int port = 8787;
    std::optional<std::filesystem::path> tablebaseDir;
    std::optional<std::filesystem::path> mtdDir;
    LocalMtdStoreMode mtdStore = LocalMtdStoreMode::Mmap;
    std::optional<std::filesystem::path> uiDir;
    std::filesystem::path executablePath;
    bool openBrowser = false;
};

struct LocalHttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

std::optional<std::filesystem::path> findDefaultTablebaseDir(
    const std::filesystem::path& cwd,
    const std::filesystem::path& executableDir);
std::optional<std::filesystem::path> findDefaultUiDir(
    const std::filesystem::path& cwd,
    const std::filesystem::path& executableDir);

bool isSafeStaticRequestPath(const std::string& requestPath);

std::string localTablebaseStatusJson(const LocalTablebaseStatus& status);
std::string localMtdStatusJson(const LocalMtdStatus& status);
LocalHttpResponse handleLocalApiRequest(
    const DenseTablebaseStore& store,
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const MaterialTargetDistanceStore* mtdStore = nullptr);

int serveLocalTablebaseBackend(const LocalBackendOptions& options);

}  // namespace sanpao15
