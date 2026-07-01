#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

namespace sanpao15 {

struct LayerEdgeProbeProgress {
    uint64_t sampledStates = 0;
    uint64_t generatedMoves = 0;
    double elapsedSeconds = 0.0;
};

using LayerEdgeProbeProgressCallback = std::function<void(const LayerEdgeProbeProgress& info)>;

struct LayerEdgeProbeOptions {
    std::filesystem::path layerPartitionDir;
    std::filesystem::path nextSeedPartitionDir;
    uint64_t sampleStates = 100000;
    uint64_t progressInterval = 0;
    uint32_t cacheBuckets = 32;
    LayerEdgeProbeProgressCallback progress;
};

struct LayerEdgeProbeStats {
    int soldierCount = 0;
    uint64_t sampledStates = 0;

    uint64_t generatedMoves = 0;
    uint64_t sameLayerGenerated = 0;
    uint64_t sameLayerFound = 0;
    uint64_t sameLayerMissing = 0;

    uint64_t captureGenerated = 0;
    uint64_t captureFound = 0;
    uint64_t captureMissing = 0;

    uint64_t lookupKeys = 0;
    uint64_t bucketsTouched = 0;
    uint64_t bucketLoads = 0;

    double totalSeconds = 0.0;
    double lookupSeconds = 0.0;
    double statesPerSecond = 0.0;
    double movesPerSecond = 0.0;
    double lookupsPerSecond = 0.0;
};

LayerEdgeProbeStats probeLayerEdges(const LayerEdgeProbeOptions& options);

}  // namespace sanpao15
