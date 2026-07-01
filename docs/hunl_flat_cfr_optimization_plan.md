# HUNL Flat CFR Optimization Refactor Plan

This document audits the current multithreaded HUNL flat DCFR solver and lays out a staged refactor plan. The goal is to reduce per-iteration latency while keeping deterministic, worker-count-independent results and preserving the existing test suite.

## 1. Inputs Reviewed

Primary repo files:

- `src/solver/hunl_flat_dcfr.cpp`
- `include/solver/hunl_flat_dcfr.hpp`
- `src/solver/hunl_flat_state.cpp`
- `include/solver/hunl_flat_state.hpp`
- `src/games/hunl_flat_graph.cpp`
- `include/games/hunl_flat_graph.hpp`
- `src/util/simd.cpp`
- `include/util/simd.hpp`
- `src/util/profiling.cpp`
- `examples/benchmarks/flat_scheduler_main.cpp`
- `examples/benchmarks/hunl_random_flat_main.cpp`
- `tests/test_hunl_flat_dcfr.cpp`
- `tests/test_hunl_flat_state.cpp`
- `tests/test_ranges_threading.cpp`

External papers:

- Real-Time Parallel Counterfactual Regret Minimization, arXiv:2605.19928v1, https://arxiv.org/html/2605.19928v1
- Parallelizing Counterfactual Regret Minimization, arXiv:2605.14277v1, https://arxiv.org/html/2605.14277v1

The first paper is the better direct fit for this codebase. Its useful constraints are:

- keep each CFR iteration split into stage-local kernels
- parallelize by infoset when the data dependency is infoset-local
- parallelize by node when the dependency is tree-local
- separate backward CFV computation from regret update
- keep the parallel implementation numerically equivalent to a serial implementation

The second paper is useful mainly as a caution: linear algebra/GPU-style formulations can diverge in exact iterates because regret matching is numerically sensitive. For this repo, that means every optimization should have deterministic CPU acceptance tests before bigger representation changes.

## 2. Profile Interpretation

Given profile excerpt:

```text
hunl_flat.worker_loop                 343743.650          16  21483978.119
hunl_flat.run_iterations                  42.399           1     42398.800
hunl_flat.run_iteration                   42.398           1     42397.600
hunl_flat.worker_pool_run_stage           42.326          17      2489.794
hunl_flat.backward_stage                  31.256           1     31255.900
hunl_flat.worker_stage[w2]                25.087          17      1475.694
hunl_flat.worker_stage[w0]                24.582          17      1445.982
hunl_flat.backward[w2]                    15.590           3      5196.800
hunl_flat.backward.decision[w2]           15.564           1     15564.000
hunl_flat.backward[w0]                    15.219           3      5073.100
hunl_flat.backward.decision[w0]           15.199           1     15199.200
hunl_flat.worker_stage[w3]                13.563          17       797.829
hunl_flat.backward[w3]                    11.475           3      3825.100
hunl_flat.backward.decision[w3]           11.454           1     11454.400
```

Important interpretation:

- `hunl_flat.worker_loop` is a lifetime scope around each worker thread in `WorkerPool::worker_loop()`. It mostly measures thread lifetime and waiting, not CPU hot work.
- `hunl_flat.worker_pool_run_stage` is inclusive waiting time for all worker stages. It is useful for wall-clock stage accounting, not as proof that condition variables alone are the bottleneck.
- The strong actionable signal is `hunl_flat.backward_stage`, specifically `hunl_flat.backward.decision`.
- Worker imbalance is real: worker 0/2 spend roughly 15 seconds in backward decisions while worker 3 spends about 11.5 seconds. The current depth partitioning divides by node count, not by node cost.

## 3. Current Pipeline Audit

### Solver stages

`HUNLFlatDCFR::run_iteration()` in `src/solver/hunl_flat_dcfr.cpp` currently runs:

1. `apply_dcfr_discount_stage()`
2. `compute_strategy_stage()`
3. `forward_reach_stage()`
4. `terminal_utility_stage()`
5. `backward_value_stage()`
6. `regret_update_stage()`
7. `average_strategy_stage()`

This matches the rough stage shape from the Parallel CFR paper, but the current implementation still leaves a lot of per-node and per-depth overhead inside each stage.

### Hot backward path

`backward_value_stage()` currently does the following for every depth:

- launches a full worker-pool stage per depth
- for chance nodes, allocates `std::vector<double> weights` and `std::vector<double> row`
- for decision nodes, allocates `std::vector<double> row` and `std::vector<double> weights`
- creates profiling scope strings inside per-node loops
- for each action and bucket, calls `normalized_bucket_mass()`
- `normalized_bucket_mass()` rescans the entire bucket range to compute the total

This makes decision backward effectively:

```text
per decision node: O(actions * buckets * buckets)
```

The intended calculation is only:

```text
per decision node: O(actions * buckets)
```

The extra factor comes from recomputing the same bucket total repeatedly.

### Reach path

`forward_reach_stage()` currently does:

- full global `std::fill` of reach vectors once per iteration
- for every depth:
  - one worker stage to seed per-worker scratch arrays
  - full scratch-array clears for every worker
  - one worker stage to reduce all nodes
  - one worker stage to reduce all buckets

This is simple and deterministic, but it is expensive:

```text
O(depths * workers * (nodes + buckets))
```

even when a depth slice touches only a small subset of nodes.

Potential correctness issue to verify before refactoring: inside a single worker and depth, `scratch.bucket_reach[bucket_range]` is used both as an accumulator and as the current node's local action mass. If multiple nodes for the same infoset are processed by the same worker in the same depth, later node action propagation can include earlier node mass. The refactor should separate local per-node bucket mass from global per-infoset bucket reach accumulation.

### Scheduler path

`HUNLFlatParallelPlan::build()` partitions:

- infosets evenly by count
- nodes evenly by count
- each depth slice evenly by node count

This ignores work variance:

- terminal nodes are cheap
- chance nodes cost `chance_count`
- decision nodes cost roughly `action_count * bucket_count`
- bucketed infosets vary in bucket count

The profile imbalance is expected under this scheduler.

### Profiling overhead

`profiling::mark()` returns early when profiling is disabled, but many hot loops build `std::string` scope names before calling it. In particular, backward decision, strategy lookup, row reduction, chance handling, and per-worker stage names allocate strings repeatedly.

When profiling is enabled, `mark()` also takes a global mutex. Fine-grained per-node marks can distort the profile and materially slow the hot path.

## 4. Refactor Principles

1. Preserve exact deterministic behavior first.
   Existing tests use tight tolerances such as `1e-12`, especially for worker-count comparisons.

2. Make every phase benchmarkable.
   Each phase should have a clear before/after command and a small set of tests.

3. Keep stage boundaries visible.
   The papers argue for stage-local kernels. Do not collapse everything into one traversal unless the stage interfaces remain testable.

4. Prefer data precomputation over repeated pointer chasing.
   This code already has flat metadata. Continue moving repeated computations into flat arrays.

5. Avoid allocations in solver iterations.
   Solver construction may allocate; `run_iteration()` should not allocate on the hot path.

6. Keep reductions ordered.
   For deterministic tests, reductions over worker scratch should use a fixed worker order.

## 5. Phase 0: Baseline And Test Harness

Purpose: make sure each optimization has a stable benchmark and a correctness gate.

Files to edit:

- `examples/benchmarks/flat_scheduler_main.cpp`
- `examples/benchmarks/hunl_random_flat_main.cpp`
- optionally `src/util/profiling.cpp`

Steps:

1. Add profile output for graph shape:
   - `nodes`
   - `infosets`
   - `depth_slices`
   - `max_depth`
   - `max_actions`
   - total bucket count
   - max bucket count
   - per-node-type counts

2. Add backward cost counters:
   - decision nodes visited
   - chance nodes visited
   - total decision action-bucket operations
   - total chance outcomes
   - slowest/fastest worker ratio per stage

3. Add a benchmark mode that runs:
   - 1 warmup iteration
   - N measured iterations
   - same graph and solver config for worker counts 1, 2, 4, 8, 16

4. Acceptance tests:
   - no solver behavior changes
   - current test suite stays green
   - benchmark still prints old stage timings

