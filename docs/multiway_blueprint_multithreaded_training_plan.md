# Multiway Blueprint Multithreaded Training Audit and Plan

## Scope

Audit the F1 blueprint training path used by `examples/multiway_workflow_main.cpp` and define the work required to use up to 16 training workers. This is a code audit and implementation plan. No build, test, solver, or benchmark was run.

## Verdict

The core training runner is already multithreaded. `MultiwayRootBatchRunner` creates a persistent thread pool, partitions trajectory IDs deterministically, keeps worker-local delta streams, and performs one fixed-order coordinator merge after all workers finish.

Production training still runs with one worker because the workflow layer disables the existing pool:

- `MultiwayWorkflowConfig::validate()` requires `reference_worker_count == 1`.
- Both checked-in F1 configs set `reference_worker_count=1`.
- `run_train()` copies that field to `config.limits.worker_count`.
- The CLI parses `--threads`, but passes it only to bucket generation and inspection. Training silently ignores it.

Enabling 16 workers is therefore mostly an activation, admission, telemetry, and qualification task. The existing shared coordinator lock and compact-policy allocation path are the likely scaling limits after activation.

## Current execution path

```text
texas_multiway_train
  -> run_train()
  -> MultiwayBlueprintTrainingSession
  -> MultiwayBlueprintTrainer::run_batches()
  -> MultiwayRootBatchRunner::run()
  -> deterministic trajectory partition
  -> persistent worker threads
  -> worker-local regret and continuation delta streams
  -> fixed-order coordinator merge
```

Existing contracts already provide:

- persistent threads rather than per-batch thread creation;
- deterministic trajectory IDs and seeds;
- bounded worker-local streams;
- no shared regret mutation during traversal;
- cancellation and exception propagation;
- fixed-order regret and continuation merges;
- direct 1/2/4-worker determinism coverage in `test_multiway_recursive_traversal.cpp`.

## Actionable review findings

Findings: 2.

[R1] P2 - Training accepts but ignores `--threads`

Location: `examples/multiway_workflow_main.cpp:750`

Evidence: The common parser stores `--threads`, but the train dispatch at line 806 calls `run_train()` without it. `run_train()` instead assigns `workflow.reference_worker_count` at line 497. A user can request `texas_multiway_train --threads 16` and still receive a one-worker training session without an error.

Impact: The CLI reports a successful run whose requested training parallelism was not applied. This blocks controlled 1/2/4/8/16 qualification and can waste the full production training window.

Fix: Pass an explicit training worker override into `run_train()`, validate it, record the requested and effective counts, and reject unsupported values rather than ignoring them.

[R2] P2 - Worker-delta capacity telemetry compares cumulative work with a per-worker limit

Location: `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:325`

Evidence: `status_.peak_worker_delta_entries` is updated from `diagnostics.worker_delta_entries_merged`, which is cumulative across all workers and batches. Line 348 compares that cumulative total with `configured_worker_delta_capacity`, the capacity of one worker stream. A sufficiently long successful run therefore reports `WorkerDeltas` exhaustion even when no worker stream approached capacity.

Impact: Sizing and production reports can falsely identify the worker-delta stage as exhausted. Increasing the worker count makes the false threshold arrive earlier and can lead to an incorrect capacity decision.

Fix: Return maximum per-worker stream occupancy from each root batch, track its true high-water mark, and keep cumulative merged entries as a separate throughput counter.

## Activation blockers and scaling risks

These are implementation requirements, not additional current defects because the checked-in workflow rejects worker counts above one.

1. **Workflow contract.** Schema 1 explicitly fixes `reference_worker_count` to one. A new schema or a narrowly compatible validation change is required. The field should be renamed to `training_worker_count`; one worker remains the reference qualification mode.
2. **Memory admission.** Worker and merge storage scale approximately with `worker_count * max_worker_delta_entries`. `preflight_multiway_memory()` accounts for this, but `run_train()` does not call it before constructing the session. Sixteen-worker activation must be rejected before allocation when it exceeds `process_memory_limit_bytes`.
3. **Artifact and checkpoint identity.** `MultiwayBlueprintTrainingConfig::identity()` includes worker count and other runtime limits. A 16-worker session cannot resume a one-worker checkpoint. Preserve this restriction in the first activation patch. Decouple execution settings from model identity only after multi-batch and resume equivalence is proven and versioned.
4. **Global coordinator lock.** Row admission, public-state admission, regret lookup, pruning lookup, and visit counters share `traversal_mutex_`. Correctness is protected, but workers can serialize at every decision and terminal node.
5. **Allocation inside the locked hot path.** Compact regret lookup returns a temporary `std::vector`, which `MultiwaySolverCoordinator::regret_matched_strategy_into()` copies while holding the coordinator mutex. This violates the repository hot-path rules and lengthens the serialized region.
6. **Insufficient training telemetry.** The report records total throughput but not requested/effective workers, worker imbalance, worker-active time, coordinator wait, merge time, or delta-sort time. A speedup result cannot currently be attributed.
7. **Batch size.** Effective workers are capped by trajectories in a batch. Small batches can spend more time waking workers and merging than traversing. Qualification must tune `trajectories_per_batch` with worker count while keeping trajectory IDs, batch semantics, and checkpoint boundaries deterministic.

## Implementation plan

### Phase 1: Expose and validate training workers

