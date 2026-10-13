# Multiway Worker Scaling Focused Plan

## Scope

Compare the current pinned, fixed round-robin worker assignment with one
deterministic-output dynamic chunk scheduler. Limit the comparison to 4 and 8
workers, three batches of 200 trajectories, and three fresh-process repeats.
This is 600 trajectories per run and 7,200 trajectories for the full matrix.
Do not use WPR or run 10,000+ trajectories.

Current code facts:

- `MultiwayRootBatchRunner` assigns trajectory `i` to worker `i % workers`.
- Trajectory IDs and seeds depend only on the global trajectory ID.
- Worker regret streams are sorted by cell, trajectory ID, and payload before a
  deterministic k-way merge. Continuation streams are sorted by trajectory ID
  and sequence before their k-way merge.
- The report already contains worker active time, coordinator wait, derived
  barrier wait, coordinator lock wait, delta sort time, merge time, per-worker
  counts, RSS, and the merge fingerprint. The PowerShell summary omits several
  of these fields.

## Ordered implementation

1. **Make small batches runnable without changing the source artifact.**
   Add `-TrajectoriesPerBatch` to
   `scripts/run_multiway_ram_1000.ps1` and
   `scripts/run_multiway_worker_matrix.ps1`. Validate a positive value, copy
   `artifacts/f1_sizing_v1/pilot_batch1.cfg` into each run directory, replace
   only `trajectories_per_batch`, and pass the run-local config to the trainer.
   Derive directory names and expected trajectory counts from the option. Keep
   the source config unchanged. Add matrix parameters for workers, scheduling
   mode, and repeats; use workers 4 and 8, pinned mode, and fixed/dynamic modes
   for this comparison.

2. **Add an explicit scheduler mode with the current path as default.**
   Define a small enum in
   `include/solver/multiway/engine/multiway_scheduler.hpp`, thread it through
   `MultiwayBlueprintTrainingConfig` and `MultiwayRootBatchRunner`, and expose
   `--scheduler fixed|dynamic` in `examples/multiway_workflow_main.cpp`.
   `fixed` must continue to call `partition_round_robin_into()` unchanged.
   Record the selected mode and chunk size in the matrix output; do not alter
   strategy/checkpoint identity merely because a trajectory ran on another
   worker.

3. **Implement the focused dynamic scheduler without hot-path allocation.**
   In `src/solver/multiway/engine/multiway_traversal.cpp`, precompute fixed
   contiguous chunks of four trajectory IDs and let active workers claim the
   next chunk with one atomic increment. Reuse the existing worker threads,
   `WorkerScratch`, action-menu scratch, and preallocated delta streams. Within
   each chunk, process IDs in ascending order. Keep global trajectory IDs,
   `multiway_deterministic_trajectory_seed(seed, trajectory_id)`, traverser
   selection, and batch boundaries unchanged. Sort every worker stream with the
   existing comparators and use the existing k-way merges. Treat any discard,
   worker-stream capacity exhaustion, or fingerprint mismatch as a failed run.

4. **Extend deterministic contracts before benchmarking.**
   Update `tests/test_multiway_scheduler.cpp` for exact chunk coverage with no
   missing or duplicate IDs. Update
   `tests/test_multiway_recursive_traversal.cpp` and
   `tests/test_multiway_parallel_training.cpp` to compare fixed and dynamic
   modes at 1, 4, and 8 workers. Require identical accepted/discarded counts,
   merged delta counts, merge fingerprints, checkpoint storage, and exported
   policies. Repeat dynamic runs to cover different runtime claim orders.

5. **Expose the existing diagnostic fields in the focused matrix.**
   Add `coordinator_lock_wait_nanoseconds`, `delta_sort_nanoseconds`, and
   `merge_nanoseconds` to the PowerShell JSON/CSV. Also record process CPU time
   from the launched process. Derive, without double-counting overlapping time:

   - throughput median and min/max across three repeats;
   - per-worker active nanoseconds per trajectory;
   - active-time imbalance: `max(active/count) / mean(active/count) - 1`;
   - barrier share: `barrier_wait / elapsed_wall`;
   - lock contention share: `coordinator_lock_wait / worker_active`;
   - serial merge share: `merge / elapsed_wall`;
   - normalized process CPU use: `process_cpu / (elapsed_wall * workers)`;
   - peak RSS and memory-preflight estimate.

6. **Human Runs the bounded comparison and decide from medians.**
   Correctness is a hard gate: every run must complete 600 accepted
   trajectories with zero discarded trajectories, no failure/capacity flag,
   and one merge fingerprint across both modes, both worker counts, and all
   repeats. Peak RSS must remain within the existing preflight estimate.

   Diagnose in this order:

   | Cause | Acceptance evidence |
   |---|---|
   | Workload imbalance | Dynamic improves median throughput by at least 10% at the same worker count and reduces active-time imbalance by at least 25%, without increasing lock or merge shares by more than 2 percentage points. |
   | Synchronization overhead | Dynamic changes throughput by less than 5%, while barrier plus merge is at least 10% of wall time or coordinator lock wait is at least 10% of aggregate worker-active time and grows from 4 to 8 workers. |
   | Memory/cache contention | Dynamic changes throughput by less than 5%, active-time imbalance is below 10%, barrier/merge and lock shares are each below 5%, but 8-worker active or process CPU time per trajectory is at least 15% worse than 4-worker time and 8-worker throughput does not improve. This is sufficient to select targeted low-overhead hardware-counter work next, not to make an architectural change. |

   If none of these gates is met, keep the fixed scheduler and collect more
   repeats of the same bounded matrix. Do not broaden worker counts, batch size,
   profiling tools, or architecture in this step.

## Files to change

- `scripts/run_multiway_ram_1000.ps1`
- `scripts/run_multiway_worker_matrix.ps1`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/engine/multiway_scheduler.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_scheduler.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `tests/test_multiway_scheduler.cpp`
- `tests/test_multiway_recursive_traversal.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `docs/PLURIBUS_LOG.md` after implementation and verification, not for this plan

No training-report schema change is required because the needed timing fields
already exist. Scheduler mode and chunk size can remain matrix-run metadata.

## Verification commands

only huamn can run after implementation:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py

& .\scripts\run_multiway_worker_matrix.ps1 `
  -Batches 3 `
  -TrajectoriesPerBatch 200 `
  -Pinning pinned `
  -Workers @(4, 8) `
  -Scheduling both `
  -Repeats 3 `
  -OutputDirectory tmp\multiway_scheduler_200
```

Review `worker_matrix.csv` for the correctness gates first, then compare the
derived medians and shares. Do not use WPR results as benchmark numbers.
