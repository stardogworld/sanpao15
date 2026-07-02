# Material Target Distance Tablebase

This document describes the prototype second-level tablebase for Sanpao15. The
existing dense `.s15res` files remain the first objective: they answer only WDL
outcome (`CannonWin`, `SoldierWin`, `Draw`, or `Unknown`). The material target
distance tablebase adds a secondary answer without changing the WDL result.

## Why MTD

Outcome-only WDL is enough to avoid losing the game result, but it cannot answer
material questions. A drawing move may save more soldiers than another drawing
move. A cannon-winning move may capture one more soldier while still preserving
the win. MTD records that material policy as a separate table so `.s15res`
stays stable and compact.

## WDL First

MTD optimizes only over WDL-preserving moves:

```text
WDL(successor) == WDL(state)
```

A cannon move is not allowed to turn a draw into a soldier win just to capture
more soldiers. A soldier move is not allowed to turn a soldier win into a draw
or cannon win just to save more soldiers. `Unknown` is never treated as `Draw`.

## Definitions

For a state with current soldier count `k`:

```text
materialTarget(state) = r
```

`r` is the final number of soldiers that the soldier side can preserve under
optimal material play, while preserving the WDL result. The cannon side
minimizes `r`; the soldier side maximizes `r`.

Derived values:

```text
cannonMaxCaptures = k - materialTarget
soldierSaved      = materialTarget
```

`targetDistance(state) = d` is a guaranteed ply distance to reach the material
target under optimal material play. The cannon side minimizes this delay; the
soldier side maximizes it. This is an adversarial delay guarantee, not DTW or
DTC.

If `materialTarget == k`, then `targetDistance = 0`.

Otherwise, over WDL-preserving and material-optimal successors:

```text
Cannon to move:  d = 1 + min(child.d)
Soldier to move: d = 1 + max(child.d)
```

`targetDistance` is measured in plies. Values `0..254` are exact. Value `255`
means `>=255` or saturated by the prototype.

Terminal states have `materialTarget = k` and `targetDistance = 0`.

## .s15mtd Format

Each soldier-count layer has one file:

```text
layer-00.s15mtd
layer-01.s15mtd
...
layer-15.s15mtd
```

Header, little-endian:

```text
magic[8]       "S15MTD1\0"
uint32_le      version = 1
uint64_le      rulesetHash
uint32_le      soldierCount
uint64_le      stateCount
uint32_le      encoding = 1
uint64_le      payloadBytes
payload        packed entries in dense index order
```

Encoding `1` is:

```text
packed12-material4-distance8
```

Each dense state stores 12 bits:

```text
bits 0..3    materialTarget, 0..15
bits 4..11   targetDistance, 0..255
```

Entry encoding:

```cpp
uint16_t entry = materialTarget | (targetDistance << 4);
```

Two entries are packed into three bytes:

```text
byte0 = v0 & 0xff
byte1 = ((v0 >> 8) & 0x0f) | ((v1 & 0x0f) << 4)
byte2 = (v1 >> 4) & 0xff
```

Payload size is:

```text
ceil(stateCount * 12 / 8)
```

Odd `stateCount` values are supported; unused high bits in the last byte must
remain zero.

## Space Estimate

The full dense tablebase has:

```text
18,787,540,800 states
```

Full MTD payload:

```text
18,787,540,800 * 12 / 8
= 28,181,311,200 bytes
~= 26.25 GiB
```

Existing WDL `.s15res` payloads:

```text
4,696,885,200 bytes
~= 4.37 GiB
```

WDL plus MTD:

```text
32,878,196,400 bytes
~= 30.62 GiB
```

Largest MTD layer:

```text
k=11
4,867,480,800 bytes
~= 4.53 GiB
```

Approximate per-layer MTD payload bytes:

```text
k=0:      6,900
k=1:      151,800
k=2:      1,593,900
k=3:      10,626,000
k=4:      50,473,500
k=5:      181,704,600
k=6:      514,829,700
k=7:      1,176,753,600
k=8:      2,206,413,000
k=9:      3,432,198,000
k=10:     4,461,857,400
k=11:     4,867,480,800
k=12:     4,461,857,400
k=13:     3,432,198,000
k=14:     2,206,413,000
k=15:     1,176,753,600
```

## Prototype Solver

The prototype has two stages:

1. Threshold material attractor over WDL-preserving moves to compute
   `materialTarget`.
2. Bucketed delay propagation over WDL-preserving, material-optimal moves to
   compute `targetDistance`.

Base layers `k=0..3` are immediate under the current ruleset:

```text
materialTarget = k
targetDistance = 0
```

For `k>=4`, the solver loads:

```text
current WDL layer-k.s15res
lower WDL layer-(k-1).s15res
lower MTD layer-(k-1).s15mtd
```

Current prototype scope is `k=0..4`. Wider production ranges should wait for a
performance pass and larger-layer memory/time measurements.

## CLI

```powershell
.\build\sanpao15_cli.exe --solve-mtd-layer 4 --wdl-dir build\prod-layers --mtd-dir build\material-target-distance --overwrite
.\build\sanpao15_cli.exe --solve-mtd-range 0 4 --wdl-dir build\prod-layers --mtd-dir build\material-target-distance --resume
.\build\sanpao15_cli.exe --verify-mtd-layer 4 --wdl-dir build\prod-layers --mtd-dir build\material-target-distance --sample 10000
.\build\sanpao15_cli.exe --inspect-mtd build\material-target-distance\layer-04.s15mtd
.\build\sanpao15_cli.exe --query-mtd build\material-target-distance --wdl-dir build\prod-layers --position "SSSS./...../...../...../.CCC. c" --moves
```

`--solve-mtd-layer` and `--solve-mtd-range` write `.s15mtd` plus
`.mtd.solve.json`. Generated MTD files are build artifacts and must not be
committed.

## Measured Prototype Run

Release build on the current workstation:

```text
k=0: states=4,600       total=0.001s  target 0 only
k=1: states=101,200     total=0.015s  target 1 only
k=2: states=1,062,600   total=0.029s  target 2 only
k=3: states=7,084,000   total=0.153s  target 3 only
k=4: states=33,649,000  total=206.265s
```

Layer `k=4`:

```text
materialTargetCounts:
  3: 33,398,108
  4: 250,892

cannonMaxCapturesCounts:
  0: 250,892
  1: 33,398,108

maxExactDistance: 50
saturatedDistanceCount: 0
stageMaterialSeconds: 91.645
stageDistanceSeconds: 114.304
estimatedMemoryBytes: 374,123,750
queuePeak: 14,303,964
outputBytes: 50,473,544
```

`verify-mtd-layer 4 --sample 10000` passed on the generated prototype file.

## Full-Run Risk

The full MTD table is roughly 26.25 GiB, before WDL files and temporary working
memory. A naive full `0..15` run is not recommended from this prototype alone.
The next engineering step should be a material-target-distance performance pass
and then a cautious wider benchmark (`k=0..5`, then possibly `k=0..6`).

Potential follow-up directions:

```text
A. material-target-distance production range solver
B. material-target-distance performance pass
C. run k=0..7 benchmark
D. full material-target-distance 0..15 production run
E. backend/UI query integration for MTD
F. wider distance encoding if saturation appears too often
```

Based on current `k=4` timing, the recommended next route is `B` before any full
production run.
