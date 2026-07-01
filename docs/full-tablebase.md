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
```

Do not commit generated `.s15res` files.

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
