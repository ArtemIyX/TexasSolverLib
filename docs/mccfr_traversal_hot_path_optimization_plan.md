# MCCFR Per-Trajectory Traversal Hot-Path Audit And Optimization Plan

## Executive Summary

The current `HUNLFlatMCCFR` implementation has already removed the earlier orchestration bottlenecks well enough that the new primary bottleneck is now per-trajectory traversal work.

The latest benchmark shape shows:

- `setup_ms` is negligible;
- `dispatch_ms` is negligible or near-zero for useful batch sizes;
- `traj_ms` dominates total runtime;
- `merge_ms` still grows with worker count, but it is now secondary on the larger, better-amortized batch sizes;
- scaling improves materially as batch size rises, which means the threading model is no longer the main blocker.

That is good news. It means the solver is now bottlenecked by the thing we ultimately want to make fast: sampled trajectory execution.

This document gives:

1. a concrete audit of the files and functions currently shaping traversal cost;
2. a bottleneck map for the hottest branches inside `traverse()`;
3. a phased optimization plan with goals, substeps, risks, and acceptance criteria.

The plan is intended to optimize the current full-graph validation backend first, while keeping deterministic semantics and existing tests intact.

## Scope

This plan covers:

- `src/solver/hunl_flat_mccfr.cpp`
- `include/solver/hunl_flat_mccfr.hpp`
- `include/solver/hunl_sampled_config.hpp`
- `examples/benchmarks/hunl_mccfr_scaling_main.cpp`
- `tests/test_hunl_flat_mccfr.cpp`
- `docs/mccfr_multithreading_performance_audit_plan.md`
- `docs/mccfr_large_tree_implementation_plan.md`

This plan does not cover:

- changing exact `HUNLFlatDCFR`;
- changing poker rules, ranges, or terminal semantics;
- adding nondeterministic reductions or floating-point atomics;
- adding poker client automation or non-solver integrations.

## Files Audited

### `src/solver/hunl_flat_mccfr.cpp`

Main findings:

- `traverse()` is now the key hot function.
- The hottest repeated helpers are:
  - `fill_current_strategy_bucket()`
  - `row_value_index()`
  - `fill_average_strategy_sampling_probabilities()`
  - `sample_chance_child()`
- `compute_current_strategy_rows()` and `rebuild_average_policy_cache()` are no longer the main issue for the current benchmark shape, though they still matter.
- The traversal still performs a lot of repeated indexing, branching, and helper dispatch inside every sampled path.

### `include/solver/hunl_flat_mccfr.hpp`

Main findings:

- `WorkerScratch` already has reusable buffers, which is good.
- The scratch model is strong enough to support deeper hot-path refactors without changing the public API.
- There is room to add precomputed row handles, offsets, and node-local scratch metadata without disturbing external behavior.

### `include/solver/hunl_sampled_config.hpp`

Main findings:

- The current config surface is good enough for the next optimization steps.
- No immediate config expansion is required for the first traversal phases, except possibly optional profiling toggles later.

### `examples/benchmarks/hunl_mccfr_scaling_main.cpp`

Main findings:

- The benchmark already exposes the metrics we need:
  - `setup_ms`
  - `dispatch_ms`
  - `traj_ms`
  - `merge_ms`
  - `traverse_ms`
  - `avg_rows`
  - `avg_nodes`
- This is sufficient to drive a traversal-focused optimization loop.

### `tests/test_hunl_flat_mccfr.cpp`

Main findings:

- The test surface already includes determinism and structural checks for:
  - static partitioning;
  - seeded multiworker reproducibility;
  - player-batch snapshot rebuild structure.
- That gives us guardrails for aggressive hot-path cleanup without relying on timing tests.

## What The Latest Results Mean

From the recent benchmark tables:

- larger `batch_size` improves scaling substantially;
- `16` workers can now outperform `1` worker strongly on large enough batches;
- `traj_ms` is the dominant bucket;
- `merge_ms` increases with worker count, but does not explain most of the remaining single-worker cost;
- average rows and nodes per trajectory are stable, which means the logical workload is stable and the regression/improvement is implementation-driven.

Short interpretation:

```text
old bottleneck: thread orchestration
new bottleneck: per-trajectory node processing
secondary bottleneck: merge growth at high worker counts
```

## Current Traversal Cost Map

## Hot Path 1: Recursive Node Dispatch In `traverse()`

Location:

- `src/solver/hunl_flat_mccfr.cpp:512`

Current behavior:

