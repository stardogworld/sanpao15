#include "test_common.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/external_closure.h"
#include "sanpao15/layered.h"
#include "sanpao15/partitioned_keyset.h"
#include "sanpao15/position.h"
#include "sanpao15/table.h"

using namespace sanpao15;

namespace {

std::filesystem::path tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

Position zeroSoldierPosition(int cannonSquare) {
    Position pos;
    pos.cannons = setBit(0, cannonSquare);
    pos.side = Side::Cannon;
    return pos;
}

Position oneSoldierPosition(int cannonSquare, int soldierSquare) {
    Position pos = zeroSoldierPosition(cannonSquare);
    pos.soldiers = setBit(0, soldierSquare);
    return pos;
}

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
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

void writeRawBucket(
    const std::filesystem::path& path,
    int soldierCount,
    uint32_t bucketId,
    uint32_t bucketCount,
    const std::vector<uint64_t>& keys,
    const char* magic = "S15BKT1") {
    std::ofstream output(path, std::ios::binary);
    output.write(magic, 7);
    writeU8(output, 0);
    writeU32LE(output, 1);
    writeU32LE(output, static_cast<uint32_t>(soldierCount));
    writeU32LE(output, bucketId);
    writeU32LE(output, bucketCount);
    writeU64LE(output, static_cast<uint64_t>(keys.size()));
    writeU64LE(output, StandardRulesetHash);
    for (uint64_t key : keys) {
        writeU64LE(output, key);
    }
}

PartitionedKeySetOptions partitionOptions(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    uint32_t buckets,
    PartitionMethod method = PartitionMethod::Splitmix64Mod) {
    PartitionedKeySetOptions options;
    options.inputFile = input;
    options.outputDir = output;
    options.bucketCount = buckets;
    options.method = method;
    options.overwrite = true;
    return options;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
}

}  // namespace

