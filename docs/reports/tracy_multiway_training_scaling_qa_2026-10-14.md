# QA Report: Tracy Multiway Training Scaling Findings

**Date:** 2026-10-14
**Scope:** Real multiway blueprint training, Tracy 0.14.2, `RelWithDebInfo`
**Workload:** Fixed scheduler, one batch, 1,000 trajectories, unpinned workers
**Source commit:** `bf929716ad1fe8f1611411a5d4b8564c26c46127`

## Executive conclusion

The training workload is correct, but scaling from one to four workers is weak.
The four-worker batch is only 12.3% faster than the one-worker batch:

| Metric | 1 worker | 4 workers | Result |
|---|---:|---:|---:|
| Batch wall time | 2.245 s | 1.968 s | 12.3% faster |
| Throughput | 445.4 trajectories/s | 508.1 trajectories/s | 14.1% higher |
| Worker traversal critical path | 1.033 s | 0.486 s | 2.1x faster |
| Coordinator admission | 1.140 s | 1.404 s | 23.1% slower |
| Delta merge | 21.5 ms | 23.0 ms | Essentially unchanged |

Worker traversal does improve with four workers. The main scaling limit is the
single coordinator admission phase. It becomes slower as worker count grows and
consumes most of the remaining batch time.

The largest end-to-end delay is outside the trajectory batch: checkpoint
writing takes 198.3 seconds with one worker and 239.8 seconds with four workers.
This is the dominant runtime problem for the current real-workflow wrapper.

## QA result

Both runs passed the correctness and provenance gates:

- 1,000 trajectories attempted and accepted in each run.
- Zero discarded trajectories.
- `failed=false`.
- The deterministic merge fingerprint is identical:
  `9806774443936985566`.
- Both runs used Tracy 0.14.2 and the same source commit.
- The requested worker counts were effective: 1 and 4.
- No worker pinning was requested or applied.

The captures are valid diagnostic evidence. They are not throughput
qualification runs because each configuration has only one batch and one
sample.

## Batch timing findings

### One worker

The one-worker report records a 2.2451662-second batch:

- Worker active time: 1.0334772 s.
- Coordinator wait: 1.0347329 s.
- Coordinator admission: 1.1402778 s.
- Discovery copy: 49.1330 ms.
- Discovery sort: 48.6436 ms.
- Delta sort: 1.2301 ms.
- Delta merge: 21.5391 ms.
- Trainer-only phase: 0.3 microseconds.
- Throughput: 445.401 trajectories/s.

With one worker, traversal and coordinator work are largely serialized. The
worker is active for about 1.03 seconds, while the coordinator performs
discovery admission and merge work around it.

### Four workers

The four-worker report records a 1.9680949-second batch:

- Aggregate worker active time: 1.5519517 s.
- Slowest worker active time: 486.0932 ms.
- Fastest worker active time: 309.2718 ms.
- Coordinator wait: 486.4896 ms.
- Coordinator admission: 1.4039453 s.
- Discovery copy: 90.9653 ms.
- Discovery sort: 54.1260 ms.
- Delta sort: 1.4081 ms.
- Delta merge: 22.9670 ms.
- Trainer-only phase: 0.1 microseconds.
- Throughput: 508.106 trajectories/s.

Each worker received exactly 250 trajectories, but active time ranged from
309 ms to 486 ms. Equal trajectory counts therefore do not produce equal work.
The fixed scheduler and unpinned execution leave measurable tail idle time:

```text
worker 0: 0.0 ms
worker 1: 176.9 ms
worker 2: 67.9 ms
worker 3: 147.7 ms
```

The four-worker trace reduces the worker critical path from 1.033 seconds to
0.486 seconds, but coordinator admission grows from 1.140 seconds to 1.404
seconds. This explains why four workers do not approach four-times throughput.

## End-to-end runtime findings

The batch timing is not the full runtime of the real workflow. The reports
record the following output costs:

| Output phase | 1 worker | 4 workers | Change |
|---|---:|---:|---:|
| Checkpoint write | 198.266 s | 239.832 s | 21.0% slower |
| Blueprint export | 7.188 s | 7.009 s | Similar |
| Checkpoint size | 137,380,835 bytes | 137,380,835 bytes | Same |
| Blueprint size | 189,818,882 bytes | 189,818,882 bytes | Same |

