# Partitioned Keyset

Layer 15 scale runs showed that a single sorted key file can pass 100M states while still being incomplete. A single file is simple and exact, but later layer-local edge generation needs fast membership checks without repeatedly scanning or loading one giant key list.

The partitioned keyset format splits a `.s15layer`, `.s15seed`, or `.s15keys` file into fixed buckets. Each bucket is independently sorted and unique.

## Partition Method

New partitions default to a deterministic stable hash method:

```text
bucket = splitmix64(key) % bucketCount
partitionMethod = "splitmix64_mod"
```

`splitmix64_mod` is fixed integer arithmetic, not `std::hash`, so the same keys map to the same buckets across platforms and builds. It became the default after real layer-15 data showed that packed bitboard keys distribute very poorly with direct modulo.

The old method remains supported for compatibility:

```text
bucket = key % bucketCount
partitionMethod = "key_mod"
```

Readers, validators, lookup, and benchmarks always use the `partitionMethod` recorded in `partition.json`. Existing `key_mod` partitions remain readable.

## Directory Layout

```text
partition-layer-15/
  partition.json
  bucket-000000.s15bucket
  bucket-000001.s15bucket
  ...
```

## Bucket Format

Bucket files are little-endian binary files:

```text
magic[8]      "S15BKT1\0"
uint32_le     version = 1
uint32_le     soldierCount
uint32_le     bucketId
uint32_le     bucketCount
uint64_le     keyCount
uint64_le     rulesetHash
uint64_le[]   sorted unique keys
```

Validation checks magic, version, ruleset hash, soldier count, bucket id/count, strict ascending order, uniqueness, decoded soldier count, and bucket membership under the manifest's partition method.

## Manifest

`partition.json` records:

```json
{
  "type": "partitioned-keyset",
  "version": 1,
  "rulesetHash": 5980780803949256449,
  "sourceFile": "../layers/layer-15.s15layer",
  "sourceKind": "layer",
  "soldierCount": 15,
  "bucketCount": 256,
  "partitionMethod": "splitmix64_mod",
  "totalKeys": 108513920,
  "bucketFiles": [
    {
      "bucketId": 0,
      "file": "bucket-000000.s15bucket",
      "keyCount": 423891,
      "minKey": 0,
      "maxKey": 0
    }
  ]
}
```

The manifest is helper metadata; validators still read every bucket file.

## Lookup Flow

`PartitionedKeySetReader::contains(key)` checks the decoded soldier count, computes the bucket from the manifest method, loads that bucket, and performs binary search. The reader has a small bucket cache controlled by `maxCachedBuckets`; the CLI exposes it as `--partition-cache-buckets N`.

Batch lookup is available through:

```cpp
std::vector<uint8_t> containsBatch(const std::vector<uint64_t>& keys) const;
BatchLookupStats containsBatchStats(std::span<const uint64_t> keys) const;
```

`containsBatchStats` reports query count, found/missing count, buckets touched, bucket file loads, elapsed time, and lookups/sec. A larger cache should reduce repeated bucket loads for locality-heavy edge generation, while still avoiding loading every bucket up front.

## Bucket-Wise Set Operations

Partitioned keysets support exact bucket-wise set operations when both inputs
have the same soldier count, ruleset hash, bucket count, and partition method:

```cpp
partitionedDifference(leftPartition, rightPartition, outputPartition, overwrite);
partitionedUnion(leftPartition, rightPartition, outputPartition, overwrite);
```

Each operation reads one matching bucket from each input, performs sorted
difference or union within that bucket, and writes the output bucket. This keeps
the operation bounded by the largest bucket instead of the full keyset. The
output is a normal partitioned keyset and can be validated with
`--validate-partition`.

## CLI

Build a partition:

```powershell
.\build\sanpao15_cli.exe ^
  --partition-keyset build\layers-overnight\layer-15.s15layer ^
  --partition-output build\partitions-splitmix\layer-15 ^
  --partition-buckets 256 ^
  --partition-method splitmix64_mod ^
  --progress 1000000
```

Validate, inspect, lookup, and benchmark:

```powershell
.\build\sanpao15_cli.exe --validate-partition build\partitions-splitmix\layer-15
.\build\sanpao15_cli.exe --inspect-partition build\partitions-splitmix\layer-15
.\build\sanpao15_cli.exe --lookup-partition build\partitions-splitmix\layer-15 --key 123456789 --partition-cache-buckets 32
.\build\sanpao15_cli.exe --benchmark-partition build\partitions-splitmix\layer-15 --sample 100000 --benchmark-mode mixed --partition-cache-buckets 32
```

`--benchmark-partition` supports `existing`, `missing`, and `mixed` modes. Missing-key mode perturbs sampled existing packed keys while preserving soldier count; it is a performance probe, not a proof that every generated key is a legal state.

Layer-local edge probing reads a layer partition and the next lower seed partition, generates legal moves for sampled layer states, and checks same-layer or capture-successor membership without storing edges:

```powershell
.\build\sanpao15_cli.exe ^
  --probe-layer-edges build\partitions-splitmix\layer-15 ^
  --next-seed-partition build\partitions-splitmix\seeds-14 ^
  --sample-states 100000 ^
  --partition-cache-buckets 32 ^
  --progress 10000
```

For partial layer/seed data, `sameLayerMissing` and `captureMissing` are observations, not automatic correctness failures. The current overnight layer-15 and seeds-14 files are partial checkpoints, so missing successors can simply be outside the saved partial closure or seed set.

## V1 Limits

- No mmap.
- No minimal perfect hash.
- No retrograde.
- No layer-local edge storage yet.
- Bucket build uses temporary raw bucket files, then sorts one bucket at a time.
- CLI exposure for bucket-wise set operations is not added yet; current callers
  use the C++ API.

## Next Work

- partitioned external closure;
- layer-local membership index integration;
- layer-local edge generation;
- partitioned result tables;
- layered retrograde.
