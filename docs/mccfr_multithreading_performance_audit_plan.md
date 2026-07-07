# MCCFR Multithreading Performance Audit And Remediation Plan

## Executive Summary

The current `HUNLFlatMCCFR` prototype is functionally multithreaded, but it is not yet engineered to scale well with worker count on small or medium benchmark workloads.

Measured result from the new MCCFR scaling benchmark:

```text
workers total_ms iter_ms iters/s speedup eff merge_ms traverse_ms ev_p0 nodes
1       37.235   18.617  53.71   1.00    1.00 0.152    36.818      14.4547 61576
2       48.026   24.013  41.64   0.78    0.39 0.746    46.636      14.4547 61576
4       58.650   29.325  34.10   0.63    0.16 1.173    56.666      14.4547 61576
8       87.828   43.914  22.77   0.42    0.05 1.909    84.973      14.4547 61576
16      122.291  61.146  16.35   0.30    0.02 3.379    117.795     14.4547 61576
```

```text
 note=scaling preset = throughput study

workers total_ms      iter_ms       iters/s       speedup     eff         merge_ms        traverse_ms     ev_p0           nodes           
1       8199.185      81.992        12.20         1.00        1.00        11.306          8150.355        19.4978         15566022        
2       9734.583      97.346        10.27         0.84        0.42        37.398          9642.964        19.4978         15566022        
4       11161.859     111.619       8.96          0.73        0.18        59.186          11044.938       19.4978         15566022        
8       13305.126     133.051       7.52          0.62        0.08        96.296          13146.961       19.4978         15566022        
16      23290.151     232.902       4.29          0.35        0.02        186.097         23033.564       19.4978         15566022        
```

This is not a contradiction of MCCFR as an algorithm. It is evidence that the current implementation is dominated by orchestration overhead:

1. thread creation and join cost;
2. repeated serial setup inside each subbatch;
3. repeated heap allocation inside traversal;
4. repeated rebuild of temporary strategy views;
5. too-small benchmark workload for the current orchestration model.

The immediate conclusion is:

- MCCFR trajectories are parallelizable in principle.
- This implementation is not yet optimized enough to realize that scaling.

The purpose of this document is to give a detailed, implementation-ready plan for fixing the performance model without changing solver semantics or breaking tests.

## Scope

This document covers:

- `src/solver/hunl_flat_mccfr.cpp`
- `include/solver/hunl_flat_mccfr.hpp`
- the MCCFR benchmark program in `examples/benchmarks/hunl_mccfr_scaling_main.cpp`
- supporting tests for deterministic behavior and correctness

This document does not cover:

- changing exact `HUNLFlatDCFR`
- changing public API semantics unless explicitly noted
- adding poker-client automation or non-research integrations

## Current Observations

## Symptom

On the current scaling benchmark, increasing workers from `1` to `16` makes runtime worse instead of better.

## Why The Result Is Real

The result is not random noise:

- expected value is stable across worker counts;
- nodes visited are identical across worker counts;
- this means the solver is doing equivalent logical work;
- the performance regression is from implementation overhead, not from changing semantics.

## Main Root Causes

### 1. Threads are created and destroyed for every subbatch

Current flow:

1. `run_iteration()`
2. `run_player_batch()`
3. loop over subbatches
4. `run_player_subbatch()`
5. create worker threads
6. join worker threads
7. merge rows
8. continue to next subbatch

This means the solver pays OS thread startup and join cost repeatedly.

With benchmark defaults:

- `traversals_per_iteration = 2048`
- `batch_size = 64`
- `2048 / 64 = 32` subbatches per player
- if both players update, `64` subbatches per iteration

So the current design may launch and join a full worker team dozens of times per iteration.

That is the opposite of what we want in a high-performance sampled solver.

### 2. Strategy recomputation runs serially before every subbatch

`compute_current_strategy_rows()` is called at the start of `run_player_subbatch()`.

That means:

- current strategy rows are recomputed before each subbatch;
- this setup work is serialized;
- the setup does not benefit from more workers;
- the same policy may be recomputed many times before meaningful state changes justify it.

Even if traversal scaled perfectly, repeated serial setup would cap or erase speedup.

### 3. Traversal still allocates memory in hot loops

Inside `traverse()` and helper paths, the current implementation repeatedly constructs temporary vectors such as:

- `action_values`
- `bucket_strategy`
- `node_values`
- average-strategy temporaries
- sampled-action masks and inclusion probability arrays in AS mode

This has several bad effects:

- allocator overhead;
- cache disruption;
- lock contention inside the allocator when many threads run at once;
- more pressure on memory bandwidth;
- more variance in runtime.

