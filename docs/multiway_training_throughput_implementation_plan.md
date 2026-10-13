# Multiway Training Throughput Implementation Plan

## Objective

Improve multiway training throughput without weakening determinism, exact-mode
behavior, immutable epoch semantics, or memory limits. Treat the current scaling
results as symptoms, not a diagnosis:

| Batch | 1 worker | 2 workers | 4 workers |
|---:|---:|---:|---:|
| 32 | 203.1 trajectories/s | 163.7 trajectories/s | 166.7 trajectories/s |
| 128 | 259.1 trajectories/s | 217.2 trajectories/s | 215.9 trajectories/s |

The matching fingerprints, microsecond-scale coordinator lock wait, and
sub-0.4 ms sort/merge times rule out several correctness and merge hypotheses.
They do not distinguish allocator contention, cache or bandwidth pressure,
processor placement, worker wake overhead, or traversal work inflation.

This document is an implementation plan only. No optimization is selected until
Ticket 0 produces the required evidence.

## Inspected implementation map

### Benchmark and launch paths

- `examples/benchmarks/multiway_training_scaling_main.cpp`
  - `BenchmarkConfig` controls workers, batch sizes, warmup, timed batches, and
    repeats.
  - `BenchmarkFixture` always constructs a pinned, immutable-epoch runner with
    the fixed scheduler.
  - `BenchmarkResult` reports aggregate worker time, coordinator wait, lock wait,
    sort, merge, per-worker trajectory extrema, one-worker peak deltas, and the
    merge fingerprint.
  - The executable prints text to stdout and has no structured output path,
    affinity-mode option, scheduler option, per-worker timing vectors, process
    CPU time, allocation counters, or RSS.
- `scripts/run_multiway_worker_matrix.ps1` and
  `scripts/run_multiway_ram_1000.ps1`
  - Already support fixed/dynamic scheduling, pinned/unpinned execution,
    process CPU time, RSS sampling, run-local configuration, JSON, and CSV for
    the production training executable.
  - Reuse their artifact layout and correctness gates instead of creating an
    unrelated measurement format.

### Worker loop and scheduling

- `include/solver/multiway/engine/multiway_traversal.hpp`
  - `MultiwayRootBatchRunner` owns persistent threads, per-worker condition
    variables, completion state, scheduler state, and aligned `WorkerScratch`.
  - `WorkerScratch` reuses regret deltas, continuation deltas, discovery data,
    action-menu scratch, and search-profile counters.
- `src/solver/multiway/engine/multiway_traversal.cpp`
  - `MultiwayRootBatchRunner::worker_loop()` waits for a batch generation,
    traverses its fixed range or claims four-trajectory dynamic chunks, sorts
    worker streams, and signals completion.
  - `MultiwayRootBatchRunner::run()` resets scratch, publishes a batch, wakes
    active workers, waits for completion, aggregates counters, admits immutable
    discoveries, and performs deterministic merge.
- `include/solver/multiway/engine/multiway_scheduler.hpp` and
  `src/solver/multiway/engine/multiway_scheduler.cpp`
  - Fixed mode uses deterministic round-robin trajectory ranges.
  - Dynamic mode uses a shared atomic chunk index and a fixed chunk size of four.
  - Per-trajectory seeds and final merge ordering are independent of worker
    claim order.

### Traversal hot path

- `MultiwayRootExternalSamplingTraversal::run()` samples the private deal and
  enters recursive traversal.
- `traverse_decision()` performs bucket lookup, immutable row lookup, regret
  matching, fixed-state successor construction, action-menu generation, child
  descriptor construction, recursive action evaluation, and delta generation.
- `traverse_public_chance()` samples chance, constructs public descriptors,
  performs street transitions, and continues traversal or settlement.
- `evaluate_leaf()` runs continuation selection and the typed leaf evaluator.
- `append_infoset_update_noalloc()` writes directly to preallocated worker delta
  streams.
- Existing optional profile stages cover private sampling, public chance,
  action-menu generation, public graph work, row lookup, regret matching,
  terminal settlement, continuation leaves, merge, and export. They are
  aggregated and do not expose worker idle, allocation, or cache behavior.

### Coordinator and immutable epochs