- every visited node enters `traverse()`;
- every node pays:
  - recursive function call overhead;
  - `switch (meta.type)`;
  - repeated `graph_.node_meta.at(node_idx)` lookup;
  - repeated branch decisions for sampling mode and variance-reduction mode.

Why it matters:

- this cost is paid for every node in every trajectory;
- current average nodes per trajectory are modest in the synthetic benchmark, but on real workloads this multiplies quickly;
- repeated generic branching blocks the compiler from producing a tighter inner loop.

## Hot Path 2: Repeated Strategy Row Reads Per Bucket

Locations:

- `fill_current_strategy_bucket()`
- call sites near:
  - opponent sampled branch
  - AS branch
  - full traversing-player branch

Current behavior:

- the solver repeatedly reloads per-bucket strategy rows from the current strategy or sparse regret state;
- the same infoset row may be touched several times inside one node visit;
- layout branching happens inside the helper.

Why it matters:

- repeated helper calls magnify indexing and branch cost;
- sparse mode also recomputes regret-matching behavior on demand;
- layout-specific logic is re-entered in hot loops.

## Hot Path 3: Repeated `row_value_index()` Arithmetic

Location:

- `src/solver/hunl_flat_mccfr.cpp:327`
- called many times inside per-bucket/per-action loops

Current behavior:

- every update of `strategy_delta`, `regret_delta`, and several export/helper paths recomputes row offsets;
- even in the dense full-graph validation path, offset computation is still repeated in tight loops.

Why it matters:

- this is simple arithmetic, but it is paid at very high frequency;
- it also inhibits cleaner contiguous-pointer loops.

## Hot Path 4: Opponent-Sampling Branch Does Full Per-Bucket Strategy-Sum Update

Location:

- opponent branch inside `traverse()`

Current behavior:

- before sampling one opponent action, the code loops all buckets and all actions to accumulate `strategy_delta`;
- then it samples one action from `average_policy_cache_`;
- then it traverses one child.

Why it matters:

- this branch is hit often in external sampling;
- the sampled action is cheap, but the strategy-sum writeback still touches the full infoset row;
- row writes can dominate when action counts or bucket counts grow.

## Hot Path 5: Average Strategy Sampling Has Extra Probability Work

Locations:

- `fill_average_strategy_sampling_probabilities()`
- AS branch inside `traverse()`

Current behavior:

- builds inclusion probabilities from average strategy sums;
- does Bernoulli draws per action;
- computes none-selected correction probability;
- may re-normalize adjusted inclusion probabilities;
- then traverses sampled actions and performs importance-corrected estimates.

Why it matters:

- AS is heavier than plain external sampling by design;
- some of that cost is algorithmic and unavoidable;
- some of it is still implementation overhead that can be cut.

## Hot Path 6: Chance Sampling Uses Repeated Probability Scans

Location:

- `sample_chance_child()`

Current behavior:

- first loop sums total chance probability;
- second loop samples a child by prefix subtraction;
- exact mode separately loops all outcomes.

Why it matters:

- in synthetic benchmarks chance fanout is small, but on larger trees this still scales with chance count;
- the sampler currently recomputes information that could be partially preprocessed for stable graphs.

## Hot Path 7: Bounds-Checked `.at()` Accesses In The Inner Loop

Locations:

- `graph_.node_meta.at(node_idx)`
- `graph_.chance_outcomes.at(...)`
- `graph_.children.at(...)`
- `infoset_table_.meta_mut().at(...)` in non-traversal paths

Current behavior:

- `.at()` is used in hot traversal code.

Why it matters:

- debug-friendly, but unnecessary in validated release hot loops;
- repeated bounds checking is avoidable once graph integrity is established.

## Hot Path 8: Mixed Dense/Sparse/Layout Branching In Shared Helpers

Current behavior:

- the same helper functions serve:
  - dense validation mode;
  - sparse mode;
  - `InfosetActionHand` layout;
  - `InfosetHandAction` layout.

Why it matters:

- flexibility is valuable;
- but the hottest loop pays for branches that often do not change during a solver run.

## Optimization Principles

All traversal optimization should follow these rules:

- keep deterministic seeded semantics;
- keep merge order fixed;
- keep scalar correctness path available;
- optimize dense validation path first;
- only specialize sparse path after dense path is clean and measured;
- prefer precomputed views and offsets over repeated helper calls;
- remove hot-path `.at()` and dynamic branching only after invariant checks are established elsewhere;
- do not optimize blind: each phase should show profile movement.

## Phased Optimization Plan

## Phase 1: Add A Traversal-Specific Audit Layer

### Goal

Make it obvious where `traj_ms` is going inside the trajectory body before we refactor aggressively.

### Work

