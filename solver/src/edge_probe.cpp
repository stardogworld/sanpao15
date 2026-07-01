#include "sanpao15/edge_probe.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/partitioned_keyset.h"
#include "sanpao15/position.h"
#include "sanpao15/rules.h"

namespace sanpao15 {

namespace {

using Clock = std::chrono::steady_clock;

void reportProgress(
    const LayerEdgeProbeOptions& options,
    uint64_t sampledStates,
    uint64_t generatedMoves,
    const Clock::time_point& started) {
    if (options.progressInterval == 0 || !options.progress || sampledStates % options.progressInterval != 0) {
        return;
    }
    LayerEdgeProbeProgress info;
    info.sampledStates = sampledStates;
    info.generatedMoves = generatedMoves;
    info.elapsedSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    options.progress(info);
}

}  // namespace

LayerEdgeProbeStats probeLayerEdges(const LayerEdgeProbeOptions& options) {
    if (options.layerPartitionDir.empty()) {
        throw std::invalid_argument("layer partition dir is required");
    }
    if (options.cacheBuckets == 0) {
        throw std::invalid_argument("partition cache buckets must be greater than zero");
    }

    PartitionedKeySetReaderOptions layerReaderOptions;
    layerReaderOptions.partitionDir = options.layerPartitionDir;
    layerReaderOptions.maxCachedBuckets = options.cacheBuckets;
    PartitionedKeySetReader sampleReader(layerReaderOptions);

    const int soldierCount = sampleReader.soldierCount();
    if (soldierCount > 0 && options.nextSeedPartitionDir.empty()) {
        throw std::invalid_argument("next seed partition dir is required for layers above zero");
    }

    PartitionedKeySetReader layerLookupReader(layerReaderOptions);
    std::unique_ptr<PartitionedKeySetReader> nextSeedReader;
    if (soldierCount > 0) {
        PartitionedKeySetReaderOptions seedReaderOptions;
        seedReaderOptions.partitionDir = options.nextSeedPartitionDir;
        seedReaderOptions.maxCachedBuckets = options.cacheBuckets;
        nextSeedReader = std::make_unique<PartitionedKeySetReader>(seedReaderOptions);
        if (nextSeedReader->soldierCount() != soldierCount - 1) {
            throw std::runtime_error("next seed partition soldier count must be layer soldier count minus one");
        }
    }

    const auto started = Clock::now();
    std::vector<uint64_t> samples = sampleReader.sampleExistingKeys(options.sampleStates);
    LayerEdgeProbeStats stats;
    stats.soldierCount = soldierCount;
    stats.sampledStates = static_cast<uint64_t>(samples.size());

    std::vector<uint64_t> sameLayerQueries;
    std::vector<uint64_t> captureQueries;
    sameLayerQueries.reserve(samples.size() * 8);
    captureQueries.reserve(samples.size());

    for (size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
        const uint64_t key = samples[sampleIndex];
        const Position pos = unpackPosition(key);
        if (popcount25(pos.soldiers) != soldierCount) {
            throw std::logic_error("sampled layer key has unexpected soldier count");
        }
        const std::vector<Move> moves = generateLegalMoves(pos);
        stats.generatedMoves += static_cast<uint64_t>(moves.size());
        for (const Move& move : moves) {
            const Position next = applyMove(pos, move);
            const int nextSoldiers = popcount25(next.soldiers);
            const uint64_t nextKey = packPosition(next);
            if (nextSoldiers == soldierCount) {
                sameLayerQueries.push_back(nextKey);
                ++stats.sameLayerGenerated;
                continue;
            }
            if (nextSoldiers == soldierCount - 1) {
                captureQueries.push_back(nextKey);
                ++stats.captureGenerated;
                continue;
            }
            throw std::logic_error("illegal soldier count transition during layer edge probe");
        }
        reportProgress(options, static_cast<uint64_t>(sampleIndex + 1), stats.generatedMoves, started);
    }

    const auto lookupStarted = Clock::now();
    if (!sameLayerQueries.empty()) {
        const BatchLookupStats sameStats =
            layerLookupReader.containsBatchStats(std::span<const uint64_t>(sameLayerQueries.data(), sameLayerQueries.size()));
        stats.sameLayerFound = sameStats.foundKeys;
        stats.sameLayerMissing = sameStats.missingKeys;
        stats.bucketLoads += sameStats.bucketLoads;
        stats.lookupKeys += sameStats.queryKeys;
        stats.bucketsTouched += sameStats.bucketsTouched;
    }
    if (!captureQueries.empty()) {
        if (!nextSeedReader) {
            throw std::logic_error("capture transitions require a next seed reader");
        }
        const BatchLookupStats captureStats =
            nextSeedReader->containsBatchStats(std::span<const uint64_t>(captureQueries.data(), captureQueries.size()));
        stats.captureFound = captureStats.foundKeys;
        stats.captureMissing = captureStats.missingKeys;
        stats.bucketLoads += captureStats.bucketLoads;
        stats.lookupKeys += captureStats.queryKeys;
        stats.bucketsTouched += captureStats.bucketsTouched;
    }
    stats.lookupSeconds = std::chrono::duration<double>(Clock::now() - lookupStarted).count();
    stats.totalSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    stats.statesPerSecond = stats.totalSeconds <= 0.0 ? 0.0 : static_cast<double>(stats.sampledStates) / stats.totalSeconds;
    stats.movesPerSecond = stats.totalSeconds <= 0.0 ? 0.0 : static_cast<double>(stats.generatedMoves) / stats.totalSeconds;
    stats.lookupsPerSecond = stats.lookupSeconds <= 0.0 ? 0.0 : static_cast<double>(stats.lookupKeys) / stats.lookupSeconds;
    return stats;
}

}  // namespace sanpao15