- `MultiwaySolverCoordinator::has_infoset_row_epoch()`,
  `regret_matched_strategy_into_epoch()`, and
  `action_below_regret_epoch()` read immutable storage without taking the
  coordinator shared mutex.
- `MultiwayRootBatchRunner::run()` admits worker discovery streams after all
  workers complete, then calls `merge_worker_streams()` in deterministic order.
- `MultiwaySolverCoordinator::merge_worker_streams_locked()` performs the
  current k-way merge over already sorted streams. Existing measurements do not
  justify changing it.
- `coordinator_lock_wait_nanoseconds` covers measured unique-lock acquisition in
  coordinator admission. It is not condition-variable wait, worker wake delay,
  read-side time, or full serialized coordinator time.

### Affinity

- `MultiwayRootBatchRunner::worker_loop()` uses
  `SetThreadAffinityMask(1 << worker_index)` on Windows.
- The return value is ignored. The mapping does not report failure, physical
  core, SMT sibling, NUMA node, CCD, or processor group.
- Existing pinned results therefore prove only the requested logical-mask mode,
  not that workers used distinct physical cores or a favorable cache topology.

### Allocation and scratch

- Worker delta, continuation, discovery, and action-menu containers are reserved
  at runner construction and rewound between batches.
- Recursive traversal still constructs owning `MultiwayPublicStateDescriptor`
  values. `MultiwayPublicBuilder::make_action_child()` copies board/history data
  and appends history, and chance/street child construction owns dynamic arrays.
- `make_multiway_betting_snapshot()` and public builder APIs use owning vectors.
  `traverse_public_chance()` also calls a vector-returning action-menu path.
- Immutable discovery aggregation creates `discoveries`, `rows`, and
  `vector<bool>` each batch. These are coordinator-side allocations after worker
  completion, not worker traversal allocations.
- This source evidence identifies allocation candidates but does not establish
  their runtime cost. Ticket 0 must count them before code is redesigned.

### Existing counters and missing counters

Existing counters include accepted/discarded trajectories, aggregate and
per-worker active time, coordinator and barrier wait, coordinator lock wait,
sort, merge, per-worker trajectory counts, cumulative merged deltas, maximum
single-worker delta entries, discovery/admission counts, profile stages, RSS,
and deterministic fingerprints.

Missing counters required for diagnosis:

- worker wake latency, CV wait, post-work tail idle, and completion-notify time;
- traversal time per worker separated from scheduling and sorting;
- per-worker profile-stage snapshots;
- worker-thread allocation count and bytes;
- regret, continuation, and discovery high-water marks per worker;
- total peak regret plus continuation delta entries across all workers;
- generated versus unique/admitted discovery and row counts;
- decision, action, terminal, leaf, and update counts per worker;
- affinity request, resolved processor/core, and pin success;
- process CPU time and working-set/RSS in the standalone scaling benchmark;
- optional cache-miss and memory-bandwidth evidence.

## Ticket 0: Measurement foundation

This is the first and smallest implementation ticket. It must not change
scheduling, traversal semantics, merge order, storage layout, or production
defaults.

### Changes

1. Extend `MultiwayRootBatchResult`, `WorkerScratch`, and
   `MultiwayBlueprintTrainingStatus` with cold-boundary counters:
   - per-worker traversal, sort, wake-latency, completion, and tail-idle time;
   - regret delta, continuation delta, discovery descriptor, and discovery row
     counts and high-water marks;
   - total peak delta entries as the sum across worker streams at the pre-merge
     boundary;
   - generated update and discovery counts.
2. Add batch timestamps around notification, worker start, traversal completion,
   sort completion, completion notification, discovery admission, and merge.
   Do not add a clock read per action, bucket, update, or trajectory.
3. Add an optional benchmark-only allocation probe that counts worker-thread
   allocations and bytes. Keep it outside the production allocator contract and
   disabled in normal training. Attribute setup, worker traversal, discovery,
   and merge scopes separately.
4. Extend `examples/benchmarks/multiway_training_scaling_main.cpp` with:
   - `--scheduler fixed|dynamic`;
   - `--affinity unpinned|logical` initially;
   - `--profile disabled|checkpoints`;
   - `--output <path>` for one JSON result plus text output;
   - batch sizes below the old 1,000-trajectory gate for diagnostic runs;
   - per-worker vectors, process CPU time, current/peak RSS, total delta peaks,
     allocation counters, and affinity status.