1. Split trajectory-side profile counters into clearer categories:
   - chance node time;
   - opponent sampled node time;
   - traversing-player full expansion time;
   - AS-specific time;
   - row writeback time if measurable cheaply.
2. Add lightweight counters for:
   - visited chance nodes;
   - visited decision nodes;
   - opponent sampled decisions;
   - traversing-player fully expanded decisions;
   - AS sampled action count;
   - average actions touched per visited decision node.
3. Keep profiling off the deepest per-action loop unless the overhead is trivial.
4. Document how to read the new counters in benchmark output or this doc.

### Why First

We already know `traj_ms` dominates, but we still want to distinguish:

- recursive dispatch cost;
- strategy-row materialization cost;
- chance/opponent/AS logic cost;
- row update cost.

### Acceptance

- benchmark output can separate at least chance, opponent-sampling, traversing-player, and AS-heavy work;
- profiling overhead is not large enough to materially distort scaling runs;
- no solver semantics change.

## Phase 2: Specialize Traversal By Mode Instead Of Re-Branching Every Node

### Goal

Reduce repeated mode checks inside the hottest recursion path.

### Work

1. Split the generic `traverse()` flow into narrower internal helpers, for example:
   - `traverse_exact()`
   - `traverse_external()`
   - `traverse_average_strategy()`
2. If needed, keep one small front door that dispatches once per trajectory or once per batch.
3. Separate variance-reduction enabled and disabled paths where doing so removes repeated inner-loop conditionals.
4. Keep the public solver API unchanged.

### Substeps

1. Introduce internal mode-specific wrappers without changing behavior.
2. Move the opponent-sampled branch into an external-mode-specific helper.
3. Move AS-only logic into an AS-specific helper.
4. Re-check tiny deterministic tests after each extraction step.

### Why

The current `traverse()` pays repeated checks for:

- sampling mode;
- whether current node belongs to traversing player;
- whether variance reduction is enabled.

Many of those conditions are stable during an entire batch.

### Acceptance

- hot traversal no longer repeatedly switches across all solver modes at every node;
- fixed-seed results remain unchanged;
- `traj_ms` improves or at minimum does not regress on 1-worker runs.

## Phase 3: Replace Repeated Helper-Based Row Access With Precomputed Row Views

### Goal

Stop reconstructing layout decisions and offsets inside every per-bucket loop.

### Work

1. Precompute infoset-row access metadata for traversal:
   - layout kind;
   - bucket stride;
   - action stride;
   - base pointer offsets for dense rows.
2. Create small row-view structs for hot traversal code, for example:
   - current strategy pointer/view;
   - regret pointer/view;
   - strategy-sum pointer/view;
   - delta-row pointer/view.
3. Let traversal compute bucket base pointers once per bucket, not per action.
4. Replace most `row_value_index()` calls in hot loops with pointer arithmetic over contiguous ranges.

### Substeps

1. Add a traversal-only row-view type in `hunl_flat_mccfr.hpp` or local `.cpp` scope.
2. Build the row view once when an infoset node is entered.
3. Rework dense `InfosetActionHand` path first, since it is already the best validation backend for throughput study.
4. Keep sparse fallback path functionally equivalent, even if it still uses slower indexing at first.

### Why

The current code repeatedly does:

```text
infoset -> meta lookup -> row_value_index(meta, bucket, action) -> storage access
```

That is too much repeated work for inner loops.

### Acceptance

- hot traversal loops no longer call `row_value_index()` for every action update in the dense fast path;
- the dense fast path operates mostly on base pointers and increments;
- deterministic tests still pass;
- `traj_ms` improves measurably.

## Phase 4: Inline Or Specialize `fill_current_strategy_bucket()` For Dense Fast Path

### Goal

Reduce repeated strategy-row materialization overhead.

### Work

1. Split `fill_current_strategy_bucket()` into:
   - dense fast path;
   - sparse fallback path.
2. For dense fast path, avoid per-call layout branching by using precomputed row-view metadata.
3. In nodes that revisit the same infoset row multiple times within one visit:
   - compute bucket base once;
   - read strategy values directly.
4. Keep sparse regret-matching fallback correct and isolated.

### Substeps

1. Add a dense helper that reads one bucket by direct pointer.
2. Replace repeated helper entry from:
   - opponent sampled branch;
   - AS branch;
   - full traversing-player branch.
3. Measure whether copying strategy into scratch is still best, or whether direct read-only pointer access is better for dense path.

### Why

`fill_current_strategy_bucket()` is small, but it is called constantly. Its generic shape is still doing more work than necessary for the common benchmark path.

### Acceptance

