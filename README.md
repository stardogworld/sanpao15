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
.\build\sanpao15_cli.exe --solve-layer 4 --lower-res build\stream-min4\layer-03.s15res --out-res build\prod-layers\layer-04.s15res --encoding 2bit
.\build\sanpao15_cli.exe --verify-layer build\prod-layers\layer-04.s15res --lower-res build\stream-min4\layer-03.s15res --sample 10000
.\build\sanpao15_cli.exe --solve-layer-range 0 4 --out-dir build\range-k0-k4 --encoding 2bit --overwrite --clean-temp
.\build\sanpao15_cli.exe --solve-layer-range 0 4 --out-dir build\range-k0-k4 --encoding 2bit --resume
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
.\build\sanpao15_cli.exe --preflight-layer-range 8 10 --out-dir build\some-empty-dir --encoding 2bit --preflight-json build\some-empty-dir\preflight.json
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c"
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves --json
.\build\sanpao15_cli.exe --explore-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --max-plies 100
.\build\sanpao15_cli.exe --explore-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --max-plies 100 --json
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
fast predecessor path. The optimized streaming hot path uses a no-rank
initialization scan for same-layer moves, index-only predecessors, unchecked
packed-table access in validated loops, a vector-backed worklist, `uint8_t`
remaining counters, and no per-predecessor parent unrank.
The low-k full tablebase prototype solves complete dense layers `k=0..3` into
outcome-only `.s15res` files and verifies sampled successor consistency; see
`docs/low-k-tablebase.md`.
Under the current ruleset all `k=0..3` dense states are immediate
`CannonWin`; the first layer where SoldierWin or Draw can appear is `k=4`.
The streaming low-k solver can solve `k=0..3` by default and allows an explicit
`k=4` benchmark with `--allow-k4`. The current optimized Release `k=4` run
completed in 01:06 with exact baseline counts: `CannonWin=33,398,108`,
`SoldierWin=736`, `Draw=250,156`, `Unknown=0`; see
`docs/scalable-tablebase-solver.md`.
The production per-layer dense solver is `--solve-layer K`. It solves exactly
one layer, loads `--lower-res` for `K>=4`, writes a single `.s15res`, writes a
matching `.solve.json` stats file, and uses a `uint32_t` work queue and
predecessor index buffer internally. For `K=0..3`, `--lower-res` must be
omitted because the material rule resolves the whole layer. `--verify-layer`
validates one layer file plus its required lower layer without requiring a
directory of `0..K` results. The production API accepts `K=0..15`; this does
not mean a full `0..15` batch should be run without more scale checks.
`--solve-layer-range START END --out-dir DIR` automates the same production
solve one layer at a time, naming files `layer-NN.s15res` and
`layer-NN.solve.json`. For `K>=4`, it automatically uses `layer-(K-1).s15res`
from the same directory, so partial ranges such as `5..5` require a valid
`layer-04.s15res` already present. `--resume` skips existing valid layers,
`--overwrite` replaces completed layers, and `--clean-temp` removes stale
`layer-NN.s15res.tmp` files before starting. Each run writes `manifest.json`
with per-layer `completed`, `skipped`, or `failed` status. Recommended smoke
flow is range `0..4`, then range `0..5` with resume; do not jump straight to
full `0..15` without more timing and memory checks.
`--preflight-layer-range START END --out-dir DIR` is the dry-run companion for
range solving. It does not solve any layer and does not create `.s15res` files;
it inspects existing result files as valid/invalid/missing, checks whether the
required lower layer chain can resume, reads present `.solve.json` stats, and
estimates output bytes, queue memory, core RAM, recommended RAM, disk slack,
and remaining time. It writes `preflight.json` by default, or a custom
`--preflight-json PATH`, so the recommended full-run flow is:

```powershell
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
.\build\sanpao15_cli.exe --solve-layer-range 0 7 --out-dir build\prod-layers --encoding 2bit --resume
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
```

If a partial range starts at `START >= 4`, preflight requires
`layer-(START-1).s15res` to be valid unless that lower layer is also inside the
planned range and can be solved first.
`.s15res --validate-res` scans payload bytes instead of checking only headers.

Dense tablebase lookup is read-only and random-access:

```powershell
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves --json
.\build\sanpao15_cli.exe --explore-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --max-plies 100
```

The query locates `layer-NN.s15res` by soldier count, validates the header
against the current ruleset hash, dense state count, and encoding, then seeks
to only the outcome byte needed for the current position. With `--moves`, each
legal successor is read the same way and classified from the side to move:
winning, drawing, or losing. This is outcome-only WDL guidance; `.s15res` does
not contain distance, DTW, or fastest-win data.

`--explore-tablebase` samples one deterministic WDL line using the same
random-read `.s15res` lookup. It chooses moves that preserve the current WDL
result when possible, detects cycles, stops at `--max-plies`, and reports
`terminal`, `cycle`, `maxPlies`, `noLegalMoves`, `missingTablebase`, or
`lookupError`. It does not solve, regenerate, or load the full tablebase.

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

The UI can also query a local dense outcome tablebase. Use the Tablebase panel
to select a directory such as `build\prod-layers`, or select `layer-00.s15res`
through `layer-15.s15res` as files. The browser keeps file handles and reads
only the target header/outcome byte for the current position and each legal
successor; it does not load the full 4.7GB outcome set. Recommended moves are
grouped as winning, drawing, or losing using the same WDL-only rule as the CLI.

The Line Explorer panel samples and plays back one WDL-only line from the
current position. It supports max-plies control, previous/next, 700ms autoplay,
click-to-jump plies, stop reason display, last-move highlight, recommended-move
highlight, undo/redo/reset, and copy/paste notation. It is not a shortest-win
or fastest-draw explorer.

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