This directly violates the intended hot-loop style from `AGENTS.md`.

### 4. Worker scratch is only partially scratch-like

The solver has worker-local storage, but it still rebuilds containers often:

- `rows.clear()`
- `row_lookup.clear()`
- `infoset_baseline_rows.clear()`
- `node_baseline_rows.clear()`
- new `std::vector<double>` for each row payload
- hash lookups for row creation

This is correctness-friendly, but not throughput-friendly.

### 5. Opponent sampling repeatedly rebuilds average action probabilities

When sampling opponent actions, the code repeatedly computes:

- average action probabilities for an infoset;
- often by looping over buckets and building temporary vectors.

For a given frozen strategy snapshot during a batch wave, this information is reusable.

The current code pays for rebuilding it many times.

### 6. The benchmark is too small for the current orchestration model

The synthetic benchmark is intentionally easy and deterministic, which is good.

But for scaling analysis, it is still too small in its current default shape:

- root actions = 4
- chance outcomes = 2
- opponent actions = 4
- reply actions = 4
- total logical work is modest

That means:

- useful trajectory work is small;
- orchestration overhead dominates quickly;
- the benchmark is good for correctness and reproducibility;
- it is not yet a strong benchmark for scaling potential.

This does not mean the benchmark is useless. It means we need two benchmark classes:

- a tiny deterministic sanity benchmark;
- a heavier scaling benchmark that amortizes thread overhead.

## Design Goal

After the remediation work:

1. `N` workers should not be slower than `1` worker on the designated scaling benchmark except for measurement noise or very small edge cases.
2. Threading overhead should be amortized over enough work that useful traversal dominates orchestration.
3. Solver outputs must remain deterministic under fixed seed and static partitioning.
4. Existing MCCFR tests must still pass.
5. No exact-solver behavior should change.

## Non-Goals

This plan does not try to:

- guarantee ideal linear speedup on all workloads;
- optimize every part of MCCFR before correctness is preserved;
- prematurely add dynamic scheduling;
- introduce atomics into regret updates;
- replace deterministic merge with a nondeterministic faster path yet.

## Phased Remediation Plan

## Phase 1: Add Better Benchmark Modes Before Optimizing

### Goal

Make benchmark results more informative, so we do not optimize blindly against a workload that is too small.

### Work

1. Keep the existing synthetic easy benchmark as a correctness-friendly baseline.
2. Add one or two heavier predefined benchmark presets in `hunl_mccfr_scaling_main.cpp`.
3. Allow the benchmark to choose preset by name.
4. Include benchmark metadata in output:
   - graph name;
   - node count;
   - traversals per iteration;
   - batch size;
   - whether both players update;
   - sampling mode.
5. Add a benchmark note in the output or docs describing:
   - tiny preset = sanity and determinism
   - scaling preset = throughput study

### Why First

If the benchmark stays too tiny, improvements may be hidden or misread.

### Acceptance

- benchmark supports at least one “tiny” and one “scaling” preset;
- same seed still gives stable expected value output;
- benchmark remains optional example-only code.

## Phase 2: Replace Per-Subbatch Thread Creation With Persistent Workers

### Goal

Eliminate the largest known overhead source.

### Work

1. Introduce a persistent worker-team model for `HUNLFlatMCCFR`.
2. Worker threads should be created once for the life of the solver, or at minimum once per solve/iteration wave.
3. Main thread should publish work ranges to workers instead of recreating threads.
4. Use a deterministic static partition for trajectory ranges, same as today.
5. Keep merge order fixed by worker index.
6. Add explicit lifecycle rules:
   - constructor prepares worker runtime if `workers > 1`;
   - destructor shuts workers down cleanly;
   - worker loop waits for work, executes, signals completion.

### Important Constraints

- no floating-point atomics for regret updates;
- no nondeterministic reduction order;
- no change in seeded result semantics.

### Why This Matters

This is the single clearest reason `16` workers are slower than `1`.

### Acceptance

- `run_player_subbatch()` no longer creates `std::thread` objects every call;
- fixed-seed 1-worker vs N-worker determinism tests still pass;
- scaling benchmark shows a clear improvement relative to the current version.

## Phase 3: Move Strategy Snapshot Computation Out Of The Inner Subbatch Loop

### Goal

Stop recomputing frozen strategy data before every small chunk of work.

### Work

1. Identify which policy-dependent data is logically frozen during a batch wave.
2. Compute current strategy rows once per player batch or once per iteration stage, not once per subbatch.
3. Explicitly define when strategy data becomes stale:
   - stale only after merge changes regrets;
   - valid throughout the current subbatch wave.