SANPAO15_TEST(partitionedKeySetBuildsSmallKeysFileAndLooksUpKeys) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-small");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    const std::vector<uint64_t> keys{
        packPosition(zeroSoldierPosition(20)),
        packPosition(zeroSoldierPosition(21)),
        packPosition(zeroSoldierPosition(22)),
        packPosition(zeroSoldierPosition(23)),
    };
    writeKeysFile(keysFile, 0, keys);

    const std::filesystem::path partitionDir = dir / "partition";
    const PartitionedKeySetStats stats = buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4));
    const PartitionValidationResult validation = validatePartitionedKeySet(partitionDir);
    const PartitionInspection inspection = inspectPartitionedKeySet(partitionDir);
    PartitionedKeySetReader reader(partitionDir);

    SANPAO15_REQUIRE(stats.inputKeys == 4);
    SANPAO15_REQUIRE(stats.outputKeys == 4);
    SANPAO15_REQUIRE(stats.partitionMethod == PartitionMethod::Splitmix64Mod);
    SANPAO15_REQUIRE(validation.valid);
    SANPAO15_REQUIRE(validation.totalKeys == 4);
    SANPAO15_REQUIRE(inspection.bucketCount == 4);
    SANPAO15_REQUIRE(inspection.partitionMethod == "splitmix64_mod");
    SANPAO15_REQUIRE(inspection.maxBucketSize >= inspection.minBucketSize);
    for (uint64_t key : keys) {
        SANPAO15_REQUIRE(reader.contains(key));
    }
    SANPAO15_REQUIRE(!reader.contains(packPosition(zeroSoldierPosition(24))));

    const PartitionLookupBenchmark bench = benchmarkPartitionedKeySet(partitionDir, 3);
    SANPAO15_REQUIRE(bench.executedLookups == 3);
    SANPAO15_REQUIRE(bench.found == 3);
    SANPAO15_REQUIRE(bench.missing == 0);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetSupportsKeyModCompatibility) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-key-mod");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    const std::vector<uint64_t> keys{
        packPosition(zeroSoldierPosition(20)),
        packPosition(zeroSoldierPosition(21)),
        packPosition(zeroSoldierPosition(22)),
        packPosition(zeroSoldierPosition(23)),
    };
    writeKeysFile(keysFile, 0, keys);

    const std::filesystem::path partitionDir = dir / "partition";
    const PartitionedKeySetStats stats =
        buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4, PartitionMethod::KeyMod));
    const PartitionInspection inspection = inspectPartitionedKeySet(partitionDir);
    PartitionedKeySetReader reader(partitionDir);

    SANPAO15_REQUIRE(stats.partitionMethod == PartitionMethod::KeyMod);
    SANPAO15_REQUIRE(inspection.partitionMethod == "key_mod");
    SANPAO15_REQUIRE(reader.partitionMethod() == PartitionMethod::KeyMod);
    for (uint64_t key : keys) {
        SANPAO15_REQUIRE(reader.contains(key));
        SANPAO15_REQUIRE(reader.bucketForKey(key) == partitionBucketForKey(key, 4, PartitionMethod::KeyMod));
    }
    SANPAO15_REQUIRE(validatePartitionedKeySet(partitionDir).valid);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetRejectsUnknownPartitionMethod) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-method-bad");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    writeKeysFile(keysFile, 0, {packPosition(zeroSoldierPosition(20))});
    const std::filesystem::path partitionDir = dir / "partition";
    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4));

    const std::filesystem::path manifest = partitionManifestPath(partitionDir);
    std::string text = readText(manifest);
    const size_t method = text.find("splitmix64_mod");
    SANPAO15_REQUIRE(method != std::string::npos);
    text.replace(method, std::string("splitmix64_mod").size(), "unknown_method");
    writeText(manifest, text);

    sanpao15::test::requireThrows(
        [&] { (void)validatePartitionedKeySet(partitionDir); },
        "unknown partition method should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetSplitmixDistributionSmoke) {
    const uint32_t bucketCount = 16;
    std::vector<uint64_t> counts(bucketCount, 0);
    for (uint64_t key = 0; key < 1024; ++key) {
        ++counts[partitionBucketForKey(key << 25, bucketCount, PartitionMethod::Splitmix64Mod)];
    }
    const uint64_t nonEmpty = static_cast<uint64_t>(std::count_if(counts.begin(), counts.end(), [](uint64_t count) {
        return count != 0;
    }));
    const uint64_t maxBucket = *std::max_element(counts.begin(), counts.end());
    SANPAO15_REQUIRE(nonEmpty == bucketCount);
    SANPAO15_REQUIRE(maxBucket < 128);

    bool foundDifferentBucket = false;
    for (uint64_t key = 1; key < 1024; ++key) {
        if (partitionBucketForKey(key, bucketCount, PartitionMethod::KeyMod) !=
            partitionBucketForKey(key, bucketCount, PartitionMethod::Splitmix64Mod)) {
            foundDifferentBucket = true;
            break;
        }
    }
    SANPAO15_REQUIRE(foundDifferentBucket);
}

