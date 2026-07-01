# Roadmap

## Implemented In The Current MVP

- Save and load `.s15tbl` result tables.
- Support table lookup for arbitrary positions present in a table.
- Show win/loss/draw/unknown for every legal move in CLI and UI table analysis.
- Recommend best moves from table data.
- Store basic win/loss distance.
- Probe graph scale with `--stats-only` and `--probe`.
- Estimate memory use for compact tables and the current graph shape.
- Run low-memory graph probes without predecessor edges.
- Use CSR graph storage by default, with vector backend retained for comparison.
- Track generated, stored, and dropped edge counts for truncated scale probes.
- Build sorted `.s15layer` reachable-state files by soldier count.
- Persist `.s15seed` files, write layered manifests, resume one-layer builds, and validate/inspect layer artifacts.
- Deduplicate next-layer seeds with exact external sorted runs via `--external-seed-dedup`.
- Read and write validated `.s15run` temporary key-set files.
- Build one layer with the external closure prototype via `--build-layer-external`.
- Read and write internal sorted `.s15keys` files and scan them with sorted union/difference.
- Use external layer closure inside `--build-layers` via `--external-layer-closure`.
- Checkpoint and resume external closure with `--resume-closure` and `--checkpoint-interval`.
- Validate closure checkpoint manifests and referenced `.s15keys` files from `--validate-layers`.
- Build, validate, inspect, lookup, and benchmark partitioned keysets from `.s15layer`, `.s15seed`, and `.s15keys` files.
- Store partitioned indexes as `partition.json` plus `.s15bucket` files.
- Default new partitioned indexes to deterministic `splitmix64_mod` buckets, while preserving `key_mod` compatibility.
- Benchmark partition readers with existing/missing/mixed lookups and a configurable bucket cache.
- Probe layer-local generated edge membership against partitioned layer/seed indexes.
- Harden external closure checkpoints so resume depends only on stable `.s15keys` files and records `requiresTransientRuns=false`.
- Repair/check closure checkpoints and optionally clean stale transient run directories.
- Merge external keyset runs with bounded fan-in to avoid opening hundreds of `.s15run` files at once.
- Run bucket-wise partitioned keyset union and difference.
- Write and validate partitioned closure checkpoint snapshots with `--partitioned-closure`.
- Migrate flat closure checkpoints with `--migrate-closure-checkpoint`.
- Resume closure from partitioned checkpoints with `--resume-partitioned-closure`.
- Rank and unrank combinations with combinadic colex order.
- Map every legal full-tablebase layer position to and from a dense index.
- Store dense outcomes in 1-byte and packed 2-bit arrays.
- Write, inspect, and validate `.s15res` dense result files.
- Print full tablebase theoretical sizes with `--tablebase-sizes`.
- Generate dense successor ids for legal moves with same-layer and capture-to-lower-layer classification.
- Inspect dense successors and sample dense layer move statistics from the CLI.
- Solve low full-tablebase layers `k=0..3` with outcome-only retrograde and write `.s15res` results.
- Verify low-k `.s15res` headers and sampled successor consistency.
- Centralize the current ruleset as `sanpao15-min-four-soldiers`, where `soldierCount < 4` is immediate `CannonWin`.

## Solver Lines

- Reachability line: starts from `SSSSS/SSSSS/SSSSS/...../.CCC. c` and explores the reachable subset with external closure and partitioned checkpoints.
- Full tablebase line: covers every legal position in each soldier-count layer with dense combinadic indexing. This is the current foundation route.

## Next Steps

- Design a scalable full `0..15` tablebase architecture.
- Prototype the first non-material layer, `k=4`, with streaming/on-the-fly predecessor handling before extending the low-k in-memory solver.
- Replace the low-k vector predecessor prototype with CSR or flat layer-local storage.
- Evaluate file-backed or mmap dense outcome tables.
- Evaluate D4 symmetry reduction for dense layers.
- Keep the partitioned reachability line available for standard-initial-position experiments.
- Improve partitioned closure performance with a direct per-bucket candidate collector if needed.
- Design layer-local CSR edge files.
- Canonicalize the 8 board symmetries.
- Implement lower-to-higher layered retrograde.
- Design compressed outcome table files.
- Compile the C++ core to WebAssembly.
- Let the UI call the WASM core directly.
- Add cancellation/resume support for later layered retrograde runs.
- Improve distance validation for larger regression positions.
- Package a desktop app with Tauri.
- Support custom positions.
- Support folk-rule variants.
