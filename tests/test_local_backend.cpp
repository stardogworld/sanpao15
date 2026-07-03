#include "test_common.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sanpao15/dense_index.h"
#include "sanpao15/dense_table.h"
#include "sanpao15/local_backend.h"
#include "sanpao15/material_target_distance.h"
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

DenseTablebaseMoveInfo fakeMove(Outcome outcome, int successorSoldierCount, uint64_t successorIndex, int from, int to) {
    DenseTablebaseMoveInfo move;
    move.move = Move{from, to, false, -1};
    move.successorSoldierCount = successorSoldierCount;
    move.successorIndex = successorIndex;
    move.successorOutcome = outcome;
    move.classification = outcome == Outcome::Draw ? "drawing" : "winning";
    return move;
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

SANPAO15_TEST(localBackendMtdMissingStatusDoesNotBlockWdl) {
    const MaterialTargetDistanceStore mtd = MaterialTargetDistanceStore::missing(
        {},
        "No MTD directory was provided. Recommendations are WDL-only.",
        LocalMtdStoreMode::Mmap);

    SANPAO15_REQUIRE(!mtd.status().loaded);
    SANPAO15_REQUIRE(!mtd.status().complete);
    SANPAO15_REQUIRE(mtd.status().missingLayers.size() == 16);
    SANPAO15_REQUIRE(!mtd.hasLayer(0));

    const std::string json = localMtdStatusJson(mtd.status());
    requireContains(json, "\"loaded\":false");
    requireContains(json, "\"code\":\"missing_mtd\"");
    requireContains(json, "\"missingLayers\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]");
}

SANPAO15_TEST(localBackendMtdPartialStatusLoadsValidLayers) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-mtd-partial");
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    table.set(1, MtdEntry{0, 7});
    saveMtdTable(table, 0, dir / "layer-00.s15mtd", StandardRulesetHash);

    const MaterialTargetDistanceStore mtd = MaterialTargetDistanceStore::open(dir, LocalMtdStoreMode::Ram);
    SANPAO15_REQUIRE(mtd.status().loaded);
    SANPAO15_REQUIRE(!mtd.status().complete);
    SANPAO15_REQUIRE(mtd.status().loadedLayers.size() == 1);
    SANPAO15_REQUIRE(mtd.status().loadedLayers[0] == 0);
    SANPAO15_REQUIRE(mtd.status().missingLayers.size() == 15);
    SANPAO15_REQUIRE(mtd.hasLayer(0));
    SANPAO15_REQUIRE(!mtd.hasLayer(1));
    SANPAO15_REQUIRE(mtd.query(0, 1).has_value());
    SANPAO15_REQUIRE(mtd.query(0, 1)->guaranteeDistance == 7);
    SANPAO15_REQUIRE(!mtd.query(1, 0).has_value());
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(localBackendMtdInvalidLayerIsReported) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-mtd-invalid");
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, dir / "layer-00.s15mtd", StandardRulesetHash + 1u);

    const MaterialTargetDistanceStore mtd = MaterialTargetDistanceStore::open(dir, LocalMtdStoreMode::Mmap);
    SANPAO15_REQUIRE(!mtd.status().loaded);
    SANPAO15_REQUIRE(!mtd.status().complete);
    SANPAO15_REQUIRE(mtd.status().invalidLayers.size() == 1);
    SANPAO15_REQUIRE(mtd.status().invalidLayers[0].soldierCount == 0);
    requireContains(localMtdStatusJson(mtd.status()), "\"code\":\"invalid_mtd\"");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(localBackendMtdScoreRules) {
    const DenseTablebaseMoveInfo cannonWin = fakeMove(Outcome::CannonWin, 4, 0, 1, 2);
    const DenseTablebaseMoveInfo soldierWin = fakeMove(Outcome::SoldierWin, 4, 0, 1, 2);
    const DenseTablebaseMoveInfo draw = fakeMove(Outcome::Draw, 4, 0, 1, 2);

    MoveScore score = scoreMoveForRecommendation(
        Side::Cannon,
        Outcome::CannonWin,
        cannonWin,
        MtdEntry{3, 5},
        true);
    SANPAO15_REQUIRE(score.usedMtd);
    SANPAO15_REQUIRE(score.primary == 5);
    SANPAO15_REQUIRE(score.secondary == 3);

    score = scoreMoveForRecommendation(
        Side::Soldier,
        Outcome::CannonWin,
        cannonWin,
        MtdEntry{3, 5},
        true);
    SANPAO15_REQUIRE(score.primary == -5);
    SANPAO15_REQUIRE(score.secondary == -3);

    score = scoreMoveForRecommendation(
        Side::Soldier,
        Outcome::SoldierWin,
        soldierWin,
        MtdEntry{4, 9},
        true);
    SANPAO15_REQUIRE(score.primary == 9);
    SANPAO15_REQUIRE(score.secondary == -4);

    score = scoreMoveForRecommendation(
        Side::Cannon,
        Outcome::SoldierWin,
        soldierWin,
        MtdEntry{4, 9},
        true);
    SANPAO15_REQUIRE(score.primary == -9);
    SANPAO15_REQUIRE(score.secondary == 4);

    score = scoreMoveForRecommendation(
        Side::Cannon,
        Outcome::Draw,
        draw,
        MtdEntry{2, 6},
        true);
    SANPAO15_REQUIRE(score.primary == 2);
    SANPAO15_REQUIRE(score.secondary == 6);

    score = scoreMoveForRecommendation(
        Side::Soldier,
        Outcome::Draw,
        draw,
        MtdEntry{2, 6},
        true);
    SANPAO15_REQUIRE(score.primary == -2);
    SANPAO15_REQUIRE(score.secondary == -6);
}

