#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sanpao15/lowk_tablebase.h"

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

struct LocalBackendOptions {
    std::string host = "127.0.0.1";
    int port = 8787;
    std::optional<std::filesystem::path> tablebaseDir;
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
LocalHttpResponse handleLocalApiRequest(
    const DenseTablebaseStore& store,
    const std::string& method,
    const std::string& path,
    const std::string& body);

int serveLocalTablebaseBackend(const LocalBackendOptions& options);

}  // namespace sanpao15
