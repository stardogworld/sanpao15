# Low-K Full Tablebase Prototype

The low-k tablebase solver is the first full-state-space solver on the dense
tablebase line. It does not start from the standard initial position and does
not use reachable-state layer files. For each soldier count `k`, it enumerates
every legal dense state in that full layer.

The original graph-backed prototype is intentionally limited to:

```text
k = 0..3
```

The streaming prototype also supports `k=0..3` by default and allows `k=4`
only when requested explicitly with `--allow-k4`.

## Solve Order

Layers are solved from low to high:

```text
k=0 -> k=1 -> k=2 -> k=3
```

This order is required because cannon captures move from layer `k` to already
solved layer `k-1`.

## Outcome Semantics

The solver is outcome-only:

```text
Unknown = 0
CannonWin = 1
SoldierWin = 2
Draw = 3
```

It does not store distance or best move. `Unknown` is only a temporary state
during solving; solved `.s15res` outputs should not contain Unknown outcomes.

Terminal states are resolved before their generated legal moves are considered:

```text
soldierCount < 4 -> CannonWin
cannon side has no legal moves -> SoldierWin
side to move has no legal move -> opponent win
```

The material rule has priority over cannon-stuck loss detection. This means all
states in `k=0..3` are immediate `CannonWin`, even when `generateLegalMoves`
would still produce moves or the cannon side would otherwise be stuck.

## Retrograde Logic

For each non-terminal state, successors are split into:

```text
SameLayer              k -> k
CaptureToLowerLayer    k -> k - 1
```

Capture successor outcomes are known from the lower layer. Same-layer
successors are resolved by retrograde propagation over a layer-local predecessor
graph.

A state is a win for the side to move if at least one successor is a win for
that same side. A state is a loss for the side to move if every successor is a
win for the opponent. After the propagation queue is empty, remaining Unknown
states are finalized as Draw.

The graph-backed prototype stores same-layer predecessors as
`std::vector<std::vector<uint32_t>>`. This is fine for `k=0..3`, but it is not
the intended storage shape for larger full layers.

The streaming solver keeps only the output table, a remaining-successor counter
array, and the resolved queue. During propagation it regenerates same-layer
predecessors on demand with the fast predecessor path. The checked predecessor
path remains available for CLI inspection and tests.
Capture-to-lower-layer successors are handled during initialization by looking
up the lower layer table.

The optimized streaming backend keeps initialization to one dense unrank per
state and uses a solver-specific scan that does not rank same-layer successor
targets. It uses an index-only predecessor API in propagation, a vector-backed
worklist, unchecked packed-table access in validated loops, and a `uint8_t`
remaining counter with a runtime guard. Propagation avoids per-predecessor
parent unrank by deriving the parent side from the child side.

Both solver entry points reset their output table to `Unknown` before solving,
so reusing a previously-filled `PackedOutcomeTable2Bit` cannot contaminate
retrograde propagation.

## CLI

Solve layers `0..K` and write `.s15res` files:

```powershell
.\build\sanpao15_cli.exe --solve-lowk 2 --out-dir build\lowk-smoke --encoding 2bit
```

Solve the same material-terminal range with the streaming backend:

```powershell
.\build\sanpao15_cli.exe --solve-lowk-streaming 3 --out-dir build\stream-min4 --encoding 2bit
```

Inspect same-layer predecessors for one dense child:

```powershell
.\build\sanpao15_cli.exe --dense-predecessors 4 0
```

Attempt the first non-material layer only as an explicit benchmark:

```powershell
.\build\sanpao15_cli.exe --solve-lowk-streaming 4 --allow-k4 --out-dir build\stream-k4 --encoding 2bit
```

Production per-layer solving uses `--solve-layer` instead of keeping `0..K`
layers resident in memory:

```powershell
.\build\sanpao15_cli.exe --solve-layer 4 --lower-res build\stream-min4\layer-03.s15res --out-res build\prod-layers\layer-04.s15res --encoding 2bit
.\build\sanpao15_cli.exe --verify-layer build\prod-layers\layer-04.s15res --lower-res build\stream-min4\layer-03.s15res --sample 10000
```

`--solve-lowk-streaming` remains a prototype and smoke-test command, limited to
`k<=4` with `--allow-k4` for the benchmark layer. `--solve-layer` is the
production command and accepts `k=0..15`. For `k=0..3`, `--lower-res` is
rejected because the material rule resolves the whole layer. For `k>=4`, the
lower result is required and must be a valid current-ruleset `.s15res` for
exactly `k-1`.

Generated names:

```text
layer-00.s15res
layer-01.s15res
layer-02.s15res
layer-03.s15res
```

Verify headers and sampled outcome consistency:

```powershell
.\build\sanpao15_cli.exe --verify-lowk build\lowk-smoke --max-k 2 --sample 10000
```

`--sample 0` verifies every state. For `k=3`, sampled verification is usually
enough for a smoke run. Full verification streams indexes directly instead of
allocating a full `vector<uint64_t>` sample list.

## Observed Prototype Scale

On the current development machine, 2-bit output produced:

```text
k  states       CannonWin  SoldierWin  Draw  Unknown  sameEdges  captureEdges
0  4,600        4,600      0           0     0        0          0
1  101,200      101,200    0           0     0        0          0
2  1,062,600    1,062,600  0           0     0        0          0
3  7,084,000    7,084,000  0           0     0        0          0
```

The old ruleset once produced `k=3 SoldierWin=32` and `Draw=20,096`; those
results are obsolete. Under `sanpao15-min-four-soldiers`, all of those states
are intercepted by the material rule and become `CannonWin`. The first layer
where SoldierWin or Draw can appear is `k=4`; it should be treated as a
benchmark for the streaming solver rather than a default unit-test workload.

Optimized Release `k=4` benchmark:

```text
states: 33,649,000
CannonWin: 33,398,108
SoldierWin: 736
Draw: 250,156
Unknown: 0
terminalStates: 184
sameLayerEdges: 282,650,840
captureEdges: 15,800,400
initialization: 13.6s
propagation: 52.5s
finalize: 0.06s
total: 01:06
```

## Next Direction

The next full tablebase work should cautiously benchmark `k=5` through
`--solve-layer`. Larger layers will still need the scalable `0..15`
architecture:

```text
CSR or flat predecessor storage
file-backed/mmap outcome tables
layer/block processing
optional distance/DTW side data
```

Distance can be added later as temporary DTW arrays or an optional sidecar
format such as `.s15dtw`; it is deliberately not part of this prototype.
