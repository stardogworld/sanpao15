# Sanpao15 三炮十五兵

`sanpao15` is a first-stage implementation of the 5x5 "三炮十五兵" game. It contains a C++20 rules core, a retrograde-analysis solver, a command-line table generator/analyzer, focused C++ tests, and a minimal TypeScript UI.

Current solver work has two lines:

- Reachability from the standard initial position, using external closure and partitioned checkpoints.
- Full tablebase dense indexing, covering every legal position in each soldier-count layer.

The active foundation work is moving toward the full tablebase route: combinadic dense indexes plus dense outcome arrays.

## Current Rule Set

The board has 25 squares numbered left-to-right and top-to-bottom:

```text
0  1  2  3  4
5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
```

Initial position:

```text
兵 兵 兵 兵 兵
兵 兵 兵 兵 兵
兵 兵 兵 兵 兵
.  .  .  .  .
.  炮 炮 炮 .
```

Notation for the initial position:

```text
SSSSS/SSSSS/SSSSS/...../.CCC. c
```

Rules implemented now:

- Cannons move one orthogonal step to an empty square.
- Cannons capture only in the form `炮 - empty - 兵`, landing on the soldier square and removing that soldier.
- Soldiers move one orthogonal step to an empty square and do not capture.
- Cannons move first, then sides alternate.
- Pieces of the same type are indistinguishable.
- Ruleset: `sanpao15-min-four-soldiers`.
- Ruleset hash: `5994631263128018692` (`0x5331355F76325F04`).
- Terminal rules: fewer than 4 soldiers means `CannonWin`; no cannon legal move means `SoldierWin`; if the side to move has no legal move, the opponent wins.
- The material rule is checked first, so a `k=3` position is `CannonWin` even if the cannon side has no legal move.

## State Representation

The C++ core uses two 25-bit bitboards and a side-to-move flag:

```cpp
struct Position {
    uint32_t cannons;
    uint32_t soldiers;
    Side side;
};
```

Packed key:

```cpp
key = cannonMask | (uint64_t(soldierMask) << 25) | (uint64_t(side) << 50);
```

## Solver Approach

This game can contain cycles, so plain recursive minimax is not a sound complete solver for the full state space. The intended full solver is finite cyclic-game analysis:

1. Generate the reachable state graph.
2. Mark terminal states.
3. Propagate proven wins and losses backward.
4. On a complete graph, mark remaining unknown states as draws.

The solver stores `outcome`, `distance`, and `bestMove` for each table entry. For first-stage CLI responsiveness, `solveFromInitial()` applies a default state cap and reports when graph generation was truncated. Passing `SolveOptions{0}` requests a full graph. When a graph is truncated, unresolved states remain `Unknown`; only a complete graph converts unresolved states to `Draw`.

The default graph backend is CSR (`--graph csr`). The older `vector<vector<int>>` backend remains available with `--graph vector` for regression comparisons.

`.s15tbl` loading is tied to the current ruleset hash and rejects incompatible
tables, trailing bytes, duplicate keys, unknown flags, and invalid stored best
moves. `Unknown` remains distinct from `Draw`.

Scale statistics distinguish three edge counts:

- `generatedEdges`: legal successors generated from expanded states.
- `storedEdges`: successor edges actually written to the graph. `totalEdges` is kept as a compatibility alias for this value.
- `droppedEdges`: generated legal successors that were not stored because a truncated probe hit `--limit`.

Dropped edges are expected in truncated probes. They are not stored and do not contribute to graph-memory estimates. `generatedEdges` is closer to the legal-move density; `storedEdges` is the number that drives CSR/vector graph memory.

## Build C++

```bash
cmake -S . -B build
cmake --build build
./build/sanpao15_cli
```

On Windows with the generated Ninja build, run:

```powershell
.\build\sanpao15_cli.exe
```

Useful CLI modes:

```powershell
.\build\sanpao15_cli.exe --tablebase-sizes
.\build\sanpao15_cli.exe --create-empty-res 0 build\empty-k0.s15res --encoding 2bit
.\build\sanpao15_cli.exe --inspect-res build\empty-k0.s15res
.\build\sanpao15_cli.exe --validate-res build\empty-k0.s15res
.\build\sanpao15_cli.exe --dense-successors 15 0
.\build\sanpao15_cli.exe --dense-predecessors 4 0
.\build\sanpao15_cli.exe --dense-move-stats 2 --sample 100000
.\build\sanpao15_cli.exe --solve-lowk 2 --out-dir build\lowk-smoke --encoding 2bit
.\build\sanpao15_cli.exe --solve-lowk-streaming 3 --out-dir build\stream-min4 --encoding 2bit
.\build\sanpao15_cli.exe --solve-lowk-streaming 4 --allow-k4 --out-dir build\stream-k4 --encoding 2bit
.\build\sanpao15_cli.exe --verify-lowk build\lowk-smoke --max-k 2 --sample 10000
.\build\sanpao15_cli.exe --limit 50000
.\build\sanpao15_cli.exe --full
.\build\sanpao15_cli.exe --analyze "SSSSS/SSSSS/SSSSS/...../.CCC. c" --limit 10000
.\build\sanpao15_cli.exe --stats-only --limit 1000000 --progress 50000
.\build\sanpao15_cli.exe --stats-only --no-pred --limit 1000000 --progress 50000
.\build\sanpao15_cli.exe --probe --limit 10000000 --progress 100000
.\build\sanpao15_cli.exe --stats-only --no-pred --limit 10000000 --graph csr --progress 500000
.\build\sanpao15_cli.exe --full --progress 50000 --save-table standard.s15tbl
.\build\sanpao15_cli.exe --limit 100000 --progress 10000 --save-table debug.s15tbl
.\build\sanpao15_cli.exe --load-table standard.s15tbl --analyze "SSSSS/SSSSS/SSSSS/...../.CCC. c"
```

Suggested workflow before a full solve:

```powershell
.\build\sanpao15_cli.exe --probe --limit 1000000 --progress 50000
.\build\sanpao15_cli.exe --stats-only --limit 10000000 --progress 500000
.\build\sanpao15_cli.exe --full --progress 500000 --save-table standard.s15tbl
```

`--stats-only` builds only the reachable graph and does not generate a result table. `--no-pred` is only valid for stats/probe runs and saves predecessor-edge memory, so it cannot be used for retrograde solving. `--probe` is for scale estimation, not final table generation. Truncated statistics are prefix statistics, not complete graph statistics. CSR reduces graph memory pressure, but full solving still needs scale checks before generating a large table.

For CSR scale probing, prefer no-predecessor runs first:

```powershell
.\build\sanpao15_cli.exe --stats-only --no-pred --limit 10000000 --graph csr --progress 500000
```

If a 100M CSR no-pred probe is still truncated and has not reached lower-soldier layers, the next phase should move toward layered solving. If the complete graph is reached under CSR with safe memory estimates, then a full solve can be considered.

Layered reachability prototype:

