#include "sanpao15/layered.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "sanpao15/bitboard.h"
#include "sanpao15/external_closure.h"
#include "sanpao15/external_keyset.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> LayerMagic{'S', '1', '5', 'L', 'Y', 'R', '1', '\0'};
constexpr std::array<char, 8> SeedMagic{'S', '1', '5', 'S', 'E', 'D', '1', '\0'};
constexpr uint32_t KeyListFileVersion = 1;
using Clock = std::chrono::steady_clock;

enum class KeyListKind {
    Layer,
    Seed,
};

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error("failed to write layer byte");
    }
}

void writeU32LE(std::ostream& output, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void writeU64LE(std::ostream& output, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

uint8_t readU8(std::istream& input) {
    const int value = input.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected end of layer file");
    }
    return static_cast<uint8_t>(value);
}

uint32_t readU32LE(std::istream& input) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(readU8(input)) << shift;
    }
    return value;
}

uint64_t readU64LE(std::istream& input) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(readU8(input)) << shift;
    }
    return value;
}

void requireSoldierCountRange(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

std::string layerFileName(int soldierCount) {
    requireSoldierCountRange(soldierCount);

    std::ostringstream out;
    out << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15layer";
    return out.str();
}

std::string seedFileName(int soldierCount) {
    requireSoldierCountRange(soldierCount);

    std::ostringstream out;
    out << "seeds-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15seed";
    return out.str();
}

const std::array<char, 8>& magicFor(KeyListKind kind) {
    return kind == KeyListKind::Layer ? LayerMagic : SeedMagic;
}

const char* kindName(KeyListKind kind) {
    return kind == KeyListKind::Layer ? "layer" : "seed";
}

void requireKeySoldierCount(uint64_t key, int soldierCount, KeyListKind kind) {
    const Position pos = unpackPosition(key);
    const int actualSoldiers = popcount25(pos.soldiers);
    if (actualSoldiers != soldierCount) {
        throw std::runtime_error(std::string(kindName(kind)) + " file key has unexpected soldier count");
    }
}

std::string jsonEscape(const std::string& text) {
    std::string escaped;
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
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string relativePathString(const std::filesystem::path& base, const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const auto relative = std::filesystem::relative(path, base, error);
    return error ? path.string() : relative.string();
}

LayerTruncationReason truncationReasonFromExternalStats(const ExternalClosureStats& stats) {
    if (stats.truncatedByMaxStates) {
        return LayerTruncationReason::MaxStatesPerLayer;
    }
    if (stats.truncatedByMaxExpandedStates) {
        return LayerTruncationReason::MaxExpandedStates;
    }
    if (stats.truncatedByMaxIterations) {
        return LayerTruncationReason::MaxIterations;
    }
    return LayerTruncationReason::None;
}

std::vector<uint64_t> sortedUniqueKeys(const std::vector<uint64_t>& keys, int soldierCount, KeyListKind kind) {
    std::vector<uint64_t> sortedKeys = keys;
    std::sort(sortedKeys.begin(), sortedKeys.end());
    sortedKeys.erase(std::unique(sortedKeys.begin(), sortedKeys.end()), sortedKeys.end());
    for (uint64_t key : sortedKeys) {
        requireKeySoldierCount(key, soldierCount, kind);
    }
    return sortedKeys;
}

void writeKeyListFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys,
    KeyListKind kind) {
    requireSoldierCountRange(soldierCount);
    const std::vector<uint64_t> sortedKeys = sortedUniqueKeys(keys, soldierCount, kind);

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open " + std::string(kindName(kind)) + " file for writing: " + path.string());
    }

    const auto& magic = magicFor(kind);
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    writeU32LE(output, KeyListFileVersion);
    writeU32LE(output, static_cast<uint32_t>(soldierCount));
    writeU64LE(output, static_cast<uint64_t>(sortedKeys.size()));
    writeU64LE(output, StandardRulesetHash);
    for (uint64_t key : sortedKeys) {
        writeU64LE(output, key);
    }
}

