# External Layer Closure Prototype

`buildLayerClosureExternal(...)` is a prototype for building one soldier-count
layer using sorted files for `visited`, `frontier`, and generated candidates.
It can run as a standalone one-layer command or as the closure backend for
`--build-layers --external-layer-closure`.

It does not solve outcomes, does not write `.s15tbl`, and is not the final
performance design.

## Algorithm

For one layer `k`, the prototype keeps these sorted unique key files:

```text
visited       states already discovered in layer k
frontier      layer-k states to expand this iteration
candidates    same-layer successors generated from frontier
nextFrontier  candidates - visited
nextSeeds     capture successors in layer k - 1
```

Each iteration:

```text
expand frontier
write same-layer successors to ExternalKeySetBuilder
write capture successors to ExternalKeySetBuilder
candidates = sorted unique same-layer successors
nextFrontier = candidates - visited
visited = visited union nextFrontier
frontier = nextFrontier
stop when frontier is empty or a limit is reached
```

The important property is sequential file access. Set operations scan sorted
files from left to right; they do not do random disk lookups and do not load both
input sets fully into memory.

## `.s15keys`

The prototype uses an internal key-list file format:

```text
magic[8]      "S15KEY1\0"
uint32_le     version = 1
uint32_le     soldierCount
uint64_le     keyCount
uint64_le     rulesetHash
uint64_le[]   sorted unique state keys
```

`.s15keys` is for intermediate frontier, visited, candidate, and next-seed sets.
Final outputs still use the public `.s15layer` and `.s15seed` formats.

Readers validate magic, version, ruleset hash, soldier count, strict ascending
order, uniqueness, and decoded soldier count.

## Set Operations

`sortedDifference(left, right, output)` computes:

```text
left - right
```

`sortedUnion(left, right, output)` computes:

```text
left union right
```

Both inputs must be sorted unique `.s15keys` files with the same soldier count.
Outputs are sorted unique `.s15keys` files.

## CLI

First create a seed file, for example by asking the existing layered builder to
write `seeds-15.s15seed`:

```powershell
.\build\sanpao15_cli.exe --build-layers build\layers-ext-closure-seed --stop-after-layer 15 --max-states-per-layer 1 --progress 0
```

Then run one standalone external closure:

```powershell
.\build\sanpao15_cli.exe --build-layer-external 15 --layer-work-dir build\layer15-work --seed-file build\layers-ext-closure-seed\seeds-15.s15seed --output-layer build\layer15-work\layer-15.s15layer --output-next-seed build\layer15-work\seeds-14.s15seed --dedup-chunk-size 1000 --max-expanded-states 10000 --progress 1000
```

Useful options:

```text
--max-iterations N        stop after N closure iterations
--max-expanded-states N   stop after N expanded frontier states
--max-states-per-layer N  stop a layered build when discovered states reach N
--dedup-chunk-size N      chunk size for ExternalKeySetBuilder
--keep-temp               keep `.s15keys` and `.s15run` work files
```

When a limit is reached, output files are still valid, but the status is
`truncated`.

Use the external closure backend in layered reachability:

```powershell
.\build\sanpao15_cli.exe --build-layers build\layers-ext --external-layer-closure --external-seed-dedup --stop-after-layer 15 --max-expanded-states 1000000 --dedup-chunk-size 100000 --progress 50000
```

`expandedStates` counts states whose legal moves were generated. `reachableStates`
or final states counts all discovered states, including a frontier that may not
have been expanded yet. For truncated external closure runs, final states can be
larger than expanded states.

## Checkpoint And Resume

External closure writes a resumable checkpoint in the layer work directory:

```text
work/layer-15/
  closure-state.json
  visited.s15keys
  frontier.s15keys
  remaining-frontier.s15keys
  next-seeds.s15keys
  base-visited.s15keys
  pending-candidates.s15keys
```

`visited.s15keys` is the discovered layer-k state set written to the partial
`.s15layer`. `frontier.s15keys` and `remaining-frontier.s15keys` are stable
materialized key files for the layer-k states that still need expansion.
`next-seeds.s15keys` is the accumulated, deduplicated capture seed set for layer
`k - 1`. If a run stops in the middle of one frontier iteration,
`base-visited.s15keys` and `pending-candidates.s15keys` preserve the iteration
boundary so resume produces the same layer and seed files as an uninterrupted
run with the same total expanded-state budget.

