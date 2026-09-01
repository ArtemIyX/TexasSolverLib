# Multiway blueprint multithreaded training

**Status:** Complete for the activation and qualification slice
**Completed:** 2026-09-01

The existing deterministic worker pool is now exposed by the workflow path. Schema 2 supports `training_worker_count` from 1 through 16, schema 1 remains a one-worker compatibility profile, and `--threads` overrides the configured training count. Training performs memory admission before session construction and reports requested/effective workers and stage timings. Worker-delta capacity telemetry now uses the true per-worker high-water mark.

## Validation

- `python scripts/full_build.py`: PASS. Debug build and all registered tests passed.
- Release benchmark fixture: three-player river depth-one, seed 119, one warmup, three timed repeats.
- Before/after activation comparison at batch size 4,000:
  - 1 worker: 26,122 trajectories/s median.
  - 4 workers: 57,463 trajectories/s median.
  - Gain: 2.20x, with identical deterministic merge fingerprint `13099238162004710642`.
- Larger batch-size check at 16,000 trajectories:
  - 1 worker: 26,128 trajectories/s median.
  - 4 workers: 44,071 trajectories/s median, 1.69x.
  - 8 workers: 40,162 trajectories/s median.

Commands:

```text
python scripts/full_build.py
build/Release/texas_solver_multiway_training_scaling.exe --workers 1,2,4,8,16 --batch-sizes 4000 --warmup-batches 1 --timed-batches 2 --repeats 3 --seed 119
build/Release/texas_solver_multiway_training_scaling.exe --workers 1,4,8 --batch-sizes 16000 --warmup-batches 1 --timed-batches 1 --repeats 3 --seed 119
```

## Qualification result

Four workers are the qualified default for this host and fixture. Eight and sixteen workers were slower than four, so the implementation does not claim a 16-worker speedup. The benchmark fixture is intentionally small and is not production-scale F1 evidence.
