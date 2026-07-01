#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/edge_probe.h"
#include "sanpao15/external_closure.h"
#include "sanpao15/layered.h"
#include "sanpao15/notation.h"
#include "sanpao15/partitioned_keyset.h"
#include "sanpao15/rules.h"
#include "sanpao15/solver.h"

namespace {

using namespace sanpao15;

constexpr uint64_t MvpStateLimit = 100000;

struct CliOptions {
    bool help = false;
    bool full = false;
    bool limitProvided = false;
    bool statsOnly = false;
    bool noPred = false;
    bool probe = false;
    bool maxStatesPerLayerProvided = false;
    bool startLayerProvided = false;
    bool stopAfterLayerProvided = false;
    bool buildLayerExternalProvided = false;
    bool externalLayerClosure = false;
    bool externalSeedDedup = false;
    bool dedupChunkSizeProvided = false;
    bool keepTemp = false;
    bool resumeClosure = false;
    bool checkpointIntervalProvided = false;
    bool partitionBucketsProvided = false;
    bool partitionMethodProvided = false;
    bool partitionCacheBucketsProvided = false;
    bool benchmarkModeProvided = false;
    bool sampleStatesProvided = false;
    bool partitionedClosure = false;
    bool closurePartitionBucketsProvided = false;
    bool closurePartitionMethodProvided = false;
    bool cleanupStaleRuns = false;
    bool repairClosureCheckpoint = false;
    bool repairDryRun = false;
    bool repairLayerProvided = false;
    bool partitionOverwrite = false;
    bool benchmarkSampleProvided = false;
    bool lookupKeyProvided = false;
    uint64_t maxStates = MvpStateLimit;
    uint64_t maxStatesPerLayer = 0;
    uint64_t dedupChunkSize = 1000000;
    uint64_t checkpointInterval = 0;
    uint64_t maxIterations = 0;
    uint64_t maxExpandedStates = 0;
    uint64_t partitionBuckets = 256;
    uint64_t closurePartitionBuckets = 256;
    uint64_t partitionCacheBuckets = 32;
    uint64_t benchmarkSample = 100000;
    uint64_t sampleStates = 100000;
    uint64_t lookupKey = 0;
    int startLayer = 15;
    int stopAfterLayer = 0;
    int buildLayerExternal = 0;
    int repairLayer = 15;
    GraphBackend graphBackend = GraphBackend::Csr;
    PartitionMethod partitionMethod = PartitionMethod::Splitmix64Mod;
    PartitionMethod closurePartitionMethod = PartitionMethod::Splitmix64Mod;
    PartitionBenchmarkMode benchmarkMode = PartitionBenchmarkMode::Existing;
    uint64_t progressInterval = 0;
    std::optional<std::filesystem::path> buildLayersDir;
    std::optional<std::filesystem::path> layerWorkDir;
    std::optional<std::filesystem::path> seedFile;
    std::optional<std::filesystem::path> outputLayerFile;
    std::optional<std::filesystem::path> outputNextSeedFile;
    std::optional<std::filesystem::path> tempDir;
    std::optional<std::filesystem::path> validateLayerPath;
    std::optional<std::filesystem::path> validateSeedPath;
    std::optional<std::filesystem::path> validateLayersDir;
    std::optional<std::filesystem::path> inspectLayerPath;
    std::optional<std::filesystem::path> inspectSeedPath;
    std::optional<std::filesystem::path> partitionKeysetInput;
    std::optional<std::filesystem::path> partitionOutputDir;
    std::optional<std::filesystem::path> validatePartitionDir;
    std::optional<std::filesystem::path> inspectPartitionDir;
    std::optional<std::filesystem::path> lookupPartitionDir;
    std::optional<std::filesystem::path> benchmarkPartitionDir;
    std::optional<std::filesystem::path> probeLayerEdgesDir;
    std::optional<std::filesystem::path> nextSeedPartitionDir;
    std::optional<std::filesystem::path> repairClosureCheckpointDir;
    std::optional<std::string> analysisNotation;
    std::optional<std::filesystem::path> saveTablePath;
    std::optional<std::filesystem::path> loadTablePath;
};

void printUsage() {
    std::cout
        << "Sanpao15 Solver\n\n"
        << "Usage:\n"
        << "  sanpao15_cli [--limit N] [--save-table FILE] [--progress N]\n"
        << "  sanpao15_cli --full [--save-table FILE] [--progress N]\n"
        << "  sanpao15_cli --stats-only [--no-pred] [--limit N] [--progress N]\n"
        << "  sanpao15_cli --probe [--no-pred] [--limit N] [--progress N]\n"
        << "  sanpao15_cli --build-layers DIR [--start-layer K] [--stop-after-layer K]\n"
        << "                  [--max-states-per-layer N] [--progress N]\n"
        << "  sanpao15_cli --build-layer-external K --layer-work-dir DIR --seed-file FILE\n"
        << "                  --output-layer FILE [--output-next-seed FILE]\n"
        << "  sanpao15_cli --validate-layers DIR\n"
        << "  sanpao15_cli --repair-closure-checkpoint DIR --layer K [--dry-run]\n"
        << "  sanpao15_cli --validate-layer FILE | --validate-seed FILE\n"
        << "  sanpao15_cli --inspect-layer FILE | --inspect-seed FILE\n"
        << "  sanpao15_cli --partition-keyset INPUT --partition-output DIR [--partition-buckets N]\n"
        << "                  [--partition-method splitmix64_mod|key_mod]\n"
        << "  sanpao15_cli --validate-partition DIR | --inspect-partition DIR\n"
        << "  sanpao15_cli --lookup-partition DIR --key KEY\n"
        << "  sanpao15_cli --benchmark-partition DIR [--sample N] [--benchmark-mode MODE]\n"
        << "  sanpao15_cli --probe-layer-edges PARTITION_DIR --next-seed-partition DIR [--sample-states N]\n"
        << "  sanpao15_cli --analyze \"SSSSS/SSSSS/SSSSS/...../.CCC. c\" [--limit N|--full]\n"
        << "  sanpao15_cli --load-table FILE --analyze \"SSSSS/SSSSS/SSSSS/...../.CCC. c\"\n\n"
        << "Options:\n"
        << "  --full          Build the complete reachable graph with no state cap.\n"
        << "  --limit N       Stop graph generation after N states. Use 0 for no cap.\n"
        << "  --analyze TEXT  Analyze a notation string instead of only the initial position.\n"
        << "  --save-table FILE  Save the solver result as a .s15tbl file.\n"
        << "  --load-table FILE  Load a .s15tbl file for table lookup analysis.\n"
        << "  --progress N    Print progress every N processed states or entries.\n"
        << "  --stats-only    Build the reachable graph and print scale statistics only.\n"
        << "  --no-pred       Do not store predecessor edges in stats-only/probe mode.\n"
        << "  --probe         Run stats-only at increasing limits.\n"
        << "  --graph MODE    Select graph backend: csr or vector. Default: csr.\n"
        << "  --build-layers DIR  Build soldier-count layer state files into DIR.\n"
        << "  --max-states-per-layer N  Cap each layer during --build-layers. Use 0 for no cap.\n"
        << "  --start-layer K  Resume --build-layers from seeds-K.s15seed.\n"
        << "  --stop-after-layer K  Stop after building layer K.\n"
        << "  --external-layer-closure  Use external frontier closure during --build-layers.\n"
        << "  --partitioned-closure  Also write partitioned closure checkpoint snapshots during external closure.\n"
        << "  --closure-partition-buckets N  Buckets for --partitioned-closure. Default: 256.\n"
        << "  --closure-partition-method M  Method for --partitioned-closure: splitmix64_mod or key_mod.\n"
        << "  --resume-closure  Resume external closure from work/layer-K checkpoint; requires --build-layers, --external-layer-closure, and --start-layer.\n"
        << "  --checkpoint-interval N  Write external closure checkpoint every N expanded states. 0 means final/truncation only.\n"
        << "  --build-layer-external K  Build one layer with external frontier closure.\n"
        << "  --layer-work-dir DIR  Work directory for --build-layer-external.\n"
        << "  --seed-file FILE  Input .s15seed for --build-layer-external.\n"
        << "  --output-layer FILE  Output .s15layer for --build-layer-external.\n"
        << "  --output-next-seed FILE  Output next .s15seed for --build-layer-external.\n"
        << "  --max-iterations N  Cap external closure iterations. Use 0 for no cap.\n"
        << "  --max-expanded-states N  Cap external closure expanded states. Use 0 for no cap.\n"
        << "  --validate-layer FILE  Validate one .s15layer file.\n"
        << "  --validate-seed FILE  Validate one .s15seed file.\n"
        << "  --validate-layers DIR  Validate present layer/seed files in DIR.\n"
        << "  --repair-closure-checkpoint DIR  Validate and rewrite closure checkpoint metadata for DIR/work/layer-K.\n"
        << "  --cleanup-stale-runs  Remove stale transient runs during closure checkpoint repair.\n"
        << "  --layer K       Layer index for --repair-closure-checkpoint.\n"
        << "  --dry-run       Validate repair target without writing changes.\n"
        << "  --inspect-layer FILE  Show summary for one .s15layer file.\n"
        << "  --inspect-seed FILE  Show summary for one .s15seed file.\n"
        << "  --partition-keyset FILE  Build partitioned index from .s15layer/.s15seed/.s15keys.\n"
        << "  --partition-output DIR  Output directory for partitioned index.\n"
        << "  --partition-buckets N  Number of partition buckets. Default: 256.\n"
        << "  --partition-method M  Partition method for new indexes: splitmix64_mod or key_mod. Default: splitmix64_mod.\n"
        << "  --partition-cache-buckets N  Reader bucket cache size for lookup/benchmark/probe. Default: 32.\n"
        << "  --overwrite     Replace existing partition output directory.\n"
        << "  --validate-partition DIR  Validate a partitioned keyset directory.\n"
        << "  --inspect-partition DIR  Show partition distribution and file sizes.\n"
        << "  --lookup-partition DIR  Lookup --key in a partitioned keyset.\n"
        << "  --key N         Key for --lookup-partition.\n"
        << "  --benchmark-partition DIR  Benchmark partition contains() lookups.\n"
        << "  --benchmark-mode MODE  existing, missing, or mixed. Default: existing.\n"
        << "  --sample N      Sample count for --benchmark-partition. Default: 100000.\n"
        << "  --probe-layer-edges DIR  Probe generated edge membership from a layer partition.\n"
        << "  --next-seed-partition DIR  Next lower seed partition for capture edge membership.\n"
        << "  --sample-states N  Sample count for --probe-layer-edges. Default: 100000.\n"
        << "  --external-seed-dedup  Use external sorted runs for next-layer seed dedup.\n"
        << "  --dedup-chunk-size N  Keys per external dedup chunk. Default: 1000000.\n"
        << "  --temp-dir DIR  Temporary run-file directory for external dedup.\n"
        << "  --keep-temp     Keep external dedup .s15run temp files.\n"
        << "  --help          Show this help text.\n";
}

uint64_t parseLimit(const std::string& text) {
    size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if (consumed != text.size()) {
        throw std::invalid_argument("limit must be an unsigned integer");
    }
    return static_cast<uint64_t>(value);
}

int parseLayerIndex(const std::string& text) {
    const uint64_t value = parseLimit(text);
    if (value > 15) {
        throw std::invalid_argument("layer index must be in 0..15");
    }
    return static_cast<int>(value);
}

GraphBackend parseGraphBackend(const std::string& text) {
    if (text == "csr") {
        return GraphBackend::Csr;
    }
    if (text == "vector") {
        return GraphBackend::Vector;
    }
    throw std::invalid_argument("--graph must be either csr or vector");
}

const char* graphBackendToString(GraphBackend backend) {
    return backend == GraphBackend::Csr ? "csr" : "vector";
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--full") {
            options.full = true;
            options.maxStates = 0;
        } else if (arg == "--limit") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--limit requires a value");
            }
            options.maxStates = parseLimit(argv[++i]);
            options.limitProvided = true;
            options.full = options.maxStates == 0;
        } else if (arg == "--analyze") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--analyze requires a notation string");
            }
            options.analysisNotation = argv[++i];
        } else if (arg == "--save-table") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--save-table requires a file path");
            }
            options.saveTablePath = std::filesystem::path(argv[++i]);
        } else if (arg == "--load-table") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--load-table requires a file path");
            }
            options.loadTablePath = std::filesystem::path(argv[++i]);
        } else if (arg == "--progress") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--progress requires a value");
            }
            options.progressInterval = parseLimit(argv[++i]);
        } else if (arg == "--stats-only") {
            options.statsOnly = true;
        } else if (arg == "--no-pred") {
            options.noPred = true;
        } else if (arg == "--probe") {
            options.probe = true;
        } else if (arg == "--graph") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--graph requires a value");
            }
            options.graphBackend = parseGraphBackend(argv[++i]);
        } else if (arg == "--build-layers") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--build-layers requires a directory path");
            }
            options.buildLayersDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--max-states-per-layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--max-states-per-layer requires a value");
            }
            options.maxStatesPerLayer = parseLimit(argv[++i]);
            options.maxStatesPerLayerProvided = true;
        } else if (arg == "--start-layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--start-layer requires a value");
            }
            options.startLayer = parseLayerIndex(argv[++i]);
            options.startLayerProvided = true;
        } else if (arg == "--stop-after-layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--stop-after-layer requires a value");
            }
            options.stopAfterLayer = parseLayerIndex(argv[++i]);
            options.stopAfterLayerProvided = true;
        } else if (arg == "--build-layer-external") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--build-layer-external requires a layer index");
            }
            options.buildLayerExternal = parseLayerIndex(argv[++i]);
            options.buildLayerExternalProvided = true;
        } else if (arg == "--external-layer-closure") {
            options.externalLayerClosure = true;
        } else if (arg == "--partitioned-closure") {
            options.partitionedClosure = true;
        } else if (arg == "--closure-partition-buckets") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--closure-partition-buckets requires a value");
            }
            options.closurePartitionBuckets = parseLimit(argv[++i]);
            options.closurePartitionBucketsProvided = true;
        } else if (arg == "--closure-partition-method") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--closure-partition-method requires a value");
            }
            options.closurePartitionMethod = partitionMethodFromString(argv[++i]);
            options.closurePartitionMethodProvided = true;
        } else if (arg == "--resume-closure") {
            options.resumeClosure = true;
        } else if (arg == "--checkpoint-interval") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--checkpoint-interval requires a value");
            }
            options.checkpointInterval = parseLimit(argv[++i]);
            options.checkpointIntervalProvided = true;
        } else if (arg == "--layer-work-dir") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--layer-work-dir requires a directory path");
            }
            options.layerWorkDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--seed-file") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--seed-file requires a file path");
            }
            options.seedFile = std::filesystem::path(argv[++i]);
        } else if (arg == "--output-layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--output-layer requires a file path");
            }
            options.outputLayerFile = std::filesystem::path(argv[++i]);
        } else if (arg == "--output-next-seed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--output-next-seed requires a file path");
            }
            options.outputNextSeedFile = std::filesystem::path(argv[++i]);
        } else if (arg == "--max-iterations") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--max-iterations requires a value");
            }
            options.maxIterations = parseLimit(argv[++i]);
        } else if (arg == "--max-expanded-states") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--max-expanded-states requires a value");
            }
            options.maxExpandedStates = parseLimit(argv[++i]);
        } else if (arg == "--validate-layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--validate-layer requires a file path");
            }
            options.validateLayerPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--validate-seed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--validate-seed requires a file path");
            }
            options.validateSeedPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--validate-layers") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--validate-layers requires a directory path");
            }
            options.validateLayersDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--repair-closure-checkpoint") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--repair-closure-checkpoint requires a directory path");
            }
            options.repairClosureCheckpointDir = std::filesystem::path(argv[++i]);
            options.repairClosureCheckpoint = true;
        } else if (arg == "--cleanup-stale-runs") {
            options.cleanupStaleRuns = true;
        } else if (arg == "--dry-run") {
            options.repairDryRun = true;
        } else if (arg == "--layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--layer requires a value");
            }
            options.repairLayer = parseLayerIndex(argv[++i]);
            options.repairLayerProvided = true;
        } else if (arg == "--inspect-layer") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--inspect-layer requires a file path");
            }
            options.inspectLayerPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--inspect-seed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--inspect-seed requires a file path");
            }
            options.inspectSeedPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--partition-keyset") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--partition-keyset requires a file path");
            }
            options.partitionKeysetInput = std::filesystem::path(argv[++i]);
        } else if (arg == "--partition-output") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--partition-output requires a directory path");
            }
            options.partitionOutputDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--partition-buckets") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--partition-buckets requires a value");
            }
            options.partitionBuckets = parseLimit(argv[++i]);
            options.partitionBucketsProvided = true;
        } else if (arg == "--partition-method") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--partition-method requires a value");
            }
            options.partitionMethod = partitionMethodFromString(argv[++i]);
            options.partitionMethodProvided = true;
        } else if (arg == "--partition-cache-buckets") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--partition-cache-buckets requires a value");
            }
            options.partitionCacheBuckets = parseLimit(argv[++i]);
            options.partitionCacheBucketsProvided = true;
        } else if (arg == "--overwrite") {
            options.partitionOverwrite = true;
        } else if (arg == "--validate-partition") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--validate-partition requires a directory path");
            }
            options.validatePartitionDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--inspect-partition") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--inspect-partition requires a directory path");
            }
            options.inspectPartitionDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--lookup-partition") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--lookup-partition requires a directory path");
            }
            options.lookupPartitionDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--key") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--key requires a value");
            }
            options.lookupKey = parseLimit(argv[++i]);
            options.lookupKeyProvided = true;
        } else if (arg == "--benchmark-partition") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--benchmark-partition requires a directory path");
            }
            options.benchmarkPartitionDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--sample") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--sample requires a value");
            }
            options.benchmarkSample = parseLimit(argv[++i]);
            options.benchmarkSampleProvided = true;
        } else if (arg == "--benchmark-mode") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--benchmark-mode requires a value");
            }
            options.benchmarkMode = partitionBenchmarkModeFromString(argv[++i]);
            options.benchmarkModeProvided = true;
        } else if (arg == "--probe-layer-edges") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--probe-layer-edges requires a directory path");
            }
            options.probeLayerEdgesDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--next-seed-partition") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--next-seed-partition requires a directory path");
            }
            options.nextSeedPartitionDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--sample-states") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--sample-states requires a value");
            }
            options.sampleStates = parseLimit(argv[++i]);
            options.sampleStatesProvided = true;
        } else if (arg == "--external-seed-dedup") {
            options.externalSeedDedup = true;
        } else if (arg == "--dedup-chunk-size") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--dedup-chunk-size requires a value");
            }
            options.dedupChunkSize = parseLimit(argv[++i]);
            options.dedupChunkSizeProvided = true;
        } else if (arg == "--temp-dir") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--temp-dir requires a directory path");
            }
            options.tempDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--keep-temp") {
            options.keepTemp = true;
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }
    if (options.loadTablePath.has_value() && options.saveTablePath.has_value()) {
        throw std::invalid_argument("--load-table and --save-table cannot be used together");
    }
    if (options.loadTablePath.has_value() && !options.analysisNotation.has_value()) {
        throw std::invalid_argument("--load-table currently requires --analyze");
    }
    if (options.noPred && !options.statsOnly && !options.probe) {
        throw std::invalid_argument("--no-pred is only valid with --stats-only or --probe");
    }
    if (options.statsOnly && options.saveTablePath.has_value()) {
        throw std::invalid_argument("--stats-only does not produce an outcome table; remove --save-table");
    }
    if (options.statsOnly && (options.loadTablePath.has_value() || options.analysisNotation.has_value())) {
        throw std::invalid_argument("--stats-only cannot be combined with --load-table or --analyze");
    }
    if (options.probe && (options.saveTablePath.has_value() || options.loadTablePath.has_value() ||
                          options.analysisNotation.has_value())) {
        throw std::invalid_argument("--probe cannot be combined with table save/load or --analyze");
    }
    if (options.probe && options.limitProvided && options.maxStates == 0) {
        throw std::invalid_argument("--probe requires a finite --limit value");
    }
    const int validationModeCount =
        static_cast<int>(options.validateLayerPath.has_value()) +
        static_cast<int>(options.validateSeedPath.has_value()) +
        static_cast<int>(options.validateLayersDir.has_value()) +
        static_cast<int>(options.repairClosureCheckpoint) +
        static_cast<int>(options.inspectLayerPath.has_value()) +
        static_cast<int>(options.inspectSeedPath.has_value());
    const int partitionModeCount =
        static_cast<int>(options.partitionKeysetInput.has_value()) +
        static_cast<int>(options.validatePartitionDir.has_value()) +
        static_cast<int>(options.inspectPartitionDir.has_value()) +
        static_cast<int>(options.lookupPartitionDir.has_value()) +
        static_cast<int>(options.benchmarkPartitionDir.has_value()) +
        static_cast<int>(options.probeLayerEdgesDir.has_value());
    if (validationModeCount + partitionModeCount > 1) {
        throw std::invalid_argument("choose only one validate/inspect/partition mode");
    }
    if (options.partitionBucketsProvided) {
        if (!options.partitionKeysetInput.has_value()) {
            throw std::invalid_argument("--partition-buckets is only valid with --partition-keyset");
        }
        if (options.partitionBuckets == 0 || options.partitionBuckets > std::numeric_limits<uint32_t>::max()) {
            throw std::invalid_argument("--partition-buckets must be in 1..4294967295");
        }
    }
    if (options.partitionMethodProvided && !options.partitionKeysetInput.has_value()) {
        throw std::invalid_argument("--partition-method is only valid with --partition-keyset");
    }
    if (options.partitionCacheBucketsProvided) {
        if (!options.lookupPartitionDir.has_value() && !options.benchmarkPartitionDir.has_value() &&
            !options.probeLayerEdgesDir.has_value()) {
            throw std::invalid_argument("--partition-cache-buckets is only valid with lookup/benchmark/probe partition modes");
        }
        if (options.partitionCacheBuckets == 0 || options.partitionCacheBuckets > std::numeric_limits<uint32_t>::max()) {
            throw std::invalid_argument("--partition-cache-buckets must be in 1..4294967295");
        }
    }
    if (options.benchmarkModeProvided && !options.benchmarkPartitionDir.has_value()) {
        throw std::invalid_argument("--benchmark-mode is only valid with --benchmark-partition");
    }
    if (options.nextSeedPartitionDir.has_value() && !options.probeLayerEdgesDir.has_value()) {
        throw std::invalid_argument("--next-seed-partition is only valid with --probe-layer-edges");
    }
    if (options.sampleStatesProvided && !options.probeLayerEdgesDir.has_value()) {
        throw std::invalid_argument("--sample-states is only valid with --probe-layer-edges");
    }
    if (options.probeLayerEdgesDir.has_value() && !options.nextSeedPartitionDir.has_value()) {
        throw std::invalid_argument("--probe-layer-edges requires --next-seed-partition for non-zero layers");
    }
    if ((options.cleanupStaleRuns || options.repairDryRun || options.repairLayerProvided) && !options.repairClosureCheckpoint) {
        throw std::invalid_argument("--cleanup-stale-runs, --dry-run, and --layer are only valid with --repair-closure-checkpoint");
    }
    if (options.partitionOverwrite && !options.partitionKeysetInput.has_value()) {
        throw std::invalid_argument("--overwrite is only valid with --partition-keyset");
    }
    if (options.partitionKeysetInput.has_value() && !options.partitionOutputDir.has_value()) {
        throw std::invalid_argument("--partition-keyset requires --partition-output");
    }
    if (options.partitionOutputDir.has_value() && !options.partitionKeysetInput.has_value()) {
        throw std::invalid_argument("--partition-output is only valid with --partition-keyset");
    }
    if (options.lookupKeyProvided && !options.lookupPartitionDir.has_value()) {
        throw std::invalid_argument("--key is only valid with --lookup-partition");
    }
    if (options.lookupPartitionDir.has_value() && !options.lookupKeyProvided) {
        throw std::invalid_argument("--lookup-partition requires --key");
    }
    if (options.benchmarkSampleProvided && !options.benchmarkPartitionDir.has_value()) {
        throw std::invalid_argument("--sample is only valid with --benchmark-partition");
    }
    const bool externalLayerOptionProvided =
        options.buildLayerExternalProvided || options.layerWorkDir.has_value() || options.seedFile.has_value() ||
        options.outputLayerFile.has_value() || options.outputNextSeedFile.has_value() ||
        options.maxIterations != 0 || options.maxExpandedStates != 0 ||
        options.resumeClosure || options.checkpointIntervalProvided ||
        options.partitionedClosure || options.closurePartitionBucketsProvided || options.closurePartitionMethodProvided;
    if (partitionModeCount != 0) {
        if (options.buildLayersDir.has_value() || options.statsOnly || options.probe || options.analysisNotation.has_value() ||
            options.saveTablePath.has_value() || options.loadTablePath.has_value() || options.full ||
            options.limitProvided || options.noPred || options.maxStatesPerLayerProvided ||
            options.startLayerProvided || options.stopAfterLayerProvided || options.externalLayerClosure || options.externalSeedDedup ||
            options.partitionedClosure || options.closurePartitionBucketsProvided || options.closurePartitionMethodProvided ||
            options.dedupChunkSizeProvided || options.tempDir.has_value() || options.keepTemp ||
            options.resumeClosure || options.checkpointIntervalProvided ||
            externalLayerOptionProvided || validationModeCount != 0) {
            throw std::invalid_argument("partition modes cannot be combined with build, solve, stats, probe, validate, or table modes");
        }
    }
    if (validationModeCount != 0) {
        if (options.buildLayersDir.has_value() || options.statsOnly || options.probe || options.analysisNotation.has_value() ||
            options.saveTablePath.has_value() || options.loadTablePath.has_value() || options.full ||
            options.limitProvided || options.noPred || options.maxStatesPerLayerProvided ||
            options.startLayerProvided || options.stopAfterLayerProvided || options.externalLayerClosure || options.externalSeedDedup ||
            options.partitionedClosure || options.closurePartitionBucketsProvided || options.closurePartitionMethodProvided ||
            options.dedupChunkSizeProvided || options.tempDir.has_value() || options.keepTemp ||
            options.resumeClosure || options.checkpointIntervalProvided ||
            externalLayerOptionProvided) {
            throw std::invalid_argument("validate/inspect modes cannot be combined with build, solve, stats, probe, or table modes");
        }
    } else if (options.buildLayerExternalProvided) {
        if (options.buildLayersDir.has_value() || options.statsOnly || options.probe || options.analysisNotation.has_value() ||
            options.saveTablePath.has_value() || options.loadTablePath.has_value() || options.full || options.limitProvided ||
            options.noPred || options.maxStatesPerLayerProvided || options.startLayerProvided || options.stopAfterLayerProvided ||
            options.externalLayerClosure || options.externalSeedDedup || options.partitionedClosure ||
            options.closurePartitionBucketsProvided || options.closurePartitionMethodProvided || options.tempDir.has_value()) {
            throw std::invalid_argument("--build-layer-external cannot be combined with solve, stats, probe, analyze, table, or --build-layers modes");
        }
        if (!options.layerWorkDir.has_value()) {
            throw std::invalid_argument("--build-layer-external requires --layer-work-dir");
        }
        if (!options.seedFile.has_value()) {
            throw std::invalid_argument("--build-layer-external requires --seed-file");
        }
        if (!options.outputLayerFile.has_value()) {
            throw std::invalid_argument("--build-layer-external requires --output-layer");
        }
        if (options.buildLayerExternal > 0 && !options.outputNextSeedFile.has_value()) {
            throw std::invalid_argument("--build-layer-external requires --output-next-seed for layers above zero");
        }
        if (options.resumeClosure) {
            throw std::invalid_argument("--resume-closure currently requires --build-layers --external-layer-closure --start-layer K");
        }
        if (options.dedupChunkSizeProvided && options.dedupChunkSize == 0) {
            throw std::invalid_argument("--dedup-chunk-size must be greater than zero");
        }
    } else if (options.buildLayersDir.has_value()) {
        if (options.statsOnly || options.probe || options.analysisNotation.has_value() ||
            options.saveTablePath.has_value() || options.loadTablePath.has_value() ||
            options.layerWorkDir.has_value() || options.seedFile.has_value() || options.outputLayerFile.has_value() ||
            options.outputNextSeedFile.has_value()) {
            throw std::invalid_argument("--build-layers cannot be combined with solve, stats, probe, analyze, save, or load modes");
        }
        if (!options.externalLayerClosure && (options.maxIterations != 0 || options.maxExpandedStates != 0)) {
            throw std::invalid_argument("--max-iterations and --max-expanded-states require --external-layer-closure with --build-layers");
        }
        if (!options.externalLayerClosure && (options.resumeClosure || options.checkpointIntervalProvided)) {
            throw std::invalid_argument("--resume-closure and --checkpoint-interval require --external-layer-closure with --build-layers");
        }
        if (!options.externalLayerClosure && options.partitionedClosure) {
            throw std::invalid_argument("--partitioned-closure requires --external-layer-closure with --build-layers");
        }
        if ((options.closurePartitionBucketsProvided || options.closurePartitionMethodProvided) && !options.partitionedClosure) {
            throw std::invalid_argument("--closure-partition-buckets and --closure-partition-method require --partitioned-closure");
        }
        if (options.closurePartitionBucketsProvided &&
            (options.closurePartitionBuckets == 0 || options.closurePartitionBuckets > std::numeric_limits<uint32_t>::max())) {
            throw std::invalid_argument("--closure-partition-buckets must be in 1..4294967295");
        }
        if (options.resumeClosure && !options.startLayerProvided) {
            throw std::invalid_argument("--resume-closure currently requires --start-layer K");
        }
        if (options.startLayer < options.stopAfterLayer) {
            throw std::invalid_argument("--start-layer must be greater than or equal to --stop-after-layer");
        }
        if (options.dedupChunkSizeProvided && options.dedupChunkSize == 0) {
            throw std::invalid_argument("--dedup-chunk-size must be greater than zero");
        }
    } else if (options.maxStatesPerLayerProvided) {
        throw std::invalid_argument("--max-states-per-layer is only valid with --build-layers");
    } else if (options.startLayerProvided || options.stopAfterLayerProvided) {
        throw std::invalid_argument("--start-layer and --stop-after-layer are only valid with --build-layers");
    } else if (options.externalSeedDedup || options.dedupChunkSizeProvided || options.tempDir.has_value() || options.keepTemp) {
        throw std::invalid_argument("external dedup/temp options are only valid with --build-layers or --build-layer-external");
    } else if (options.externalLayerClosure) {
        throw std::invalid_argument("--external-layer-closure is only valid with --build-layers");
    } else if (options.partitionedClosure || options.closurePartitionBucketsProvided || options.closurePartitionMethodProvided) {
        throw std::invalid_argument("partitioned closure options are only valid with --build-layers --external-layer-closure");
    } else if (options.resumeClosure || options.checkpointIntervalProvided) {
        throw std::invalid_argument("--resume-closure and --checkpoint-interval are only valid with external closure modes");
    } else if (externalLayerOptionProvided) {
        throw std::invalid_argument("external layer closure options are only valid with --build-layer-external");
    }
    return options;
}

