#include "test_common.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "sanpao15/dense_index.h"
#include "sanpao15/dense_table.h"
#include "sanpao15/local_backend.h"
#include "sanpao15/notation.h"
#include "sanpao15/ruleset.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempBackendDir(const std::string& name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void requireContains(const std::string& text, const std::string& needle) {
    SANPAO15_REQUIRE(text.find(needle) != std::string::npos);
}

}  // namespace

SANPAO15_TEST(localBackendStatusReportsMissingTablebase) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sanpao15-backend-missing";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const DenseTablebaseStore store = DenseTablebaseStore::open(dir);
    const LocalHttpResponse response = handleLocalApiRequest(store, "GET", "/api/status", {});

    SANPAO15_REQUIRE(response.status == 200);
    requireContains(response.body, "\"tablebaseLoaded\":false");
    requireContains(response.body, "\"code\":\"missing_tablebase\"");
}

SANPAO15_TEST(localBackendRejectsStaticPathTraversal) {
    SANPAO15_REQUIRE(isSafeStaticRequestPath("/index.html"));
    SANPAO15_REQUIRE(!isSafeStaticRequestPath("/../../secret.txt"));
    SANPAO15_REQUIRE(!isSafeStaticRequestPath("/assets\\secret.txt"));
}

SANPAO15_TEST(localBackendRejectsBadJsonRequest) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-bad-json");
    DenseOutcomeTable k0(denseStateCount(0), Outcome::CannonWin);
    saveDenseResultTable(k0, 0, dir / "layer-00.s15res", StandardRulesetHash);
    const DenseTablebaseStore store = DenseTablebaseStore::open(dir);
    const LocalHttpResponse response = handleLocalApiRequest(store, "POST", "/api/query", "{bad");

    SANPAO15_REQUIRE(response.status == 400);
    requireContains(response.body, "\"code\":\"bad_request\"");
}

SANPAO15_TEST(localBackendQueryRejectsInvalidPosition) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-invalid-position");
    DenseOutcomeTable k0(denseStateCount(0), Outcome::CannonWin);
    saveDenseResultTable(k0, 0, dir / "layer-00.s15res", StandardRulesetHash);
    const DenseTablebaseStore store = DenseTablebaseStore::open(dir);
    const LocalHttpResponse response = handleLocalApiRequest(
        store,
        "POST",
        "/api/query",
        "{\"position\":\"...../...../...../...../..... c\"}");

    SANPAO15_REQUIRE(response.status == 400);
    requireContains(response.body, "\"code\":\"invalid_position\"");
}

SANPAO15_TEST(localBackendQueriesGeneratedK0Layer) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-k0");
    DenseOutcomeTable k0(denseStateCount(0), Outcome::CannonWin);
    saveDenseResultTable(k0, 0, dir / "layer-00.s15res", StandardRulesetHash);
    const DenseTablebaseStore store = DenseTablebaseStore::open(dir);
    const std::string body = "{\"position\":\"...../...../...../...../.CCC. c\"}";

    const LocalHttpResponse query = handleLocalApiRequest(store, "POST", "/api/query", body);
    SANPAO15_REQUIRE(query.status == 200);
    requireContains(query.body, "\"outcome\":\"CannonWin\"");
    requireContains(query.body, "\"denseIndex\":\"");

    const LocalHttpResponse recommend = handleLocalApiRequest(store, "POST", "/api/recommend", body);
    SANPAO15_REQUIRE(recommend.status == 200);
    requireContains(recommend.body, "\"legalMoveCount\"");
    requireContains(recommend.body, "\"recommendedMoves\"");
}