4. If needed, restructure the execution order to:
   - build strategy snapshot;
   - run many trajectories against it;
   - merge deltas;
   - rebuild snapshot.
5. Re-check exact/public-chance/external/AS semantics after this refactor.

### Risks

The main risk is accidentally changing the effective update schedule.

That is why tests for deterministic output and small-game convergence must stay in place.

### Acceptance

- `compute_current_strategy_rows()` is not called once per tiny subbatch anymore;
- output remains deterministic under fixed seed;
- existing MCCFR tests still pass.

## Phase 4: Remove Heap Allocation From Traversal Hot Paths

### Goal

Make per-trajectory traversal allocator-free in steady state.

### Work

1. Extend `WorkerScratch` with reusable buffers for:
   - `action_values`
   - `bucket_strategy`
   - `node_values`
   - AS sampled masks
   - AS inclusion probabilities
2. Size these buffers using known graph maxima:
   - `graph_.max_actions`
   - maximum bucket count for the traversed player
3. Replace temporary `std::vector` construction in `traverse()` with scratch-backed spans or pointer ranges.
4. Keep scalar validation behavior identical.
5. Avoid new allocation in:
   - decision-node evaluation
   - opponent-sampling branch
   - AS branch
   - regret update branch

### Important Note

This is not just about raw speed. It also reduces allocator contention between workers.

### Acceptance

- no `std::vector` construction remains in the hot traversal branch during steady-state execution;
- output remains unchanged under fixed seed;
- benchmark runtime improves for both 1-worker and N-worker runs.

## Phase 5: Make Worker Delta Storage Truly Preallocated

### Goal

Reduce per-subbatch row bookkeeping and allocator churn.

### Work

1. Audit `WorkerScratch::ensure_row()` and baseline row creation.
2. For the current full-graph validation backend, prefer indexed storage keyed by infoset id where practical.
3. Replace repeated hash-map row creation with:
   - visited bitset or dense small index map;
   - preallocated delta rows;
   - dirty row list for merge.
4. Keep sparse production semantics conceptually compatible, but optimize the validation backend first.
5. Separate:
   - row activation bookkeeping
   - row payload storage

### Why

The current worker-local model is deterministic, but still container-heavy.

### Acceptance

- worker delta storage does not allocate new payload vectors per active row in normal steady state;
- merge still runs in fixed worker index order;
- benchmark merge cost and total cost improve.

## Phase 6: Cache Average Action Probabilities Per Batch Wave

### Goal

Avoid recomputing identical averaged action distributions repeatedly.

### Work

1. Identify all places where `average_action_probabilities()` is used in hot paths.
2. Build a batch-local cache keyed by infoset id.
3. Populate it from the frozen current strategy snapshot.
4. Reuse cached averages for:
   - opponent action sampling;
   - any other branch that only needs average policy, not bucket-local policy.
5. Keep invalidation simple:
   - clear or rebuild cache after each merge wave.

### Risks

Do not confuse:

- bucket-local strategy needed for regret updates
- infoset-average strategy needed for opponent sampling

These are not interchangeable.

### Acceptance

- repeated calls to `average_action_probabilities()` are removed from hot traversal paths;
- output remains unchanged under fixed seed;
- profile shows reduced traversal cost.

## Phase 7: Revisit Batch Size And Work Granularity

### Goal

Make sure each worker gets enough work to amortize coordination overhead.

### Work

1. Benchmark multiple `batch_size` values.
2. Compare:
   - `64`
   - `128`
   - `256`
   - `512`
   - `1024`
3. Measure:
   - total runtime
   - merge runtime
   - worker efficiency
4. Document recommended defaults for scaling benchmarks versus RTA-style deadline mode.

### Important Distinction

The best batch size for throughput benchmarking may not be the best batch size for strict-deadline root snapshots.

That is acceptable. We can keep different defaults by use case later if necessary.

### Acceptance

- benchmark documentation includes a recommended scaling-study batch size;
- scaling benchmark is no longer obviously overhead-bound by too-tiny subbatches.

## Phase 8: Extend Profiling So Bottlenecks Are Visible

### Goal

Make future regressions obvious from profile output, not just from total wall-clock time.

### Work

1. Split “traverse” timing into clearer categories where useful:
   - worker startup or dispatch wait
   - strategy snapshot build
   - trajectory execution
   - merge
2. Add counters for:
   - thread team launches or wakeups
   - average active rows per worker
   - average nodes per trajectory
   - strategy snapshot rebuild count
