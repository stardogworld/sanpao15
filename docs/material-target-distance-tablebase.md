# Material Target Distance Tablebase

This document describes the outcome-aware v2 material-target-distance (MTD)
tablebase. Dense `.s15res` files remain the primary tablebase and answer only
WDL: `CannonWin`, `SoldierWin`, `Draw`, or `Unknown`. MTD files add secondary
guidance without changing `.s15res`, the ruleset, or the `Outcome` enum.

Version 1 `.s15mtd` files were prototype files with obsolete semantics. Current
code writes header version 2 and rejects version 1 with an explicit regeneration
error.

## Packed Entry

Each state still stores 12 bits:

```text
bits 0..3    materialTarget, 0..15
bits 4..11   guaranteeDistance, 0..255
```

`guaranteeDistance` is measured in plies. Values `0..254` are exact; `255`
means `>=255` or saturated.

The two fields are interpreted by WDL outcome:

```text
CannonWin   shortest forced cannon win distance
SoldierWin  shortest forced soldier encirclement distance
Draw        material target plus delay to that target
```

`Unknown` WDL is never treated as `Draw`. MTD solve, verify, and query fail fast
if a referenced WDL layer contains `Unknown`.

## CannonWin

For `WDL(state) == CannonWin`, the main answer is:

```text
guaranteeDistance = shortest forced cannon win in plies
```

The terminal target is the rules terminal `soldierCount < 4` or another
terminal CannonWin condition. Terminal CannonWin states store:

```text
materialTarget = current soldier count
guaranteeDistance = 0
```

For non-terminal CannonWin states, only CannonWin successors are considered:

```text
Cannon to move:  guaranteeDistance = 1 + min(child.guaranteeDistance)
Soldier to move: guaranteeDistance = 1 + max(child.guaranteeDistance)
```

`materialTarget` is the terminal soldier count reached under the selected
distance policy. Ties at equal distance prefer fewer remaining soldiers for
cannon choices and more remaining soldiers for soldier choices.

## SoldierWin

For `WDL(state) == SoldierWin`, the main answer is:

```text
guaranteeDistance = shortest forced soldier encirclement win in plies
```

The terminal target is `cannon has no legal moves` with `soldierCount >= 4`.
Terminal SoldierWin states store:

```text
materialTarget = current soldier count
guaranteeDistance = 0
```

For non-terminal SoldierWin states, only SoldierWin successors are considered:

```text
Soldier to move: guaranteeDistance = 1 + min(child.guaranteeDistance)
Cannon to move:  guaranteeDistance = 1 + max(child.guaranteeDistance)
```

`materialTarget` is the terminal soldier count at encirclement under the
selected distance policy. Ties at equal distance prefer more remaining soldiers
for soldier choices and fewer remaining soldiers for cannon choices.

## Draw

For `WDL(state) == Draw`, MTD keeps the material-target semantics:

```text
materialTarget = r
cannonMaxCaptures = currentSoldierCount - r
soldierSaved = r
```

The cannon side minimizes `r`; the soldier side maximizes `r`, while preserving
Draw. `guaranteeDistance` is the adversarial delay to reach that material target
under material-optimal Draw play.

If `materialTarget == currentSoldierCount`, then:

```text
guaranteeDistance = 0
```

Otherwise, only Draw-preserving successors with the same material target are
considered:

```text
Cannon to move:  guaranteeDistance = 1 + min(child.guaranteeDistance)
Soldier to move: guaranteeDistance = 1 + max(child.guaranteeDistance)
```