The checkpoint size is unchanged, but the four-worker checkpoint write is
approximately 41.6 seconds slower. The current Tracy zones do not identify
which checkpoint sub-operation causes this increase. This requires additional
zones around checkpoint serialization, file writes, compression or flushing.

The Tracy capture files are small, so trace recording is not responsible for
the 198-240 second output delay. The long `mw.worker.cv_wait` CSV entries are
also not batch computation. They include worker lifetime waiting while the main
thread performs initialization, checkpoint output, blueprint export and
shutdown. The JSON report gives the batch-level CV waits, which remain below
0.4 ms per worker.

## Current problems identified

### P0: Checkpoint writing dominates end-to-end runtime

Checkpoint output takes about 198 seconds with one worker and 240 seconds with
four workers. It is several orders of magnitude larger than the batch compute
time. The four-worker configuration makes this phase slower even though the
checkpoint file size is unchanged.

Required investigation:

1. Add Tracy zones for checkpoint construction, serialization, file writing,
   flushing and any compression or temporary-file replacement.
2. Record bytes written and write duration per checkpoint section.
3. Check whether four worker streams cause extra serialization, locking or
   ordering work despite the identical final file size.
4. Keep checkpoint output outside throughput measurements or provide an
   explicit output-disabled diagnostic mode.

### P1: Coordinator admission is the scaling bottleneck

`mw.coordinator.discovery_admit` increases by 23.1% from one to four workers.
The phase is single-coordinator work and remains approximately 1.4 seconds
while worker traversal falls below 0.5 seconds for the slowest worker.

Required investigation:

1. Add sub-zones inside discovery admission for duplicate detection, capacity
   checks, row admission and metadata updates.
2. Measure candidate discovery counts, duplicate counts and admitted counts per
   worker before and after sorting.
3. Check whether the coordinator repeatedly scans shared structures as worker
   count increases.
4. Evaluate whether admission can use a pre-sorted merge, flat-array indexing
   or independent worker-local admission preparation without changing the
   deterministic merge contract.

### P1: Fixed scheduling produces worker imbalance

All four workers receive 250 trajectories, but active time varies by 57% from
the fastest worker to the slowest worker. Three workers report tail idle time
while waiting for the slowest worker.

Required investigation:

1. Compare fixed and dynamic scheduling with the same seed and workload.
2. Record trajectory cost or traversal-state complexity by worker chunk.
3. Repeat with more batches so chunk assignment is not represented by one
   sample only.
4. Test worker pinning separately. The current captures are unpinned, with
   workers observed on processors 24, 25, 16 and 23.

### P2: One-batch captures are insufficient for final scaling qualification

The captures are sufficient to locate coarse bottlenecks, but one batch gives
only one timing sample and makes worker imbalance sensitive to the deterministic
trajectory partition. The next diagnostic should use at least two or four
identical batches, while keeping the same worker count, seed and configuration.

For fast iteration, the isolated scaling benchmark should be used to study
worker scheduling and coordinator phases. The real-workflow capture should be
used separately to study bucket loading, memory behavior, checkpoint output and
blueprint export.

## Interpretation notes for Tracy CSV

The CSV `total_perc` values must not be summed. Zones overlap across threads
and nesting levels. In particular, `mw.worker.cv_wait` represents condition
variable waits over the worker thread lifetime, not only the measured training
batch.

Use the JSON report as the authoritative source for:

- batch wall time;
- worker active time;
- coordinator wait and admission;
- worker tail idle time;
- accepted and discarded trajectory counts;
- memory and output timing.

Use the Tracy CSV to locate coarse phase relationships and then refine the
implementation with narrower zones.

## Recommended development order

1. Instrument checkpoint serialization and file output.
2. Subdivide coordinator discovery admission.
3. Run fixed versus dynamic scheduling with two or more batches.
4. Repeat the four-worker capture with worker pinning enabled.
5. Compare normal and Tracy binaries only after output timing is separated from
   batch throughput.

## Evidence files

- `tmp/tracy/real-1w-1x1000/real-1w-1x1000.csv`
- `tmp/tracy/real-1w-1x1000/report.json`
- `tmp/tracy/real-4w-1x1000/real-4w-1x1000.csv`
- `tmp/tracy/real-4w-1x1000/report.json`
- `tmp/tracy/real-1w-1x1000/run_manifest.json`
- `tmp/tracy/real-4w-1x1000/run_manifest.json`