3. In benchmark output, print:
   - total time
   - total traverse time
   - total merge time
   - snapshot/setup time if added
4. Keep profiling overhead lightweight.

### Why

Without this, every future slowdown becomes guesswork.

### Acceptance

- benchmark output shows enough categories to distinguish traversal work from orchestration overhead;
- added profiling does not materially slow benchmark runs.

## Phase 9: Add Regression Tests For Performance-Critical Structural Behavior

### Goal

Protect the new threading architecture from accidental backsliding.

### Work

1. Add structural unit tests where possible, for example:
   - deterministic static partition still matches across worker counts;
   - worker pool lifecycle stays deterministic;
   - batch-local caches rebuild only when expected.
2. Keep tests lightweight and avoid wall-clock assertions.
3. Prefer invariant-based tests, not fragile timing thresholds.

### Important Rule

Do not add tests that assume a certain machine performance.

Test correctness of structure and determinism, not benchmark speed.

### Acceptance

- existing tests still pass;
- new tests cover threading structure without being flaky.

## Detailed Problem Mapping

## Problem A: Thread Launch Overhead

### Current Behavior

`run_player_subbatch()` creates a vector of `std::thread`, launches worker threads, then joins them for each subbatch.

### Why It Hurts

- OS thread startup cost
- synchronization cost
- stack setup and teardown
- repeated scheduler involvement

### Target State

Persistent worker threads with reusable work submission.

## Problem B: Repeated Serial Strategy Setup

### Current Behavior

`compute_current_strategy_rows()` runs before each subbatch.

### Why It Hurts

- repeats setup work
- is single-threaded
- reduces the fraction of time spent on useful parallel trajectory execution

### Target State

One strategy snapshot per meaningful update wave.

## Problem C: Heap Allocation In Traversal

### Current Behavior

Traversal allocates multiple temporary vectors inside node evaluation.

### Why It Hurts

- allocator cost
- thread contention
- cache disruption

### Target State

Scratch-only steady-state traversal.

## Problem D: Container-Heavy Worker Deltas

### Current Behavior

Worker rows are activated through hash maps and payload vectors.

### Why It Hurts

- repeated row setup
- poor locality
- extra allocator pressure

### Target State

Preallocated indexed row storage plus dirty-row tracking.

## Problem E: Benchmark Underloads The Runtime

### Current Behavior

The benchmark graph is intentionally easy and small.

### Why It Hurts

- too little useful work per worker
- speedup is masked by fixed overhead

### Target State

Two presets:

- tiny deterministic preset
- heavier scaling preset

## Order Of Execution

Recommended implementation order:

1. Add heavier scaling benchmark preset and richer benchmark reporting.
2. Replace per-subbatch thread creation with persistent workers.
3. Move strategy snapshot build out of the inner subbatch loop.
4. Remove heap allocation from traversal.
5. Preallocate worker delta storage more aggressively.
6. Add batch-local average-policy cache.
7. Tune batch size and benchmark presets.
8. Expand profiling.
9. Add structural regression tests.

This order minimizes risk because:

- profiling and benchmark clarity improve first;
- the largest overhead source is removed early;
- hot-loop cleanup happens after threading structure is stabilized;
- tests remain a guardrail throughout.

## Expected Outcome

After these phases:

- 1-worker performance should improve modestly from less allocation and less repeated setup;
- multiworker performance should improve much more materially;
- `2`, `4`, `8`, and `16` workers should stop regressing so badly on the scaling preset;
- merge cost should remain visible but no longer dominate;
- deterministic seeded behavior should remain intact;
- existing MCCFR correctness tests should still pass.

This does not guarantee perfect linear scaling.

What it should produce is a solver where:

- thread count is no longer an obvious liability;
- sampled trajectory execution dominates runtime;
- the implementation is much closer to the intended architecture from the large-tree MCCFR plan.

## Verification Checklist

When implementing the remediation work, verify after each phase:

1. No exact-solver tests regress.
2. Existing MCCFR deterministic tests still pass.
3. Existing sparse-storage tests still pass.
4. Existing AS, DCFR, and variance-reduction tests still pass.
5. Benchmark expected value remains stable across worker counts.
6. Nodes visited remain consistent when the benchmark config is unchanged.
7. Scaling benchmark no longer shows severe negative scaling on the heavier preset.

## Final Note

The current result is useful because it exposed the exact kind of gap that often exists between:

- a correct prototype;
- and a production-quality parallel solver.

That is normal.

The good news is that the measured regression points to clear engineering work, not to a dead-end algorithm choice.