5. Add or extend a PowerShell matrix wrapper so every sample is a fresh process,
   stdout/stderr goes to its own file under `tmp/`, and the wrapper produces one
   CSV and JSON summary. Never use console text as the retained evidence.

### Affected files and symbols

- `include/solver/multiway/engine/multiway_traversal.hpp`
  - `MultiwayRootBatchResult`, `MultiwayRootBatchRunner::WorkerScratch`
- `src/solver/multiway/engine/multiway_traversal.cpp`
  - `MultiwayRootBatchRunner::worker_loop()`, `run()`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
  - `MultiwayBlueprintTrainingStatus`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
  - `MultiwayBlueprintTrainer::run_batches()`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/benchmarks/multiway_training_scaling_main.cpp`
- `scripts/run_multiway_worker_matrix.ps1`, or a narrowly named scaling wrapper
  if production and synthetic fixtures cannot share one interface
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Tests

- Counter vectors match configured worker count.
- Sum of worker trajectory counts equals attempted trajectories.
- Total peak regret deltas equals the sum of worker regret-stream sizes at the
  sampled pre-merge boundary and is at least the maximum per-worker value.
- Continuation and discovery totals use the same explicit sum rule.
- Time components are non-negative and obey boundary invariants without
  asserting flaky wall-clock thresholds.
- Report JSON preserves all new fields and rejects inconsistent vector sizes or
  impossible totals.
- Allocation probing disabled versus enabled produces identical checkpoint,
  policy, accepted/discarded counts, and fingerprint.

### Rollback criteria

Remove or redesign a counter if disabled-mode median throughput changes by more
than 2%, RSS grows by more than 1%, output identity changes, or hot loops gain
allocation, formatting, logging, hashing, virtual dispatch, or per-item clock
reads.

## Reproducible measurement plan

### Fixed inputs

Record in every result: Git commit, dirty state, build configuration, compiler,
CPU model, logical/physical processor counts when available, OS, power plan,
scheduler, affinity mode, pin success, fixture identity, seed, warmup batches,
timed batches, worker count, batch size, and profile/allocation-probe state.

Use the same seed and fixture for every cell. Run unrelated CPU-intensive work
outside the matrix. Do not mix profiled and unprofiled results.

### Baseline matrix

- Batch sizes: 32, 128, 256, 512.
- Workers: 1, 2, 4, 8 where the host supports them.
- Scheduler: fixed first; dynamic as a paired comparison after the fixed matrix.
- Affinity: logical pinned and unpinned because both are already supported.
- Warmup: one batch per fresh process.
- Timed work: three batches per sample.
- Repeats: five fresh-process samples per cell. If median absolute deviation is
  above 5% of the median, extend only that cell to ten samples.
- Each sample remains below 10,000 trajectories. At batch 512, one warmup plus
  three timed batches is 2,048 total trajectories.

For each cell calculate median, median absolute deviation, minimum, maximum,
speedup versus one worker, parallel efficiency, process CPU per trajectory,
critical-path traversal per trajectory, aggregate worker time per trajectory,
wake and tail-idle shares, profile-stage time per trajectory, allocations and
bytes per trajectory, total delta entries, and peak RSS/working set.

### Output layout

Use a run root such as:

```text
tmp/multiway_throughput_baseline/<commit>/<scheduler>/<affinity>/b<batch>/w<workers>/r<repeat>/
  stdout.txt
  stderr.txt
  metrics.json