LayerFileData readKeyListFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount,
    KeyListKind kind) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open " + std::string(kindName(kind)) + " file for reading: " + path.string());
    }

    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != magicFor(kind)) {
        throw std::runtime_error("invalid " + std::string(kindName(kind)) + " file magic");
    }

    const uint32_t version = readU32LE(input);
    if (version != KeyListFileVersion) {
        throw std::runtime_error("unsupported " + std::string(kindName(kind)) + " file version");
    }

    const int soldierCount = static_cast<int>(readU32LE(input));
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::runtime_error(std::string(kindName(kind)) + " file has invalid soldier count");
    }
    if (expectedSoldierCount.has_value() && soldierCount != *expectedSoldierCount) {
        throw std::runtime_error(std::string(kindName(kind)) + " file soldier count does not match expected layer");
    }

    const uint64_t keyCount = readU64LE(input);
    const uint64_t rulesetHash = readU64LE(input);
    if (rulesetHash != StandardRulesetHash) {
        throw std::runtime_error(std::string(kindName(kind)) + " file ruleset hash does not match current rules");
    }

    LayerFileData data;
    data.soldierCount = soldierCount;
    data.keys.reserve(static_cast<size_t>(keyCount));
    uint64_t previous = 0;
    for (uint64_t i = 0; i < keyCount; ++i) {
        const uint64_t key = readU64LE(input);
        if (i > 0 && key <= previous) {
            throw std::runtime_error(std::string(kindName(kind)) + " file keys are not strictly ascending");
        }
        requireKeySoldierCount(key, soldierCount, kind);
        data.keys.push_back(key);
        previous = key;
    }

    return data;
}

KeyListFileSummary makeSummary(
    const std::filesystem::path& path,
    const LayerFileData& data,
    size_t sampleLimit) {
    KeyListFileSummary summary;
    summary.path = path;
    summary.soldierCount = data.soldierCount;
    summary.keyCount = static_cast<uint64_t>(data.keys.size());
    if (!data.keys.empty()) {
        summary.minKey = data.keys.front();
        summary.maxKey = data.keys.back();
        const size_t firstCount = std::min(sampleLimit, data.keys.size());
        summary.firstKeys.assign(data.keys.begin(), data.keys.begin() + static_cast<std::ptrdiff_t>(firstCount));
        const size_t lastCount = std::min(sampleLimit, data.keys.size());
        summary.lastKeys.assign(data.keys.end() - static_cast<std::ptrdiff_t>(lastCount), data.keys.end());
    }
    return summary;
}

