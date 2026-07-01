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

Do not run `k=5` as a default smoke test. The `k=4` result is now fast enough
that the next step can add a per-layer production CLI, then run a cautious
`k=5` benchmark.

Inspect same-layer predecessors for one dense state:

```powershell
.\build\sanpao15_cli.exe --dense-predecessors 4 0
```

## Output

The streaming solver still writes outcome-only `.s15res` files. It does not
store distance, DTW, or best moves.

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

Because optimized `k=4` completes in under five minutes with exact baseline
counts, the recommended next step is a per-layer production CLI for the dense
streaming solver. File-backed or mmap outcome tables are still likely needed
for much larger layers, but they should follow a production per-layer solve
path and another measured `k=5` benchmark. Distance and best move data should
remain optional side data until outcome solving is stable.