- dense validation backend no longer pays generic layout branching on every bucket fetch;
- sparse mode remains correct;
- 1-worker and N-worker `traj_ms` improve.

## Phase 5: Reduce Opponent-Sampled Node Writeback Cost

### Goal

Make external-sampling opponent nodes cheaper without changing estimator semantics.

### Work

1. Audit the opponent branch separately from traversing-player nodes.
2. Precompute any row state needed for:
   - per-bucket strategy-sum accumulation;
   - sampled action child selection.
3. Use contiguous pointer loops for `strategy_delta` writes.
4. Consider separating:
   - row update loop;
   - action sampling loop;
   - baseline correction loop.
5. Profile whether the main cost is:
   - reading bucket strategy;
   - writing strategy deltas;
   - sampling from average policy.

### Substeps

1. Add counters for opponent-node visits and opponent-node write volume.
2. Refactor the row update loop to operate on per-bucket base pointers.
3. If helpful, cache a pointer to `average_policy_cache_[infoset_id]` in a node-local variable once.
4. Keep merge order and row activation behavior unchanged.

### Why

External sampling spends a lot of time in opponent nodes. Even though only one action is traversed, the strategy-sum update still touches all buckets and all actions.

### Acceptance

- opponent-node handling has a clearly faster dense fast path;
- output remains unchanged under fixed seed;
- external-sampling benchmark shows lower `traj_ms`.

## Phase 6: Precompute Chance Sampling Tables For Static Graphs

### Goal

Reduce repeated chance-outcome scanning work.

### Work

1. Audit chance nodes in the benchmark graph and typical target graphs.
2. Precompute per-chance-node metadata where useful:
   - total probability mass;
   - prefix probabilities or alias-style structures if justified;
   - direct child span pointers.
3. Keep exact mode logic clear and separate.
4. Avoid expensive generalized sampling structures unless chance fanout is high enough to justify them.

### Substeps

1. Add a simple precomputed `chance_total_probability` field or side table first.
2. If that helps little, stop there.
3. Only consider richer sampling tables if future profiles show chance nodes are still material.

### Why

This is not guaranteed to be the biggest win, but it is a clean targeted cleanup once row-access overhead has been reduced.

### Acceptance

- chance sampling no longer recomputes the same total mass every visit;
- fixed-seed chance selection semantics remain unchanged;
- profile shows no regression and ideally some chance-side improvement.

## Phase 7: Add Node-Local Traversal Metadata Cache

### Goal

Turn repeated node interpretation into one-time preprocessing.

### Work

1. Build a compact traversal metadata array indexed by node id.
2. Include fields such as:
   - node type;
   - player;
   - child begin pointer or child span;
   - action count;
   - infoset id;
   - pre-resolved infoset meta pointer/index;
   - flags for traversing-player/opponent branch behavior if useful.
3. Use this cache only in hot traversal; keep original graph structures as source of truth.

### Substeps

1. Start with a tiny metadata view that mirrors the fields already read repeatedly.
2. Replace repeated `graph_.node_meta.at(node_idx)` and scattered metadata lookups with direct indexed access.
3. Avoid pointer-heavy ownership; use flat arrays and integer ids.

### Why

The graph is static for the life of the solver. Reinterpreting node metadata on every visit is unnecessary.

### Acceptance

- traversal reads compact per-node metadata through flat arrays or direct indexed structs;
- `.at()` calls are removed from hot traversal loops;
- deterministic behavior remains intact.

## Phase 8: Separate Dense Validation Fast Path From Sparse Production Path More Explicitly

### Goal

Let the benchmarked validation backend become truly fast without forcing sparse-mode complexity into the same inner loops.

### Work

1. Identify the exact fast path used by the scaling benchmark:
   - dense table;
   - `InfosetActionHand` layout;
   - external sampling.
2. Give that combination a dedicated internal path.
3. Leave sparse mode and alternate layouts correct, but not in the same deepest loops unless necessary.
4. Document that validation-path optimizations are intentional and do not preclude later sparse optimization.

### Substeps

1. Add internal helper names that make the fast path explicit.
2. Avoid branch ladders for layout and storage mode in the dense path.
3. Once dense path is clean, audit sparse path separately.

### Why

Trying to make one generic loop optimal for all combinations usually leaves all of them slower than necessary.

### Acceptance

- dense benchmark path has a visibly simpler traversal implementation;
- sparse tests still pass;
- benchmark throughput improves.

## Phase 9: Audit Whether Recursive Traversal Should Become An Explicit Frame Stack

### Goal

Check whether recursion itself is now a measurable part of the remaining `traj_ms`.

