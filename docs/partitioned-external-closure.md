# Partitioned External Closure

This document describes the v1 foundation for moving external layer closure from
large flat `.s15keys` files toward bucketed storage.

## Failure Diagnosis

A long layer-15 resume previously failed while opening:

```text
build\layers-overnight\work\layer-15\runs\candidates-34\keys-508.s15run
```

The stable checkpoint did not require that `.s15run` file. The practical cause
was the temporary run merge path opening too many run readers at once after a
large chunked candidate pass. On Windows this can hit file-handle limits around
hundreds of run files. The external keyset builder now uses bounded fan-in merge
passes, writing intermediate `merge-pass-*.s15run` files and opening at most a
fixed group of run readers at one time.

## Hardened Checkpoint Rule

Closure checkpoints must depend only on stable materialized files:

```text
visited.s15keys
frontier.s15keys
remaining-frontier.s15keys
next-seeds.s15keys
base-visited.s15keys
pending-candidates.s15keys
closure-state.json
```

They must not require transient files under:

```text
runs/candidates-*/
runs/next-seeds-*/
```

Version 2 manifests record:

```json
{
  "checkpointVersion": 2,
  "checkpointKind": "iteration-boundary",
  "requiresTransientRuns": false,
  "remainingFrontierFile": "remaining-frontier.s15keys"
}
```

`--validate-layers` checks that the referenced stable files exist, have the
expected soldier count, and match the manifest key counts. Stale run directories
can be removed only after the stable checkpoint validates.

## Repair And Cleanup

The safe repair command validates stable files and rewrites checkpoint metadata
without recomputing state:

```powershell
.\build\sanpao15_cli.exe --repair-closure-checkpoint build\layers-overnight --layer 15 --dry-run
```

Cleanup is guarded by the same checkpoint validation:

```powershell
.\build\sanpao15_cli.exe --repair-closure-checkpoint build\layers-overnight --layer 15 --cleanup-stale-runs
```

Use `--dry-run` first for real large checkpoints.

## Partitioned Keyset Operations

The storage foundation exposes exact bucket-wise operations:

```cpp
partitionedDifference(left, right, output, overwrite);
partitionedUnion(left, right, output, overwrite);
```

Inputs must share the same ruleset hash, soldier count, bucket count, and
partition method. The operation works one bucket at a time and writes a normal
partitioned keyset output.

## Partitioned Checkpoint Migration

`--migrate-closure-checkpoint` converts an existing stable flat closure
checkpoint into standalone partitioned snapshots without rewriting the flat
checkpoint:

```powershell
.\build\sanpao15_cli.exe --migrate-closure-checkpoint build\layers-overnight ^
  --layer 15 ^
  --partition-buckets 256 ^
  --partition-method splitmix64_mod
```

Use `--dry-run` first on large checkpoints. The output manifest is:

```text
work/layer-K/partitioned/partitioned-closure-state.json
```

It records checkpoint kind, expanded count, bucket count, partition method, and
the active snapshots.

## True Partitioned Closure Resume V1

`--partitioned-closure` still runs the proven flat external closure algorithm,
but it now also writes a standalone partitioned checkpoint manifest. That
checkpoint, or one created by migration, can be resumed with:

```powershell
.\build\sanpao15_cli.exe --resume-partitioned-closure build\partitioned-closure-smoke ^
  --layer 15 ^
  --expanded-budget 10000 ^
  --progress 1000
```

Dry-run only validates the executable partitioned state:

```powershell
.\build\sanpao15_cli.exe --resume-partitioned-closure build\layers-overnight ^
  --layer 15 ^
  --dry-run
```

Active state depends on checkpoint kind.

Iteration-boundary checkpoints use:

```text
visited
frontier
next-seeds
```

A resume step expands `frontier`, writes generated same-layer candidates and
capture seeds, computes:

```text
nextFrontier = candidates - visited
visited = visited union nextFrontier
nextSeeds = nextSeeds union captureSeeds
frontier = nextFrontier
```

Mid-iteration checkpoints use:

```text
base-visited
remaining-frontier
pending-candidates
next-seeds
```

Resume continues `remaining-frontier`, unions new same-layer candidates into
`pending-candidates`, unions capture seeds into `next-seeds`, and when the
current iteration finishes computes:

```text
nextFrontier = pending-candidates - base-visited
visited = base-visited union nextFrontier
frontier = nextFrontier
```

If `--expanded-budget` stops before all active frontier buckets are processed,
the command writes a valid mid-iteration partitioned checkpoint with
`base-visited`, `remaining-frontier`, `pending-candidates`, and `next-seeds`.
The v1 budget check happens at bucket boundaries, so `expandedThisRun` can be
slightly above the requested budget. No frontier state is lost or double-counted.

New or changed snapshots are written under a run-specific directory below
`work/layer-K/partitioned/runs/`. The active manifest is updated last, after all
new partitions validate, so an interrupted run does not leave the manifest
pointing at incomplete snapshots.

## Smoke Build

```powershell
.\build\sanpao15_cli.exe --build-layers build\partitioned-closure-smoke ^
  --external-layer-closure ^
  --partitioned-closure ^
  --external-seed-dedup ^
  --stop-after-layer 15 ^
  --max-expanded-states 10000 ^
  --closure-partition-buckets 16 ^
  --dedup-chunk-size 1000 ^
  --progress 1000
```

The checkpoint layout is:

```text
work/layer-15/partitioned/
  partitioned-closure-state.json
  visited/
  frontier/
  remaining-frontier/
  next-seeds/
  pending-candidates/
  base-visited/
```

Each directory is a standard `partition.json` plus `.s15bucket` files. The
standalone partitioned manifest records active snapshot paths. `--validate-layers`
validates both migrated and resumed partitioned checkpoints.

## V1 Limits

- `--expanded-budget` is enforced at bucket boundaries for v1. Actual expanded
  states can exceed the budget by one bucket.
- Generated candidates are materialized as temporary sorted `.s15keys` before
  being partitioned. This is correct and bounded, but not yet the fastest
  possible per-bucket collector.
- It does not write partitioned edge files.
- It does not run layered retrograde.
- Large 90M+ resumes should be run in small slices until timing and disk growth
  are characterized.

## Next Steps

1. Run the real 90M checkpoint in controlled partitioned closure slices.
2. Replace temporary candidate `.s15keys` materialization with a direct
   per-bucket collector if performance requires it.
3. Add partitioned layer-local edge files.
4. Evaluate D4 symmetry canonicalization before committing to an edge format.
5. Build a layered retrograde outcome-only prototype after layer-local storage is
   stable.