std::string formatInteger(uint64_t value) {
    std::string text = std::to_string(value);
    for (int insert = static_cast<int>(text.size()) - 3; insert > 0; insert -= 3) {
        text.insert(static_cast<size_t>(insert), ",");
    }
    return text;
}

std::string formatRate(uint64_t count, double seconds) {
    if (seconds <= 0.0) {
        return "-";
    }
    return formatInteger(static_cast<uint64_t>(static_cast<double>(count) / seconds)) + "/s";
}

std::string formatLayerRate(uint64_t count, double seconds) {
    return formatRate(count, seconds);
}

std::string moveToString(const Move& move) {
    std::string text = std::to_string(move.from) + "->" + std::to_string(move.to);
    if (move.capture) {
        text += " captures ";
        text += std::to_string(move.capturedSquare);
    }
    return text;
}

void printLayerArray(const char* title, const std::array<uint64_t, 16>& values) {
    std::cout << title << ":\n";
    for (int count = 15; count >= 0; --count) {
        std::cout << count << ": " << values[count] << "\n";
    }
}

void printSolveStats(const SolveStats& stats, std::optional<Outcome> initialOutcome = std::nullopt) {
    std::cout << "Status: " << (stats.truncated ? "truncated" : "complete graph") << "\n";
    std::cout << "Graph backend: " << graphBackendToString(stats.graphBackend) << "\n";
    std::cout << "Reachable states: " << stats.reachableStates << "\n";
    std::cout << "Generated edges: " << stats.generatedEdges << "\n";
    std::cout << "Stored edges: " << stats.storedEdges << "\n";
    std::cout << "Dropped edges: " << stats.droppedEdges << "\n";
    std::cout << "Drop ratio: " << formatDropRatio(stats.droppedEdges, stats.generatedEdges) << "\n";
    std::cout << "Total edges: " << stats.totalEdges << " (compatibility alias for stored edges)\n";
    std::cout << "Cannon win states: " << stats.cannonWinStates << "\n";
    std::cout << "Soldier win states: " << stats.soldierWinStates << "\n";
    std::cout << "Draw states: " << stats.drawStates << "\n";
    std::cout << "Unknown states: " << stats.unknownStates << "\n";
    if (initialOutcome.has_value()) {
        std::cout << "Initial outcome: " << outcomeToString(*initialOutcome) << "\n";
    }
    if (stats.truncated) {
        std::cout << "Note: truncated statistics describe the explored prefix, not the complete graph.\n";
    }
    std::cout << "\n";

    printLayerArray("States by soldier count", stats.statesBySoldierCount);
    std::cout << "\n";
    printLayerArray("Generated edges by soldier count", stats.generatedEdgesBySoldierCount);
    std::cout << "\n";
    printLayerArray("Stored edges by soldier count", stats.storedEdgesBySoldierCount);
    std::cout << "\n";
    printLayerArray("Dropped edges by soldier count", stats.droppedEdgesBySoldierCount);
    std::cout << "\n";

    std::cout << "Timing:\n";
    std::cout << "Build graph: " << formatDuration(stats.buildGraphSeconds) << "\n";
    if (stats.ranRetrograde) {
        std::cout << "Retrograde: " << formatDuration(stats.retrogradeSeconds) << "\n";
        std::cout << "Finalize: " << formatDuration(stats.finalizeSeconds) << "\n";
    }
    std::cout << "Total: " << formatDuration(stats.totalSeconds) << "\n\n";

    std::cout << "Diagnostics:\n";
    std::cout << "Max BFS queue size: " << stats.maxBfsQueueSize << "\n";
    if (stats.ranRetrograde) {
        std::cout << "Max retrograde queue size: " << stats.maxRetrogradeQueueSize << "\n";
    }
    std::cout << "Predecessor edges stored: " << (stats.storesPred ? "yes" : "no") << "\n";
}

