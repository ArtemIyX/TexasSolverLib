# Parallel CFR / DCFR Review

Short version:

- CFR can be parallelized, but only when the work is split so each thread does enough independent traversal to amortize synchronization.
- The best-performing approaches usually avoid fine-grained sharing of regrets/strategies during traversal.
- For this codebase, the current design is closest to "parallel work over a frontier, then merge per-thread accumulators." That is the right direction, but it can still lose to single-threaded execution if the frontier is too small, the merge cost is too high, or threads spend too much time waiting at barriers.

## What the recent research suggests

Two recent papers are especially relevant:

- [Parallelizing Counterfactual Regret Minimization](https://arxiv.org/abs/2605.14277)
- [Real-Time Parallel Counterfactual Regret Minimization](https://arxiv.org/abs/2605.19928)

The common theme is that CFR becomes much more parallel-friendly when you treat each iteration as a collection of independent numeric updates instead of a tightly coupled recursive walk with shared mutable state. In particular:

- Split work by information set or tree region so workers can traverse independently.
- Keep per-thread regret/strategy accumulators while computing.
- Merge results in batches at synchronization points, not on every node.
- Use a larger enough task granularity that thread scheduling overhead does not dominate the actual game-tree work.

That is why approaches built around batched traversal or frontier partitioning tend to scale better than ones that lock shared tables at every infoset.

## Why some multithreaded versions are slower than single-threaded

This matters for your current code, because a parallel solver can still lose if:

- The tree is small, so thread startup and synchronization cost exceed the saved work.
- Threads repeatedly wait on a barrier each iteration or each player pass.
- Strategy snapshots are copied often.
- A mutex protects a hot path, causing contention and cache-line bouncing.
- Each worker does too little work before it needs to merge its local state.
- The workload is imbalanced, so some threads finish early and sit idle.

In other words, "more threads" only helps when the program has enough independent CPU work to hide synchronization and memory traffic.

## What this project already does

The current parallel path is in [`src/solver/parallel_dcfr.cpp`](../src/solver/parallel_dcfr.cpp) and is exposed through [`include/solver/parallel_dcfr.hpp`](../include/solver/parallel_dcfr.hpp) and [`include/solver/solver.hpp`](../include/solver/solver.hpp).

Current design summary:

- The solver builds a frontier of root branches before running threaded traversal.
- Each worker owns a local `ParallelWorkerState` with its own regret/strategy accumulator map.
- Workers compute against a snapshot of the current strategy.
- Results are merged back into the canonical infoset table after each player pass.
- Parallel mode is enabled through `TEXASSOLVER_PARALLEL_CFR`.
- Worker count can be configured through `TEXASSOLVER_PARALLEL_CFR_WORKERS`.

That design is fundamentally sound because it removes shared-writer pressure from the recursive traversal. The main remaining risk is overhead:

- strategy snapshot copying
- batch scheduling
- synchronization at every iteration and player pass
- merge work after each pass

## Why a parallel approach should be faster in theory

Parallel CFR should beat single-threaded CFR when the solver can do this:

1. Partition the tree into independent subproblems.
2. Let each worker traverse its own subproblem without locks.
3. Accumulate regrets locally.
4. Merge once per batch or per iteration.

That increases performance because the expensive part of CFR is the repeated tree traversal and regret update math. If those updates are independent, multiple cores can work at the same time.

The speedup comes from parallel compute, not from parallelizing the merge itself. The merge is just bookkeeping. If merge/synchronization becomes too large relative to traversal, the program stops scaling.

## Step-by-step implementation plan for this project

1. Keep the sequential solver as the baseline.
   - Preserve [`DCFRSolver`](../include/solver/dcfr.hpp) for correctness, debugging, and small trees.
   - Use it automatically when `workers == 1` or when the tree is too small to benefit from threads.

2. Make the parallel path work on coarse batches.
   - Keep a frontier of independent root-level branches or subtrees.
   - Increase the minimum amount of work per batch so one batch is large enough to amortize scheduling overhead.
   - Prefer fewer, heavier tasks over many tiny tasks.

3. Avoid shared writes during traversal.
   - Keep regrets and strategy sums thread-local during the recursive CFR walk.
   - Do not update the global infoset table inside the hot recursive path.
   - Merge thread-local maps only after the traversal phase completes.

4. Reduce strategy snapshot overhead.
   - Build the strategy snapshot once per player pass.
   - Reuse the same snapshot across workers.
   - If possible, represent snapshots in a cache-friendly layout instead of repeatedly copying unordered maps.

5. Tune frontier size for the game.
   - For small games like Kuhn, parallelism may never win.
   - For Leduc and especially HUNL postflop, increase `frontier_multiplier` until each worker has enough work.
   - The best value is usually "large enough to keep cores busy" but not so large that frontier creation becomes expensive.

6. Improve load balancing.
   - Keep batches dynamic so workers can steal work when they finish early.
   - Split on the heaviest branches first.
   - Rebalance if some branches are much deeper than others.

7. Minimize synchronization.
   - Keep one barrier per player pass, not per node.
   - Use atomics only for work distribution.
   - Keep mutex-protected sections short and outside the recursion path.

8. Add performance tests, not only correctness tests.
   - Measure single-thread vs multithread on the same tree and iteration count.
   - Benchmark Kuhn, Leduc, and one HUNL postflop configuration separately.
   - Track wall time, not just iteration count.

9. Add a heuristic fallback.
   - If `branch_count` is small or estimated frontier work is low, force the sequential solver.
   - This prevents the parallel path from regressing on tiny trees.

10. Re-check merge costs.
    - If merge time is large, consider chunked accumulation or a more cache-friendly accumulator type.
    - Merging should be much cheaper than full traversal.

## Practical optimization checklist

- Prefer thread-local state over shared state.
- Keep task sizes coarse.
- Avoid copying large `unordered_map` objects more than necessary.
- Reuse worker threads across iterations, which this code already does.
- Skip parallelism on small trees.
- Measure scaling on the target game before assuming a change helps.

## Bottom line

For this project, parallel CFR should improve performance when the solver can give each thread a substantial independent subtree, keep local updates thread-private, and merge only at phase boundaries. If the tree is small or the frontier is too shallow, the overhead from thread management, strategy copying, and merging will outweigh the benefit.

The current code already follows the right high-level idea. The next gains are most likely to come from:

- better frontier sizing
- less strategy-copy overhead
- fewer synchronization points
- a sequential fallback for small solves