void writeManifest(const LayeredBuildOptions& options, const LayeredBuildStats& stats) {
    const std::filesystem::path path = manifestFilePath(options.outputDir);
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open layer manifest for writing: " + path.string());
    }

    output << "{\n";
    output << "  \"rulesetHash\": " << StandardRulesetHash << ",\n";
    output << "  \"createdBy\": \"sanpao15_cli\",\n";
    output << "  \"format\": \"sanpao15-layer-manifest-v1\",\n";
    output << "  \"status\": \"" << (stats.truncated ? "truncated" : "complete") << "\",\n";
    output << "  \"resumed\": " << (stats.resumed ? "true" : "false") << ",\n";
    output << "  \"startLayer\": " << stats.startLayer << ",\n";
    output << "  \"stopAfterLayer\": " << stats.stopAfterLayer << ",\n";
    output << "  \"closureBackend\": \"" << layerClosureBackendToString(options.closureBackend) << "\",\n";
    output << "  \"externalSeedDedup\": " << (options.useExternalSeedDedup ? "true" : "false") << ",\n";
    output << "  \"totalLayerStates\": " << stats.totalLayerStates << ",\n";
    output << "  \"totalGeneratedSameLayerEdges\": " << stats.totalGeneratedSameLayerEdges << ",\n";
    output << "  \"totalGeneratedCaptureEdges\": " << stats.totalGeneratedCaptureEdges << ",\n";
    output << "  \"totalSeconds\": " << std::fixed << std::setprecision(6) << stats.totalSeconds << ",\n";
    output << "  \"layers\": [\n";
    for (int layer = 15; layer >= 0; --layer) {
        const LayerStats& item = stats.layers[layer];
        output << "    {\n";
        output << "      \"soldierCount\": " << item.soldierCount << ",\n";
        output << "      \"seedStates\": " << item.seedStates << ",\n";
        output << "      \"reachableStates\": " << item.reachableStates << ",\n";
        output << "      \"generatedSameLayerEdges\": " << item.generatedSameLayerEdges << ",\n";
        output << "      \"generatedCaptureEdges\": " << item.generatedCaptureEdges << ",\n";
        output << "      \"newNextLayerSeeds\": " << item.newNextLayerSeeds << ",\n";
        output << "      \"duplicateNextLayerSeeds\": " << item.duplicateNextLayerSeeds << ",\n";
        output << "      \"truncated\": " << (item.truncated ? "true" : "false") << ",\n";
        output << "      \"truncationReason\": \"" << layerTruncationReasonToString(item.truncationReason) << "\",\n";
        output << "      \"complete\": " << (item.complete ? "true" : "false") << ",\n";
        output << "      \"resumedClosure\": " << (item.resumedClosure ? "true" : "false") << ",\n";
        output << "      \"checkpointWritten\": " << (item.checkpointWritten ? "true" : "false") << ",\n";
        output << "      \"partitionedClosure\": " << (item.partitionedClosure ? "true" : "false") << ",\n";
        output << "      \"closurePartitionBuckets\": " << item.closurePartitionBuckets << ",\n";
        output << "      \"partitionedSnapshotsWritten\": " << item.partitionedSnapshotsWritten << ",\n";
        output << "      \"initialVisitedStates\": " << item.initialVisitedStates << ",\n";
        output << "      \"initialFrontierStates\": " << item.initialFrontierStates << ",\n";
        output << "      \"finalFrontierStates\": " << item.finalFrontierStates << ",\n";
        output << "      \"checkpointDir\": \"" << jsonEscape(item.checkpointDir) << "\",\n";
        output << "      \"skipped\": " << (item.skipped ? "true" : "false") << ",\n";
        output << "      \"closureBackend\": \"" << layerClosureBackendToString(item.closureBackend) << "\",\n";
        output << "      \"iterations\": " << item.iterations << ",\n";
        output << "      \"expandedStates\": " << item.expandedStates << ",\n";
        output << "      \"generatedCandidateKeys\": " << item.generatedCandidateKeys << ",\n";
        output << "      \"newFrontierStates\": " << item.newFrontierStates << ",\n";
        output << "      \"duplicateOrVisitedCandidates\": " << item.duplicateOrVisitedCandidates << ",\n";
        output << "      \"expansionSeconds\": " << std::fixed << std::setprecision(6) << item.expansionSeconds << ",\n";
        output << "      \"candidateDedupSeconds\": " << std::fixed << std::setprecision(6) << item.candidateDedupSeconds << ",\n";
        output << "      \"differenceSeconds\": " << std::fixed << std::setprecision(6) << item.differenceSeconds << ",\n";
        output << "      \"unionSeconds\": " << std::fixed << std::setprecision(6) << item.unionSeconds << ",\n";
        output << "      \"partitionSeconds\": " << std::fixed << std::setprecision(6) << item.partitionSeconds << ",\n";
        output << "      \"buildSeconds\": " << std::fixed << std::setprecision(6) << item.buildSeconds << ",\n";
        output << "      \"layerFile\": \"" << jsonEscape(item.layerFile) << "\",\n";
        output << "      \"seedFile\": \"" << jsonEscape(item.seedFile) << "\",\n";
        output << "      \"nextSeedFile\": \"" << jsonEscape(item.nextSeedFile) << "\",\n";
        output << "      \"externalSeedDedup\": " << (item.externalSeedDedup ? "true" : "false") << ",\n";
        output << "      \"externalChunksWritten\": " << item.externalSeedStats.chunksWritten << ",\n";
        output << "      \"externalTempBytesWritten\": " << item.externalSeedStats.tempBytesWritten << ",\n";
        output << "      \"externalChunkSortSeconds\": " << std::fixed << std::setprecision(6)
               << item.externalSeedStats.chunkSortSeconds << ",\n";
        output << "      \"externalMergeSeconds\": " << std::fixed << std::setprecision(6)
               << item.externalSeedStats.mergeSeconds << "\n";
        output << "    }" << (layer == 0 ? "\n" : ",\n");
    }
    output << "  ]\n";
    output << "}\n";
}