void printMemoryEstimate(const MemoryEstimate& estimate) {
    std::cout << "Memory estimate:\n";
    std::cout << "Compact table @16 bytes/state: " << formatBytes(estimate.compactTable16Bytes) << "\n";
    std::cout << "Compact table @24 bytes/state: " << formatBytes(estimate.compactTable24Bytes) << "\n";
    std::cout << "States vector: " << formatBytes(estimate.statesVectorBytes) << "\n";
    std::cout << "Outcome/info vector: " << formatBytes(estimate.outcomeVectorBytes) << "\n";
    std::cout << "Remaining vector: " << formatBytes(estimate.remainingVectorBytes) << "\n";
    std::cout << "Flat edges one direction: " << formatBytes(estimate.flatEdgesBytesOneDirection) << "\n";
    std::cout << "Flat edges both directions: " << formatBytes(estimate.flatEdgesBytesBothDirections) << "\n";
    std::cout << "CSR succ flat: " << formatBytes(estimate.csrSuccFlatBytes) << "\n";
    std::cout << "CSR succ offset: " << formatBytes(estimate.csrSuccOffsetBytes) << "\n";
    std::cout << "CSR pred flat: " << formatBytes(estimate.csrPredFlatBytes) << "\n";
    std::cout << "CSR pred offset: " << formatBytes(estimate.csrPredOffsetBytes) << "\n";
    std::cout << "CSR total graph: " << formatBytes(estimate.csrTotalGraphBytes) << "\n";
    std::cout << "vector<vector<int>> overhead succ: " << formatBytes(estimate.vectorVectorOverheadSucc) << "\n";
    std::cout << "vector<vector<int>> overhead pred: " << formatBytes(estimate.vectorVectorOverheadPred) << "\n";
    std::cout << "Vector total graph estimate: " << formatBytes(estimate.vectorTotalGraphEstimateBytes) << "\n";
    std::cout << "Rough current graph estimate: " << formatBytes(estimate.roughTotalCurrentGraphBytes) << "\n";
}

