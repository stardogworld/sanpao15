# Scalable Tablebase Solver Notes

The full tablebase line now has a streaming layer-solver foundation. It is
separate from the reachable-state layer closure work and covers every legal
dense position in a soldier-count layer.

## Current Ruleset Boundary

The active ruleset is:

```text
sanpao15-min-four-soldiers
rulesetHash = 5994631263128018692 (0x5331355F76325F04)
```

The material terminal rule is checked before generated moves:

```text
soldierCount < 4 -> CannonWin
```

Therefore `k=0..3` are all immediate `CannonWin`. The first non-material layer
where `SoldierWin` or `Draw` can appear is `k=4`.

## Streaming Solver Foundation

The original low-k prototype stores a full same-layer predecessor graph as:

```cpp
std::vector<std::vector<uint32_t>>
```

That is useful for tiny layers, but it is not a scalable storage shape. The
streaming solver avoids storing predecessor edges. It keeps:

```text
2-bit outcome table
remaining-successor counter array
resolved queue
```

When a child is resolved, same-layer predecessors are regenerated directly from
the child position. The predecessor generator has two modes:

```cpp
DensePredecessorValidation::Checked
DensePredecessorValidation::None
```

The default CLI/test path keeps checked validation and verifies each parent by
calling the successor generator. The streaming solver uses the fast `None`
path during propagation, which only performs local legality checks and avoids
the expensive successor roundtrip.

Capture-to-lower-layer edges are not regenerated during propagation. They are
handled during initialization by looking up the already-solved lower layer.

## Dense Layer Performance Pass

The `dense-layer-solver-performance-pass` optimized the streaming hot path
without changing outcomes or move semantics:

- initialization unranks each dense state once, then uses position-aware
  successor and terminal helpers;
- successor generation reuses a scratch vector and avoids per-state temporary
  move vectors;
- propagation no longer unranks every predecessor parent, deriving the parent
  side from the child side instead;
- predecessor generation has a scratch-buffer API, so streaming propagation
  reuses one `std::vector<DensePredecessor>`;
- the propagation queue is a vector-backed worklist with `queuePeak` reported
  as maximum pending queue length;
- the remaining-successor counter array is `uint8_t` with a runtime overflow
  guard.

The `k=4` benchmark is unchanged in result counts:

```text
states: 33,649,000
CannonWin: 33,398,108
SoldierWin: 736
Draw: 250,156
Unknown: 0
terminalStates: 184
sameLayerEdges: 282,650,840
captureEdges: 15,800,400
```

Timing improved from the previous 17:31 baseline to 04:48:

```text
initialization: 10:20 -> 01:56
propagation:    07:11 -> 02:52
finalize:       0.25s -> 0.13s
total:          17:31 -> 04:48
```

## K4 Sub-3 Hot-Path Pass

The `k4-sub3-hotpath-pass` confirmed a separate Release build first. Release
baseline was already below the three-minute target:

```text
initialization: 35.5s
propagation:    54.0s
finalize:       0.08s
total:          01:30
```

The pass then tightened the single-thread hot path:

- streaming initialization uses a solver-specific scan and no longer computes
  `toIndex` for same-layer moves;
- only capture-to-lower-layer moves compute a lower-layer dense index;
- fast predecessor generation skips checked-mode sort/dedup;
- propagation uses an index-only predecessor API and does not build
  `DensePredecessor` or `Move` structs;
- packed 2-bit outcome tables expose checked and unchecked get/set APIs, with
  unchecked calls limited to solver loops where dense indexes are already
  validated;
- terminal-with-successors avoids an extra cannon-move scan when the side to
  move is Cannon.

The Release `k=4` run after this pass stayed count-identical and finished in
01:06:

```text
initialization: 13.6s
propagation:    52.5s
finalize:       0.06s
total:          01:06
```

## CLI

Default streaming low-k solve:

```powershell
.\build\sanpao15_cli.exe --solve-lowk-streaming 3 --out-dir build\stream-min4 --encoding 2bit
.\build\sanpao15_cli.exe --verify-lowk build\stream-min4 --max-k 3 --sample 10000
```

`k=4` is the first meaningful benchmark and must be requested explicitly:

```powershell
.\build\sanpao15_cli.exe --solve-lowk-streaming 4 --allow-k4 --out-dir build\stream-k4 --encoding 2bit
.\build\sanpao15_cli.exe --verify-lowk build\stream-k4 --max-k 4 --sample 10000
```