SANPAO15_TEST(partitionedKeySetBatchAndCacheAreConsistent) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-batch");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    std::vector<uint64_t> keys;
    for (int square = 0; square < 20; ++square) {
        keys.push_back(packPosition(zeroSoldierPosition(square)));
    }
    std::sort(keys.begin(), keys.end());
    writeKeysFile(keysFile, 0, keys);

    const std::filesystem::path partitionDir = dir / "partition";
    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 8));

    PartitionedKeySetReaderOptions cacheOneOptions;
    cacheOneOptions.partitionDir = partitionDir;
    cacheOneOptions.maxCachedBuckets = 1;
    PartitionedKeySetReader cacheOne(cacheOneOptions);
    PartitionedKeySetReader defaultCache(partitionDir);

    std::vector<uint64_t> queries{keys[0], keys[5], packPosition(zeroSoldierPosition(24)), keys[10], keys[0]};
    const std::vector<uint8_t> batch = cacheOne.containsBatch(queries);
    SANPAO15_REQUIRE(batch.size() == queries.size());
    for (size_t i = 0; i < queries.size(); ++i) {
        SANPAO15_REQUIRE(static_cast<bool>(batch[i]) == defaultCache.contains(queries[i]));
    }

    const BatchLookupStats smallCache = cacheOne.containsBatchStats(queries);
    PartitionedKeySetReaderOptions largeCacheOptions;
    largeCacheOptions.partitionDir = partitionDir;
    largeCacheOptions.maxCachedBuckets = 8;
    PartitionedKeySetReader largeCache(largeCacheOptions);
    const BatchLookupStats largeCacheStats = largeCache.containsBatchStats(queries);
    SANPAO15_REQUIRE(smallCache.queryKeys == queries.size());
    SANPAO15_REQUIRE(smallCache.foundKeys == 4);
    SANPAO15_REQUIRE(smallCache.missingKeys == 1);
    SANPAO15_REQUIRE(largeCacheStats.bucketLoads <= smallCache.bucketLoads);

    sanpao15::test::requireThrows(
        [&] {
            PartitionedKeySetReaderOptions badOptions;
            badOptions.partitionDir = partitionDir;
            badOptions.maxCachedBuckets = 0;
            PartitionedKeySetReader badReader(badOptions);
        },
        "zero cache buckets should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetBenchmarkModesWork) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-bench-modes");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    std::vector<uint64_t> keys;
    for (int square = 0; square < 20; ++square) {
        keys.push_back(packPosition(zeroSoldierPosition(square)));
    }
    std::sort(keys.begin(), keys.end());
    writeKeysFile(keysFile, 0, keys);
    const std::filesystem::path partitionDir = dir / "partition";
    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 8));

    const PartitionLookupBenchmark existing =
        benchmarkPartitionedKeySet(partitionDir, 10, PartitionBenchmarkMode::Existing, 2);
    const PartitionLookupBenchmark missing =
        benchmarkPartitionedKeySet(partitionDir, 10, PartitionBenchmarkMode::Missing, 2);
    const PartitionLookupBenchmark mixed =
        benchmarkPartitionedKeySet(partitionDir, 10, PartitionBenchmarkMode::Mixed, 2);

    SANPAO15_REQUIRE(existing.executedLookups == 10);
    SANPAO15_REQUIRE(existing.found == 10);
    SANPAO15_REQUIRE(missing.executedLookups == 10);
    SANPAO15_REQUIRE(missing.missing >= 1);
    SANPAO15_REQUIRE(mixed.executedLookups == 10);
    SANPAO15_REQUIRE(mixed.found >= 1);
    SANPAO15_REQUIRE(mixed.missing >= 1);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetSupportsSeedAndLayerSources) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-source");
    const std::vector<uint64_t> keys{
        packPosition(oneSoldierPosition(20, 0)),
        packPosition(oneSoldierPosition(21, 1)),
        packPosition(oneSoldierPosition(22, 2)),
    };

    const std::filesystem::path seedFile = seedFilePath(dir, 1);
    writeSeedFile(seedFile, 1, keys);
    const std::filesystem::path seedPartition = dir / "seed-partition";
    const PartitionedKeySetStats seedStats = buildPartitionedKeySet(partitionOptions(seedFile, seedPartition, 4));
    SANPAO15_REQUIRE(seedStats.sourceKind == PartitionSourceKind::Seed);
    SANPAO15_REQUIRE(validatePartitionedKeySet(seedPartition).totalKeys == 3);

    const std::filesystem::path layerFile = layerFilePath(dir, 1);
    writeLayerFile(layerFile, 1, keys);
    const std::filesystem::path layerPartition = dir / "layer-partition";
    const PartitionedKeySetStats layerStats = buildPartitionedKeySet(partitionOptions(layerFile, layerPartition, 4));
    SANPAO15_REQUIRE(layerStats.sourceKind == PartitionSourceKind::Layer);
    SANPAO15_REQUIRE(validatePartitionedKeySet(layerPartition).totalKeys == 3);
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetRejectsBadBuckets) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-bad");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    const uint64_t key0 = packPosition(zeroSoldierPosition(20));
    const uint64_t key1 = packPosition(zeroSoldierPosition(21));
    writeKeysFile(keysFile, 0, {key0, key1});
    const std::filesystem::path partitionDir = dir / "partition";
    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4));

    writeRawBucket(partitionBucketPath(partitionDir, 0), 0, 0, 4, {key1, key0});
    sanpao15::test::requireThrows(
        [&] { (void)validatePartitionedKeySet(partitionDir); },
        "unsorted bucket should be rejected");

    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4));
    const uint32_t wrongBucket = (partitionBucketForKey(key0, 4) + 1) % 4;
    writeRawBucket(partitionBucketPath(partitionDir, wrongBucket), 0, wrongBucket, 4, {key0});
    sanpao15::test::requireThrows(
        [&] { (void)validatePartitionedKeySet(partitionDir); },
        "wrong bucket membership should be rejected");

    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4));
    writeRawBucket(partitionBucketPath(partitionDir, 0), 0, 0, 4, {}, "BADBKT1");
    sanpao15::test::requireThrows(
        [&] { (void)validatePartitionedKeySet(partitionDir); },
        "bad bucket magic should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetValidateFailsForMissingBucket) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-missing");
    const std::filesystem::path keysFile = dir / "input.s15keys";
    writeKeysFile(keysFile, 0, {packPosition(zeroSoldierPosition(20))});
    const std::filesystem::path partitionDir = dir / "partition";
    (void)buildPartitionedKeySet(partitionOptions(keysFile, partitionDir, 4));
    std::filesystem::remove(partitionBucketPath(partitionDir, 0));
    sanpao15::test::requireThrows(
        [&] { (void)validatePartitionedKeySet(partitionDir); },
        "missing bucket should be rejected");
    std::filesystem::remove_all(dir);
}