- Add `training_worker_count` to workflow config schema 2. Accept `1..16` for the F1 profile.
- Keep schema 1 readable as a one-worker legacy profile if compatibility is required.
- Pass `--threads` to `run_train()` as an optional override. Use the config value when no override is supplied.
- Resolve `effective_workers = min(requested_workers, trajectories_per_batch)`. Reject zero and values above the qualified cap.
- Change train help text from bucket-only wording and print requested/effective training workers before allocating the session.
- Set `config.limits.worker_count` from the resolved training count.
- Update `f1_sizing_v1.cfg` first. Do not change the acceptance profile to 16 until qualification passes.

### Phase 2: Add mandatory memory admission

- Call `preflight_multiway_memory()` in `run_train()` after all limits and the worker count are resolved and before `MultiwayBlueprintTrainingSession` construction.
- Derive an ordered warning/operating/reject budget from `process_memory_limit_bytes` without exceeding the configured process ceiling.
- Include the worker streams, continuation streams, merge scratch, sparse storage, range copies, and export capacity in the inputs.
- Fail with the estimated bytes and failing admission stage before allocating worker buffers.
- Define `worker_delta_capacity` explicitly as a per-worker capacity. Size it from measured maximum per-worker occupancy, not aggregate merged entries.

### Phase 3: Correct and extend telemetry

- Add root-batch fields for maximum per-worker delta entries, minimum/maximum trajectories, worker-active nanoseconds, coordinator-wait nanoseconds, sort nanoseconds, and merge nanoseconds.
- Aggregate true high-water marks in `MultiwayBlueprintTrainingStatus`.
- Add requested and effective worker counts, batch size, merge time, worker imbalance, and preflight estimate to the training report.
- Bump the training report schema and update its parser, validation, acceptance aggregation, and tests.
- Keep cumulative merged entries separate from capacity high-water telemetry.

### Phase 4: Prove activation correctness

- Extend workflow-config tests for schema 1 compatibility, schema 2 worker counts, zero, 17, and CLI override precedence.
- Add full training-session comparisons for 1/2/4/8/16 workers over multiple batches.
- Compare accepted/discarded trajectories, coverage counters, checkpoint storage arrays, continuation rows, merged delta fingerprints, and exported blueprint payloads.
- Add continuous versus checkpoint-resumed comparisons at the same worker count.
- Explicitly test and document rejection when resuming a checkpoint with a different worker-bound identity.
- Cover worker exception, cancellation, capacity exhaustion, and `trajectories_per_batch < worker_count`.

### Phase 5: Measure before changing synchronization

Run a Release matrix with identical seeds, trajectory IDs, capacities, and artifact input:

| Workers | Batch sizes | Required evidence |
|---:|---:|---|
| 1 | baseline and tuned | throughput, RSS, stage timing, payload hash |
| 2 | baseline and tuned | speedup, imbalance, lock/merge share |
| 4 | baseline and tuned | speedup, imbalance, lock/merge share |
| 8 | baseline and tuned | speedup, imbalance, lock/merge share |
| 16 | baseline and tuned | speedup, imbalance, lock/merge share |

Use at least three repeated samples after one warm-up. Select the default worker count from the best stable median that stays within the memory ceiling. Do not assume 16 is optimal merely because 16 hardware threads are available.

### Phase 6: Remove demonstrated bottlenecks

Apply these only when Phase 5 telemetry identifies them:

1. Add an allocation-free `MultiwayCompactStorage::regret_matched_strategy_into()` and call it directly under the coordinator lock.
2. Move terminal, leaf, missing-lookup, and street counters into worker-local scratch and reduce them after join.
3. Combine row admission and strategy lookup to avoid two mutex acquisitions at each decision.
4. Separate graph mutation from policy reads. Prefer a short exclusive admission path and parallel read path after a row exists. Any shared-lock design must protect vector relocation and preserve fixed-order merge behavior.
5. Replace linear public-state lookup only if profiling shows it is material. Use compact deterministic indexing; do not introduce hash lookup into the per-action or per-bucket hot loop.
6. Keep delta sort and merge single-coordinator and deterministic unless measurements prove it dominates. If parallelized later, use fixed partitions and a fixed reduction tree with cross-worker bitwise tests.

## Qualification gates

Activation is complete only when all gates pass:

- No data race, crash, discard regression, or capacity overrun in the worker matrix.
- One-worker behavior remains compatible with schema 1 and existing deterministic tests.
- Repeated runs at each worker count are bitwise stable for checkpoint state and exported policy payload.
- Cross-worker results match the one-worker reference for the same trajectory and batch schedule, excluding explicitly versioned execution metadata.
- Memory preflight admits the run and observed peak RSS remains below the configured process limit.
- Training reports identify requested/effective workers and isolate traversal, waiting, sorting, and merge costs.
- The selected multithreaded default has a repeatable material speedup over one worker. If 16 workers do not improve the median, retain the faster qualified count and use the telemetry to drive Phase 6.

## Expected file set

- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `configs/multiway/f1_sizing_v1.cfg`
- `configs/multiway/f1_dev_v1.cfg` after qualification
- focused workflow, traversal, training, memory, report, and acceptance tests

## Recommended first implementation slice

Implement Phases 1 through 4 without coordinator redesign. This activates the existing worker pool safely, fixes misleading capacity telemetry, and produces the evidence needed to decide whether synchronization work is justified. Then run the Release worker matrix and implement only the measured Phase 6 bottlenecks.
