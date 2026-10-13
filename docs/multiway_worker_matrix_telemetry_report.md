# Multiway Worker Matrix Telemetry

**Date:** 2026-10-13
**Run:** 160 trajectories per case, 5 repeats

## Configuration

- Pinning: `unpinned`
- Scheduling: `fixed`, `dynamic`
- Workers: `1`, `2`, `4`, `8`
- Batches: `5`
- Trajectories per batch: `32`
- Repeats: `5`
- Total runs: `40`

## Throughput

Median trajectories per second across the five repeats:

| Workers | Fixed | Dynamic | Dynamic vs fixed |
|---:|---:|---:|---:|
| 1 | 20.75 | 21.01 | +1.30% |
| 2 | 19.16 | 19.11 | -0.27% |
| 4 | 19.16 | 21.70 | +13.24% |
| 8 | 18.54 | 18.43 | -0.59% |

## Validation

The worker matrix passed all gates:

- 40 runs completed.
- Zero discarded trajectories.
- One deterministic merge fingerprint across all runs.
- Bounded peak RSS for every run.
- Correctness and capacity-exhaustion gates passed.

Average peak RSS was approximately 18.3 GiB per run.

## Observations

- This 160-trajectory workload does not scale with worker count.
- Dynamic scheduling was faster at four workers in this sample, but not at eight workers.
- Worker active-time imbalance increased with worker count, especially for dynamic scheduling.
- The results validate correctness and telemetry collection. They are not yet a stable scaling conclusion because the workload is small and only five repeats were used.

## Artifacts

- `tmp/ticket0/b32/unpinned/worker_matrix.json`
- `tmp/ticket0/b32/unpinned/worker_matrix.csv`