SANPAO15_TEST(partitionedKeySetUnionAndDifferenceAreBucketWise) {
    const std::filesystem::path dir = tempDir("sanpao15-partition-ops");
    std::vector<uint64_t> leftKeys{
        packPosition(zeroSoldierPosition(20)),
        packPosition(zeroSoldierPosition(21)),
        packPosition(zeroSoldierPosition(22)),
    };
    std::vector<uint64_t> rightKeys{
        packPosition(zeroSoldierPosition(21)),
        packPosition(zeroSoldierPosition(23)),
    };
    std::sort(leftKeys.begin(), leftKeys.end());
    std::sort(rightKeys.begin(), rightKeys.end());

    const std::filesystem::path leftFile = dir / "left.s15keys";
    const std::filesystem::path rightFile = dir / "right.s15keys";
    writeKeysFile(leftFile, 0, leftKeys);
    writeKeysFile(rightFile, 0, rightKeys);

    const std::filesystem::path leftPartition = dir / "left-partition";
    const std::filesystem::path rightPartition = dir / "right-partition";
    (void)buildPartitionedKeySet(partitionOptions(leftFile, leftPartition, 4));
    (void)buildPartitionedKeySet(partitionOptions(rightFile, rightPartition, 4));

    const std::filesystem::path differencePartition = dir / "difference";
    const std::filesystem::path unionPartition = dir / "union";
    const PartitionedKeySetOpStats diffStats =
        partitionedDifference(leftPartition, rightPartition, differencePartition, true);
    const PartitionedKeySetOpStats unionStats =
        partitionedUnion(leftPartition, rightPartition, unionPartition, true);

    SANPAO15_REQUIRE(validatePartitionedKeySet(differencePartition).totalKeys == 2);
    SANPAO15_REQUIRE(validatePartitionedKeySet(unionPartition).totalKeys == 4);
    SANPAO15_REQUIRE(diffStats.leftKeys == 3);
    SANPAO15_REQUIRE(diffStats.rightKeys == 2);
    SANPAO15_REQUIRE(diffStats.outputKeys == 2);
    SANPAO15_REQUIRE(unionStats.outputKeys == 4);

    PartitionedKeySetReader diffReader(differencePartition);
    SANPAO15_REQUIRE(diffReader.contains(packPosition(zeroSoldierPosition(20))));
    SANPAO15_REQUIRE(!diffReader.contains(packPosition(zeroSoldierPosition(21))));
    SANPAO15_REQUIRE(diffReader.contains(packPosition(zeroSoldierPosition(22))));

    PartitionedKeySetReader unionReader(unionPartition);
    for (int square = 20; square <= 23; ++square) {
        SANPAO15_REQUIRE(unionReader.contains(packPosition(zeroSoldierPosition(square))));
    }
    std::filesystem::remove_all(dir);
}