Do not run `k=6` as a default smoke test. The `k=4` and `k=5` results are fast
enough that the next step is range/resume validation before considering `k=6`.

Production single-layer solve:

```powershell
.\build\sanpao15_cli.exe --solve-layer 4 --lower-res build\stream-min4\layer-03.s15res --out-res build\prod-layers\layer-04.s15res --encoding 2bit
.\build\sanpao15_cli.exe --verify-layer build\prod-layers\layer-04.s15res --lower-res build\stream-min4\layer-03.s15res --sample 10000
```

`--solve-layer` supports `k=0..15`, but it solves only one layer per command.
For `k=0..3`, the lower layer must be omitted because every state is material
terminal `CannonWin`. For `k>=4`, `--lower-res` is required and strictly
validated against the current ruleset hash, `k-1`, dense state count, encoding,
and payload shape. The output `.s15res` is written through a temporary file,
validated, then renamed into place; a matching `.solve.json` stats file is
written next to it.

Production range solve:

```powershell
.\build\sanpao15_cli.exe --solve-layer-range 0 4 --out-dir build\range-k0-k4 --encoding 2bit --overwrite --clean-temp
.\build\sanpao15_cli.exe --solve-layer-range 0 4 --out-dir build\range-k0-k4 --encoding 2bit --resume
```

The range runner solves `START..END` in order, uses `layer-(k-1).s15res`
automatically for `k>=4`, writes `manifest.json`, and supports layer-level
resume by skipping existing valid target layers. It does not checkpoint
mid-propagation. `--overwrite` is rejected together with `--resume`; stale
`.s15res.tmp` files require `--clean-temp` or manual cleanup.

Range preflight:

```powershell
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
```

Preflight is a dry run. It validates existing `.s15res` files, detects
invalid/missing layers, checks whether the lower-layer chain can resume, reads
available `.solve.json` stats, estimates queue memory, core RAM, recommended
RAM, disk slack, and remaining time, then writes `preflight.json`. It does not
solve new layers and does not create `.s15res` files. Use it before widening a
range:

```powershell
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
.\build\sanpao15_cli.exe --solve-layer-range 0 7 --out-dir build\prod-layers --encoding 2bit --resume
.\build\sanpao15_cli.exe --preflight-layer-range 0 15 --out-dir build\prod-layers --encoding 2bit
```

Inspect same-layer predecessors for one dense state:

```powershell
.\build\sanpao15_cli.exe --dense-predecessors 4 0
```

Read-only dense lookup after a range has produced `.s15res` files:

```powershell
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves
.\build\sanpao15_cli.exe --query-tablebase build\prod-layers --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" --moves --json
```

Lookup does not call the solver and does not load full layers. It validates the
selected layer header, seeks to the target outcome byte, and repeats that
single-byte read for legal successors when recommendations are requested.

## Output

The streaming solver still writes outcome-only `.s15res` files. It does not
store distance, DTW, or best moves.

The production per-layer path keeps the public dense index type as `uint64_t`
but uses a `uint32_t` resolved queue and `uint32_t` predecessor-index scratch
buffer after checking that the selected layer has at most `UINT32_MAX` states.

Useful stats printed per layer include:

```text
terminalStates
sameLayerEdges
captureEdges
resolvedByTerminal
resolvedByLowerLayer
resolvedByPropagation
drawAfterQueue
maxSuccessors
maxRemaining
queuePeak
estimatedMemoryBytes
initializationSeconds
propagationSeconds
finalizeSeconds
predecessorCalls
generatedPredecessors
maxPredecessors
```

## Format Hardening

`.s15tbl` and `.s15res` files carry the active ruleset hash. Default `.s15tbl`
loading rejects incompatible hashes, and table analysis checks the hash again
before using an in-memory `ResultTable`.

`.s15tbl` loading also rejects trailing bytes, duplicate keys, unknown flags,
and invalid best-move square fields. `.s15res --validate-res` scans payload
bytes: byte encoding must contain only valid outcome values, and packed 2-bit
payloads are checked for valid size and unused-bit cleanliness when applicable.

## Next Direction

Because optimized Release `k=4` and `k=5` complete with stable counts and the
range runner now exists, the recommended next step is range `0..5` using
resume. File-backed or mmap outcome tables are still likely needed for much
larger layers, but they should follow measured range behavior. Distance and
best move data should remain optional side data until outcome solving is stable.