`closure-state.json` records the ruleset hash, soldier count, complete/truncated
status, truncation reason, iteration and edge counters, key counts, and the file
names above. Version 2 manifests also record:

```json
{
  "checkpointVersion": 2,
  "checkpointKind": "iteration-boundary",
  "requiresTransientRuns": false,
  "remainingFrontierFile": "remaining-frontier.s15keys"
}
```

The key rule is that checkpoints do not depend on transient `.s15run` files
under `runs/`. A previous large layer-15 resume failure at
`runs/candidates-34/keys-508.s15run` was traced to a merge fan-in problem in the
temporary run builder, not to a stable checkpoint requirement. The run builder
now performs bounded fan-in merge passes, and checkpoint validation rejects any
manifest that claims it still requires transient runs.

The manifest is written last, after the key files have been atomically replaced.

Resume through the layered builder:

```powershell
# First slice
.\build\sanpao15_cli.exe --build-layers build\layers15 --external-layer-closure --external-seed-dedup --stop-after-layer 15 --max-expanded-states 1000000 --dedup-chunk-size 100000 --progress 50000

# Next slice from build\layers15\work\layer-15
.\build\sanpao15_cli.exe --build-layers build\layers15 --external-layer-closure --external-seed-dedup --resume-closure --start-layer 15 --stop-after-layer 15 --max-expanded-states 1000000 --dedup-chunk-size 100000 --progress 50000
```

For the first version, `--resume-closure` requires
`--build-layers --external-layer-closure --start-layer K`. In resume mode,
`--max-expanded-states N` is an additional budget for this invocation, not the
cumulative target. A `10000` run followed by `--resume-closure
--max-expanded-states 10000` therefore reaches about `20000` cumulative expanded
states.

Use `--checkpoint-interval N` to also write checkpoints after roughly each `N`
additional expanded states. With `0`, the builder writes a checkpoint only when
the layer completes or truncates.

`--validate-layers DIR` validates closure checkpoints when present. It checks
the manifest, ruleset hash, soldier count, checkpoint kind, key counts, and
referenced stable `.s15keys` files. If stale transient run directories are
present but stable checkpoint files are complete, validation reports the
checkpoint as valid and those transient files can be ignored.

Safe repair and cleanup are available for checkpoint metadata:

```powershell
.\build\sanpao15_cli.exe --repair-closure-checkpoint build\layers-overnight --layer 15 --dry-run
.\build\sanpao15_cli.exe --repair-closure-checkpoint build\layers-overnight --layer 15 --cleanup-stale-runs
```

The dry run validates the target and reports the before/after metadata without
writing. Cleanup removes only stale `runs/` entries after the stable checkpoint
has validated.

## Partitioned Checkpoint Snapshots

`--partitioned-closure` can be used with
`--build-layers --external-layer-closure` to also write partitioned snapshots of
checkpoint key files:

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

Each subdirectory is a normal `partition.json` plus `.s15bucket` keyset. This is
a foundation for partitioned external closure. The current v1 still uses the
flat external closure algorithm for expansion and set operations, then writes
bucketed checkpoint snapshots for validation and later migration work.

`expandedStates` counts states whose legal moves have actually been generated.
`reachableStates`/`finalStates` counts discovered states, including a frontier
that may not have been expanded. A truncated checkpoint is resumable, but it is
not a complete layer and must not be treated as one.

## Current Limits

- Final `.s15layer` and `.s15seed` writers still materialize vectors.
- `--partitioned-closure` v1 writes partitioned checkpoint snapshots but does not
  yet replace the full closure loop with bucket-by-bucket frontier processing.
- There is no layer-local edge file yet.
- There is no layered retrograde yet.
- This prototype favors correctness and observability over final throughput.

## Next Work

- stream final layer/seed writers for very large files;
- add chunked frontier scheduling;
- design layer-local key indexes and mmap-friendly lookup;
- write layer-local CSR edge files;
- implement lower-to-higher layered retrograde.
