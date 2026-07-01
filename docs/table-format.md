# Table Format

`.s15tbl` is a little-endian binary result table that can be read by both C++ and the TypeScript UI. The writer never dumps C++ structs directly; every field is written explicitly to avoid padding and platform issues.

## State Key

```text
bits 0..24   cannon bitboard
bits 25..49  soldier bitboard
bit  50      side to move, 0 = Cannon, 1 = Soldier
```

The initial key is for:

```text
SSSSS/SSSSS/SSSSS/...../.CCC. c
```

## Header

```text
char[8]  magic        "S15TBL1\0"
u32      version      1
u32      flags        bit 0 = exact, bit 1 = truncated
u64      stateCount
u64      initialKey
u64      rulesetHash
```

`rulesetHash` is currently a fixed constant for this rule set:

```text
5x5, soldiers 0..14, cannons 21/22/23, cannon first,
orthogonal moves, cannon-empty-soldier capture,
soldierCount < 4 is immediate CannonWin
```

Current ruleset hash: `5994631263128018692` (`0x5331355F76325F04`).

## Entries

Entries are saved by ascending `key`.

```text
u64  key
u8   outcome
i32  distance
i8   bestFrom
i8   bestTo
i8   bestCapturedSquare
u8   flags
```

Outcome values:

```text
0 Unknown
1 CannonWin
2 SoldierWin
3 Draw
```

Best move conventions:

```text
bestFrom = -1 means no bestMove
bestTo = -1 means no bestMove
bestCapturedSquare = -1 means no capture
flags bit 0 = hasBestMove
flags bit 1 = bestMoveIsCapture
```

`Unknown` is used for truncated tables, missing table entries, and unresolved states. Only exact complete tables convert unresolved states to `Draw`.

## CLI Examples

```bash
./build/sanpao15_cli --full --progress 50000 --save-table standard.s15tbl
./build/sanpao15_cli --limit 100000 --progress 10000 --save-table debug.s15tbl
./build/sanpao15_cli --load-table standard.s15tbl --analyze "SSSSS/SSSSS/SSSSS/...../.CCC. c"
```

## UI Usage

Place a generated table at:

```text
ui/public/tables/standard.s15tbl
```

Then run:

```bash
cd ui
npm run dev
```
