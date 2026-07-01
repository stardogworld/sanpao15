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
the child position with `generateDensePredecessors(k, childIndex)`.

Capture-to-lower-layer edges are not regenerated during propagation. They are
handled during initialization by looking up the already-solved lower layer.

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

Do not run `k=5` with this prototype.

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
```

## Next Direction

The recommended next step is file-backed or mmap outcome tables. The in-memory
2-bit output array is compact, but production `0..15` solving should avoid
keeping every large layer resident in ordinary heap vectors. Distance and best
move data should remain optional side data until outcome solving is stable.
