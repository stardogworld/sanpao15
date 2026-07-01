# Dense Successor Indexing

Dense successor indexing connects the full tablebase dense id scheme to legal
move generation. Given a soldier-count layer `k` and dense index `i`, the code
unranks the position, generates legal moves with the shared rules engine,
applies each move, and ranks the target position back to a dense index.

This is full-tablebase logic. It enumerates legal positions by formula and does
not depend on whether a state is reachable from the standard initial position.

## Successor Kinds

Every legal move stays in the same soldier-count layer or captures one soldier:

```text
SameLayer              k -> k
CaptureToLowerLayer    k -> k - 1
```

Any other soldier-count change is treated as an internal error. For `k=0`, no
successor can be `CaptureToLowerLayer`.

The public result stores:

```text
fromSoldierCount
toSoldierCount
fromIndex
toIndex
move
kind
```

`toIndex` is always ranked in the target layer, so capture edges from layer `k`
point directly into the dense result table for layer `k-1`.

## Terminal Helper

`terminalOutcomeForDenseState(k, index)` reuses the existing terminal rules:

```text
no soldiers -> CannonWin
cannon side has no legal moves -> SoldierWin
side to move has no legal move -> opponent win
otherwise -> not terminal
```

The helper does not infer draws. `Unknown` remains only the non-terminal default
for later retrograde analysis.

## Stats API

`analyzeDenseLayerMoves(k, sampleLimit)` counts terminal states, same-layer
successors, capture successors, total successors, and max successors. A
`sampleLimit` of `0` means full-layer enumeration.

The CLI refuses implicit full scans for layers `k>=2`. Use `--sample N` for a
bounded probe or `--full` for an explicit full scan.

## CLI

Print successors for one dense state:

```powershell
.\build\sanpao15_cli.exe --dense-successors 15 0
```

Print full stats for small layers:

```powershell
.\build\sanpao15_cli.exe --dense-move-stats 0
.\build\sanpao15_cli.exe --dense-move-stats 1
```

Probe a larger layer with a sample:

```powershell
.\build\sanpao15_cli.exe --dense-move-stats 2 --sample 100000
```

Explicitly enumerate a larger full layer only when the size is acceptable:

```powershell
.\build\sanpao15_cli.exe --dense-move-stats 2 --full
```

## Role In Full Tablebase Retrograde

The low-k full tablebase prototype can now iterate dense state ids directly:

```text
for index in layer k:
  terminal = terminalOutcomeForDenseState(k, index)
  successors = generateDenseSuccessors(k, index)
```

Same-layer edges support cyclic retrograde inside one layer. Capture edges link
to already-solved lower layers when solving upward from `k=0`.

## Difference From Reachability Edge Probe

The partitioned reachability edge probe checks whether generated successors are
present in externally discovered keysets for the standard initial position. It
is sparse and may report missing successors when the layer files are partial.

Dense successor indexing does not query keysets. Every legal target is ranked
into the complete full-tablebase layer space, so successor ids are available
even for states that are unreachable from the standard initial position.
