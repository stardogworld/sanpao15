# Full Tablebase Dense Indexing

The full tablebase route covers every legal position in a soldier-count layer,
not only the subset reachable from the standard initial position. This is a
different target from the external reachability line.

## Why Dense Indexing

Reachability files store explicit packed keys because they describe a sparse
subset found by search. A full tablebase should not store a key list: the whole
legal layer is known by formula, so each legal position can be mapped directly
to a dense integer in `[0, stateCount(k))`.

The result table can then store only outcomes in dense-index order.

The companion material-target-distance prototype stores secondary material
guidance in `.s15mtd` files. It is deliberately separate from `.s15res`: WDL
outcome remains the first objective and the `.s15res` format is unchanged. See
`docs/material-target-distance-tablebase.md`.

## Layer Size

For soldier count `k`:

```text
cannonMask: choose 3 of 25 squares
soldierMask: choose k of the remaining 22 squares
side: 2 choices

stateCount(k) = C(25,3) * C(22,k) * 2
```

Known sizes:

```text
k=0:      4,600 states
k=11: 3,244,987,200 states
k=15:   784,502,400 states
total: 18,787,540,800 states
```

Outcome-only storage:

```text
2-bit packed: 4,696,885,200 bytes, about 4.37 GiB
1-byte debug: 18,787,540,800 bytes, about 17.50 GiB
```

The CLI prints the full table:

```powershell
.\build\sanpao15_cli.exe --tablebase-sizes
```

Current ruleset metadata:

```text
name: sanpao15-min-four-soldiers
summary: soldier-count-below-four-is-cannon-win
hash: 5994631263128018692 (0x5331355F76325F04)
```

## Combinadic Order

Combination ranking uses colexicographic order. For set bits:

```text
b_0 < b_1 < ... < b_(k-1)
rank = sum_i C(b_i, i + 1)
```

This order is stable and compact; it is not the same as sorting masks by their
numeric value.

## Dense Position Index

For a position with exactly three cannons and `k` soldiers:

```text
cannonRank = rankCombination(cannonMask, 25, 3)
soldierLocalMask = soldierMask compressed into the 22 non-cannon squares
soldierRank = rankCombination(soldierLocalMask, 22, k)
sideRank = 0 for Cannon, 1 for Soldier

index = ((cannonRank * C(22,k) + soldierRank) * 2 + sideRank)
```

Unranking reverses the formula:

```text
sideRank = index % 2
tmp = index / 2
soldierRank = tmp % C(22,k)
cannonRank = tmp / C(22,k)
```

The cannon combination is unranked in 25 squares. The soldier combination is
unranked in 22 local non-cannon squares and expanded back to the 25-square
board.

## Dense Outcome Tables

The foundation provides two outcome arrays:

```text
DenseOutcomeTable          1 byte per state
PackedOutcomeTable2Bit    2 bits per state
```

Outcome encoding preserves the existing semantics:

```text
Unknown = 0
CannonWin = 1
SoldierWin = 2
Draw = 3
```

`Unknown` is not `Draw`.

## .s15res Format v1

`.s15res` stores dense result payloads in dense index order. It stores no packed
position keys, distances, or best moves.

Header fields are written explicitly little-endian:

```text
magic[8]      "S15RES1\0"
uint32_le     version = 1
uint64_le     rulesetHash
uint32_le     soldierCount
uint64_le     stateCount
uint32_le     encoding: 1 = byte, 2 = packed 2-bit
uint64_le     payloadBytes
payload       outcome array in dense index order
```

Useful CLI commands:

```powershell
.\build\sanpao15_cli.exe --create-empty-res 0 build\empty-k0.s15res --encoding 2bit
.\build\sanpao15_cli.exe --inspect-res build\empty-k0.s15res
.\build\sanpao15_cli.exe --validate-res build\empty-k0.s15res
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves --json
.\build\sanpao15_cli.exe --explore-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --max-plies 100
.\build-release\sanpao15_cli.exe --serve-ui --tablebase-dir build\prod-layers --ui-dir ui\dist --port 8787
```