### Work

1. Measure after Phases 2-8 before deciding.
2. If recursion remains significant:
   - prototype an iterative traversal for the most common mode first;
   - use a fixed-capacity or preallocated per-worker frame stack;
   - keep exact semantics identical.
3. If recursion is not a material cost, do not force this refactor.

### Substeps

1. Add a small experiment branch or guarded prototype.
2. Compare code complexity against measured gain.
3. Abandon it if the gain is minor.

### Why

This is the highest-risk structural optimization. It should happen only if smaller cleanup phases do not capture enough of the bottleneck.

### Acceptance

- only proceed if measured gain justifies complexity;
- if adopted, deterministic outputs remain unchanged;
- code remains maintainable enough for future sparse/lazy production work.

## Phase 10: Optimize Average Strategy Sampling As A Separate Cost Center

### Goal

Keep AS mode from becoming disproportionately expensive once external mode is clean.

### Work

1. Audit the AS-specific path after general traversal cleanup lands.
2. Focus on:
   - `fill_average_strategy_sampling_probabilities()`;
   - none-selected correction;
   - sampled action mask handling;
   - importance-corrected action value writes.
3. Consider precomputing average strategy sum vectors once per snapshot rebuild if profiling justifies it.

### Substeps

1. Add AS-only counters if not already present.
2. Compare AS path against external path on wide-action synthetic graphs.
3. Only optimize further if AS is clearly a real target mode for the product path.

### Why

AS has extra algorithmic work, so it deserves its own tuning pass rather than being mixed into every earlier phase.

### Acceptance

- AS mode remains deterministic;
- AS benchmark cost per trajectory falls relative to current baseline;
- no semantic drift in AS tests.

## Phase 11: Revisit Merge Only After Trajectory Work Is Cleaner

### Goal

Avoid over-optimizing merge before the dominant traversal cost is reduced.

### Work

1. Re-measure after trajectory-focused phases.
2. If merge becomes the new clear bottleneck at high worker counts:
   - shrink dirty-row payload touched per worker;
   - reduce row activation overhead further;
   - consider grouped or chunked merge order while keeping deterministic worker order.
3. Keep merge work visible in profiling.

### Why

The current evidence says merge is secondary. It may become primary only after traversal gets much faster.

### Acceptance

- merge optimization work is driven by post-traversal-cleanup profiles, not guesswork;
- deterministic merge order is preserved.

## Recommended Execution Order

Recommended order from here:

1. Phase 1: traversal-specific profiling split
2. Phase 2: mode specialization
3. Phase 3: precomputed row views and offset removal
4. Phase 4: dense fast path for current-strategy bucket reads
5. Phase 5: opponent-node writeback cleanup
6. Phase 7: compact node-local traversal metadata
7. Phase 6: chance-node preprocessing
8. Phase 8: explicit dense vs sparse path separation
9. Phase 10: AS-specific tuning
10. Phase 9: iterative stack only if still justified
11. Phase 11: merge revisit after trajectory cleanup

Reason for this order:

- it attacks the highest-frequency hot work first;
- it preserves deterministic behavior and testability;
- it avoids large risky rewrites before lower-risk wins are captured.

## Verification Checklist

After each phase, verify:

1. Existing MCCFR determinism tests still pass.
2. Static scheduler partition tests still pass.
3. `1` worker vs `N` worker fixed-seed equivalence still holds where expected.
4. Average strategy export and expected value stay stable.
5. `traj_ms` moves in the intended direction on the scaling preset.
6. `merge_ms` is not accidentally inflated by traversal refactors.
7. Sparse-storage and AS tests still pass.

Do not use wall-clock thresholds in unit tests. Use:

- structural invariants;
- deterministic output invariants;
- benchmark comparison outside unit tests.

## Expected Outcome

If the plan is executed well, the likely outcome is:

- better single-worker throughput from lower per-node overhead;
- better multiworker throughput because workers spend more time in useful arithmetic and less time in helper/indexing overhead;
- cleaner separation between dense validation fast path and future sparse production path;
- a profile where `traj_ms` is still dominant, but much smaller in absolute terms;
- only then, merge becoming the next worthwhile target.

## Final Recommendation

The next optimization phase should not start with merge or new threading changes.

It should start with the trajectory body itself:

1. add traversal-specific profiling;
2. specialize traversal by mode;
3. remove repeated row indexing and generic helper overhead from dense fast path;
4. only then revisit chance, AS, sparse path, and finally merge.

That is the cleanest path from:

```text
threading bottleneck solved
```

to:

```text
trajectory engine is actually fast
```