SANPAO15_TEST(localBackendPartialMtdFallbackRequiresCompleteBestTier) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-mtd-fallback");
    PackedMtdTable12 table(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(table, 0, dir / "layer-00.s15mtd", StandardRulesetHash);
    const MaterialTargetDistanceStore mtd = MaterialTargetDistanceStore::open(dir, LocalMtdStoreMode::Ram);

    DenseTablebaseLookupResult result;
    result.position = parsePositionNotation("...../...../...../...../.CCC. c");
    result.soldierCount = 1;
    result.denseIndex = 0;
    result.outcome = Outcome::Draw;
    result.moves.push_back(fakeMove(Outcome::Draw, 0, 0, 20, 15));
    result.moves.push_back(fakeMove(Outcome::Draw, 1, 0, 21, 16));

    bool enabled = true;
    std::string reason;
    const std::vector<RankedMove> ranked = rankRecommendedMoves(result, &mtd, &enabled, &reason);
    SANPAO15_REQUIRE(!enabled);
    SANPAO15_REQUIRE(reason == "MTD data is incomplete for the best WDL tier; ranking is WDL-only.");
    SANPAO15_REQUIRE(ranked.size() == 2);
    SANPAO15_REQUIRE(!ranked[0].score.usedMtd);
    SANPAO15_REQUIRE(ranked[0].rank == 1);
    SANPAO15_REQUIRE(ranked[1].rank == 1);
    SANPAO15_REQUIRE(ranked[0].isOptimal);
    SANPAO15_REQUIRE(ranked[1].isOptimal);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(localBackendRecommendJsonIncludesMtdFields) {
    const std::filesystem::path dir = tempBackendDir("sanpao15-backend-api-mtd");
    DenseOutcomeTable k0(denseStateCount(0), Outcome::CannonWin);
    saveDenseResultTable(k0, 0, dir / "layer-00.s15res", StandardRulesetHash);
    PackedMtdTable12 mtdTable(denseStateCount(0), MtdEntry{0, 0});
    saveMtdTable(mtdTable, 0, dir / "layer-00.s15mtd", StandardRulesetHash);

    const DenseTablebaseStore store = DenseTablebaseStore::open(dir);
    const MaterialTargetDistanceStore mtd = MaterialTargetDistanceStore::open(dir, LocalMtdStoreMode::Ram);
    const std::string body = "{\"position\":\"...../...../...../...../.CCC. c\"}";

    const LocalHttpResponse status = handleLocalApiRequest(store, "GET", "/api/status", {}, &mtd);
    SANPAO15_REQUIRE(status.status == 200);
    requireContains(status.body, "\"mtd\":");
    requireContains(status.body, "\"loadedLayers\":[0]");

    const LocalHttpResponse query = handleLocalApiRequest(store, "POST", "/api/query", body, &mtd);
    SANPAO15_REQUIRE(query.status == 200);
    requireContains(query.body, "\"currentMtd\":");
    requireContains(query.body, "\"mtdAvailable\":true");

    const LocalHttpResponse recommend = handleLocalApiRequest(store, "POST", "/api/recommend", body, &mtd);
    SANPAO15_REQUIRE(recommend.status == 200);
    requireContains(recommend.body, "\"bestMove\":");
    requireContains(recommend.body, "\"rank\":");
    requireContains(recommend.body, "\"isOptimal\":");
    requireContains(recommend.body, "\"reason\":");
    requireContains(recommend.body, "\"successorMtd\":");
    requireContains(recommend.body, "\"score\":");
    requireContains(recommend.body, "\"move\":");
    requireContains(recommend.body, "\"classification\":");
    std::filesystem::remove_all(dir);
}