Do not commit generated `.s15res` files.

Do not commit generated `.s15mtd` files either. They use dense index order like
`.s15res`, but store 12-bit entries containing `materialTarget` and
`targetDistance`.

`inspect-res` reads only the header and file size. `validate-res` additionally
scans the payload and rejects invalid byte-encoded outcomes. Packed 2-bit
layers naturally encode only values `0..3`; validation also checks packed
unused bits when a file shape can contain them.

`--query-tablebase DIR --position TEXT` performs a read-only random-access
lookup against `DIR/layer-NN.s15res`. It validates the file header and file
size, computes the dense index, then reads only one payload byte for the target
outcome. Byte and packed 2-bit encodings are both supported. With `--moves`,
the CLI generates legal successors and reads one outcome byte for each
successor, grouping moves as winning, drawing, or losing from the current side
to move. With `--json`, the same data is emitted for tools or UI integration.
This is WDL-only information: `.s15res` has no DTW, distance, or best-move
payload.

`--explore-tablebase DIR --position TEXT --max-plies N [--json]` uses the same
random-access lookup to sample one deterministic WDL line. The selection rule
preserves the current result when possible: winning positions keep the moving
side winning, draw positions keep Draw, and losing positions choose the best
practical WDL tier. Ties prefer captures, then lower successor soldier count
for captures, then lexical move order. The result includes cycle detection and
stop reasons. It does not load full layers and does not provide shortest-win,
fastest-draw, DTW, or DTC information.

The TypeScript UI defaults to Chinese labels and uses the same `.s15res`
random-read model in the browser. It keeps `File` handles for selected layer
files, validates headers, and reads only the target outcome byte for the current
position and legal successors. The UI does not load the complete 4.7 GiB outcome
set into memory. Its line explorer is also WDL-only: rows show hand number,
side to move, chosen move, successor outcome, classification, soldier count,
alternatives, cycle markers, and stop reason, but no distance-to-win or
shortest-draw claim.

The browser UI also includes an arbitrary-position editor and best-move
analysis layer. Users can switch between analysis mode and edit mode, freely
place or erase `炮`/`兵`, move pieces without rule constraints while composing a
position, switch the side to move, paste relaxed position notation, and then
query when the position is inside the dense tablebase domain. The validation
rule is exactly the dense domain: 3 cannons, 0..15 soldiers, no overlap, and the
matching `layer-NN.s15res` loaded. For queryable positions, the UI shows the
dense index, current WDL outcome, best WDL moves, grouped legal moves,
cannon-to-move versus soldier-to-move comparison, and successor-outcome labels
on legal target squares. This remains WDL-only guidance and still performs
random `.s15res` byte reads rather than loading full layers.

For local desktop use, `--serve-ui` starts a read-only C++ HTTP backend and
serves the static UI from `--ui-dir`:

```powershell
.\build-release\sanpao15_cli.exe --serve-ui --tablebase-dir build\prod-layers --ui-dir ui\dist --port 8787
```

The backend defaults to `127.0.0.1:8787`. It validates `layer-00.s15res` through
`layer-15.s15res` metadata at startup and caches only the metadata. Query,
recommendation, side-comparison, and line-explorer API calls still random-read
individual `.s15res` bytes. Static file service is limited to `--ui-dir` and
rejects path traversal; the API exposes no arbitrary file-read, write, delete,
or tablebase mutation endpoint. If the backend is absent, the UI keeps the
browser directory/file picker fallback.

## Dense Successor Indexing

Dense successor indexing is available for the full tablebase route. Given a
layer `k` and dense index, the API unranks the position, generates legal moves
with the shared rules engine, applies each move, and ranks the target position
in its target layer.

Successors are classified as:

```text
SameLayer              k -> k
CaptureToLowerLayer    k -> k - 1
```

The CLI can inspect one state or sample a layer:

```powershell
.\build\sanpao15_cli.exe --dense-successors 15 0
.\build\sanpao15_cli.exe --dense-move-stats 2 --sample 100000
```

See `docs/dense-successor.md` for the API and command details.

