# Layer Edge Probe

The layer edge probe is a scale and performance tool. It does not solve positions, write edge files, or change `Unknown`/`Draw` semantics.

Input:

- one partitioned `layer-k` keyset;
- one partitioned `seeds-(k-1)` keyset when `k > 0`;
- a sample size.

For each sampled layer state, the probe generates legal moves. Same-soldier successors are looked up in the layer partition. Capture successors with one fewer soldier are looked up in the next seed partition.

CLI:

```powershell
.\build\sanpao15_cli.exe ^
  --probe-layer-edges build\partitions-splitmix\layer-15 ^
  --next-seed-partition build\partitions-splitmix\seeds-14 ^
  --sample-states 100000 ^
  --partition-cache-buckets 32 ^
  --progress 10000
```

Reported fields include sampled states, generated moves, same-layer found/missing, capture found/missing, lookup keys, bucket loads, lookup time, total time, states/sec, moves/sec, and lookups/sec.

For partial checkpoints, missing membership is not automatically a bug. A same-layer successor may not have been discovered yet, and capture successors from unexpanded frontier areas may not be present in the accumulated seed file. Complete smoke tests can require zero missing results, but real partial layer-15 data cannot.

The probe is intended to answer whether partitioned keyset membership is fast enough to support later layer-local edge generation or partitioned/block edge files.