void reportLayerProgress(
    const LayeredBuildOptions& options,
    const LayerStats& layer,
    uint64_t visitedStates,
    uint64_t queueSize,
    bool complete,
    double elapsedSeconds) {
    if (!options.progress) {
        return;
    }
    if (layer.closureBackend == LayerClosureBackend::InMemory &&
        !complete && options.progressInterval != 0 && visitedStates != 0 &&
        visitedStates % options.progressInterval != 0) {
        return;
    }

    LayeredProgressInfo info;
    info.soldierCount = layer.soldierCount;
    info.closureBackend = layer.closureBackend;
    info.iteration = layer.iterations;
    info.seedStates = layer.seedStates;
    info.visitedStates = visitedStates;
    info.expandedStates = layer.expandedStates;
    info.queueSize = queueSize;
    info.candidateStates = layer.generatedCandidateKeys;
    info.nextFrontierStates = layer.newFrontierStates;
    info.generatedSameLayerEdges = layer.generatedSameLayerEdges;
    info.generatedCaptureEdges = layer.generatedCaptureEdges;
    info.newNextLayerSeeds = layer.newNextLayerSeeds;
    info.duplicateNextLayerSeeds = layer.duplicateNextLayerSeeds;
    info.complete = complete;
    info.truncated = layer.truncated;
    info.elapsedSeconds = elapsedSeconds;
    options.progress(info);
}

void copyExternalClosureStatsToLayer(
    LayerStats& layer,
    const ExternalClosureStats& closureStats) {
    layer.seedStates = closureStats.seedStates;
    layer.reachableStates = closureStats.finalStates;
    layer.generatedSameLayerEdges = closureStats.generatedSameLayerEdges;
    layer.generatedCaptureEdges = closureStats.generatedCaptureEdges;
    layer.newNextLayerSeeds = closureStats.nextSeedStates;
    layer.duplicateNextLayerSeeds = closureStats.duplicateNextSeeds;
    layer.truncated = closureStats.truncated;
    layer.truncationReason = truncationReasonFromExternalStats(closureStats);
    layer.iterations = closureStats.iterations;
    layer.expandedStates = closureStats.expandedStates;
    layer.generatedCandidateKeys = closureStats.generatedCandidateKeys;
    layer.newFrontierStates = closureStats.newFrontierStates;
    layer.duplicateOrVisitedCandidates = closureStats.duplicateOrVisitedCandidates;
    layer.resumedClosure = closureStats.resumed;
    layer.complete = closureStats.complete;
    layer.checkpointWritten = closureStats.checkpointWritten;
    layer.partitionedClosure = closureStats.partitionedClosure;
    layer.closurePartitionBuckets = closureStats.closurePartitionBuckets;
    layer.partitionedSnapshotsWritten = closureStats.partitionedSnapshotsWritten;
    layer.initialVisitedStates = closureStats.initialVisitedStates;
    layer.initialFrontierStates = closureStats.initialFrontierStates;
    layer.finalFrontierStates = closureStats.finalFrontierStates;
    layer.checkpointDir = closureStats.checkpointDir;
    layer.expansionSeconds = closureStats.expansionSeconds;
    layer.candidateDedupSeconds = closureStats.candidateDedupSeconds;
    layer.differenceSeconds = closureStats.differenceSeconds;
    layer.unionSeconds = closureStats.unionSeconds;
    layer.partitionSeconds = closureStats.partitionSeconds;
    layer.buildSeconds = closureStats.totalSeconds;
}

}  // namespace

std::filesystem::path layerFilePath(const std::filesystem::path& outputDir, int soldierCount) {
    return outputDir / layerFileName(soldierCount);
}

std::filesystem::path seedFilePath(const std::filesystem::path& outputDir, int soldierCount) {
    return outputDir / seedFileName(soldierCount);
}

std::filesystem::path manifestFilePath(const std::filesystem::path& outputDir) {
    return outputDir / "manifest.json";
}

const char* layerClosureBackendToString(LayerClosureBackend backend) {
    switch (backend) {
        case LayerClosureBackend::InMemory:
            return "memory";
        case LayerClosureBackend::External:
            return "external";
    }
    return "memory";
}

const char* layerTruncationReasonToString(LayerTruncationReason reason) {
    switch (reason) {
        case LayerTruncationReason::None:
            return "none";
        case LayerTruncationReason::MaxStatesPerLayer:
            return "max-states";
        case LayerTruncationReason::MaxExpandedStates:
            return "max-expanded";
        case LayerTruncationReason::MaxIterations:
            return "max-iterations";
    }
    return "none";
}

void writeLayerFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys) {
    writeKeyListFile(path, soldierCount, keys, KeyListKind::Layer);
}

void writeSeedFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys) {
    writeKeyListFile(path, soldierCount, keys, KeyListKind::Seed);
}

LayerFileData readLayerFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount) {
    return readKeyListFile(path, expectedSoldierCount, KeyListKind::Layer);
}

SeedFileData readSeedFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount) {
    return readKeyListFile(path, expectedSoldierCount, KeyListKind::Seed);
}

KeyListFileSummary validateLayerFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount) {
    return inspectLayerFile(path, expectedSoldierCount, 0);
}

KeyListFileSummary validateSeedFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount) {
    return inspectSeedFile(path, expectedSoldierCount, 0);
}

KeyListFileSummary inspectLayerFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount,
    size_t sampleLimit) {
    return makeSummary(path, readLayerFile(path, expectedSoldierCount), sampleLimit);
}

KeyListFileSummary inspectSeedFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount,
    size_t sampleLimit) {
    return makeSummary(path, readSeedFile(path, expectedSoldierCount), sampleLimit);
}

LayeredBuildStats buildReachableLayers(const LayeredBuildOptions& options) {
    return buildReachableLayersFromSeed(initialPosition(), options);
}

LayeredBuildStats buildReachableLayersFromSeed(const Position& initial, const LayeredBuildOptions& options) {
    if (options.outputDir.empty()) {
        throw std::invalid_argument("layered build outputDir is required");
    }
    const int initialSoldiers = popcount25(initial.soldiers);
    requireSoldierCountRange(initialSoldiers);
    const int startLayer = options.startLayer.value_or(initialSoldiers);
    const int stopAfterLayer = options.stopAfterLayer.value_or(0);
    requireSoldierCountRange(startLayer);
    requireSoldierCountRange(stopAfterLayer);
    if (startLayer < stopAfterLayer) {
        throw std::invalid_argument("start layer must be greater than or equal to stop-after layer");
    }

    const auto totalStarted = Clock::now();
    std::filesystem::create_directories(options.outputDir);

    LayeredBuildStats result;
    result.resumed = options.startLayer.has_value();
    result.startLayer = startLayer;
    result.stopAfterLayer = stopAfterLayer;
    for (int k = 0; k <= 15; ++k) {
        result.layers[k].soldierCount = k;
        result.layers[k].closureBackend = options.closureBackend;
        result.layers[k].seedFile = relativePathString(options.outputDir, seedFilePath(options.outputDir, k));
        result.layers[k].layerFile = relativePathString(options.outputDir, layerFilePath(options.outputDir, k));
        if (k > 0) {
            result.layers[k].nextSeedFile = relativePathString(options.outputDir, seedFilePath(options.outputDir, k - 1));
        }
    }

    if (!options.startLayer.has_value()) {
        writeSeedFile(seedFilePath(options.outputDir, startLayer), startLayer, {packPosition(initial)});
    } else if (!std::filesystem::exists(seedFilePath(options.outputDir, startLayer))) {
        throw std::runtime_error("seed file is required for resumed layered build: " +
                                 seedFilePath(options.outputDir, startLayer).string());
    }

    for (int skippedLayer = 15; skippedLayer > startLayer; --skippedLayer) {
        result.layers[skippedLayer].skipped = true;
    }
    for (int skippedLayer = stopAfterLayer - 1; skippedLayer >= 0; --skippedLayer) {
        result.layers[skippedLayer].skipped = true;
    }

    for (int soldierCount = startLayer; soldierCount >= stopAfterLayer; --soldierCount) {
        LayerStats& layer = result.layers[soldierCount];
        const SeedFileData seeds = readSeedFile(seedFilePath(options.outputDir, soldierCount), soldierCount);
        layer.seedStates = static_cast<uint64_t>(seeds.keys.size());
        if (layer.seedStates == 0) {
            writeLayerFile(layerFilePath(options.outputDir, soldierCount), soldierCount, {});
            if (soldierCount > 0) {
                writeSeedFile(seedFilePath(options.outputDir, soldierCount - 1), soldierCount - 1, {});
            }
            continue;
        }

        if (options.closureBackend == LayerClosureBackend::External) {
            ExternalClosureOptions closureOptions;
            closureOptions.workDir = options.tempDir.empty()
                                         ? (options.outputDir / "work" / ("layer-" + std::to_string(soldierCount)))
                                         : (options.tempDir / ("layer-" + std::to_string(soldierCount)));
            closureOptions.seedFile = seedFilePath(options.outputDir, soldierCount);
            closureOptions.outputLayerFile = layerFilePath(options.outputDir, soldierCount);
            if (soldierCount > 0) {
                closureOptions.outputNextSeedFile = seedFilePath(options.outputDir, soldierCount - 1);
            }
            closureOptions.soldierCount = soldierCount;
            closureOptions.chunkKeyLimit = options.externalChunkKeyLimit;
            closureOptions.progressInterval = options.progressInterval;
            closureOptions.checkpointInterval = options.checkpointInterval;
            closureOptions.maxIterations = options.maxIterations;
            closureOptions.maxExpandedStates = options.maxExpandedStates;
            closureOptions.maxDiscoveredStates = options.maxStatesPerLayer;
            closureOptions.keepTempFiles = options.keepTempFiles;
            closureOptions.resume = options.resumeClosure;
            closureOptions.partitionedClosure = options.partitionedClosure;
            closureOptions.closurePartitionBuckets = options.closurePartitionBuckets;
            closureOptions.closurePartitionMethod = options.closurePartitionMethod;
            if (options.progress) {
                closureOptions.progress = [&](const ExternalClosureProgressInfo& info) {
                    LayerStats progressLayer = layer;
                    progressLayer.reachableStates = info.visitedStates;
                    progressLayer.expandedStates = info.expandedStates;
                    progressLayer.iterations = info.iteration;
                    progressLayer.generatedSameLayerEdges = info.generatedSameLayerEdges;
                    progressLayer.generatedCaptureEdges = info.generatedCaptureEdges;
                    progressLayer.generatedCandidateKeys = info.candidateStates;
                    progressLayer.newFrontierStates = info.nextFrontierStates;
                    progressLayer.newNextLayerSeeds = info.nextSeedStates;
                    progressLayer.duplicateNextLayerSeeds = info.duplicateNextSeeds;
                    progressLayer.truncated = info.truncated;
                    reportLayerProgress(
                        options,
                        progressLayer,
                        info.visitedStates,
                        info.frontierStates,
                        info.complete,
                        info.elapsedSeconds);
                };
            }

            const ExternalClosureStats closureStats = buildLayerClosureExternal(closureOptions);
            copyExternalClosureStatsToLayer(layer, closureStats);
            layer.closureBackend = LayerClosureBackend::External;
            layer.externalSeedDedup = options.useExternalSeedDedup && soldierCount > 0;
            result.totalLayerStates += layer.reachableStates;
            result.totalGeneratedSameLayerEdges += layer.generatedSameLayerEdges;
            result.totalGeneratedCaptureEdges += layer.generatedCaptureEdges;
            if (layer.truncated) {
                result.truncated = true;
                for (int skippedLayer = soldierCount - 1; skippedLayer >= 0; --skippedLayer) {
                    if (skippedLayer == soldierCount - 1) {
                        result.layers[skippedLayer].seedStates = layer.newNextLayerSeeds;
                    }
                    result.layers[skippedLayer].skipped = true;
                    result.layers[skippedLayer].closureBackend = options.closureBackend;
                }
                break;
            }
            continue;
        }

        const auto layerStarted = Clock::now();
        std::unordered_set<uint64_t> visited;
        std::queue<uint64_t> pending;
        std::unordered_set<uint64_t> nextLayerSeeds;
        std::unique_ptr<ExternalKeySetBuilder> externalSeeds;
        if (options.useExternalSeedDedup && soldierCount > 0) {
            ExternalKeySetOptions keyOptions;
            keyOptions.tempDir = options.tempDir.empty()
                                     ? (options.outputDir / "tmp" / ("layer-" + std::to_string(soldierCount)))
                                     : (options.tempDir / ("layer-" + std::to_string(soldierCount)));
            keyOptions.chunkKeyLimit = options.externalChunkKeyLimit;
            keyOptions.keepTempFiles = options.keepTempFiles;
            externalSeeds = std::make_unique<ExternalKeySetBuilder>(keyOptions);
            layer.externalSeedDedup = true;
        }
        for (uint64_t seed : seeds.keys) {
            pending.push(seed);
        }

        reportLayerProgress(options, layer, 0, pending.size(), false, 0.0);

        while (!pending.empty()) {
            const uint64_t key = pending.front();
            pending.pop();
            if (visited.find(key) != visited.end()) {
                continue;
            }

            const Position pos = unpackPosition(key);
            const int keySoldiers = popcount25(pos.soldiers);
            if (keySoldiers != soldierCount) {
                throw std::logic_error("layer queue contains a state with the wrong soldier count");
            }

            if (options.maxStatesPerLayer != 0 && visited.size() >= options.maxStatesPerLayer) {
                layer.truncated = true;
                layer.truncationReason = LayerTruncationReason::MaxStatesPerLayer;
                result.truncated = true;
                break;
            }

            visited.insert(key);
            if (isTerminal(pos)) {
                const auto now = Clock::now();
                reportLayerProgress(
                    options,
                    layer,
                    static_cast<uint64_t>(visited.size()),
                    pending.size(),
                    false,
                    std::chrono::duration<double>(now - layerStarted).count());
                continue;
            }

            const std::vector<Move> moves = generateLegalMoves(pos);
            for (const Move& move : moves) {
                const Position next = applyMove(pos, move);
                const int nextSoldiers = popcount25(next.soldiers);
                const uint64_t nextKey = packPosition(next);
                if (nextSoldiers == soldierCount) {
                    ++layer.generatedSameLayerEdges;
                    if (visited.find(nextKey) == visited.end()) {
                        pending.push(nextKey);
                    }
                    continue;
                }

                if (nextSoldiers == soldierCount - 1) {
                    ++layer.generatedCaptureEdges;
                    if (soldierCount == 0) {
                        throw std::logic_error("0-soldier layer generated a capture transition");
                    }
                    if (externalSeeds) {
                        externalSeeds->add(nextKey);
                    } else {
                        const auto [_, inserted] = nextLayerSeeds.insert(nextKey);
                        if (inserted) {
                            ++layer.newNextLayerSeeds;
                        } else {
                            ++layer.duplicateNextLayerSeeds;
                        }
                    }
                    continue;
                }

                throw std::logic_error("illegal soldier count transition during layered build");
            }

            const auto now = Clock::now();
            reportLayerProgress(
                options,
                layer,
                static_cast<uint64_t>(visited.size()),
                pending.size(),
                false,
                std::chrono::duration<double>(now - layerStarted).count());
        }

        std::vector<uint64_t> keys(visited.begin(), visited.end());
        std::sort(keys.begin(), keys.end());
        layer.reachableStates = static_cast<uint64_t>(keys.size());
        writeLayerFile(layerFilePath(options.outputDir, soldierCount), soldierCount, keys);
        if (soldierCount > 0) {
            std::vector<uint64_t> nextSeedKeys;
            if (externalSeeds) {
                externalSeeds->finishToStream([&](uint64_t key) { nextSeedKeys.push_back(key); });
                layer.externalSeedStats = externalSeeds->stats();
                layer.newNextLayerSeeds = layer.externalSeedStats.uniqueKeys;
                layer.duplicateNextLayerSeeds = layer.externalSeedStats.duplicateKeys;
            } else {
                nextSeedKeys.assign(nextLayerSeeds.begin(), nextLayerSeeds.end());
            }
            writeSeedFile(seedFilePath(options.outputDir, soldierCount - 1), soldierCount - 1, nextSeedKeys);
        }

        layer.buildSeconds = std::chrono::duration<double>(Clock::now() - layerStarted).count();
        reportLayerProgress(options, layer, layer.reachableStates, pending.size(), true, layer.buildSeconds);

        result.totalLayerStates += layer.reachableStates;
        result.totalGeneratedSameLayerEdges += layer.generatedSameLayerEdges;
        result.totalGeneratedCaptureEdges += layer.generatedCaptureEdges;
        if (layer.truncated) {
            for (int skippedLayer = soldierCount - 1; skippedLayer >= 0; --skippedLayer) {
                if (skippedLayer == soldierCount - 1) {
                    result.layers[skippedLayer].seedStates = layer.newNextLayerSeeds;
                }
                result.layers[skippedLayer].skipped = true;
            }
            break;
        }
    }

    result.totalSeconds = std::chrono::duration<double>(Clock::now() - totalStarted).count();
    writeManifest(options, result);
    return result;
}

}  // namespace sanpao15