## Low-K Solver Prototype

The low-k full tablebase prototype solves complete dense layers `0..K` for
`K <= 3` and writes outcome-only `.s15res` files:

```powershell
.\build\sanpao15_cli.exe --solve-lowk 2 --out-dir build\lowk-smoke --encoding 2bit
.\build\sanpao15_cli.exe --verify-lowk build\lowk-smoke --max-k 2 --sample 10000
```

It is not a reachable-subset solve. It enumerates every legal dense state in
each layer, uses solved lower-layer outcomes for capture edges, performs
same-layer retrograde for cycles, and finalizes remaining Unknown states as
Draw. The prototype stores only outcomes, not distance or best moves.

Under the current minimum-four-soldiers ruleset, every state in `k=0..3` is
terminal `CannonWin`. Earlier low-k results from the old ruleset, including
`k=3 SoldierWin=32` and `Draw=20,096`, are intentionally invalidated by the new
ruleset hash.

See `docs/low-k-tablebase.md` for the detailed semantics and observed scale.

The streaming low-k solver adds on-the-fly same-layer predecessor generation so
retrograde propagation no longer needs a resident
`std::vector<std::vector<uint32_t>>` predecessor graph:

```powershell
.\build\sanpao15_cli.exe --solve-lowk-streaming 3 --out-dir build\stream-min4 --encoding 2bit
.\build\sanpao15_cli.exe --dense-predecessors 4 0
```

The predecessor API keeps a checked mode for CLI/tests and a fast mode for
streaming propagation:

```cpp
generateDensePredecessors(k, childIndex, DensePredecessorValidation::Checked)
generateDensePredecessors(k, childIndex, DensePredecessorValidation::None)
```

The streaming hot path now uses position-aware successor helpers, a
solver-specific initialization scan, an index-only predecessor API, a
vector-backed worklist, unchecked packed-table access in validated loops, and a
`uint8_t` remaining counter guarded against overflow. Initialization does not
rank same-layer successor targets, and propagation derives the parent side from
the child side, so it does not unrank each predecessor parent.

`k=4` is available only as an explicit benchmark:

```powershell
.\build\sanpao15_cli.exe --solve-lowk-streaming 4 --allow-k4 --out-dir build\stream-k4 --encoding 2bit
```

The current optimized Release `k=4` run completed in 01:06 with exact expected counts:

```text
CannonWin=33,398,108
SoldierWin=736
Draw=250,156
Unknown=0
sameLayerEdges=282,650,840
captureEdges=15,800,400
```

See `docs/scalable-tablebase-solver.md` for the streaming solver notes.

## Production Per-Layer Solver

The production entry point solves one dense layer at a time:

```powershell
.\build\sanpao15_cli.exe --solve-layer 4 --lower-res build\stream-min4\layer-03.s15res --out-res build\prod-layers\layer-04.s15res --encoding 2bit
.\build\sanpao15_cli.exe --verify-layer build\prod-layers\layer-04.s15res --lower-res build\stream-min4\layer-03.s15res --sample 10000
```

For `k=0..3`, the current ruleset's material rule resolves the whole layer as
`CannonWin`, so `--lower-res` must be omitted. For `k>=4`, `--lower-res` is
required and must be a valid `.s15res` file for exactly `k-1` with the current
ruleset hash and matching state count.

`--solve-layer` creates the output parent directory, refuses to overwrite an
existing `.s15res` or `.solve.json` unless `--overwrite` is passed, writes a
temporary `.s15res.tmp`, validates it, then renames it into place. On success it
also writes `layer-NN.solve.json` next to the result file with outcome counts,
edge counts, queue/predecessor stats, timings, output bytes, and estimated
working memory. The production path keeps dense indexes public as `uint64_t`
but uses a `uint32_t` queue and `uint32_t` predecessor-index scratch buffer
internally after checking that the layer is uint32-addressable. It supports
`k=0..15`; larger-layer feasibility still depends on measured memory and time.

## Production Range Solver

`--solve-layer-range START END` runs the production single-layer solver in
order and chains lower layers automatically:

```powershell
.\build\sanpao15_cli.exe --solve-layer-range 0 4 --out-dir build\range-k0-k4 --encoding 2bit --overwrite --clean-temp
.\build\sanpao15_cli.exe --solve-layer-range 0 4 --out-dir build\range-k0-k4 --encoding 2bit --resume
```

Output names are fixed:

```text
layer-00.s15res
layer-00.solve.json
...
manifest.json
```

For `k>=4`, the runner validates and loads `outputDir/layer-(k-1).s15res`.
If `START >= 4`, that lower file must already exist and validate before the
range starts. `--resume` skips existing valid target layers; if an existing
target layer fails validation, the run stops and records a failed manifest
entry. `--overwrite` allows replacing existing targets and is rejected together
with `--resume`. `--clean-temp` removes stale `layer-NN.s15res.tmp` files for
the selected range before solving; without it, stale temp files stop the run so
a half-written table is not mistaken for state.

Every range run writes `manifest.json` with format
`sanpao15-layer-range-manifest`, version, ruleset name/hash, encoding,
start/end layers, per-layer status (`completed`, `skipped`, or `failed`),
result/stats paths, state count, output bytes, total seconds, and outcome
counts. The recommended validation path is range `0..4`, then range `0..5`
using resume. A direct full `0..15` run is still not recommended without more
scale data.

## Range Preflight

Before a long production range, run the dry-run preflight:

```powershell
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
.\build\sanpao15_cli.exe --preflight-layer-range 8 10 --out-dir build\some-empty-dir --encoding 2bit
```

Preflight does not call the solver, does not write `.s15res`, and does not
change existing result layers. It may create or overwrite `preflight.json`.
For each layer it reports:

```text
stateCount
2-bit and byte output payload sizes
selected output bytes
remaining-counter bytes
lower-layer payload bytes
estimated queue bytes
estimated core memory and recommended RAM
estimated seconds
result status: missing, valid, or invalid
stats JSON presence
manifest entry presence
skip/solve/error action
lower-layer availability
risk/error text
```

Existing `.s15res` files are validated against the current ruleset hash,
soldier count, dense state count, encoding, payload size, and payload shape.
Present `.solve.json` files are scanned for simple numeric fields such as
`stateCount`, `queuePeak`, `totalSeconds`, and `estimatedMemoryBytes`; these
stats refine queue and time estimates. If no stats are present, the estimator
falls back to the measured `k=4..6` production runs.

The default JSON report is:

```text
outputDir/preflight.json
```

`--preflight-json PATH` overrides that location. The JSON format is
`sanpao15-layer-range-preflight`, version 1. It records ruleset metadata,
encoding, range, output directory, disk space check, totals, and the same
per-layer fields shown in the CLI table.

Recommended full-run flow:

```powershell
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
.\build\sanpao15_cli.exe --solve-layer-range 0 7 --out-dir build\prod-layers --encoding 2bit --resume
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
```

If `START >= 4`, preflight requires `outputDir/layer-(START-1).s15res` to be
valid unless that lower layer is part of the planned range and can be produced
first. This catches missing-lower scenarios such as range `8..10` in an empty
directory before any solve begins.

## Solver Direction

The full tablebase solver should proceed from low soldier counts upward:

```text
k=0 -> k=1 -> ... -> k=15
```

Lower layers are needed because cannon captures move from layer `k` to `k-1`.
The current foundation assigns dense ids, stores outcomes, maps legal moves to
dense successor ids, and includes a low-k outcome-only retrograde prototype. It
does not yet implement scalable full `0..15` solving, distances, or best moves.

## Relationship To Reachability

There are now two lines:

```text
A. Reachability from the standard initial position
B. Full tablebase dense-index solver
```

The reachability line uses external closure, partitioned keysets, and resumable
checkpoints to explore the subset reachable from
`SSSSS/SSSSS/SSSSS/...../.CCC. c`.

The full tablebase line ignores initial reachability and covers every legal
position in each layer by formula. The current main development direction is the
full tablebase foundation.