ProgressCallback makeProgressCallback(const CliOptions& options) {
    if (options.progressInterval == 0) {
        return {};
    }
    return [](const ProgressInfo& info) {
        std::cout << '[' << info.stage << "] ";
        if (!info.message.empty()) {
            std::cout << info.message << "\n";
            return;
        }
        if (info.stage == "build graph") {
            std::cout << "states=" << formatInteger(info.states)
                      << " generatedEdges=" << formatInteger(info.generatedEdges)
                      << " storedEdges=" << formatInteger(info.storedEdges)
                      << " droppedEdges=" << formatInteger(info.droppedEdges)
                      << " dropRatio=" << formatDropRatio(info.droppedEdges, info.generatedEdges)
                      << " queue=" << formatInteger(info.queueSize)
                      << " states/s=" << formatRate(info.processed, info.elapsedSeconds)
                      << " elapsed=" << formatDuration(info.elapsedSeconds) << "\n";
            return;
        }
        if (info.stage == "retrograde") {
            std::cout << "processed=" << formatInteger(info.processed)
                      << " queue=" << formatInteger(info.queueSize)
                      << " solved=" << formatInteger(info.solved)
                      << " states/s=" << formatRate(info.processed, info.elapsedSeconds)
                      << " elapsed=" << formatDuration(info.elapsedSeconds) << "\n";
            return;
        }
        std::cout << "processed=" << formatInteger(info.processed);
        if (info.total != 0) {
            std::cout << "/" << formatInteger(info.total);
        }
        if (info.elapsedSeconds > 0.0) {
            std::cout << " elapsed=" << formatDuration(info.elapsedSeconds);
        }
        std::cout << "\n";
    };
}

SolveOptions makeSolveOptions(const CliOptions& options) {
    SolveOptions solveOptions;
    solveOptions.maxStates = options.maxStates;
    solveOptions.graphBackend = options.graphBackend;
    solveOptions.progressInterval = options.progressInterval;
    solveOptions.progress = makeProgressCallback(options);
    return solveOptions;
}

GraphStatsOptions makeGraphStatsOptions(const CliOptions& options, uint64_t limit) {
    GraphStatsOptions statsOptions;
    statsOptions.maxStates = limit;
    statsOptions.graphBackend = options.graphBackend;
    statsOptions.storePred = !options.noPred;
    statsOptions.progressInterval = options.progressInterval;
    statsOptions.progress = makeProgressCallback(options);
    return statsOptions;
}