tmp/multiway_throughput_baseline/<commit>/matrix.csv
tmp/multiway_throughput_baseline/<commit>/matrix.json
```

Inspect `metrics.json`, `matrix.csv`, error files, and derived summaries after
the run. Do not rely on console output. Treat a cell as invalid if any sample has
discarded work, a capacity error, a failed affinity request when pinning was
requested, inconsistent counters, or a different deterministic fingerprint.

### Profile and hardware-counter runs

Run the complete matrix with checkpoint profiling and allocation probing off.
Then run three diagnostic repeats for batch 512 at 1, 4, and 8 workers with
checkpoint profiling on, followed by allocation probing on. Quantify probe
overhead against the paired unprofiled cells.

If a low-overhead counter tool is already available on the machine, collect one
short diagnostic sample for 1, 4, and 8 workers at batch 512. Capture cycles,
instructions, LLC misses or miss rate, memory bandwidth, context switches, and
CPU frequency if the tool supports them. Do not use the profiled throughput as
the performance result. If no suitable tool is installed, record that the
hardware-counter branch is unavailable and continue with allocation, CPU-time,
RSS, and affinity evidence.

## Bottleneck decision tree

1. **Is worker idle time high?**
   - High wake latency with low traversal imbalance indicates dispatch/CV cost.
     Implement the worker synchronization ticket.
   - Low wake latency but high tail idle indicates task-cost imbalance. Compare
     fixed and dynamic scheduling before changing traversal.
   - Idle shares that shrink materially from batch 32 to 512 indicate fixed
     per-batch overhead. Evaluate batch amortization without changing the
     production epoch size yet.
2. **Does traversal time per trajectory grow with worker count?**
   - If allocation count or bytes per trajectory also grow, or allocator samples
     dominate, implement reusable scratch and allocation removal.
   - If allocations remain flat but cache misses, bandwidth, or CPU time per
     trajectory worsen, implement traversal locality work.
   - If neither signal changes, check affinity resolution, SMT placement, CPU
     frequency, and context switches before changing algorithms.
3. **Do affinity modes change results?**
   - If unpinned beats logical pinning or physical-core separation beats both,
     implement topology-aware affinity with explicit pin verification.
   - If differences remain within noise, leave affinity configurable and do not
     make it the default explanation.
4. **Do allocations dominate?**
   - Separate worker traversal allocations from post-worker discovery and merge
     allocations. Optimize the dominant scope first.
   - Require zero worker hot-path allocations only after a fixed-capacity
     replacement preserves public-state identity and capacity behavior.
5. **Do memory bandwidth or cache misses dominate?**
   - Prefer compact transient state, precomputed immutable lookup metadata, and
     contiguous worker-local data. Do not add SIMD to irregular traversal.
   - Retest physical-core placement and one-worker latency to distinguish cache
     locality from bandwidth saturation.
6. **Is one coordinator phase serialized?**
   - High discovery/admission share selects coordinator admission work.
   - High merge share selects merge work.
   - High trainer-only time selects discount/prune or reporting work.
   - Do not optimize coordinator sort/merge while each remains below 5% of wall
     time. Reconsider it at 10%, and require action at 15% or more.

## Ordered optimization backlog

The percentages below are acceptance targets, not unmeasured promises. Each
ticket is implemented only when the decision tree selects it.

### O1. Reduce worker synchronization and wake overhead

- **Affected files/symbols:** `multiway_traversal.hpp/.cpp`,
  `MultiwayRootBatchRunner::worker_loop()`, `run()`, pool condition variables,
  completion signaling.
- **Bottleneck:** High wake/CV/completion time or idle share with balanced
  traversal.
- **Expected throughput impact:** Target 5% to 15% at batch 32/128 and no
  regression at 256/512.
- **RAM impact:** Neutral or less than one cache line per worker.
- **Correctness risk:** Medium. Missed generations, premature completion, and
  shutdown races are possible.
- **Unit tests:** Repeated empty/small batches, active-worker subsets, deadline
  cancellation, worker exception propagation, destruction while idle, and
  deterministic 1/2/4/8-worker equivalence.
- **Benchmark:** Full baseline matrix plus wake, CV, tail-idle, and context-switch
  comparison.
- **Rollback:** Revert if any hang/race appears, fingerprint changes, idle share
  does not fall by at least 25%, or median throughput gain is below 3%.

### O2. Improve task partitioning

- **Affected files/symbols:** `multiway_scheduler.hpp/.cpp`,
  `MultiwayScheduler::dynamic_chunk_range()`, `worker_loop()` dynamic claim path,
  scheduler configuration in trainer/workflow/benchmark.
- **Bottleneck:** High tail idle or per-worker traversal-time imbalance with low
  wake overhead.
- **Expected throughput impact:** Target 5% to 20% at 4/8 workers and at least a
  25% reduction in critical-path imbalance.
- **RAM impact:** Neutral. Keep ranges/chunks in caller-owned fixed storage.
- **Correctness risk:** Medium. Claim order must not affect trajectory IDs,
  seeds, delta ordering, epoch boundaries, or fingerprints.
- **Unit tests:** Exact chunk coverage, no duplicate/missing trajectory IDs,
  fixed/dynamic checkpoint and policy equality, repeated dynamic runs, tail
  chunks, workers greater than trajectories, and capacity exhaustion.
- **Benchmark:** Compare fixed and chunk sizes 1/4/8/16 only on cells with proven
  imbalance. Include atomic-claim count and time.
- **Rollback:** Keep fixed mode if dynamic gain is below 5%, atomic time exceeds
  2% of wall time, or determinism/capacity gates fail.

### O3. Improve and verify worker affinity

- **Affected files/symbols:** affinity setup around
  `MultiwayRootBatchRunner::worker_loop()`, runner configuration, benchmark and
  matrix metadata. Add a small platform helper only if needed.
- **Bottleneck:** Throughput, CPU time, or cache indicators change materially by
  affinity mode; current logical masks place workers poorly or fail.
- **Expected throughput impact:** Target 5% to 20% at 2/4/8 workers on the
  affected host.
- **RAM impact:** Negligible.
- **Correctness risk:** Low to medium. Platform topology and processor groups are
  easy to map incorrectly.
- **Unit tests:** Pure mapping tests with synthetic topology, bounds, processor
  groups, SMT sibling avoidance, fallback on unsupported platforms, and config
  validation. Strategy identity must ignore placement.
- **Benchmark:** Unpinned, logical-compact, and distinct-physical-core modes at
  all batch sizes. Record resolved CPU/core and pin result per worker.
- **Rollback:** Default to unpinned if pinning fails, improves less than 3%, or
  worsens any supported worker count by more than 3%.

### O4. Increase batch amortization carefully

- **Affected files/symbols:** training configuration, benchmark/matrix scripts,
  `MultiwayBlueprintTrainer::run_batches()`, immutable epoch boundaries.
- **Bottleneck:** Fixed dispatch/discovery/merge cost dominates small batches and
  its share falls at 256/512.
- **Expected throughput impact:** Target 5% to 15% by selecting a measured batch
  size, with no claim of worker parallelism improvement.
- **RAM impact:** Higher worker-delta, continuation, and discovery high-water
  marks proportional to batch size. Must stay inside preflight and RSS limits.
- **Correctness risk:** Medium. Batch size changes visibility between immutable
  epochs and can change training results even when each configuration is
  deterministic.
- **Unit tests:** Clean epoch boundaries, checkpoint/resume at each supported
  size, capacity rejection without partial publication, deterministic repeat at
  fixed size, and documented cross-size comparison semantics.
- **Benchmark:** Full batch-size matrix with total peak deltas, RSS, discovery,
  and fingerprints compared only within the same batch size.
- **Rollback:** Reject a larger default if RSS/preflight headroom is exceeded,
  discards occur, quality gates regress, or gain is below 5%.

### O5. Reuse worker construction scratch and remove proven allocations

- **Affected files/symbols:** `WorkerScratch`, `TraversalContext`,
  `traverse_decision()`, `traverse_public_chance()`, `evaluate_leaf()`,
  `make_multiway_betting_snapshot()`, `MultiwayPublicBuilder` child factories,
  `MultiwayActionAbstraction::make_legal_actions_into()`, terminal sampling and
  settlement helpers selected by allocation evidence.
- **Bottleneck:** Worker traversal allocation count/bytes or allocator time grows
  with worker count.
- **Expected throughput impact:** Target 10% to 30% at 2/4/8 workers and reduced
  worker-active time per trajectory.
- **RAM impact:** Small fixed worker-local scratch increase; lower transient heap
  traffic and equal or lower peak RSS. Every fixed capacity needs preflight.
- **Correctness risk:** High. Public-state history, action identity, vector
  lifetime, recursion depth, and terminal arrays must remain exact.
- **Unit tests:** Allocation-free contract after warmup, action/public-state ID
  differential tests, maximum action/depth cases, chance and street transitions,
  terminal settlement, scratch overflow rejection, and 1/2/4/8-worker exact
  checkpoint/policy equivalence.
- **Benchmark:** Allocation probe plus full unprofiled matrix. Compare allocations
  and bytes per trajectory, worker time, RSS, and fingerprints.
- **Rollback:** Revert a conversion if it changes IDs/results, adds truncation,
  exceeds fixed capacity, fails to remove at least 80% of targeted allocations,
  or improves throughput by less than 5%.

### O6. Improve traversal locality

- **Affected files/symbols:** `TraversalContext` cached bucket metadata,
  `MultiwayBucketRegistry` lookup path, compact row metadata lookup,
  `MultiwayPublicStateDescriptor` transient representation, and worker-local
  frequently written counters.
- **Bottleneck:** LLC misses, memory bandwidth, or CPU time per trajectory grows
  with workers after allocations and affinity are controlled.
- **Expected throughput impact:** Target 10% to 25% at 4/8 workers.
- **RAM impact:** Prefer neutral or lower. Any lookup table must have an explicit
  bounded estimate and remain below the configured memory budget.
- **Correctness risk:** Medium to high. Cached IDs, bucket mappings, and row
  metadata can become stale across immutable epochs.
- **Unit tests:** Cached/uncached differential coverage, every street, board and
  bucket boundaries, epoch invalidation, compact/scalar storage comparison, and
  deterministic worker-count equivalence.
- **Benchmark:** Full matrix plus hardware counters when available, profile-stage
  distribution, RSS, and one-worker latency.
- **Rollback:** Revert if cache/bandwidth indicators do not improve, one-worker
  latency regresses over 3%, RAM rises without preflight, or any differential
  test fails.

### O7. Reduce serialized coordinator work

- **Affected files/symbols:** immutable discovery aggregation in
  `MultiwayRootBatchRunner::run()`, `MultiwaySolverCoordinator::admit_public_state()`,
  `admit_infoset_row()`, trainer discount/prune boundaries, and their scratch.
- **Bottleneck:** Post-worker discovery/admission or trainer-only serialized time
  is at least 10% of wall time and grows with workers or model size.
- **Expected throughput impact:** Target 5% to 20% on affected batches.
- **RAM impact:** Reused coordinator scratch may retain bounded capacity but must
  not duplicate graph-sized data.
- **Correctness risk:** High. Parent-before-child admission, row shape checks,
  immutable publication, and capacity failure atomicity must remain unchanged.
- **Unit tests:** Duplicate discovery, parent ordering, conflicting descriptor,
  row shape conflict, capacity failure with no partial publication, and exact
  epoch/checkpoint equivalence.
- **Benchmark:** Separate traversal, discovery aggregation, admission,
  discount/prune, and merge time. Compare generated versus unique admissions.
- **Rollback:** Revert if the selected serialized share falls by less than 25%,
  total throughput gains less than 3%, RAM exceeds estimate, or epoch behavior
  changes.

### O8. Optimize sorting or merge only if justified

- **Affected files/symbols:** worker `sort_fixed_order()`,
  `MultiwaySolverCoordinator::merge_worker_streams_locked()`, continuation
  selector merge, compact storage apply path.
- **Bottleneck:** Sort plus merge reaches at least 15% of wall time after earlier
  work, or hardware counters show it is the dominant bandwidth phase.
- **Expected throughput impact:** Target 5% to 15% total throughput and at least
  30% lower measured sort/merge time.
- **RAM impact:** Neutral or lower. Do not recreate a full copied delta stream.
- **Correctness risk:** High. Tie order and floating-point accumulation order
  define the bitwise fingerprint.
- **Unit tests:** Scalar/reference merge differential, duplicate rejection,
  worker-empty streams, cell/trajectory tie ordering, compact and float storage,
  randomized deterministic streams, and exact fingerprint preservation.
- **Benchmark:** 1/2/4/8 workers across all batch sizes with sort/merge share,
  delta volume, memory bandwidth, and RSS.
- **Rollback:** Do not ship if fingerprint changes, merge share remains below the
  action threshold, time falls by less than 20%, or RAM increases materially.

## Correctness constraints for every ticket

- Preserve deterministic fingerprints across worker counts for the same seed,
  batch size, scheduler contract, and trajectory set.
- Preserve exact-mode behavior. Do not modify `HUNLFlatDCFR` or exact HUNL paths.
- Preserve immutable epoch behavior. Workers read the epoch snapshot, write only
  worker-local deltas/discovery, and publish only after a clean batch.
- Preserve trajectory IDs, per-trajectory seeds, traverser selection, action and
  public-chance sampling order, fixed merge tie order, and clean-batch rollback.
- Use C++17.
- Keep hot traversal, terminal, row-update, and merge loops free of strings,
  logging, formatting, heap allocation, `new`/`delete`, shared ownership,
  `std::function`, virtual dispatch, exceptions as normal flow, hash lookup, and
  unreserved vector growth.
- Own memory with RAII at subsystem boundaries. Pass fixed arrays, pointers, or
  existing lightweight views into hot kernels. Raw pointers never own memory.
- Keep scalar validation paths and focused regression tests for every behavior
  change. Do not use wall-clock thresholds in unit tests.

## Verification after each implementation ticket

Run verification only when implementation and execution are explicitly
authorized. Save verbose output under `tmp/` and inspect compact summaries.

1. Focused test executable or test-name filter for the changed contract.
2. Compact build:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/compact_build.py
   ```

