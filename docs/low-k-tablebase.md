# Low-K Full Tablebase Prototype

The low-k tablebase solver is the first full-state-space solver on the dense
tablebase line. It does not start from the standard initial position and does
not use reachable-state layer files. For each soldier count `k`, it enumerates
every legal dense state in that full layer.

The prototype is intentionally limited to:

```text
k = 0..3
```

Larger layers need a scalable architecture before they should be attempted.

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
no soldiers -> CannonWin
cannon side has no legal moves -> SoldierWin
side to move has no legal move -> opponent win
```

This is especially important for `k=0`: all 4,600 states are `CannonWin`, even
when `generateLegalMoves` would still produce cannon moves.

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

The current prototype stores same-layer predecessors as
`std::vector<std::vector<uint32_t>>`. This is fine for `k=0..3`, but it is not
the intended storage shape for larger full layers.

## CLI

Solve layers `0..K` and write `.s15res` files:

```powershell
.\build\sanpao15_cli.exe --solve-lowk 2 --out-dir build\lowk-smoke --encoding 2bit
```

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
enough for a smoke run.

## Observed Prototype Scale

On the current development machine, 2-bit output produced:

```text
k  states       CannonWin  SoldierWin  Draw    Unknown  sameEdges   captureEdges
0  4,600        4,600      0           0       0        0           0
1  101,200      101,200    0           0       0        566,720     13,860
2  1,062,600    1,062,600  0           0       0        7,084,000   277,200
3  7,084,000    7,063,872  32          20,096  0        53,838,376  2,633,400
```

The `k=3` run completed, but its predecessor graph is already large enough that
future layers should not simply extend this in-memory vector prototype.

## Next Direction

The next full tablebase work should design the scalable `0..15` architecture:

```text
CSR or flat predecessor storage
file-backed/mmap outcome tables
layer/block processing
optional distance/DTW side data
```

Distance can be added later as temporary DTW arrays or an optional sidecar
format such as `.s15dtw`; it is deliberately not part of this prototype.
