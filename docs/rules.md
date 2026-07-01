# Rules

## Board

The board is 5x5 with square indexes:

```text
0  1  2  3  4
5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
```

## Initial Position

```text
兵 兵 兵 兵 兵
兵 兵 兵 兵 兵
兵 兵 兵 兵 兵
.  .  .  .  .
.  炮 炮 炮 .
```

Soldiers occupy `0..14`, cannons occupy `21, 22, 23`, and Cannon moves first.

## Moves

Cannons:

- Move one orthogonal step to an empty square.
- Capture in an orthogonal jump exactly shaped as `炮 - empty - 兵`.
- Land on the captured soldier's square.
- Cannot move diagonally.
- Cannot jump over a cannon.
- Cannot jump over a soldier to capture another soldier.

Soldiers:

- Move one orthogonal step to an empty square.
- Cannot move diagonally.
- Cannot capture cannons.
- Cannot move to an occupied square.

## Terminal Rules

- `soldiers == 0`: Cannon wins.
- Cannon has no legal moves: Soldier wins.
- If the current side has no legal continuation, the opponent wins.
- Cycles are handled by the solver through retrograde analysis, not recursive minimax.

## Notation

Rows are separated by `/`; `S` is soldier, `C` is cannon, `.` is empty. The final token is side to move:

```text
SSSSS/SSSSS/SSSSS/...../.CCC. c
```
