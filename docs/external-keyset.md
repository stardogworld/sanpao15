# External Key Set

`ExternalKeySetBuilder` is a reusable exact deduplication component for large
`uint64_t` state-key streams. It is currently used by layered reachability to
deduplicate capture-generated seeds for the next soldier-count layer.

This is not a complete external-memory layered solver by itself. It provides
the sorted-run dedup primitive used by external seed dedup and by the external
layer closure prototype.

## Why It Exists

Layer 15 already produces many capture successors during truncated probes. A
single `std::unordered_set<uint64_t>` for all next-layer seeds grows with the
number of generated capture edges and becomes an avoidable memory pressure point.

The external key set moves that specific dedup step to a sorted-run workflow:

1. collect keys into a bounded in-memory chunk;
2. sort and deduplicate the chunk;
3. write a temporary `.s15run` file;
4. k-way merge all sorted runs;
5. emit each unique key exactly once in ascending order.

No Bloom filter or approximate structure is used. Deduplication is exact.

## Run File Format

Temporary run files use the `.s15run` extension and a small binary v1 format:

```text
magic[8]      "S15RUN1\0"
uint32_le     version = 1
uint64_le     keyCount
uint64_le[]   sorted unique state keys
```

Readers validate magic, version, count, strict ascending order, and uniqueness.
Writers use explicit little-endian encoding rather than serializing C++ structs.

By default temporary runs are deleted after `finishToVector()` or
`finishToStream()`. Passing `keepTempFiles=true` keeps the `.s15run` files for
inspection.

## Builder API

The public API lives in
`solver/include/sanpao15/external_keyset.h`:

```cpp
ExternalKeySetOptions options;
options.tempDir = "build/layers/tmp/layer-15";
options.chunkKeyLimit = 1000000;

ExternalKeySetBuilder builder(options);
builder.add(key);
builder.addBatch(keys);
builder.flush();

std::vector<uint64_t> keys = builder.finishToVector();
// or stream without materializing the final key list inside the builder:
builder.finishToStream([](uint64_t key) { /* write or collect key */ });
```

Statistics include added keys, chunks written, chunk-deduped keys, final unique
keys, duplicate keys, temp bytes, streamed key bytes, chunk sort time, merge time,
and total time.

## Layered Integration

The default layered builder still uses in-memory seed dedup. Enable external seed
dedup explicitly:

```powershell
.\build\sanpao15_cli.exe --build-layers build\layers-ext --max-states-per-layer 1000000 --external-seed-dedup --dedup-chunk-size 100000 --progress 50000
```

Related options:

```text
--external-seed-dedup   Use sorted temporary runs for next-layer seed dedup.
--dedup-chunk-size N    Number of keys per sorted run chunk.
--temp-dir DIR          Root directory for temporary run files.
--keep-temp             Keep `.s15run` files after finishing a layer.
```

If `--temp-dir` is omitted, the builder uses:

```text
<outputDir>/tmp/layer-K
```

where `K` is the current soldier count.

The final `.s15seed` content must match the older in-memory dedup path exactly:
sorted, unique keys for layer `K - 1`. The manifest records whether external
dedup was used and the per-layer run statistics.

## Current Limits

External key set v1 is a primitive. In the main `--build-layers` path it only
externalizes capture seed dedup. The separate external closure prototype also
uses it for per-iteration same-layer candidates. It does not yet:

- stream `.s15layer` files directly from external runs;
- store layer-local edges;
- solve outcomes;
- generate `.s15tbl` files.

The current layered writer still materializes the final seed vector before
calling the existing `.s15seed` writer. This preserves the established file
validation path while removing the biggest unordered-set dependency from seed
dedup.

## Next Work

- integrate the external layer-closure prototype into full layered builds;
- chunked frontier processing;
- layer-local key indexes;
- mmap-friendly binary search over sorted key files;
- layer-local CSR edge files;
- lower-to-higher layered retrograde;
- compressed final outcome tables.