3. Scan build errors if the compact build fails:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/scan_build_errors.py
   ```

4. Compact CTest:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/compact_ctest.py
   ```

5. Scan CTest errors if needed:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/scan_ctest_errors.py
   ```

6. Full build and test workflow:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py
   ```

7. Deterministic smoke benchmark at batch 32 for workers 1/2/4/8. Require zero
   discards, no capacity failure, identical merge fingerprints and checkpoints,
   consistent total counters, and bounded RSS.
8. Unprofiled throughput matrix below 10,000 trajectories per sample. Compare
   before/after medians, dispersion, speedup, efficiency, CPU time, worker
   critical path, allocations, total peak deltas, and RSS.
9. Profiled diagnostic subset to explain the result. Do not report profiled
   throughput as the performance result.
10. Independent C++ review with `cpp-code-review`. Fix only actionable findings
    and rerun affected verification.

An optimization passes only if correctness gates pass, its selected bottleneck
metric improves by the ticket threshold, median throughput improves by at least
5% for an affected multiworker cell, no unaffected cell regresses more than 3%,
and RSS remains inside the preflight estimate.

## Documentation and Git workflow

- Do not update `docs/PLURIBUS_LOG.md` for this plan. Update it after an
  implementation ticket is complete, reviewed, and its actual verification is
  known.