## `.s15mtd` Format

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
uint32_le      version = 2
uint64_le      rulesetHash
uint32_le      soldierCount
uint64_le      stateCount
uint32_le      encoding = 1
uint64_le      payloadBytes
payload        packed entries in dense index order
```

Encoding `1` is `packed12-material4-distance8`. Two entries are packed into
three bytes:

```text
byte0 = v0 & 0xff
byte1 = ((v0 >> 8) & 0x0f) | ((v1 & 0x0f) << 4)
byte2 = (v1 >> 4) & 0xff
```

Payload size is `ceil(stateCount * 12 / 8)`. Odd `stateCount` values are
supported; unused high bits in the final byte must remain zero.

## Validation Modes

MTD validation is split into two paths:

```text
validateMtdHeaderOnly  checks magic, version, ruleset hash, layer, shape, and file size
validateMtdFileFull    loads and scans the packed payload
```

`--solve-mtd-range --resume` uses header-only validation for existing
`.s15mtd` files. This avoids accidentally scanning multi-GiB payloads while
resuming a range, but it also means resume is not a full semantic verification.
Use `--verify-mtd-layer` for payload and WDL-preserving semantic checks.

## Solver Shape

The v2 prototype solves each layer in outcome-aware phases:

```text
1. Load solved current WDL, and lower WDL/MTD when k >= 4.
2. Reject any Unknown WDL entry.
3. Solve CannonWin guarantee distance in the CannonWin subgraph.
4. Solve SoldierWin guarantee distance in the SoldierWin subgraph.
5. Solve Draw materialTarget with a Draw-only threshold attractor.
6. Solve Draw guaranteeDistance over Draw material-optimal successors.
```

Base layers `k=0..3` are immediate under the current ruleset:

```text
WDL = CannonWin
materialTarget = k
guaranteeDistance = 0
```

The production solve path writes packed12 output directly from the solved
material and distance arrays. It no longer constructs a second resident
`PackedMtdTable12` for the current layer before writing. The legacy
`saveMtdTable(PackedMtdTable12, ...)` API remains for tests and small-table
utilities.

## Threading

MTD commands accept a conservative CPU threading option:

```text
--threads 1   deterministic single-thread baseline
--threads 0   use std::thread::hardware_concurrency(), falling back to 1
--threads N   use N workers, clamped to at most 256 and at most the work count
```

Threading is supported by:

```text
--solve-mtd-layer
--solve-mtd-range
--verify-mtd-layer
--inspect-mtd
```

`--query-mtd` remains random-access and single-request oriented.

The threaded pass parallelizes only safe linear scans and initialization scans:

```text
wdlSolvedScan
outcomeCount
winningInitialScan
drawThresholdInitialScan
drawDistanceInitialScan
inspect stats scan
verify sample/full scan
```

The predecessor/worklist propagation stages are intentionally still
single-threaded:

```text
winning predecessor propagation
Draw material threshold propagation
Draw distance propagation
```

The threading pass must not change MTD v2 semantics. For the same inputs,
`--threads N` output is expected to be byte-identical to `--threads 1`; compare
generated `.s15mtd` files with SHA256 when benchmarking a new platform.
`layer-XX.mtd.solve.json` records the resolved `threads` value and the solve
stages that were parallelized.

## CLI

```powershell
.\build\sanpao15_cli.exe --solve-mtd-range 0 4 --wdl-dir build\prod-layers --mtd-dir build\material-target-distance-v2 --overwrite --threads 1
.\build\sanpao15_cli.exe --solve-mtd-range 0 4 --wdl-dir build\prod-layers --mtd-dir build\material-target-distance-v2-t16 --overwrite --threads 16
.\build\sanpao15_cli.exe --verify-mtd-layer 4 --wdl-dir build\prod-layers --mtd-dir build\material-target-distance-v2-t16 --sample 10000 --threads 16
.\build\sanpao15_cli.exe --inspect-mtd build\material-target-distance-v2-t16\layer-04.s15mtd --threads 16
.\build\sanpao15_cli.exe --query-mtd build\material-target-distance-v2 --wdl-dir build\prod-layers --position "SSSS./...../...../...../.CCC. c" --moves
```

Generated `.s15mtd` and `.mtd.solve.json` files are build artifacts and must not
be committed.

## Measured v2 Run

Release build on the current workstation, solving `k=0..4` from existing
`build\prod-layers` WDL:

```text
k  states       CannonWin   SoldierWin  Draw     maxExact  saturated  total
0  4,600        4,600       0           0        0         0          0.00s
1  101,200      101,200     0           0        0         0          0.01s
2  1,062,600    1,062,600   0           0        0         0          0.01s
3  7,084,000    7,084,000   0           0        0         0          0.05s
4  33,649,000   33,398,108  736         250,156  50        0          1:27
```

Layer `k=4` distributions:

```text
materialTargetCounts:
  3: 33,398,108
  4: 250,892

cannonMaxCapturesCounts:
  0: 250,892
  1: 33,398,108

queuePeak: 14,303,964
outputBytes: 50,473,544
estimatedMemoryBytes: 374,123,750
```

Full `verify-mtd-layer 4 --sample 0` passed:

```text
sampledStates: 33,649,000
checkedTransitions: 295,586,452
maxExactDistance: 50
saturatedDistanceCount: 0
```

Example queries:

```text
CannonWin:  SSSS./...../...../...../.CCC. c
  guaranteeDistance = 23 ply
  materialTarget = 3

SoldierWin: CCC.S/SSS../...../...../..... s
  guaranteeDistance = 1 ply
  materialTarget = 4

Draw:       CCCS./SS.S./...../...../..... c
  materialTarget = 4
  cannonMaxCaptures = 0
  soldierSaved = 4
  guaranteeDistance = 0 ply
```

## Space Estimate

The full dense tablebase has `18,787,540,800` states. Full MTD payload is:

```text
18,787,540,800 * 12 / 8 = 28,181,311,200 bytes ~= 26.25 GiB
```

WDL plus MTD payload is about `30.62 GiB`, before temporary working memory.
Large MTD production runs require a 64-bit build. The current single-threaded
solver keeps public counts and byte sizes in `uint64_t`, keeps checked
`uint32_t` queue indexes for currently addressable dense layers, and treats
distance `255` as saturated `>=255`, not an exact ply count.

Streaming packed12 output reduces peak memory by removing the current-layer
packed output table. For the largest layer, `k=11`, this removes about
`mtdPayloadBytes(denseStateCount(11)) = 4,867,480,800` bytes, roughly
`4.53 GiB`, from the solve-time resident set estimate.

The recommended next steps are:

```text
1. threaded performance pass
2. cautious k=5/k=6 benchmark
3. production range planning
4. backend/UI MTD query integration
```