LayeredBuildOptions makeLayeredOptions(const CliOptions& options) {
    LayeredBuildOptions layeredOptions;
    layeredOptions.outputDir = *options.buildLayersDir;
    layeredOptions.maxStatesPerLayer = options.maxStatesPerLayer;
    layeredOptions.maxExpandedStates = options.maxExpandedStates;
    layeredOptions.maxIterations = options.maxIterations;
    layeredOptions.progressInterval = options.progressInterval;
    layeredOptions.checkpointInterval = options.checkpointInterval;
    layeredOptions.resumeClosure = options.resumeClosure;
    layeredOptions.partitionedClosure = options.partitionedClosure;
    layeredOptions.closurePartitionBuckets = static_cast<uint32_t>(options.closurePartitionBuckets);
    layeredOptions.closurePartitionMethod = partitionMethodToString(options.closurePartitionMethod);
    layeredOptions.closureBackend =
        options.externalLayerClosure ? LayerClosureBackend::External : LayerClosureBackend::InMemory;
    layeredOptions.useExternalSeedDedup = options.externalSeedDedup;
    layeredOptions.externalChunkKeyLimit = options.dedupChunkSize;
    layeredOptions.keepTempFiles = options.keepTemp;
    if (options.tempDir.has_value()) {
        layeredOptions.tempDir = *options.tempDir;
    }
    if (options.startLayerProvided) {
        layeredOptions.startLayer = options.startLayer;
    }
    if (options.stopAfterLayerProvided) {
        layeredOptions.stopAfterLayer = options.stopAfterLayer;
    }
    if (options.progressInterval != 0) {
        layeredOptions.progress = [](const LayeredProgressInfo& info) {
            std::cout << "[layer " << info.soldierCount;
            if (info.closureBackend == LayerClosureBackend::External) {
                std::cout << " external";
            }
            std::cout << "] ";
            if (info.complete) {
                std::cout << "complete states=" << formatInteger(info.visitedStates)
                          << " expanded=" << formatInteger(info.expandedStates)
                          << " iterations=" << formatInteger(info.iteration)
                          << " sameEdges=" << formatInteger(info.generatedSameLayerEdges)
                          << " captureEdges=" << formatInteger(info.generatedCaptureEdges)
                          << " nextSeeds=" << formatInteger(info.newNextLayerSeeds)
                          << " duplicateNextSeeds=" << formatInteger(info.duplicateNextLayerSeeds)
                          << " elapsed=" << formatDuration(info.elapsedSeconds)
                          << " status=" << (info.truncated ? "truncated" : "complete") << "\n";
                return;
            }

            if (info.closureBackend == LayerClosureBackend::External) {
                std::cout << "iter=" << formatInteger(info.iteration)
                          << " frontier=" << formatInteger(info.queueSize)
                          << " visited=" << formatInteger(info.visitedStates)
                          << " candidates=" << formatInteger(info.candidateStates)
                          << " nextFrontier=" << formatInteger(info.nextFrontierStates)
                          << " expanded=" << formatInteger(info.expandedStates)
                          << " sameEdges=" << formatInteger(info.generatedSameLayerEdges)
                          << " captureEdges=" << formatInteger(info.generatedCaptureEdges)
                          << " elapsed=" << formatDuration(info.elapsedSeconds) << "\n";
                return;
            }

            std::cout << "visited=" << formatInteger(info.visitedStates)
                      << " queue=" << formatInteger(info.queueSize)
                      << " sameEdges=" << formatInteger(info.generatedSameLayerEdges)
                      << " captureEdges=" << formatInteger(info.generatedCaptureEdges)
                      << " nextSeeds=" << formatInteger(info.newNextLayerSeeds)
                      << " states/s=" << formatLayerRate(info.visitedStates, info.elapsedSeconds)
                      << " elapsed=" << formatDuration(info.elapsedSeconds) << "\n";
        };
    }
    return layeredOptions;
}

ExternalClosureOptions makeExternalClosureOptions(const CliOptions& options) {
    ExternalClosureOptions closureOptions;
    closureOptions.workDir = *options.layerWorkDir;
    closureOptions.seedFile = *options.seedFile;
    closureOptions.outputLayerFile = *options.outputLayerFile;
    if (options.outputNextSeedFile.has_value()) {
        closureOptions.outputNextSeedFile = *options.outputNextSeedFile;
    }
    closureOptions.soldierCount = options.buildLayerExternal;
    closureOptions.chunkKeyLimit = options.dedupChunkSize;
    closureOptions.progressInterval = options.progressInterval;
    closureOptions.checkpointInterval = options.checkpointInterval;
    closureOptions.maxIterations = options.maxIterations;
    closureOptions.maxExpandedStates = options.maxExpandedStates;
    closureOptions.keepTempFiles = options.keepTemp;
    if (options.progressInterval != 0) {
        closureOptions.progress = [](const ExternalClosureProgressInfo& info) {
            std::cout << "[layer " << info.soldierCount << " external] ";
            if (info.complete) {
                std::cout << "complete visited=" << formatInteger(info.visitedStates)
                          << " expanded=" << formatInteger(info.expandedStates)
                          << " sameEdges=" << formatInteger(info.generatedSameLayerEdges)
                          << " captureEdges=" << formatInteger(info.generatedCaptureEdges)
                          << " elapsed=" << formatDuration(info.elapsedSeconds)
                          << " status=" << (info.truncated ? "truncated" : "complete") << "\n";
                return;
            }
            std::cout << "iter=" << formatInteger(info.iteration)
                      << " frontier=" << formatInteger(info.frontierStates)
                      << " visited=" << formatInteger(info.visitedStates)
                      << " candidates=" << formatInteger(info.candidateStates)
                      << " nextFrontier=" << formatInteger(info.nextFrontierStates)
                      << " expanded=" << formatInteger(info.expandedStates)
                      << " sameEdges=" << formatInteger(info.generatedSameLayerEdges)
                      << " captureEdges=" << formatInteger(info.generatedCaptureEdges)
                      << " elapsed=" << formatDuration(info.elapsedSeconds) << "\n";
        };
    }
    return closureOptions;
}

PartitionedKeySetOptions makePartitionOptions(const CliOptions& options) {
    PartitionedKeySetOptions partitionOptions;
    partitionOptions.inputFile = *options.partitionKeysetInput;
    partitionOptions.outputDir = *options.partitionOutputDir;
    partitionOptions.bucketCount = static_cast<uint32_t>(options.partitionBuckets);
    partitionOptions.method = options.partitionMethod;
    partitionOptions.progressInterval = options.progressInterval;
    partitionOptions.overwrite = options.partitionOverwrite;
    if (options.progressInterval != 0) {
        partitionOptions.progress = [](uint64_t scanned) {
            std::cout << "[partition] scanned=" << formatInteger(scanned) << "\n";
        };
    }
    return partitionOptions;
}

void printPartitionBuild(const CliOptions& options) {
    std::cout << "Partition keyset build\n";
    std::cout << "Input: " << options.partitionKeysetInput->string() << "\n";
    std::cout << "Output dir: " << options.partitionOutputDir->string() << "\n";
    std::cout << "Buckets: " << formatInteger(options.partitionBuckets) << "\n";
    std::cout << "Method: " << partitionMethodToString(options.partitionMethod) << "\n";
    std::cout << "Overwrite: " << (options.partitionOverwrite ? "yes" : "no") << "\n\n";

    const PartitionedKeySetStats stats = buildPartitionedKeySet(makePartitionOptions(options));

    std::cout << "Partition build complete\n";
    std::cout << "Source kind: " << partitionSourceKindToString(stats.sourceKind) << "\n";
    std::cout << "Soldiers: " << stats.soldierCount << "\n";
    std::cout << "Input keys: " << formatInteger(stats.inputKeys) << "\n";
    std::cout << "Output keys: " << formatInteger(stats.outputKeys) << "\n";
    std::cout << "Bucket count: " << formatInteger(stats.bucketCount) << "\n";
    std::cout << "Partition method: " << partitionMethodToString(stats.partitionMethod) << "\n";
    std::cout << "Min bucket size: " << formatInteger(stats.minBucketSize) << "\n";
    std::cout << "Max bucket size: " << formatInteger(stats.maxBucketSize) << "\n";
    std::cout << "Average bucket size: " << std::fixed << std::setprecision(2) << stats.averageBucketSize << "\n";
    std::cout << "Empty buckets: " << formatInteger(stats.emptyBuckets) << "\n";
    std::cout << "Build time: " << formatDuration(stats.buildSeconds) << "\n";
    std::cout << "Manifest: " << partitionManifestPath(*options.partitionOutputDir).string() << "\n";
}

void printValidatePartition(const std::filesystem::path& dir) {
    const PartitionValidationResult result = validatePartitionedKeySet(dir);
    std::cout << "Validate partition: " << dir.string() << "\n";
    std::cout << "Soldiers: " << result.soldierCount << "\n";
    std::cout << "Total keys: " << formatInteger(result.totalKeys) << "\n";
    std::cout << "Bucket count: " << formatInteger(result.bucketCount) << "\n";
    std::cout << "Status: " << (result.valid ? "valid" : "invalid") << "\n";
}

void printBucketRankings(const std::vector<PartitionBucketInfo>& buckets, bool largest) {
    std::vector<PartitionBucketInfo> sorted = buckets;
    std::sort(sorted.begin(), sorted.end(), [largest](const PartitionBucketInfo& lhs, const PartitionBucketInfo& rhs) {
        if (lhs.keyCount != rhs.keyCount) {
            return largest ? lhs.keyCount > rhs.keyCount : lhs.keyCount < rhs.keyCount;
        }
        return lhs.bucketId < rhs.bucketId;
    });
    size_t printed = 0;
    for (const PartitionBucketInfo& bucket : sorted) {
        if (!largest && bucket.keyCount == 0) {
            continue;
        }
        std::cout << "  bucket " << bucket.bucketId << ": " << formatInteger(bucket.keyCount) << "\n";
        ++printed;
        if (printed >= 10) {
            break;
        }
    }
}

void printInspectPartition(const std::filesystem::path& dir) {
    const PartitionInspection inspection = inspectPartitionedKeySet(dir);
    std::cout << "Inspect partition: " << dir.string() << "\n";
    std::cout << "Soldiers: " << inspection.soldierCount << "\n";
    std::cout << "Total keys: " << formatInteger(inspection.totalKeys) << "\n";
    std::cout << "Bucket count: " << formatInteger(inspection.bucketCount) << "\n";
    std::cout << "Partition method: " << inspection.partitionMethod << "\n";
    std::cout << "Min bucket size: " << formatInteger(inspection.minBucketSize) << "\n";
    std::cout << "Max bucket size: " << formatInteger(inspection.maxBucketSize) << "\n";
    std::cout << "Average bucket size: " << std::fixed << std::setprecision(2) << inspection.averageBucketSize << "\n";
    std::cout << "Empty buckets: " << formatInteger(inspection.emptyBuckets) << "\n";
    std::cout << "Total bucket file size: " << formatBytes(inspection.totalBucketFileBytes) << "\n";
    std::cout << "Largest buckets:\n";
    printBucketRankings(inspection.buckets, true);
    std::cout << "Smallest non-empty buckets:\n";
    printBucketRankings(inspection.buckets, false);
}

void printLookupPartition(const CliOptions& options) {
    PartitionedKeySetReaderOptions readerOptions;
    readerOptions.partitionDir = *options.lookupPartitionDir;
    readerOptions.maxCachedBuckets = static_cast<uint32_t>(options.partitionCacheBuckets);
    PartitionedKeySetReader reader(readerOptions);
    const uint32_t bucket = reader.bucketForKey(options.lookupKey);
    const bool found = reader.contains(options.lookupKey);
    std::cout << "Lookup partition: " << options.lookupPartitionDir->string() << "\n";
    std::cout << "Key: " << options.lookupKey << "\n";
    std::cout << "Bucket: " << bucket << "\n";
    std::cout << "Partition method: " << partitionMethodToString(reader.partitionMethod()) << "\n";
    std::cout << "Cache buckets: " << formatInteger(options.partitionCacheBuckets) << "\n";
    std::cout << "Found: " << (found ? "yes" : "no") << "\n";
}