Suggested commands:

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build/Release/texas_solver_flat_scheduler_benchmark.exe 10 1,2,4,8,16 hand-action
```

On non-Windows generators, adapt executable paths accordingly.

## 6. + Phase 1: Fix Profiling Overhead And Hot-Loop Allocations

Purpose: remove overhead that does not change solver math.

Files to edit:

- `src/solver/hunl_flat_dcfr.cpp`
- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `src/util/profiling.cpp`
- `include/util/profiling.hpp`

### + 1.1 Gate detailed profiling

Why:

Per-node profiling strings and global mutex marks are too expensive inside `backward_value_stage()`.

How:

1. Add a `profiling::detail_enabled()` flag controlled by `TEXASSOLVER_PROFILE_DETAIL`.
2. Keep stage-level timers always available when `TEXASSOLVER_PROFILE=1`.
3. Move per-node marks such as:
   - `hunl_flat.backward.decision`
   - `hunl_flat.backward.strategy_lookup`
   - `hunl_flat.backward.row_reduction`
   - `hunl_flat.backward.chance`
   behind `detail_enabled()`.
4. Avoid constructing `std::string` names unless the relevant profiling flag is enabled.
5. Rename or remove `TEXASSOLVER_PROFILE_SCOPE("hunl_flat.worker_loop")`.
   If kept, rename it to `hunl_flat.worker_loop.lifetime` so it is not mistaken for CPU work.

Tests:

- `ctest --test-dir build -C Release --output-on-failure`
- profile run with `TEXASSOLVER_PROFILE=1`
- profile run with `TEXASSOLVER_PROFILE=1 TEXASSOLVER_PROFILE_DETAIL=1`

### + 1.2 Add per-worker row scratch

Why:

`backward_value_stage()` allocates `std::vector<double>` for every chance and decision node. This is high overhead and fragments allocator behavior.

How:

1. Extend `HUNLFlatWorkerScratch` in `include/solver/hunl_flat_state.hpp`:

```cpp
HUNLAlignedVector<double> row_values;
HUNLAlignedVector<double> row_weights;
HUNLAlignedVector<double> local_bucket_mass;
```

2. Add `ensure_capacity(..., max_child_count, max_bucket_count)` overload.
3. In `HUNLFlatDCFR` construction, scan `graph_.node_meta` once:
   - `max_child_count = max(meta.child_count, meta.chance_count)`
   - `max_bucket_count = max(infoset_meta.bucket_count)`
4. Replace local `std::vector<double> row` and `weights` with scratch spans:
   - `auto* row = scratch.row_values.data();`
   - `auto* weights = scratch.row_weights.data();`
5. Do not clear the whole scratch row buffers. Only overwrite `[0, child_count)`.

Tests:

- `tests/test_hunl_flat_state.cpp`
- `tests/test_hunl_flat_dcfr.cpp`
- `tests/test_ranges_threading.cpp`

Acceptance:

- no allocation inside `backward_value_stage()` in a sampled allocation profile
- identical strategies across worker counts

## 7. + Phase 2: Precompute Normalized Bucket Reach Once Per Iteration

Purpose: remove the largest visible algorithmic overhead in backward, regret, and average strategy stages.

Files to edit:

- `include/solver/hunl_flat_dcfr.hpp`
- `src/solver/hunl_flat_dcfr.cpp`
- possibly `include/solver/hunl_flat_state.hpp`

Current issue:

`normalized_bucket_mass()` recomputes the same bucket total every call. In backward decisions it is called inside:

```text
action loop * bucket loop
```

and each call scans all buckets. Regret and average stages repeat the same work.

How:

1. Add solver arrays:

```cpp
HUNLAlignedVector<double> normalized_bucket_reach_;
HUNLAlignedVector<double> infoset_bucket_totals_;
```

2. Initialize:

```cpp
normalized_bucket_reach_(infoset_table_.total_bucket_count(), 0.0)
infoset_bucket_totals_(graph_.infosets.size(), 0.0)
```

3. Add method:

```cpp
void normalize_bucket_reach_stage();
```

4. Call it after `forward_reach_stage()` and before `terminal_utility_stage()`.

5. Implementation:

```text
parallel by infoset:
  range = infoset bucket range
  total = sum(bucket_reach_[range])
  infoset_bucket_totals_[infoset] = total
  if total > 0:
      normalized_bucket_reach_[bucket] = bucket_reach_[bucket] / total
  else:
      normalized_bucket_reach_[bucket] = prior_bucket_weight(...)
