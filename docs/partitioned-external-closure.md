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

## Partitioned Closure V1

`--partitioned-closure` currently means: run the proven flat external closure
algorithm, then write partitioned checkpoint snapshots for the stable checkpoint
files.

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

The snapshot layout is:

```text
work/layer-15/partitioned/
  visited/
  frontier/
  remaining-frontier/
  next-seeds/
  pending-candidates/
  base-visited/
```

Each directory is a standard `partition.json` plus `.s15bucket` files. The
checkpoint manifest records `partitionedClosure`, bucket count, and partition
method. `--validate-layers` validates these snapshots when they are present in
the manifest.

## V1 Limits

- It does not migrate the existing 90M checkpoint.
- It does not yet replace closure expansion with bucket-by-bucket frontier
  processing.
- It does not write partitioned edge files.
- It does not run layered retrograde.
- It is intended for small correctness smoke runs and for validating the storage
  contract before large migration work.

## Next Steps

1. Add a migration command for a stable large checkpoint into partitioned
   checkpoint snapshots.
2. Replace flat candidate/frontier set operations with partitioned bucket-wise
   difference and union.
3. Add partitioned layer-local edge files.
4. Evaluate D4 symmetry canonicalization before committing to an edge format.
5. Build a layered retrograde outcome-only prototype after layer-local storage is
   stable.