void printBenchmarkPartition(const CliOptions& options) {
    const PartitionLookupBenchmark result =
        benchmarkPartitionedKeySet(
            *options.benchmarkPartitionDir,
            options.benchmarkSample,
            options.benchmarkMode,
            static_cast<uint32_t>(options.partitionCacheBuckets));
    std::cout << "Benchmark partition: " << options.benchmarkPartitionDir->string() << "\n";
    std::cout << "Mode: " << partitionBenchmarkModeToString(result.mode) << "\n";
    std::cout << "Cache buckets: " << formatInteger(result.cacheBuckets) << "\n";
    std::cout << "Requested samples: " << formatInteger(result.requestedSamples) << "\n";
    std::cout << "Executed lookups: " << formatInteger(result.executedLookups) << "\n";
    std::cout << "Found: " << formatInteger(result.found) << "\n";
    std::cout << "Missing: " << formatInteger(result.missing) << "\n";
    std::cout << "Buckets touched: " << formatInteger(result.bucketsTouched) << "\n";
    std::cout << "Bucket loads: " << formatInteger(result.bucketLoads) << "\n";
    std::cout << "Lookup time: " << formatDuration(result.seconds) << "\n";
    std::cout << "Lookups/sec: " << formatInteger(static_cast<uint64_t>(result.lookupsPerSecond)) << "\n";
    if (result.mode != PartitionBenchmarkMode::Existing) {
        std::cout << "Missing-key note: benchmark perturbs existing packed keys while preserving soldier count.\n";
    }
}

LayerEdgeProbeOptions makeLayerEdgeProbeOptions(const CliOptions& options) {
    LayerEdgeProbeOptions probeOptions;
    probeOptions.layerPartitionDir = *options.probeLayerEdgesDir;
    probeOptions.nextSeedPartitionDir = *options.nextSeedPartitionDir;
    probeOptions.sampleStates = options.sampleStates;
    probeOptions.cacheBuckets = static_cast<uint32_t>(options.partitionCacheBuckets);
    probeOptions.progressInterval = options.progressInterval;
    if (options.progressInterval != 0) {
        probeOptions.progress = [](const LayerEdgeProbeProgress& info) {
            std::cout << "[edge probe] sampled=" << formatInteger(info.sampledStates)
                      << " moves=" << formatInteger(info.generatedMoves)
                      << " elapsed=" << formatDuration(info.elapsedSeconds) << "\n";
        };
    }
    return probeOptions;
}

void printLayerEdgeProbe(const CliOptions& options) {
    const LayerEdgeProbeStats stats = probeLayerEdges(makeLayerEdgeProbeOptions(options));
    std::cout << "Layer edge probe\n";
    std::cout << "Layer partition: " << options.probeLayerEdgesDir->string() << "\n";
    std::cout << "Next seed partition: " << options.nextSeedPartitionDir->string() << "\n";
    std::cout << "Soldiers: " << stats.soldierCount << "\n";
    std::cout << "Sampled states: " << formatInteger(stats.sampledStates) << "\n";
    std::cout << "Generated moves: " << formatInteger(stats.generatedMoves) << "\n";
    std::cout << "Same-layer generated: " << formatInteger(stats.sameLayerGenerated) << "\n";
    std::cout << "Same-layer found: " << formatInteger(stats.sameLayerFound) << "\n";
    std::cout << "Same-layer missing: " << formatInteger(stats.sameLayerMissing) << "\n";
    std::cout << "Capture generated: " << formatInteger(stats.captureGenerated) << "\n";
    std::cout << "Capture found: " << formatInteger(stats.captureFound) << "\n";
    std::cout << "Capture missing: " << formatInteger(stats.captureMissing) << "\n";
    std::cout << "Lookup keys: " << formatInteger(stats.lookupKeys) << "\n";
    std::cout << "Buckets touched: " << formatInteger(stats.bucketsTouched) << "\n";
    std::cout << "Bucket loads: " << formatInteger(stats.bucketLoads) << "\n";
    std::cout << "Lookup time: " << formatDuration(stats.lookupSeconds) << "\n";
    std::cout << "Total time: " << formatDuration(stats.totalSeconds) << "\n";
    std::cout << "States/sec: " << formatInteger(static_cast<uint64_t>(stats.statesPerSecond)) << "\n";
    std::cout << "Moves/sec: " << formatInteger(static_cast<uint64_t>(stats.movesPerSecond)) << "\n";
    std::cout << "Lookups/sec: " << formatInteger(static_cast<uint64_t>(stats.lookupsPerSecond)) << "\n";
    std::cout << "Note: missing successors are observations for partial partitions, not proof of an error.\n";
}

void printInitialSolve(const CliOptions& options) {
    const Position initial = initialPosition();
    const SolveResult result = solveFromInitial(makeSolveOptions(options));

    std::cout << "Sanpao15 Solver\n";
    std::cout << "Initial position:\n";
    std::cout << boardToString(initial) << "\n";
    std::cout << "Side: " << sideToString(initial.side) << "\n\n";

    printSolveStats(result.stats, result.initialOutcome);

    if (options.saveTablePath.has_value()) {
        saveResultTable(result.table, *options.saveTablePath, options.progressInterval, makeProgressCallback(options));
        std::cout << "\nSaved table: " << options.saveTablePath->string() << "\n";
    }
}

void printStatsOnly(const CliOptions& options) {
    std::cout << "Mode: stats-only\n";
    std::cout << "Building graph...\n";
    const GraphStatsResult result = collectGraphStatsFromInitial(makeGraphStatsOptions(options, options.maxStates));
    std::cout << "Graph build complete.\n\n";
    printSolveStats(result.stats);
    std::cout << "\n";
    std::cout << "Memory note: graph memory estimates use stored edges only; dropped edges are generated legal moves "
                 "that were not written to the graph because of truncation.\n\n";
    printMemoryEstimate(result.memory);
}

std::vector<uint64_t> probeLimits(const CliOptions& options) {
    const std::vector<uint64_t> defaults{10000, 100000, 1000000};
    if (!options.limitProvided) {
        return defaults;
    }

    std::vector<uint64_t> limits;
    for (uint64_t value : defaults) {
        if (value < options.maxStates) {
            limits.push_back(value);
        }
    }
    if (limits.empty() || limits.back() != options.maxStates) {
        limits.push_back(options.maxStates);
    }
    return limits;
}

void printProbe(const CliOptions& options) {
    struct ProbeRow {
        uint64_t limit = 0;
        GraphStatsResult result;
    };

    std::vector<ProbeRow> rows;
    for (uint64_t limit : probeLimits(options)) {
        std::cout << "Probe limit " << formatInteger(limit) << "...\n";
        GraphStatsResult result = collectGraphStatsFromInitial(makeGraphStatsOptions(options, limit));
        rows.push_back({limit, result});
        if (!result.stats.truncated) {
            break;
        }
    }

    std::cout << "\nProbe results:\n";
    std::cout << std::left
              << std::setw(14) << "Limit"
              << std::setw(14) << "States"
              << std::setw(14) << "GenEdges"
              << std::setw(14) << "StoredEdges"
              << std::setw(14) << "Dropped"
              << std::setw(10) << "Drop%"
              << std::setw(14) << "Stored/St"
              << std::setw(14) << "Build Time"
              << std::setw(14) << "States/s"
              << std::setw(16) << "Est Table 16B"
              << std::setw(12) << "Status"
              << "\n";
    for (const ProbeRow& row : rows) {
        const double storedEdgesPerState =
            row.result.stats.reachableStates == 0
                ? 0.0
                : static_cast<double>(row.result.stats.storedEdges) /
                      static_cast<double>(row.result.stats.reachableStates);
        std::ostringstream storedEdgesPerStateText;
        storedEdgesPerStateText << std::fixed << std::setprecision(2) << storedEdgesPerState;
        std::cout << std::left
                  << std::setw(14) << formatInteger(row.limit)
                  << std::setw(14) << formatInteger(row.result.stats.reachableStates)
                  << std::setw(14) << formatInteger(row.result.stats.generatedEdges)
                  << std::setw(14) << formatInteger(row.result.stats.storedEdges)
                  << std::setw(14) << formatInteger(row.result.stats.droppedEdges)
                  << std::setw(10) << formatDropRatio(row.result.stats.droppedEdges, row.result.stats.generatedEdges)
                  << std::setw(14) << storedEdgesPerStateText.str()
                  << std::setw(14) << formatDuration(row.result.stats.buildGraphSeconds)
                  << std::setw(14) << formatRate(row.result.stats.reachableStates, row.result.stats.buildGraphSeconds)
                  << std::setw(16) << formatBytes(row.result.memory.compactTable16Bytes)
                  << std::setw(12) << (row.result.stats.truncated ? "truncated" : "complete")
                  << "\n";
        if (!row.result.stats.truncated) {
            std::cout << "Complete graph reached; later probe limits skipped.\n";
        }
    }
}