```

6. Replace `normalized_bucket_mass(...)` calls in:
   - `backward_value_stage()`
   - `regret_update_stage()`
   - `average_strategy_stage()`
   with direct reads from `normalized_bucket_reach_`.

7. Keep `normalized_bucket_mass()` temporarily only for tests or delete it after all callers move.

Why this is safe:

The value only depends on the completed reach stage and static priors. It does not change during backward, regret, or average strategy stages.

Expected speedup:

- Backward decision cost drops from `O(actions * buckets * buckets)` to `O(actions * buckets)`.
- Regret and average stages drop from `O(buckets * buckets)` to `O(buckets)`.

Tests to add:

- In `tests/test_hunl_flat_dcfr.cpp`, add `hunl_flat_dcfr_precomputes_normalized_bucket_reach`.
- Assert each infoset normalized bucket row sums to 1.
- Assert zero-reach infosets use prior bucket weights.
- Keep worker-count comparison tests at `1e-12`.

## 8. + Phase 3: Correct And Slim Reach Propagation

Purpose: remove avoidable per-depth clears/reductions and separate local node mass from global bucket reach.

Files to edit:

- `src/solver/hunl_flat_dcfr.cpp`
- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `tests/test_hunl_flat_dcfr.cpp`
- `tests/test_ranges_threading.cpp`

### + 3.1 Separate local node bucket mass

Why:

`scratch.bucket_reach` is currently used both to accumulate global per-infoset bucket reach and to compute current node child propagation. These are different quantities.

How:

1. In decision-node reach propagation, compute local bucket mass into `scratch.local_bucket_mass[0:bucket_count)`.
2. Add local mass to `scratch.bucket_reach[bucket_range]` for later global reduction.
3. Use only `local_bucket_mass` when computing `bucketed_action_mass` for each child.

Pseudocode:

```text
for bucket:
  local_bucket_mass[bucket] = acting_reach * prior_bucket_weight(...)
  scratch.bucket_reach[bucket_range.begin + bucket] += local_bucket_mass[bucket]

for action:
  bucketed_action_mass = dot(local_bucket_mass, strategy[action])
  propagate to child
```

Tests:

- Add a graph fixture where two nodes share the same infoset in one depth slice if available.
- If such a graph is hard to build, add a targeted synthetic flat graph test.
- Compare single-worker and multi-worker reaches and strategy sums.

### + 3.2 Reduce only touched depth nodes

Why:

Each depth currently reduces all graph nodes, even though only children in the next depth can receive new reach.

How:

1. Build `depth_reduce_ranges` in `HUNLFlatParallelPlan`.
2. For depth `d`, reduce only the next depth slice `d + 1`, or the known child target ranges for nodes in depth `d`.
3. Keep root initialization outside the loop.

Low-risk version:

- Reduce `graph_.depth_slices[depth + 1]` instead of `parallel_plan_.workers[worker].node_range`.
- For the final depth, skip node reduction.

Expected effect:

- Removes `O(depths * nodes)` reduction work.

### + 3.3 Avoid full scratch clears per depth

Why:

Clearing all per-worker node and bucket scratch arrays per depth is expensive.

How:

1. Introduce dirty lists in `HUNLFlatWorkerScratch`:

```cpp
std::vector<std::uint32_t> dirty_nodes;
std::vector<std::uint32_t> dirty_buckets;
```

2. When writing a scratch node or bucket slot from zero, push its index into the dirty list.
3. After reduction, clear only dirty slots.
4. Keep deterministic reduction order by reducing workers in index order.

Alternative first implementation:

- Clear only the next depth node slice and all bucket slots in the worker's bucket range.
- This is less optimal than dirty lists but simpler.

Tests:

- `ranges_multiworker_run_does_not_corrupt_range_tables`
- `ranges_repeated_runs_with_same_config_are_deterministic`
- add stress with worker counts 1, 2, 3, 4 on the same graph.

## 9. + Phase 4: Weighted Per-Depth Scheduling

Purpose: reduce the observed backward worker imbalance.

Files to edit:

- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `tests/test_hunl_flat_state.cpp`
- `examples/benchmarks/flat_scheduler_main.cpp`

Current scheduler:

```text
split each depth slice by node count
```

Better scheduler:

```text
split each depth slice by estimated work
```

Node cost model:

```text
TerminalFold:       1
TerminalShowdown:   1, or bucket matrix cost if terminal eval is included
DepthLimited:       1
Chance:             max(1, chance_count)
Decision:           max(1, action_count * bucket_count)
```

How:

1. Add:

```cpp
std::uint32_t estimated_backward_cost(const HUNLFlatNodeMeta&, const HUNLFlatInfosetTable&);
```

2. In `HUNLFlatParallelPlan::build(graph, infoset_table, workers)`, build `depth_node_ranges` by cumulative cost rather than equal node count.
3. Preserve contiguous ranges within each depth slice for cache locality and deterministic writes.
4. Add diagnostics:
   - expected cost per worker per depth
   - actual time per worker per stage

Tests:

- Update `hunl_flat_parallel_plan_assigns_disjoint_infoset_and_depth_ranges` to require:
  - complete coverage
  - no overlap
  - ordered ranges
  - not necessarily equal node counts
- Add a cost-skew fixture where one worker would otherwise receive most decision nodes.

Expected effect:

- Backward worker times should converge.
- In the supplied profile, the target is to move worker 0/2 closer to worker 3 instead of leaving a 30 percent gap.

## 10. + Phase 5: Backward Decision Kernel Specialization

Purpose: make the hottest loop cache-friendly and layout-aware.

Files to edit:

- `src/solver/hunl_flat_dcfr.cpp`
- `include/util/simd.hpp`
- `src/util/simd.cpp`
- `tests/test_simd.cpp`
- `tests/test_hunl_flat_dcfr.cpp`

### + 5.1 Add dot-product helpers

Why:

Backward decision computes action probabilities as:

```text
weight[action] = sum_bucket normalized_bucket_reach[bucket] * strategy[action, bucket]
```

This should be a tight vector kernel.

How:

1. Add scalar/SSE2/AVX2 helper:

```cpp
double dot_product(const double* lhs, const double* rhs, std::size_t len) noexcept;
```

2. Use it for `InfosetActionHand` where each action row is contiguous:

```cpp
strategy + action * bucket_count
normalized_bucket_reach + bucket_begin
```

3. For `InfosetHandAction`, either keep the strided loop or add:

```cpp
double dot_product_strided(
    const double* lhs,
    const double* rhs,
    std::size_t len,
    std::size_t rhs_stride) noexcept;
