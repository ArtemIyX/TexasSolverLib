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

```text
batch       workers   total_ms      iter_ms       iters/s       speedup     eff         merge_ms        traverse_ms     ev_p0           
64          1         418.349       2.092         478.07        1.00        1.00        5.527           396.770         14.4810         
64          2         309.754       1.549         645.67        1.35        0.68        13.455          274.281         14.4810         
64          4         304.120       1.521         657.63        1.38        0.34        26.883          250.555         14.4810         
64          8         353.492       1.767         565.78        1.18        0.15        53.519          266.615         14.4810         
64          16        664.866       3.324         300.81        0.63        0.04        116.733         499.130         14.4810         
128         1         409.969       2.050         487.84        1.00        1.00        2.809           398.707         14.4810         
128         2         270.724       1.354         738.76        1.51        0.76        7.234           251.027         14.4810         
128         4         220.464       1.102         907.18        1.86        0.46        14.902          189.266         14.4810         
128         8         274.656       1.373         728.18        1.49        0.19        31.104          220.788         14.4810         
128         16        333.913       1.670         598.96        1.23        0.08        60.510          247.497         14.4810         
256         1         420.967       2.105         475.10        1.00        1.00        2.013           412.527         14.4810         
256         2         390.176       1.951         512.59        1.08        0.54        5.371           374.865         14.4810         
256         4         242.708       1.214         824.04        1.73        0.43        9.399           222.416         14.4810         
256         8         185.921       0.930         1075.73       2.26        0.28        15.672          158.789         14.4810         
256         16        208.942       1.045         957.20        2.01        0.13        32.321          161.830         14.4810         
512         1         418.056       2.090         478.40        1.00        1.00        1.225           412.595         14.4810         
512         2         361.345       1.807         553.49        1.16        0.58        2.943           352.314         14.4810         
512         4         213.885       1.069         935.08        1.95        0.49        4.872           202.314         14.4810         
512         8         138.107       0.691         1448.16       3.03        0.38        7.954           123.516         14.4810         
512         16        132.133       0.661         1513.63       3.16        0.20        16.394          107.584         14.4810         
1024        1         415.413       2.077         481.45        1.00        1.00        0.842           411.320         14.4810         
1024        2         348.019       1.740         574.68        1.19        0.60        1.512           342.973         14.4810         
1024        4         195.548       0.978         1022.77       2.12        0.53        2.610           189.054         14.4810         
1024        8         115.613       0.578         1729.90       3.59        0.45        4.042           107.749         14.4810         
1024        16        90.578        0.453         2208.04       4.59        0.29        8.274           77.470          14.4810  
```

```text
batch       workers   total_ms      iter_ms       iters/s       speedup     eff         setup_ms      dispatch_ms   traj_ms       merge_ms        traverse_ms     ev_p0           avg_rows      avg_nodes     
64          1         421.981       2.110         473.95        1.00        1.00        0.271         6.638         391.437       5.508           398.849         14.4810         2.50          7.50          
64          2         334.919       1.675         597.16        1.26        0.63        0.754         6.115         401.599       14.273          292.326         14.4810         2.50          7.50          
64          4         322.305       1.612         620.53        1.31        0.33        0.714         3.991         416.071       26.432          264.279         14.4810         2.50          7.50          
64          8         420.706       2.104         475.39        1.00        0.13        0.856         10.753        489.408       58.205          322.467         14.4810         2.50          7.50          
64          16        681.955       3.410         293.27        0.62        0.04        0.946         22.853        701.950       118.452         511.915         14.4810         2.50          7.50          
128         1         410.973       2.055         486.65        1.00        1.00        0.279         3.393         395.071       2.809           398.867         14.4810         2.50          7.50          
128         2         295.630       1.478         676.52        1.39        0.70        0.705         0.920         403.361       7.623           271.602         14.4810         2.50          7.50          
128         4         273.192       1.366         732.08        1.50        0.38        0.879         0.190         562.740       16.762          235.225         14.4810         2.50          7.50          
128         8         274.499       1.372         728.60        1.50        0.19        0.863         0.230         694.558       27.743          223.549         14.4810         2.50          7.50          
128         16        374.229       1.871         534.43        1.10        0.07        1.037         0.254         727.415       66.370          275.449         14.4810         2.50          7.50          
256         1         422.540       2.113         473.33        1.00        1.00        0.512         2.178         411.644       1.800           414.034         14.4810         2.50          7.50          
256         2         387.687       1.938         515.88        1.09        0.54        0.699         0.016         667.699       5.023           371.465         14.4810         2.50          7.50          
256         4         240.254       1.201         832.45        1.76        0.44        0.751         0.003         672.560       8.994           220.083         14.4810         2.50          7.50          
256         8         182.734       0.914         1094.49       2.31        0.29        0.707         0.012         689.143       14.426          156.882         14.4810         2.50          7.50          
256         16        211.350       1.057         946.30        2.00        0.12        0.816         0.050         514.834       33.377          163.100         14.4810         2.50          7.50          
512         1         397.423       1.987         503.24        1.00        1.00        0.282         0.864         392.959       0.726           393.924         14.4810         2.50          7.50          
512         2         227.559       1.138         878.89        1.75        0.87        0.673         0.000         397.096       2.150           219.783         14.4810         2.50          7.50          
512         4         137.010       0.685         1459.75       2.90        0.73        0.689         0.000         399.900       3.763           127.139         14.4810         2.50          7.50          
512         8         97.568        0.488         2049.86       4.07        0.51        0.551         0.021         409.335       6.580           85.832          14.4810         2.50          7.50          
512         16        125.004       0.625         1599.95       3.18        0.20        0.740         0.000         522.052       17.159          100.021         14.4810         2.50          7.50          
1024        1         445.751       2.229         448.68        1.00        1.00        0.797         1.027         439.658       0.854           440.756         14.4810         2.50          7.50          
1024        2         344.560       1.723         580.45        1.29        0.65        0.823         0.000         646.956       1.512           338.563         14.4810         2.50          7.50          
1024        4         190.857       0.954         1047.91       2.34        0.58        0.665         0.000         663.289       2.340           184.593         14.4810         2.50          7.50          
1024        8         114.424       0.572         1747.89       3.90        0.49        0.730         0.000         685.039       3.927           106.174         14.4810         2.50          7.50          
1024        16        88.515        0.443         2259.51       5.04        0.31        0.658         0.000         688.154       7.768           76.173          14.4810         2.50          7.50       
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

## + Phase 1: Add Better Benchmark Modes Before Optimizing

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

## + Phase 2: Replace Per-Subbatch Thread Creation With Persistent Workers

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

## + Phase 3: Move Strategy Snapshot Computation Out Of The Inner Subbatch Loop

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

## + Phase 4: Remove Heap Allocation From Traversal Hot Paths

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

## + Phase 5: Make Worker Delta Storage Truly Preallocated

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

## + Phase 6: Cache Average Action Probabilities Per Batch Wave

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

## + Phase 7: Revisit Batch Size And Work Granularity

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

## + Phase 8: Extend Profiling So Bottlenecks Are Visible

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
