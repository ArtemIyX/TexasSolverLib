# Parallel CPU Solver Plan

## Goal

Build a production HUNL solver backend that delivers real multithreaded CPU speedup, with a realistic target of **2x-4x** over the current single-threaded baseline on meaningful postflop subgames.

This document is the implementation plan for what we do next.

It is based on:

- the current project state in this repository
- our profiling results showing that the current threaded recursive solver gets slower with more workers
- the paper [Parallelizing Counterfactual Regret Minimization](https://arxiv.org/abs/2605.14277)
- the paper [Real-Time Parallel Counterfactual Regret Minimization](https://arxiv.org/abs/2605.19928)

## What the papers imply for this project

The main lesson from the papers is not just "use more threads."

The architectural lesson is:

- do **not** keep CFR as a recursive state-machine with hot-path shared lookups
- do **not** rely on per-worker dynamic hash tables and repeated state expansion
- do turn each CFR iteration into a sequence of bulk operations over prebuilt numeric data
- do parallelize over **contiguous node ranges** and **infoset groups**

That means our next step is **not** more tuning of the current `ParallelDCFRSolver`.

Our next step is to build a new **flat HUNL CPU backend** around the already existing tree/vector direction in the repository.

## Why the current parallel solver does not scale

Today the generic parallel solver still has several scaling blockers:

- it resolves infosets during traversal
- it still takes a global mutex in the hot path
- it still recursively calls `legal_actions()`, `chance_outcomes()`, and `next_state()`
- it duplicates worker-local accumulators and merges them later
- it parallelizes mainly over a root frontier, not over the full tabular update structure

That design can preserve correctness, but it is a weak fit for efficient CPU parallelism.

The current threaded solver should stay in the repo as:

- a correctness reference
- a fallback for tiny games
- a debugging tool

It should **not** be treated as the path to the final 2x-4x CPU speedup target.

## Final architecture we want

After this document is implemented, the solver should work like this:

1. Build a flat HUNL game tree once.
2. Assign stable numeric infoset IDs once.
3. Precompute node metadata once.
4. Store regrets, strategy sums, and temporary iteration buffers in flat contiguous arrays.
5. Run each CFR iteration as explicit stages over node and infoset ranges.
6. Parallelize those stages with worker threads over disjoint contiguous blocks.
7. Avoid locks and dynamic allocation in the iteration hot path.
8. Export string infoset keys only at the end.

This is the architecture most aligned with the papers and the one most likely to produce the target CPU speedup.

## Success criteria

We should treat this project as successful only if all of these are true:

- correctness matches the sequential baseline within tight tolerances
- scaling improves on real HUNL postflop benchmarks
- `2` workers is faster than `1`
- `4` workers is clearly faster than `1`
- total wall time improves by at least `2x` on one benchmark that matters to us
- the design leaves room for later GPU or neural leaf evaluation integration

Suggested concrete target:

- benchmark date: after Phase 5
- workload: one fixed HUNL postflop benchmark tree
- target: `4` workers achieves at least `2.0x` speedup and ideally `2.5x-4.0x`

## Non-goals for this phase

These are explicitly not the main focus of the next implementation:

- more frontier heuristics for the current recursive parallel solver
- more tuning of `frontier_multiplier`
- general-purpose parallelization for all games first
- GPU offload in the first milestone
- neural evaluation in the first milestone

Those may matter later, but they are not the shortest path to real CPU scaling.

## Step-by-step implementation plan

## Phase 0. Freeze the current baseline

Purpose:
Make sure we always know whether the new architecture is actually better.

### Step 0.1. Define one official benchmark workload

Substeps:

- Pick one HUNL postflop benchmark configuration and keep it fixed.
- Record exact tree settings, stack size, starting street, board, hole cards, and iteration count.
- Make this the default benchmark for all parallel performance work.

Output:

- one canonical benchmark configuration in code
- one canonical command line for measuring it

### Step 0.2. Record baseline timings

Substeps:

- Run current sequential solver on the benchmark.
- Run current parallel solver with `2`, `4`, `8`, and `16` workers.
- Save total time, traversal time, finalize time, and postprocess time.
- Save infoset count and tree size.

Output:

- a committed baseline table in docs

### Step 0.3. Lock in correctness tests

Substeps:

- Keep existing sequential-vs-parallel correctness tests.
- Add one HUNL regression test around the selected benchmark tree.
- Save one golden output with tolerance-based checks.

Output:

- tests that allow us to safely replace the backend

## + Phase 1. Build a flat HUNL solve representation

Purpose:
Move all game-structure discovery out of the hot path.

### + Step 1.1. Introduce a dedicated flat HUNL solve graph

Substeps:

- Create a new backend-specific structure, for example `HUNLFlatSolveGraph`.
- Build it from the existing HUNL tree.
- Store nodes in stable index order.
- Store node type explicitly:
  - terminal fold
  - terminal showdown
  - chance
  - decision

Output:

- a compact, index-addressable graph for solving

### + Step 1.2. Assign stable numeric infoset IDs during graph build

Substeps:

- During graph construction, assign each decision node an infoset ID.
- Group nodes belonging to the same infoset.
- Store action count once in infoset metadata.
- Do not do infoset registration during solve iterations.

Output:

- no runtime infoset interning in the hot path

### + Step 1.3. Precompute node metadata

Substeps:

- For every node, precompute:
  - player
  - child start offset
  - child count
  - infoset ID if decision node
  - terminal metadata if leaf
  - chance child probabilities if chance node
- Store children in one flat array instead of nested vectors when possible.

Output:

- one compact metadata block for fast indexed traversal

### Step 1.4. Precompute traversal orderings

Substeps:

- Build arrays for forward order and reverse order.
- Build per-street or per-depth slices if they help scheduling.
- Build node ranges that workers can process independently.

Output:

- explicit stage-friendly traversal order

## Phase 2. Replace dynamic infoset storage with flat arrays

Purpose:
Make the solve state cache-friendly and thread-friendly.

### Step 2.1. Introduce flat infoset tables

Substeps:

- Create contiguous arrays for:
  - regret sums
  - strategy sums
  - current strategy
- Use one offset per infoset.
- Store `action_count` and `offset` in a compact infoset metadata table.

Output:

- all regret and strategy data addressable by `(infoset_id, action_idx[, hand_idx])`

### Step 2.2. Decide hand layout

Substeps:

- For the HUNL backend, define whether memory is laid out as:
  - infoset-major then hand-major then action-major
  - infoset-major then action-major then hand-major
- Benchmark both on a small prototype.
- Pick the one with better CPU cache behavior and simpler vectorization.

Output:

- a documented canonical memory layout

### Step 2.3. Remove string-key dependence from the solve loop

Substeps:

- Ensure the backend never needs `InfosetKey` during iteration.
- Keep string keys only in export metadata.
- Export final strategies by mapping `infoset_id -> string` after solve completes.

Output:

- zero string work in the hot loop

### Step 2.4. Remove per-worker hash tables

Substeps:

- Replace worker-local infoset maps with:
  - shared canonical arrays for persistent regrets and strategy sums
  - worker-local scratch arrays for temporary stage data only
- Ensure thread ownership is by disjoint ranges, not by local merge maps.

Output:

- no large worker-local accumulation maps

## Phase 3. Turn CFR iteration into explicit pipeline stages

Purpose:
Match the staged bulk-processing architecture suggested by the papers.

### Step 3.1. Define explicit per-iteration stages

Minimum stage set:

1. strategy computation from regrets
2. forward reach propagation
3. terminal utility evaluation
4. backward value propagation
5. regret update
6. average-strategy update

Output:

- a non-recursive iteration driver

### Step 3.2. Implement strategy computation as a flat pass

Substeps:

- For each infoset, compute regret matching over contiguous rows.
- Write results into a current-strategy buffer.
- Avoid per-node allocations.
- Keep this stage independent from the tree walk.

Output:

- strategy generation as one bulk pass over infosets

### Step 3.3. Implement forward reach propagation

Substeps:

- Propagate player reach and opponent reach through the graph.
- Use precomputed node order.
- Write reach values into node-aligned arrays.
- Handle chance nodes through weighted propagation.

Output:

- no recursive reach computation

### Step 3.4. Implement terminal evaluation stage

Substeps:

- Evaluate all terminal nodes into a terminal-value buffer.
- Keep fold and showdown logic isolated from the rest of the pipeline.
- Precompute as much static showdown metadata as possible.

Output:

- one explicit leaf-value stage

### Step 3.5. Implement backward counterfactual value propagation

Substeps:

- Traverse reverse node order.
- Compute child action values and node values.
- Write results into reusable value buffers.

Output:

- one explicit reverse pass for counterfactual values

### Step 3.6. Implement regret and average-strategy updates

Substeps:

- Update regrets from action values minus node values.
- Update average strategy from reach-weighted strategy.
- Keep DCFR discount logic separate and explicit.

Output:

- a complete non-recursive CFR iteration

## Phase 4. Add CPU multithreading to the staged backend

Purpose:
Parallelize the correct units of work.

### Step 4.1. Introduce a reusable worker pool

Substeps:

- Create a persistent thread pool owned by the backend.
- Reuse worker threads across iterations.
- Use simple barriers between stages.

Output:

- worker lifecycle cost removed from the hot benchmark

### Step 4.2. Parallelize by infoset for strategy stages

Substeps:

- Partition infoset ranges into contiguous blocks.
- Give each worker a disjoint block.
- Compute current strategy and average-strategy updates in parallel.

Output:

- lock-free parallel strategy stages

### Step 4.3. Parallelize by node range for propagation stages

Substeps:

- Partition node ranges into contiguous blocks where dependencies allow.
- Process forward reach stage by level, depth bucket, or other valid ordering.
- Process reverse value stage in reverse buckets.

Output:

- lock-free parallel node passes

### Step 4.4. Keep ownership disjoint

Substeps:

- Make sure each worker writes only to its assigned output region within a stage.
- If a stage needs reductions, use:
  - thread-local scratch for that stage
  - one explicit reduction step
- Do not use mutexes in steady-state stage execution.

Output:

- no hot-path global lock

### Step 4.5. Add scheduler diagnostics

Substeps:

- Measure per-worker time per stage.
- Measure imbalance between the fastest and slowest worker.
- Print stage timing breakdown in benchmark mode.

Output:

- visibility into whether scaling is blocked by imbalance or memory bandwidth

## Phase 5. Optimize the hottest buffers and loops

Purpose:
Convert architectural win into actual wall-clock speedup.

### Step 5.1. Eliminate temporary vector churn

Substeps:

- Replace `std::vector<double>` returns in hot computation with:
  - preallocated buffers
  - spans/views
  - per-thread scratch arenas
- Reuse buffers across iterations.

Output:

- no repeated allocation in hot stages

### Step 5.2. Tune memory alignment and locality

Substeps:

- Align hot arrays to cache-friendly boundaries.
- Keep action rows contiguous.
- Avoid false sharing by padding worker-owned stage scratch where needed.

Output:

- fewer cache line conflicts between threads

### Step 5.3. Add SIMD-friendly inner loops

Substeps:

- Identify regret-matching and reduction loops suitable for SIMD.
- Use the existing SIMD utilities where helpful.
- Keep a scalar fallback for correctness and portability.

Output:

- better single-core throughput and better per-thread performance

### Step 5.4. Precompute terminal helpers

Substeps:

- Cache fold payoffs where possible.
- Precompute showdown-related helper tables for the chosen benchmark mode.
- Reduce expensive leaf-stage repeated work.

Output:

- terminal stage no longer dominates scaling

## Phase 6. Integrate backend selection and fallback policy

Purpose:
Make the new backend usable without breaking the existing library.

### Step 6.1. Add a dedicated HUNL flat backend API

Substeps:

- Introduce a new backend class, for example `HUNLFlatDCFR`.
- Keep the public wrapper API stable.
- Route HUNL postflop solves to the new backend behind a controlled switch.

Output:

- new backend available without breaking callers

### Step 6.2. Keep the old solver as fallback

Substeps:

- Use the old generic sequential solver for:
  - debugging
  - tiny trees
  - correctness comparison
- Use the new flat backend for benchmarked HUNL postflop solving.

Output:

- safe migration path

### Step 6.3. Add backend comparison mode

Substeps:

- Expose a benchmark or test mode that runs:
  - old sequential
  - old parallel
  - new flat sequential
  - new flat parallel
- Print all four results side by side.

Output:

- proof that the architecture change is the source of the speedup

## Phase 7. Benchmark until the speedup target is real

Purpose:
Make performance improvement measurable and non-accidental.

### Step 7.1. Add stage-level benchmark output

Substeps:

- Print:
  - total time
  - strategy stage
  - reach stage
  - terminal stage
  - value-backprop stage
  - regret update stage
  - average-strategy stage
- Print per-worker timing when requested.

Output:

- precise bottleneck visibility

### Step 7.2. Measure scaling at fixed workers

Substeps:

- Run `1`, `2`, `4`, `8`, and optionally `16` workers.
- Record speedup and efficiency.
- Use the same benchmark tree every time.

Output:

- scaling table for each major milestone

### Step 7.3. Define the stop condition

We consider this roadmap complete when:

- correctness is stable
- `2` workers beats `1`
- `4` workers beats `2`
- `4` workers gives at least `2x` speedup on the main benchmark
- the speedup is reproducible across repeated runs

## Recommended implementation order

This is the safest and highest-leverage order:

1. Phase 0 baseline and tests
2. Phase 1 flat solve graph
3. Phase 2 flat infoset storage
4. Phase 3 non-recursive single-thread staged iteration
5. Prove new flat single-thread backend is correct
6. Phase 4 multithread the stages
7. Phase 5 optimize loops and buffers
8. Phase 6 integrate backend selection
9. Phase 7 benchmark until target is reached

Important rule:

- do **not** add multithreading before the staged single-thread backend exists and is correct

## Detailed coding checklist

This is the short execution checklist for implementation.

### Milestone A. New graph

- add `HUNLFlatSolveGraph`
- convert `HUNLTree` into solver-friendly flat arrays
- assign stable infoset IDs at build time
- store export keys as metadata only

### Milestone B. New single-thread backend

- add `HUNLFlatDCFR`
- implement explicit stages without recursion
- use flat arrays for regrets, strategies, reaches, and values
- validate against existing sequential HUNL solver

### Milestone C. Parallel backend

- add worker pool
- parallelize infoset stages
- parallelize node stages
- add barriered stage execution
- remove locks from hot stages

### Milestone D. Performance pass

- remove temporary allocations
- add scratch buffers
- improve memory alignment
- tune range partitioning
- add stage timing output

### Milestone E. Productization

- wire into `solve_hunl_postflop`
- keep fallback switch
- add benchmark docs and expected speedup table

## Risks and how to handle them

### Risk 1. Single-thread flat backend is not faster

Meaning:

- the current bottleneck may be terminal evaluation or hand-range handling, not only recursion and locking

Response:

- finish stage timing before further multithreading
- optimize the slowest stage first

### Risk 2. Parallel stage execution is correct but still memory-bound

Meaning:

- we improved architecture, but layout or scratch ownership still causes bandwidth pressure

Response:

- revisit memory layout
- reduce duplicated temporary buffers
- test alternative hand/action storage ordering

### Risk 3. Dependencies between node ranges complicate parallelization

Meaning:

- not every traversal order is valid for parallel execution

Response:

- use explicit buckets by depth, street, or topo level
- parallelize inside buckets, synchronize between buckets

### Risk 4. Terminal evaluation dominates

Meaning:

- the rest of CFR scales, but leaf work does not

Response:

- cache more terminal helpers
- batch leaf evaluation better
- revisit evaluator data layout

## What we should stop doing

Until the new staged backend exists, we should avoid spending serious time on:

- improving the current recursive frontier splitter
- adding more worker-local accumulation tricks
- tuning merge behavior in the current threaded generic solver
- adding more complexity to the lock-based infoset path

Those can produce small local improvements, but they are unlikely to produce the target 2x-4x CPU speedup.

## Bottom line

The next implementation should be:

- **flat**
- **numeric**
- **staged**
- **non-recursive in the hot path**
- **parallel over contiguous node and infoset ranges**

If we execute this plan in order, we give ourselves a realistic path to a working solver with meaningful CPU multithread speedup instead of continuing to optimize an architecture that fundamentally fights parallel execution.