```

4. Prefer `InfosetActionHand` for large bucketed HUNL runs if backward remains dominant.

Tests:

- Exact scalar expectations in `tests/test_simd.cpp`.
- Existing strategy normalization tests for both layouts.

### + 5.2 Compute node value in one pass

Current backward decision:

1. fill `row`
2. fill `weights`
3. copy `row` to `action_values_`
4. reduce `row * weights`

Better:

```text
node_value = 0
for action:
  child_value = node_values_[child]
  action_values_[edge] = child_value
  action_prob = dot(...)
  node_value += child_value * action_prob
node_values_[node] = node_value
```

This removes row scratch for decision nodes entirely. Keep row scratch for chance nodes only if needed.

Tests:

- `hunl_flat_dcfr_backward_stage_writes_action_values_from_children`
- `hunl_flat_dcfr_backward_stage_computes_root_value_from_children`
- `hunl_flat_dcfr_regret_update_uses_action_minus_node_value`

## 11. + Phase 6: Regret And Average Strategy Kernel Cleanup

Purpose: use precomputed normalized bucket reach and reduce duplicated layout branches.

Files to edit:

- `src/solver/hunl_flat_dcfr.cpp`
- `include/util/simd.hpp`
- `src/util/simd.cpp`

How:

1. In `regret_update_stage()`, cache once per infoset:
   - `bucket_norm = normalized_bucket_reach_.data() + bucket_range.begin`
   - `edge_values = action_values_.data() + node_meta.child_begin`
   - `base_value = node_values_[node_idx]`
   - `cf_reach`
2. For `InfosetHandAction`, keep the bucket-contiguous `update_regret_sum()` call.
3. For `InfosetActionHand`, add a helper that updates one bucket across strided action rows, or consider storing a second action-contiguous temporary for action-major runs.
4. In `average_strategy_stage()`, mirror the same structure.
5. Remove repeated `infoset_table_.infoset_bucket_range(meta.id)` calls from inner loops.

Tests:

- average strategy update test
- worker count matching tests
- full `test_hunl_flat_dcfr`

## 12. + Phase 7: Worker Pool Stage Dispatch Refinement

Purpose: reduce stage launch overhead only after the hot kernels are fixed.

Files to edit:

- `include/solver/hunl_flat_dcfr.hpp`
- `src/solver/hunl_flat_dcfr.cpp`

Current:

- each stage is a `std::function<void(std::size_t)>`
- each depth in reach/backward invokes worker-pool synchronization

Possible improvements:

1. Replace `std::function` with a lightweight enum dispatch:
   - WorkerPool stores `StageCommand`
   - solver stores current stage context
   - worker calls a non-allocating member function
2. Fuse per-depth backward execution into one worker command:
   - each worker loops depths in reverse internally
   - add a barrier between depths inside the worker pool
3. Fuse reach seed/reduce where possible:
   - process depth
   - barrier
   - reduce next depth
   - barrier

Do this after Phases 1-5 because otherwise dispatch changes make debugging harder.

Tests:

- worker-count deterministic tests
- repeated run deterministic tests
- long benchmark with `TEXASSOLVER_PROFILE=1`

## 13. Phase 8: Paper-Aligned Longer-Term Pipeline

Purpose: move closer to the seven-stage Parallel CFR design when the current scalar-node implementation is no longer enough.

Files likely to edit or add:

- `include/solver/hunl_flat_pipeline.hpp`
- `src/solver/hunl_flat_pipeline.cpp`
- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `src/solver/hunl_flat_dcfr.cpp`

Target shape:

1. Forward profile by infoset chain.
2. Aggregate reach probability by node/card or node/bucket.
3. Opponent reach by node and then infoset.
4. Showdown equity by terminal node.
5. Optional batched leaf evaluator for depth-limited nodes.
6. Backward CFV by node/infoset.
7. Regret update by infoset.

This is larger than a hot-path refactor. Treat it as a second milestone after the current flat solver stops wasting time in avoidable allocations, redundant normalizations, and count-based scheduling.

## 14. Test Matrix For Every Phase

Run these after each phase:

```bash
ctest --test-dir build -C Release --output-on-failure
```

Minimum targeted tests:

```bash
ctest --test-dir build -C Release -R "hunl_flat|ranges_threading|simd" --output-on-failure
```

Benchmark before and after:

```bash
build/Release/texas_solver_flat_scheduler_benchmark.exe 5 1,2,4,8 hand-action
build/Release/texas_solver_flat_scheduler_benchmark.exe 5 1,2,4,8 action-hand
build/Release/texas_solver_hunl_random_flat.exe --debug --workers 4 --iterations 1
```

Acceptance criteria:

- single-worker and multi-worker outputs match within existing tolerances
- repeated multi-worker runs are deterministic
- no `NaN` or negative reach table entries
- `backward_seconds` decreases after Phase 2
- worker imbalance decreases after Phase 4
- profiling output no longer suggests `worker_loop` is a CPU hotspot

## 15. Recommended Implementation Order

1. Phase 1.1 profiling hygiene.
   This makes future profiles trustworthy and removes hot-loop string allocations.

2. Phase 1.2 row scratch.
   This removes per-node allocator overhead with minimal algorithmic risk.

3. Phase 2 normalized bucket reach.
   This is likely the largest immediate backward-stage win.

4. Phase 3.1 local reach mass.
   Do this before changing reach scheduling so correctness is explicit.

5. Phase 4 weighted scheduling.
   The supplied profile shows enough imbalance to justify it.

6. Phase 5 backward decision kernel.
   This tightens the now-correct and now-balanced hot loop.

7. Phase 3.2/3.3 and Phase 7.
   These are deeper scheduler changes. Do them after the algorithmic waste is gone.

8. Phase 8 paper-aligned pipeline.
   Treat this as a new milestone, not part of the first optimization pass.

## 16. Risks And Guardrails

Risk: floating-point order changes break `1e-12` tests.

Guardrail:

- keep reductions in fixed worker order
- avoid atomics for floating-point accumulation
- prefer static ranges over dynamic stealing until tests can tolerate small numeric drift

Risk: optimizing for one layout regresses the other.

Guardrail:

- keep both `InfosetHandAction` and `InfosetActionHand` tests
- benchmark both layouts
- document which layout is recommended for large HUNL runs

Risk: profile instrumentation changes hide real issues.

Guardrail:

- keep stage-level profiling always available
- make detailed profiling opt-in
- add benchmark counters independent of profiler mutex timing

Risk: reach refactor changes solver semantics.

Guardrail:

- add a synthetic shared-infoset reach test before changing reductions
- compare all reach arrays, bucket reach, node values, action values, regrets, and average strategy after one iteration

## 17. Expected First Milestone Outcome

After Phases 1, 2, 3.1, and 4:

- `backward.decision` should stop doing repeated bucket-total scans
- backward should allocate no vectors per node
- per-worker backward times should be better balanced
- profile output should distinguish worker lifetime from worker CPU work
- existing deterministic tests should still pass

That is the best first milestone because it directly targets the profile without forcing a full solver architecture rewrite.