- After measured implementation, update
  `docs/multiway_blueprint_training_scaling_benchmark.md` with current code facts
  and before/after throughput evidence.
- Update `docs/multiway_memory_scaling_completion_report.md` with total peak
  delta entries, internal RSS, external working set, and memory impact.
- Store raw run artifacts only under `tmp/`. Commit only concise source,
  tests, scripts, and documentation.
- Make small commits. Use a title such as
  `pb-1 Multiway: Add worker throughput instrumentation` and a two- or
  three-line body describing the measured reason and preserved contracts.
- Keep instrumentation, each optimization, tests, and measured documentation in
  reviewable commits when practical.
- Do not stage or alter unrelated work, including the existing untracked
  `docs/multiway_worker_scaling_focused_plan.md`.
- Do not push to any server.

## First-ticket exit gate

Implement Ticket 0 only. Before selecting O1 through O8, collect these exact
measurements for batch sizes 32/128/256/512 and workers 1/2/4/8:

- five fresh-process repeats for fixed scheduling in logical-pinned and unpinned
  modes;
- paired dynamic results after the fixed baseline;
- wall throughput, process CPU time, aggregate and per-worker traversal time;
- worker wake latency, CV wait, completion time, and tail idle;
- existing profile stages per worker and aggregate;
- worker allocation count/bytes with probe overhead quantified;
- regret, continuation, discovery, update, and total peak delta counts;
- generated and admitted public-state/row counts;
- sort, merge, coordinator admission, and trainer-only time;
- current/peak RSS and externally sampled working set;
- resolved affinity and pin success;
- identical deterministic fingerprints, checkpoint contents, accepted counts,
  and zero discards within every comparable cell.

Proceed to exactly one next optimization selected by the decision tree. If the
measurements are noisy or no branch meets its gate, repeat the bounded baseline
instead of making a speculative code change.
