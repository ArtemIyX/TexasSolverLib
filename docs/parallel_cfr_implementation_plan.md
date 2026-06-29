# Parallel CFR Implementation Plan

This document is an implementation order, not a design brainstorm. The goal is to add real multithreaded CFR support without breaking the current public API, solver correctness, or tests.

## Goal

Implement a correct parallel CFR execution path for generic games and HUNL postflop, while keeping the current sequential solver as the fallback reference until the threaded path proves equivalent.

## Implementation order

### 1. Freeze the sequential baseline

What to do:
- Keep `DCFRSolver<G>` as the reference implementation.
- Make sure current tests for Kuhn, Leduc, and HUNL continue to pass with the sequential path.
- Do not change solver math while the parallel path is being added.

Why this comes first:
- We need a stable reference before introducing concurrency.
- If sequential behavior changes at the same time, we will not know whether a regression came from threading or from solver math.

How to do it:
- Keep the existing `solve_kuhn()`, `solve_leduc()`, and `solve_hunl_postflop()` entry points intact.
- Treat the sequential solver as the source of truth for values, strategies, and exploitability.

### 2. Keep the parallel entry point behind a switch

What to do:
- Expose a parallel solver path in `solver/`, but keep it disabled by default.
- Route users to the sequential solver unless the parallel mode is explicitly enabled.

Why this comes next:
- This lets us integrate code incrementally without changing the default behavior.
- It gives us a safe way to compile, run, and compare the new path.

How to do it:
- Use a single runtime switch for now.
- Keep the public API unchanged so callers do not need to know whether the solver is sequential or parallel.

### 3. Implement worker-local accumulation

What to do:
- Change the parallel solver so every worker writes to its own local regret and strategy buffers.
- Do not let workers write into shared solver state during traversal.

Why this comes before real threading:
- Shared writes are the main correctness risk.
- Local accumulation keeps the recursion simple and avoids data races.

How to do it:
- Mirror the `InfosetAccum` layout in worker-local maps.
- Merge the local maps only after the worker finishes its assigned work.

### 4. Choose a true partition boundary

What to do:
- Partition work into disjoint subtrees or other independent work units.
- Start with a boundary that matches the game tree structure and does not overlap between workers.

Why this is critical:
- Parallel CFR only works if each worker can process an independent portion of the tree.
- If two workers touch the same mutable path at the same time, the result will drift from the sequential baseline.

How to do it:
- Prefer subtree splits near a high-branching node.
- Keep the partition deterministic so the same input produces the same worker layout.

### 5. Snapshot strategy at iteration boundaries

What to do:
- Ensure all workers in an iteration read the same strategy snapshot.
- Do not let one worker see partially merged updates from another worker in the same iteration.

Why this matters:
- CFR iteration semantics depend on a consistent policy snapshot.
- Mixed snapshots will produce values that do not match the sequential solver.

How to do it:
- Build the strategy snapshot once per iteration.
- Pass that snapshot into each worker as read-only input.

### 6. Merge results deterministically

What to do:
- Merge worker-local regrets and strategy sums back into the canonical solver state in a fixed order.
- Verify that action counts match when merging the same infoset from multiple workers.

Why this is next:
- Even correct additive math can become hard to debug if merge order is unstable.
- Deterministic merging makes the threaded path easier to validate and reproduce.

How to do it:
- Use a stable worker ordering.
- Add explicit checks for mismatched action vector sizes before combining entries.

### 7. Restore equivalence with the sequential solver

What to do:
- Run the threaded path against the sequential path on the same input and compare the outputs.
- Keep tightening until values and strategies match within a small tolerance.

Why this is a separate step:
- A parallel implementation is only useful if it remains mathematically consistent.
- This is where correctness is proven, not assumed.

How to do it:
- Compare `iterations`, `game_value`, `exploitability`, and strategy outputs.
- Use small tolerances for floating-point differences, but do not accept large drift.

### 8. Expand regression tests

What to do:
- Keep tests for Kuhn, Leduc, and HUNL postflop.
- Compare sequential and parallel outputs for the same iteration counts and configurations.

Why this comes after equivalence work:
- Tests should validate behavior that is already intended, not define unfinished behavior.
- If the threaded path is still wrong, the tests should fail for a clear reason.

How to do it:
- Use small, fast test configurations.
- Compare public solver outputs only, not internal solver state.

### 9. Benchmark and tune

What to do:
- Measure runtime, scaling, and memory use after correctness is stable.
- Decide whether the chosen partition boundary is worth the threading overhead.

Why this is last:
- Performance tuning before correctness is premature.
- We only want to optimize a design that already behaves correctly.

How to do it:
- Benchmark HUNL postflop first, since that is the main target workload.
- Keep Kuhn and Leduc mostly as correctness guards.

## Expected implementation sequence

1. Keep sequential solver behavior stable.
2. Add a disabled parallel solver entry point.
3. Build worker-local accumulators.
4. Add deterministic partitioning of the tree.
5. Snapshot strategy per iteration.
6. Merge local results safely.
7. Compare parallel output against sequential output.
8. Strengthen regression tests.
9. Benchmark and tune.

## Success criteria

- Current solver APIs still work.
- Sequential and parallel outputs match within tolerance.
- The threaded path does not introduce race conditions.
- Tests pass for Kuhn, Leduc, and HUNL.
- The parallel path can be enabled without changing caller code.

