# Solver

The solver is designed for finite cyclic-game analysis.

## Why Not Plain Minimax

The game graph can contain repeated positions. A recursive minimax that assumes every line eventually reaches a leaf can either loop forever or collapse cycles into incorrect win/loss claims. Draws need to emerge from the graph structure after all forced wins and losses are propagated.

## Current Algorithm

1. Build a reachable graph from the requested start position.
2. Store `states` and graph edges using the selected backend.
3. Mark terminal states using the core rules.
4. Use a distance-prioritized queue to propagate proven outcomes backward:
   - If a predecessor can move to a state won by the predecessor's side, mark the predecessor as that win.
   - Otherwise decrement its remaining unresolved successors.
   - If all successors are proven wins for the opponent, mark the predecessor as an opponent win.
5. If the graph is complete, convert unresolved states to `Draw`.

## Distance And Best Move

Every solved table entry stores:

- `outcome`: `Unknown`, `CannonWin`, `SoldierWin`, or `Draw`.
- `distance`: ply distance to a win/loss terminal under best play, or `-1` for draw/unknown.
- `bestMove`: absent for terminal and unknown states.

Winning states choose the legal move that reaches the same side's win with the shortest child distance. Losing states choose the legal move that maximizes delay before the opponent's win. Draw states use `distance = -1` and prefer a move to another draw when one is available.

## Graph Backends

The default backend is CSR:

```text
successors of u:
succFlat[succOffset[u] ... succOffset[u + 1])

predecessors of u:
predFlat[predOffset[u] ... predOffset[u + 1])
```

State ids are stored as `uint32_t`, and offsets are stored as `uint64_t`. The builder checks state-id overflow instead of silently wrapping. CSR avoids per-state `vector` overhead and is the preferred backend for scale probing and future full solves.

The previous adjacency-list backend remains available:

```bash
./build/sanpao15_cli --stats-only --limit 10000 --graph vector
```

Use `--graph csr` or `--graph vector` to choose explicitly. If omitted, `--graph csr` is used.

## First-Stage Limit

`solveFromInitial()` uses a default state cap so the CLI remains responsive during the MVP phase. `SolveStats::truncated` tells callers whether the graph was capped. `SolveOptions{0}` requests an uncapped graph. Truncated runs keep unresolved positions as `Unknown` because the missing successors can still change the result; complete runs convert unresolved positions to `Draw`.

## Edge Accounting

Graph statistics report three edge counts:

- `generatedEdges`: all legal successors generated from expanded non-terminal states.
- `storedEdges`: edges actually written to the selected graph backend. `totalEdges` is retained as a compatibility alias for `storedEdges`.
- `droppedEdges`: generated legal successors that were skipped because `maxStates` had already been reached.

For every run, `generatedEdges == storedEdges + droppedEdges`; the same invariant is tracked by soldier-count layer. Complete, non-truncated graphs should have `droppedEdges == 0`. Truncated probes can have dropped edges, and those edges must not be interpreted as losses or draws.

Memory estimates are based on stored edges because only stored edges occupy graph arrays:

```text
CSR succ flat = storedEdges * sizeof(uint32_t)
CSR pred flat = storedEdges * sizeof(uint32_t)
```

Use `generatedEdges` when estimating legal move density, and `storedEdges` when estimating graph memory.

The CLI supports the same distinction:

```text
sanpao15_cli --limit 100000
sanpao15_cli --full
sanpao15_cli --analyze "SSSSS/SSSSS/SSSSS/...../.CCC. c" --limit 10000
sanpao15_cli --full --progress 50000 --save-table standard.s15tbl
sanpao15_cli --limit 100000 --progress 10000 --save-table debug.s15tbl
sanpao15_cli --load-table standard.s15tbl --analyze "SSSSS/SSSSS/SSSSS/...../.CCC. c"
```

The UI can read the same `.s15tbl` file when it is placed at `ui/public/tables/standard.s15tbl`.

## Scale Probe

Before attempting a full solve, use graph-only probes to estimate reachable-state scale, edges, memory pressure, and build speed:

```bash
./build/sanpao15_cli --stats-only --limit 1000000 --progress 50000
./build/sanpao15_cli --stats-only --no-pred --limit 1000000 --progress 50000
./build/sanpao15_cli --probe --limit 10000000 --progress 100000
./build/sanpao15_cli --stats-only --no-pred --limit 10000000 --graph csr --progress 500000
```

`--stats-only` builds the reachable graph and reports:

- reachable states plus generated, stored, and dropped edges;
- states and generated/stored/dropped edges by soldier count;
- graph-build timing and states/second progress;
- max BFS queue size;
- rough memory estimates for compact tables, flat edge arrays, CSR, and the current `vector<vector<int>>` graph.

`--no-pred` skips predecessor lists and is only valid for `--stats-only` or `--probe`; retrograde requires predecessors. `--probe` runs a few increasing limits and prints a compact comparison table. None of these modes writes a final result table.

Recommended full-solve preparation:

```bash
./build/sanpao15_cli --probe --limit 1000000 --progress 50000
./build/sanpao15_cli --stats-only --limit 10000000 --progress 500000
./build/sanpao15_cli --full --progress 500000 --save-table standard.s15tbl
```

When a stats/probe run is truncated, its state and stored-edge counts describe only the explored prefix. Dropped edges are normal in this mode and show how many legal successors fell beyond the cap. CSR lowers memory pressure but does not prove the full solve will fit; if a 100M CSR no-pred probe is still truncated and has not reached lower-soldier layers, later work should move toward layered solving and compressed tables. If the complete graph is reached under CSR with safe memory estimates, a full solve can be considered.

## Layered Route

The next solver route is layered by soldier count. Normal moves stay in the same layer, captures move from `k` soldiers to `k - 1`, and no move increases soldier count. The first layered prototype writes reachable state-key sets to `.s15layer` files:

```bash
./build/sanpao15_cli --build-layers build/layers --max-states-per-layer 1000000 --progress 50000
```

This is not outcome solving. It prepares stable layer files for later layer-local edge storage and lower-to-higher retrograde.