void printLayeredBuild(const CliOptions& options) {
    std::cout << "Layered reachability build\n";
    std::cout << "Output dir: " << options.buildLayersDir->string() << "\n";
    std::cout << "Closure backend: " << (options.externalLayerClosure ? "external" : "memory") << "\n";
    std::cout << "Start layer: " << options.startLayer << (options.startLayerProvided ? " (resume)" : " (standard seed)") << "\n";
    std::cout << "Stop after layer: " << options.stopAfterLayer << "\n";
    std::cout << "Max states per layer: "
              << (options.maxStatesPerLayer == 0 ? std::string("unlimited") : formatInteger(options.maxStatesPerLayer))
              << "\n";
    if (options.externalLayerClosure) {
        std::cout << "Max expanded states: "
                  << (options.maxExpandedStates == 0 ? std::string("unlimited") : formatInteger(options.maxExpandedStates))
                  << "\n";
        std::cout << "Max iterations: "
                  << (options.maxIterations == 0 ? std::string("unlimited") : formatInteger(options.maxIterations))
                  << "\n";
        std::cout << "Resume closure: " << (options.resumeClosure ? "yes" : "no") << "\n";
        std::cout << "Checkpoint interval: "
                  << (options.checkpointInterval == 0 ? std::string("final/truncation only") : formatInteger(options.checkpointInterval))
                  << "\n";
        std::cout << "Partitioned closure: " << (options.partitionedClosure ? "yes" : "no") << "\n";
        if (options.partitionedClosure) {
            std::cout << "Closure partition buckets: " << formatInteger(options.closurePartitionBuckets) << "\n";
            std::cout << "Closure partition method: " << partitionMethodToString(options.closurePartitionMethod) << "\n";
        }
    }
    std::cout << "\n";
    std::cout << "External seed dedup: " << (options.externalSeedDedup ? "yes" : "no") << "\n";
    if (options.externalSeedDedup) {
        std::cout << "Dedup chunk size: " << formatInteger(options.dedupChunkSize) << "\n";
        std::cout << "Temp dir: "
                  << (options.tempDir.has_value() ? options.tempDir->string() : (*options.buildLayersDir / "tmp").string())
                  << "\n";
        std::cout << "Keep temp: " << (options.keepTemp ? "yes" : "no") << "\n";
    }
    std::cout << "\n";

    const LayeredBuildStats stats = buildReachableLayers(makeLayeredOptions(options));

    std::cout << "\nSummary:\n";
    std::cout << std::left
              << std::setw(8) << "Layer"
              << std::setw(10) << "Backend"
              << std::setw(14) << "Seeds"
              << std::setw(14) << "States"
              << std::setw(14) << "Expanded"
              << std::setw(10) << "Iters"
              << std::setw(16) << "SameEdges"
              << std::setw(16) << "CaptureEdges"
              << std::setw(14) << "NextSeeds"
              << std::setw(18) << "DuplicateSeeds"
              << std::setw(10) << "Ext"
              << std::setw(10) << "Resume"
              << std::setw(14) << "Frontier"
              << std::setw(10) << "Chunks"
              << std::setw(12) << "Time"
              << std::setw(12) << "Status"
              << "\n";
    for (int layer = 15; layer >= 0; --layer) {
        const LayerStats& item = stats.layers[layer];
        const char* status = item.skipped ? "skipped" : (item.truncated ? "truncated" : "complete");
        std::cout << std::left
                  << std::setw(8) << item.soldierCount
                  << std::setw(10) << layerClosureBackendToString(item.closureBackend)
                  << std::setw(14) << formatInteger(item.seedStates)
                  << std::setw(14) << formatInteger(item.reachableStates)
                  << std::setw(14) << formatInteger(item.expandedStates)
                  << std::setw(10) << formatInteger(item.iterations)
                  << std::setw(16) << formatInteger(item.generatedSameLayerEdges)
                  << std::setw(16) << formatInteger(item.generatedCaptureEdges)
                  << std::setw(14) << formatInteger(item.newNextLayerSeeds)
                  << std::setw(18) << formatInteger(item.duplicateNextLayerSeeds)
                  << std::setw(10) << (item.externalSeedDedup ? "yes" : "no")
                  << std::setw(10) << (item.resumedClosure ? "yes" : "no")
                  << std::setw(14) << formatInteger(item.finalFrontierStates)
                  << std::setw(10) << formatInteger(item.externalSeedStats.chunksWritten)
                  << std::setw(12) << formatDuration(item.buildSeconds)
                  << std::setw(12) << (item.truncated
                                            ? std::string(status) + "(" + layerTruncationReasonToString(item.truncationReason) + ")"
                                            : std::string(status))
                  << "\n";
        if (item.externalSeedDedup && item.externalSeedStats.chunksWritten > 0) {
            std::cout << "        external tempBytes=" << formatBytes(item.externalSeedStats.tempBytesWritten)
                      << " chunkSort=" << formatDuration(item.externalSeedStats.chunkSortSeconds)
                      << " merge=" << formatDuration(item.externalSeedStats.mergeSeconds) << "\n";
        }
        if (item.closureBackend == LayerClosureBackend::External && !item.skipped) {
            std::cout << "        closure expansion=" << formatDuration(item.expansionSeconds)
                      << " candidateDedup=" << formatDuration(item.candidateDedupSeconds)
                      << " diff=" << formatDuration(item.differenceSeconds)
                      << " union=" << formatDuration(item.unionSeconds)
                      << " partition=" << formatDuration(item.partitionSeconds)
                      << " partitionedSnapshots=" << formatInteger(item.partitionedSnapshotsWritten)
                      << " duplicateOrVisitedCandidates=" << formatInteger(item.duplicateOrVisitedCandidates)
                      << " checkpoint=" << (item.checkpointWritten ? item.checkpointDir : std::string("none"))
                      << "\n";
        }
    }

    std::cout << "\nTotal layer states: " << formatInteger(stats.totalLayerStates) << "\n";
    std::cout << "Total same-layer edges: " << formatInteger(stats.totalGeneratedSameLayerEdges) << "\n";
    std::cout << "Total capture edges: " << formatInteger(stats.totalGeneratedCaptureEdges) << "\n";
    std::cout << "Total time: " << formatDuration(stats.totalSeconds) << "\n";
    std::cout << "Status: " << (stats.truncated ? "truncated" : "complete") << "\n";
    std::cout << "Manifest: " << manifestFilePath(*options.buildLayersDir).string() << "\n";
}

void printExternalLayerClosure(const CliOptions& options) {
    std::cout << "External layer closure\n";
    std::cout << "Layer: " << options.buildLayerExternal << "\n";
    std::cout << "Work dir: " << options.layerWorkDir->string() << "\n";
    std::cout << "Seed file: " << options.seedFile->string() << "\n";
    std::cout << "Output layer: " << options.outputLayerFile->string() << "\n";
    if (options.outputNextSeedFile.has_value()) {
        std::cout << "Output next seed: " << options.outputNextSeedFile->string() << "\n";
    }
    std::cout << "Dedup chunk size: " << formatInteger(options.dedupChunkSize) << "\n";
    std::cout << "Max iterations: "
              << (options.maxIterations == 0 ? std::string("unlimited") : formatInteger(options.maxIterations))
              << "\n";
    std::cout << "Max expanded states: "
              << (options.maxExpandedStates == 0 ? std::string("unlimited") : formatInteger(options.maxExpandedStates))
              << "\n";
    std::cout << "Checkpoint interval: "
              << (options.checkpointInterval == 0 ? std::string("final/truncation only") : formatInteger(options.checkpointInterval))
              << "\n";
    std::cout << "Keep temp: " << (options.keepTemp ? "yes" : "no") << "\n\n";

    const ExternalClosureStats stats = buildLayerClosureExternal(makeExternalClosureOptions(options));

    std::cout << "\nExternal layer closure complete\n";
    std::cout << "Layer: " << stats.soldierCount << "\n";
    std::cout << "Seeds: " << formatInteger(stats.seedStates) << "\n";
    std::cout << "Final states: " << formatInteger(stats.finalStates) << "\n";
    std::cout << "Iterations: " << formatInteger(stats.iterations) << "\n";
    std::cout << "Expanded states: " << formatInteger(stats.expandedStates) << "\n";
    std::cout << "Same-layer edges: " << formatInteger(stats.generatedSameLayerEdges) << "\n";
    std::cout << "Capture edges: " << formatInteger(stats.generatedCaptureEdges) << "\n";
    std::cout << "Generated candidate keys: " << formatInteger(stats.generatedCandidateKeys) << "\n";
    std::cout << "New frontier states: " << formatInteger(stats.newFrontierStates) << "\n";
    std::cout << "Duplicate/visited candidates: " << formatInteger(stats.duplicateOrVisitedCandidates) << "\n";
    std::cout << "Next seeds: " << formatInteger(stats.nextSeedStates) << "\n";
    std::cout << "Duplicate next seeds: " << formatInteger(stats.duplicateNextSeeds) << "\n";
    std::cout << "Complete: " << (stats.complete ? "yes" : "no") << "\n";
    std::cout << "Checkpoint: " << (stats.checkpointWritten ? stats.checkpointDir : std::string("none")) << "\n";
    std::cout << "Final frontier: " << formatInteger(stats.finalFrontierStates) << "\n";
    std::cout << "Expansion time: " << formatDuration(stats.expansionSeconds) << "\n";
    std::cout << "Candidate dedup time: " << formatDuration(stats.candidateDedupSeconds) << "\n";
    std::cout << "Difference time: " << formatDuration(stats.differenceSeconds) << "\n";
    std::cout << "Union time: " << formatDuration(stats.unionSeconds) << "\n";
    std::cout << "Partition time: " << formatDuration(stats.partitionSeconds) << "\n";
    std::cout << "Total time: " << formatDuration(stats.totalSeconds) << "\n";
    std::cout << "Status: " << (stats.truncated ? "truncated" : "complete") << "\n";
}

void printKeyListSummary(const char* label, const KeyListFileSummary& summary, bool showSamples) {
    std::cout << label << ": " << summary.path.string() << "\n";
    std::cout << "Soldiers: " << summary.soldierCount << "\n";
    std::cout << "States: " << summary.keyCount << "\n";
    if (summary.keyCount > 0) {
        std::cout << "Min key: " << summary.minKey << "\n";
        std::cout << "Max key: " << summary.maxKey << "\n";
    }
    if (showSamples && !summary.firstKeys.empty()) {
        std::cout << "First positions:\n";
        for (uint64_t key : summary.firstKeys) {
            std::cout << positionToNotation(unpackPosition(key)) << "\n";
        }
        std::cout << "Last positions:\n";
        for (uint64_t key : summary.lastKeys) {
            std::cout << positionToNotation(unpackPosition(key)) << "\n";
        }
    }
    std::cout << "Status: valid\n";
}

void printValidateLayer(const std::filesystem::path& path) {
    printKeyListSummary("Layer file", validateLayerFile(path), false);
}

void printValidateSeed(const std::filesystem::path& path) {
    printKeyListSummary("Seed file", validateSeedFile(path), false);
}

void printInspectLayer(const std::filesystem::path& path) {
    printKeyListSummary("Layer file", inspectLayerFile(path), true);
}

void printInspectSeed(const std::filesystem::path& path) {
    printKeyListSummary("Seed file", inspectSeedFile(path), true);
}