```powershell
.\build\sanpao15_cli.exe --build-layers build\layers --max-states-per-layer 1000000 --progress 50000
.\build\sanpao15_cli.exe --build-layers build\layers --stop-after-layer 15 --progress 50000
.\build\sanpao15_cli.exe --build-layers build\layers --start-layer 14 --stop-after-layer 14 --progress 50000
.\build\sanpao15_cli.exe --build-layers build\layers-ext --stop-after-layer 15 --max-states-per-layer 1000000 --external-seed-dedup --dedup-chunk-size 100000 --progress 50000
.\build\sanpao15_cli.exe --build-layers build\layers-ext --external-layer-closure --external-seed-dedup --stop-after-layer 15 --max-expanded-states 1000000 --dedup-chunk-size 100000 --progress 50000
.\build\sanpao15_cli.exe --build-layers build\layers-ext --external-layer-closure --external-seed-dedup --resume-closure --start-layer 15 --stop-after-layer 15 --max-expanded-states 1000000 --dedup-chunk-size 100000 --progress 50000
.\build\sanpao15_cli.exe --build-layers build\partitioned-closure-smoke --external-layer-closure --partitioned-closure --external-seed-dedup --stop-after-layer 15 --max-expanded-states 10000 --closure-partition-buckets 16 --dedup-chunk-size 1000 --progress 1000
.\build\sanpao15_cli.exe --resume-partitioned-closure build\partitioned-closure-smoke --layer 15 --expanded-budget 10000 --progress 1000
.\build\sanpao15_cli.exe --resume-partitioned-closure build\layers-overnight --layer 15 --dry-run
.\build\sanpao15_cli.exe --build-layer-external 15 --layer-work-dir build\layer15-work --seed-file build\layers\seeds-15.s15seed --output-layer build\layer15-work\layer-15.s15layer --output-next-seed build\layer15-work\seeds-14.s15seed --dedup-chunk-size 1000 --max-expanded-states 10000 --progress 1000
.\build\sanpao15_cli.exe --validate-layers build\layers
.\build\sanpao15_cli.exe --repair-closure-checkpoint build\layers --layer 15 --dry-run
.\build\sanpao15_cli.exe --inspect-layer build\layers\layer-15.s15layer
.\build\sanpao15_cli.exe --inspect-seed build\layers\seeds-14.s15seed
```

This writes sorted, deduplicated `.s15layer` state-key files by soldier count, persists capture-generated `.s15seed` entry files for resume, and writes `manifest.json`. It is reachability only; it does not run retrograde and does not produce `.s15tbl`.

`--external-seed-dedup` uses exact external sorted runs for next-layer seed dedup. It reduces memory pressure from the capture seed set, but layer-local visited states are still held in memory. `--dedup-chunk-size`, `--temp-dir`, and `--keep-temp` control run size and temporary `.s15run` behavior.

`--external-layer-closure` uses internal sorted `.s15keys` files for `visited`, `frontier`, `candidates`, set difference, and set union inside `--build-layers`. It writes a checkpoint under `work\layer-K` with `closure-state.json`, `visited.s15keys`, `frontier.s15keys`, `remaining-frontier.s15keys`, and `next-seeds.s15keys`; mid-iteration slices also keep `base-visited.s15keys` and `pending-candidates.s15keys` so resume matches an uninterrupted run. Checkpoint v2 records `requiresTransientRuns=false`, so resume does not depend on temporary `.s15run` files. `--resume-closure --start-layer K` continues that checkpoint, and `--checkpoint-interval N` writes intermediate checkpoints. In resume mode, `--max-expanded-states N` is an additional per-command budget. `--repair-closure-checkpoint DIR --layer K --dry-run` validates the stable checkpoint and reports a safe metadata repair path; `--cleanup-stale-runs` removes only transient run directories after validation. `--build-layer-external K` runs the same closure for one layer. `--max-expanded-states` limits expanded frontier states; `--max-states-per-layer` limits discovered/final states. A truncated checkpoint is resumable, not complete.

`--partitioned-closure` writes executable partitioned checkpoint snapshots under `work\layer-K\partitioned`. `--migrate-closure-checkpoint DIR --layer K` converts an existing flat checkpoint into the same format without rewriting the flat checkpoint. `--resume-partitioned-closure DIR --layer K --expanded-budget N` resumes from `DIR\work\layer-K\partitioned` using bucket-wise expansion and partitioned difference/union. V1 enforces `--expanded-budget` at bucket boundaries, so a slice can expand slightly more than the requested budget while preserving a valid checkpoint. `--validate-layers` validates migrated and resumed partitioned checkpoints.

Partition large key lists into bucketed membership indexes:

```powershell
.\build\sanpao15_cli.exe --partition-keyset build\layers-overnight\layer-15.s15layer --partition-output build\partitions\layer-15 --partition-buckets 256 --progress 1000000
.\build\sanpao15_cli.exe --validate-partition build\partitions\layer-15
.\build\sanpao15_cli.exe --inspect-partition build\partitions\layer-15
.\build\sanpao15_cli.exe --benchmark-partition build\partitions\layer-15 --sample 100000
```

New partitioned indexes default to stable `splitmix64_mod` buckets because real packed keys distributed poorly under direct `key_mod`; old `key_mod` manifests remain readable. Reader benchmarks support `--benchmark-mode existing|missing|mixed` and `--partition-cache-buckets N`.

The C++ partitioned keyset API also supports exact bucket-wise `partitionedDifference` and `partitionedUnion` for compatible partitioned inputs. These are storage building blocks for the next partitioned/block closure step.

Full tablebase dense-index foundation:

```powershell
.\build\sanpao15_cli.exe --tablebase-sizes
```

For each layer `k`, all legal positions are assigned a dense index:

```text
index = ((rank(cannonMask) * C(22,k) + rank(compressedSoldierMask)) * 2 + side)
```

This covers all positions, not just states reachable from the standard initial
position. The total full tablebase space is `18,787,540,800` states; outcome-only
storage is about `4.37 GiB` at 2 bits/state or `17.50 GiB` at 1 byte/state.
See `docs/full-tablebase.md` for the `.s15res` format and rank/unrank details.
Dense successor indexing maps legal moves from `(layer, denseIndex)` directly to
target dense ids, with same-layer and capture-to-lower-layer classification; see
`docs/dense-successor.md`.
Dense same-layer predecessor generation supports on-the-fly retrograde
propagation without storing a `vector<vector<uint32_t>>` predecessor graph.
The CLI/test path uses checked predecessors; streaming propagation uses the
fast predecessor path. The optimized streaming hot path uses position-aware
successor helpers, scratch predecessor buffers, a vector-backed worklist,
`uint8_t` remaining counters, and no per-predecessor parent unrank.
The low-k full tablebase prototype solves complete dense layers `k=0..3` into
outcome-only `.s15res` files and verifies sampled successor consistency; see
`docs/low-k-tablebase.md`.
Under the current ruleset all `k=0..3` dense states are immediate
`CannonWin`; the first layer where SoldierWin or Draw can appear is `k=4`.
The streaming low-k solver can solve `k=0..3` by default and allows an explicit
`k=4` benchmark with `--allow-k4`. The current optimized `k=4` run completed
in 04:48 with exact baseline counts: `CannonWin=33,398,108`,
`SoldierWin=736`, `Draw=250,156`, `Unknown=0`; see
`docs/scalable-tablebase-solver.md`.
`.s15res --validate-res` scans payload bytes instead of checking only headers.

Layer-local edge probe:

```powershell
.\build\sanpao15_cli.exe --probe-layer-edges build\partitions-splitmix\layer-15 --next-seed-partition build\partitions-splitmix\seeds-14 --sample-states 100000 --partition-cache-buckets 32 --progress 10000
```

The probe checks generated same-layer and capture-successor membership against partitioned indexes without storing edges or running retrograde. On partial layer files, missing successors are expected observations, not automatic errors.

## Run Tests

```bash
ctest --test-dir build --output-on-failure
```

## Run UI

```bash
cd ui
npm install
npm run dev
```

To use table analysis in the UI, place a generated table at:

```text
ui/public/tables/standard.s15tbl
```

The UI mirrors the rules in TypeScript and reads `.s15tbl` files directly. A later milestone will compile the C++ core to WebAssembly so the UI can call the same engine directly.

`Unknown` means not proven, not found in the table, or unresolved because the table was truncated. It must not be treated as `Draw`.

## Layout

```text
core/    C++ bitboard, move generation, rules, notation
solver/  C++ reachable graph and retrograde solver skeleton
cli/     command-line solver entry point
ui/      TypeScript + HTML + CSS playable interface
tests/   C++ unit tests
docs/    rules, solver notes, table/external formats, roadmap
```