void printValidateLayers(const std::filesystem::path& dir) {
    std::cout << "Validate layers: " << dir.string() << "\n";
    if (std::filesystem::exists(manifestFilePath(dir))) {
        std::cout << "Manifest: present\n";
        std::ifstream manifest(manifestFilePath(dir));
        std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
        if (text.find("\"truncated\": true") != std::string::npos) {
            std::cout << "Manifest note: contains truncated layer(s)\n";
        }
        if (text.find("\"skipped\": true") != std::string::npos) {
            std::cout << "Manifest note: contains skipped layer(s)\n";
        }
        if (text.find("\"closureBackend\": \"external\"") != std::string::npos) {
            std::cout << "Manifest note: closureBackend=external\n";
        } else if (text.find("\"closureBackend\": \"memory\"") != std::string::npos) {
            std::cout << "Manifest note: closureBackend=memory\n";
        }
        if (text.find("\"checkpointWritten\": true") != std::string::npos) {
            std::cout << "Manifest note: closure checkpoint present\n";
        }
        if (text.find("\"resumedClosure\": true") != std::string::npos) {
            std::cout << "Manifest note: resumedClosure=true\n";
        }
        const std::vector<std::string> reasons{"max-states", "max-expanded", "max-iterations"};
        for (const std::string& reason : reasons) {
            if (text.find("\"truncationReason\": \"" + reason + "\"") != std::string::npos) {
                std::cout << "Manifest note: truncationReason=" << reason << "\n";
                break;
            }
        }
    } else {
        std::cout << "Manifest: missing\n";
    }

    int validFiles = 0;
    int missingFiles = 0;
    for (int layer = 15; layer >= 0; --layer) {
        const auto seedPath = seedFilePath(dir, layer);
        if (std::filesystem::exists(seedPath)) {
            const auto summary = validateSeedFile(seedPath, layer);
            ++validFiles;
            std::cout << "valid seed  " << seedPath.filename().string()
                      << " soldiers=" << summary.soldierCount
                      << " count=" << formatInteger(summary.keyCount) << "\n";
        } else {
            ++missingFiles;
            std::cout << "missing seed " << seedPath.filename().string() << "\n";
        }

        const auto layerPath = layerFilePath(dir, layer);
        if (std::filesystem::exists(layerPath)) {
            const auto summary = validateLayerFile(layerPath, layer);
            ++validFiles;
            std::cout << "valid layer " << layerPath.filename().string()
                      << " soldiers=" << summary.soldierCount
                      << " count=" << formatInteger(summary.keyCount) << "\n";
        } else {
            ++missingFiles;
            std::cout << "missing layer " << layerPath.filename().string() << "\n";
        }
    }
    int validCheckpoints = 0;
    int missingCheckpoints = 0;
    const auto workDir = dir / "work";
    if (std::filesystem::exists(workDir)) {
        for (int layer = 15; layer >= 0; --layer) {
            const auto checkpointDir = workDir / ("layer-" + std::to_string(layer));
            if (!std::filesystem::exists(checkpointDir)) {
                continue;
            }
            if (!std::filesystem::exists(closureCheckpointManifestPath(checkpointDir))) {
                ++missingCheckpoints;
                std::cout << "missing checkpoint layer-" << layer << " closure-state.json\n";
                continue;
            }
            const auto checkpoint = inspectClosureCheckpoint(checkpointDir, layer);
            ++validCheckpoints;
            std::cout << "valid checkpoint layer-" << layer
                      << " version=" << checkpoint.checkpointVersion
                      << " kind=" << checkpoint.checkpointKind
                      << " visited=" << formatInteger(checkpoint.visitedStates)
                      << " frontier=" << formatInteger(checkpoint.frontierStates)
                      << " expanded=" << formatInteger(checkpoint.expandedStates)
                      << " complete=" << (checkpoint.complete ? "yes" : "no")
                      << " truncated=" << (checkpoint.truncated ? "yes" : "no")
                      << " requiresTransientRuns=" << (checkpoint.requiresTransientRuns ? "yes" : "no")
                      << "\n";
            if (!checkpoint.requiresTransientRuns) {
                std::cout << "checkpoint note layer-" << layer
                          << ": stable checkpoint is valid; stale transient runs can be ignored\n";
            }
        }
    }
    std::cout << "Valid files: " << validFiles << "\n";
    std::cout << "Missing files: " << missingFiles << "\n";
    if (validCheckpoints != 0 || missingCheckpoints != 0) {
        std::cout << "Valid checkpoints: " << validCheckpoints << "\n";
        std::cout << "Missing checkpoints: " << missingCheckpoints << "\n";
    }
    std::cout << "Status: valid\n";
}

std::filesystem::path closureCheckpointDirForRepair(const CliOptions& options) {
    const std::filesystem::path root = *options.repairClosureCheckpointDir;
    const std::filesystem::path layeredCheckpoint = root / "work" / ("layer-" + std::to_string(options.repairLayer));
    if (std::filesystem::exists(closureCheckpointManifestPath(layeredCheckpoint))) {
        return layeredCheckpoint;
    }
    return root;
}

void printRepairClosureCheckpoint(const CliOptions& options) {
    const std::filesystem::path checkpointDir = closureCheckpointDirForRepair(options);
    const ClosureCheckpointRepairResult result =
        repairClosureCheckpoint(checkpointDir, options.repairLayer, options.repairDryRun);
    uint64_t removedRuns = 0;
    if (options.cleanupStaleRuns && !options.repairDryRun) {
        removedRuns = cleanupStaleClosureRuns(checkpointDir);
    }
    const ExternalClosureCheckpointInfo info = inspectClosureCheckpoint(checkpointDir, options.repairLayer);

    std::cout << "Repair closure checkpoint\n";
    std::cout << "Checkpoint dir: " << checkpointDir.string() << "\n";
    std::cout << "Layer: " << options.repairLayer << "\n";
    std::cout << "Dry run: " << (options.repairDryRun ? "yes" : "no") << "\n";
    std::cout << "Before kind: " << result.beforeKind << "\n";
    std::cout << "After kind: " << result.afterKind << "\n";
    std::cout << "Before requiresTransientRuns: " << (result.beforeRequiresTransientRuns ? "yes" : "no") << "\n";
    std::cout << "After requiresTransientRuns: " << (result.afterRequiresTransientRuns ? "yes" : "no") << "\n";
    std::cout << "Removed stale run entries: " << formatInteger(removedRuns) << "\n";
    std::cout << "Checkpoint version: " << info.checkpointVersion << "\n";
    std::cout << "Visited: " << formatInteger(info.visitedStates) << "\n";
    std::cout << "Frontier: " << formatInteger(info.frontierStates) << "\n";
    std::cout << "Expanded: " << formatInteger(info.expandedStates) << "\n";
    std::cout << "Status: valid\n";
}

void printAnalysisDetails(const Position& pos, const Analysis& analysis, const std::optional<std::filesystem::path>& tablePath) {

    std::cout << "Sanpao15 Position Analysis\n";
    std::cout << "Position:\n";
    std::cout << boardToString(pos) << "\n";
    std::cout << "Side: " << sideToString(pos.side) << "\n";
    std::cout << "Notation: " << positionToNotation(pos) << "\n\n";
    if (tablePath.has_value()) {
        std::cout << "Table: " << tablePath->string() << "\n";
    }
    std::cout << "Table status: ";
    if (analysis.tableExact) {
        std::cout << "exact\n";
    } else if (analysis.tableTruncated) {
        std::cout << "truncated\n";
    } else {
        std::cout << "partial\n";
    }
    if (!analysis.foundInTable) {
        std::cout << "Reason: position not found in table\n";
    }
    std::cout << "Current outcome: " << outcomeToString(analysis.outcome) << "\n";
    std::cout << "Distance: " << analysis.distance << "\n";
    if (analysis.bestMove.has_value()) {
        std::cout << "\nBest move:\n";
        std::cout << moveToString(*analysis.bestMove) << "\n";
    } else {
        std::cout << "\nBest move:\nnone\n";
    }

    std::cout << "\nLegal moves:\n";
    for (const MoveAnalysis& item : analysis.legalMoves) {
        std::cout << "  " << moveToString(item.move)
                  << " -> " << outcomeToString(item.resultingOutcome)
                  << ", distance " << item.distance;
        if (item.isBest) {
            std::cout << " best";
        }
        std::cout << "\n";
    }
}

void printPositionAnalysis(const CliOptions& options) {
    const Position pos = parsePositionNotation(*options.analysisNotation);
    if (options.loadTablePath.has_value()) {
        const ResultTable table = loadResultTable(*options.loadTablePath);
        const Analysis analysis = analyzePositionFromTable(pos, table);
        printAnalysisDetails(pos, analysis, options.loadTablePath);
        return;
    }

    const Analysis analysis = analyzePosition(pos, makeSolveOptions(options));
    printAnalysisDetails(pos, analysis, std::nullopt);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parseArgs(argc, argv);
        if (options.help) {
            printUsage();
            return 0;
        }
        if (options.validateLayerPath.has_value()) {
            printValidateLayer(*options.validateLayerPath);
            return 0;
        }
        if (options.validateSeedPath.has_value()) {
            printValidateSeed(*options.validateSeedPath);
            return 0;
        }
        if (options.validateLayersDir.has_value()) {
            printValidateLayers(*options.validateLayersDir);
            return 0;
        }
        if (options.repairClosureCheckpoint) {
            printRepairClosureCheckpoint(options);
            return 0;
        }
        if (options.inspectLayerPath.has_value()) {
            printInspectLayer(*options.inspectLayerPath);
            return 0;
        }
        if (options.inspectSeedPath.has_value()) {
            printInspectSeed(*options.inspectSeedPath);
            return 0;
        }
        if (options.partitionKeysetInput.has_value()) {
            printPartitionBuild(options);
            return 0;
        }
        if (options.validatePartitionDir.has_value()) {
            printValidatePartition(*options.validatePartitionDir);
            return 0;
        }
        if (options.inspectPartitionDir.has_value()) {
            printInspectPartition(*options.inspectPartitionDir);
            return 0;
        }
        if (options.lookupPartitionDir.has_value()) {
            printLookupPartition(options);
            return 0;
        }
        if (options.benchmarkPartitionDir.has_value()) {
            printBenchmarkPartition(options);
            return 0;
        }
        if (options.probeLayerEdgesDir.has_value()) {
            printLayerEdgeProbe(options);
            return 0;
        }
        if (options.buildLayerExternalProvided) {
            printExternalLayerClosure(options);
            return 0;
        }
        if (options.buildLayersDir.has_value()) {
            printLayeredBuild(options);
            return 0;
        }
        if (options.probe) {
            printProbe(options);
            return 0;
        }
        if (options.statsOnly) {
            printStatsOnly(options);
            return 0;
        }
        if (options.analysisNotation.has_value()) {
            printPositionAnalysis(options);
        } else {
            printInitialSolve(options);
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n";
        printUsage();
        return 1;
    }

    return 0;
}
