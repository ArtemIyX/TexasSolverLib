# MCCFR And New Solver Module Plan For Large Flat HUNL Solves

## Executive Answer

Yes, MCCFR is possible for this solver, and it is the right direction for large flop trees where a full exact flat DCFR iteration is still around 18 seconds.

Architectural decision: this should be a new solver module, not a small patch inside the current exact `HUNLFlatDCFR`. We are allowed to build it from scratch while reusing proven pieces from the old solver as references, tests, and shared utilities.

The important caveat is that there are two different problems:

1. Per-iteration time: MCCFR can reduce this quickly by sampling public chance, opponent actions, or updating-player actions instead of traversing the full tree.
2. Memory: MCCFR only fixes memory if the implementation does not first build the entire full flop graph and dense strategy table. A full-graph MCCFR prototype is useful for correctness, but the production version needs lazy/sparse sampled graph and infoset storage.

Recommended implementation order:

1. Create a new greenfield sampled solver module with its own builder/cache, sparse storage, scheduler, terminal evaluator, and exports.
2. Add a full-graph sampled validation backend for correctness: public-chance sampling first, then external-sampling MCCFR.
3. Add deterministic multithreaded trajectory batches with worker-local deltas and fixed-order merges.
4. Add lazy/sparse sampled storage so flop memory scales with visited public states and visited infosets, not with every turn/river branch.
5. Add Average Strategy Sampling after the baseline estimator is stable. AS is useful when action menus are large; for the current RTA presets, public chance and opponent-action sampling will probably matter more first.

## Sources Reviewed

External:

- Efficient Monte Carlo Counterfactual Regret Minimization in Games with Many Player Actions, Gibson/Burch/Lanctot/Szafron, NeurIPS 2012: https://papers.nips.cc/paper/2012/file/3df1d4b96d8976ff5986393e8767f5b2-Paper.pdf
- Real-Time Parallel Counterfactual Regret Minimization, Li/Huang, 2026: https://arxiv.org/html/2605.19928v1
- Emergent Mind MCCFR overview, used as a secondary index of variants and risks: https://www.emergentmind.com/topics/monte-carlo-counterfactual-regret-minimization-cfr
- Variance Reduction in Monte Carlo Counterfactual Regret Minimization, Schmid et al., 2018: https://arxiv.org/abs/1809.03057
- Stochastic Regret Minimization in Extensive-Form Games, Farina/Kroer/Sandholm, 2020: https://arxiv.org/abs/2002.08493

Project files:

- `include/games/hunl.hpp`
- `src/games/hunl.cpp`
- `include/games/hunl_flat_graph.hpp`
- `src/games/hunl_flat_graph.cpp`
- `include/games/hunl_flat_builder.hpp`
- `src/games/hunl_flat_builder.cpp`
- `include/solver/hunl_flat_dcfr.hpp`
- `src/solver/hunl_flat_dcfr.cpp`
- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `include/solver/hunl_flat_pipeline.hpp`
- `src/solver/hunl_flat_pipeline.cpp`
- `include/solver/hunl_bucket_terminal.hpp`
- `src/solver/hunl_bucket_terminal.cpp`
- `include/util/pcs.hpp`
- `src/util/pcs.cpp`
- `include/util/suit_iso.hpp`
- `src/util/suit_iso.cpp`
- `examples/benchmarks/flat_scheduler_main.cpp`
- `examples/benchmarks/hunl_random_flat_main.cpp`
- `tests/test_pcs.cpp`
- `tests/test_hunl_flat_dcfr.cpp`
- `tests/test_hunl_flat_pipeline.cpp`
- `tests/test_ranges_threading.cpp`
- `docs/flop_rta_memory_optimization_plan.md`
- `docs/hunl_flat_cfr_optimization_plan.md`

## What The Papers Mean For This Solver

MCCFR replaces a full tree traversal with an unbiased sampled traversal. In the Gibson et al. paper:

- chance sampling samples one chance branch and still evaluates all player actions;
- external sampling samples chance and opponent actions, but branches across all actions for the traversing player;
- outcome sampling samples one complete terminal trajectory;
- Average Strategy Sampling samples a subset of the traversing player's actions using average-strategy mass with exploration.

The paper's Average Strategy Sampling formula is:

```text
rho(I, a) = max(epsilon, (beta + tau * s(I, a)) / (beta + sum_b s(I, b)))
```

with probability capped at 1. `s(I, a)` is the cumulative average-strategy table. The paper uses `epsilon = 0.05`, `beta = 1e6`, and `tau = 1000` in its experiments. Those constants are not guaranteed best for our solver, but they are a good first test range.

Practical translation:

- Use public-chance sampling first because flop trees explode mostly from future board runouts.
- Use external sampling second because it reduces opponent action expansion while keeping lower variance than outcome sampling.
- Use Average Strategy Sampling third because it helps most when action menus are wide. Current RTA presets have small action menus, so AS may not be the first bottleneck.
- Do not start with pure outcome sampling for this project. It is memory-light, but high-variance and harder to validate against the existing exact flat solver.
- Add variance reduction later, not first. VR-MCCFR is promising, but it needs a correct sampled baseline and good baseline values.

## Current Project State

The current exact flat solver is `HUNLFlatDCFR`.

The exact iteration pipeline is in `src/solver/hunl_flat_pipeline.cpp` and calls:

1. `apply_dcfr_discount_stage()`
2. `compute_strategy_stage()`
3. `forward_reach_stage()`
4. `normalize_bucket_reach_stage()`
5. `showdown_equity_stage()`
6. `depth_limited_eval_stage()`
7. `backward_value_stage()`
8. `regret_update_stage()`
9. `average_strategy_stage()`

The code already has several good foundations:

- `HUNLFlatSolveGraph` is compact and uses `HUNLFlatPackedBoard`.
- `HUNLFlatBuilder` builds flat graphs directly.
- public chance classes are already collapsed by suit isomorphism in bucketed mode.
- `HUNLFlatWorkerScratch` uses depth-slice reach scratch, dirty lists, row scratch, and local bucket mass.
- `HUNLFlatParallelPlan` partitions backward depth slices by estimated cost.
- `HUNLFlatInfosetTable` supports `Float64` and `Float32`.
- memory preflight already exists in benchmark tools.
- `PcsRng` already gives deterministic sampled utility functions.

The remaining exact-solver problem is structural: even optimized exact DCFR still touches the whole reachable graph and all dense infoset rows every iteration. On a large flop tree, that is too much work.

## Why MCCFR Fits

For an exact full traversal, one iteration is roughly:

```text
O(nodes + edges + infoset_values)
```

where `infoset_values ~= sum_infoset(bucket_count * action_count)`.

For MCCFR, one traversal is roughly:

```text
O(sampled_path_depth * sampled_branch_width * bucket_count_touched)
```

and a multithreaded batch is:

```text
O(batch_traversals / workers)
```

This is a better fit for flop because:

- many turn/river public branches do not need to be visited in every iteration;
- opponent choices can be sampled on-policy;
- updating-player action branches can later be partially sampled with AS;
- independent sampled traversals parallelize cleanly;
- worker-local sparse deltas avoid atomics and false sharing.

The tradeoff is variance. One sampled iteration is not equivalent to one exact iteration. We should compare by wall-clock and nodes visited, not by raw iteration count.

## RTA Goal And Success Definition

The goal is an RTA-style postflop solver that produces a strong useful strategy inside a strict decision budget on a 64 GB desktop. The goal is not to solve full flop HUNL to perfect GTO quality.

Target:

```text
hardware budget:       64 GB RAM desktop
decision budget:       10-15 seconds
first street target:   turn and river subgames
main target:           bounded flop subgames
quality target:        strong approximate strategy, stable root action mix, exploitable mistakes avoided
not required:          full 100bb dense-action full-flop near-perfect equilibrium
```

This matches the practical lesson from Real-Time Parallel CFR: real-time strength comes from the number of useful CFR/MCCFR updates completed inside the decision window, not from building the largest possible exact tree. That paper emphasizes depth-limited solving, pruning, abstraction, and parallel pipeline decomposition as the real-time stack. For this project, MCCFR adds one more lever: avoid touching most public chance branches and opponent actions on every update.

RTA acceptance should be wall-clock based:

- produce root strategy in <= 15 seconds for conservative flop preset;
- produce root strategy in <= 10 seconds for turn/river preset;
- keep peak resident memory below 56 GB, leaving OS/allocator headroom;
- avoid dense allocations that can exceed 64 GB before solving starts;
- improve action quality over current heuristic/baseline in repeated benchmark spots;
- keep outputs stable enough across seeds to be usable.

Important philosophy:

- We do not need fully GTO.
- We need a robust, hard-to-exploit, high-EV approximate strategy for the current spot.
- Small action abstractions, bucket abstractions, depth-limited leaves, and pruning are acceptable.
- The solver should spend time where the decision matters: current public state, root/action neighborhoods, high-reach lines.

## Required Feature Compatibility

MCCFR must support the existing practical features, not replace them.

### Multithreading

Supported and required.

Implementation model:

```text
worker 0: trajectories [0, N0)
worker 1: trajectories [N0, N1)
...
merge worker-local deltas in fixed order
```

Rules:

- workers run independent sampled traversals;
- no global regret writes inside traversal;
- no floating-point atomics;
- deterministic seed = hash(base_seed, iteration, player, trajectory_id);
- deterministic merge first, optional dynamic scheduling later;
- profile traversals/sec, nodes/sec, merge time, and worker imbalance.

This is different from exact `HUNLFlatDCFR`, which parallelizes staged full-tree passes by node or infoset. MCCFR parallelizes by independent trajectory batches.

### Ranges

Supported and required.

Ranges should enter the sampled solver as:

- initial combo weights;
- initial bucket weights;
- public-board blocked combo masks;
- sampling weights for private hand/bucket sampling if that is added later;
- reach priors for bucketed infoset updates.

First implementation:

- do not sample private buckets;
- keep bucket/range aggregation exact inside visited infosets;
- use current `HUNLFlatBucketMap` and range projection semantics;
- apply range inputs lazily when a sampled public state or infoset is first visited.

Later implementation:

- sample private buckets or combos only if bucket updates remain too expensive;
- use importance weighting if private range sampling is introduced;
- cache board-blocked range projections by public board.

### Buckets

Supported and required.

Buckets are the main way to fit postflop solving into memory. The sampled solver should keep bucketed strategy rows:

```text
row size = bucket_count * action_count
```

but allocate rows only on first visit.

Recommended RTA bucket defaults:

```text
conservative flop: flop 64, turn 48, river 32
balanced flop:     flop 96, turn 64, river 48
turn solve:        turn 64, river 48
river solve:       river 64 or 96
```

Use `Float32` regret and strategy sum in production. Use `Float64` only for validation.

### Depth Limiting

Supported and required.

MCCFR can terminate a sampled trajectory at a depth-limited leaf:

```text
if node is depth-limited:
    return leaf_value(range_state, bucket_state, board, pot, action_history)
```

First implementation:

- use existing heuristic leaf value for smoke tests;
- expose depth-limited leaves in sampled traversal;
- include leaf calls in profiling.

RTA implementation:

- use bucket/range-conditioned leaf values;
- support batched leaf evaluation later;
- cache leaf values by public board, pot class, SPR class, action history class, and bucket distribution summary;
- allow future neural/value-table backends without changing traversal code.

Scalar leaf heuristics are acceptable for first compile/test, but not for final RTA quality.

## What Not To Do

- Do not just add a `sampled=true` branch inside `HUNLFlatDCFR` while still building the whole full flop graph and dense table for production.
- Do not force the new solver to preserve every exact-solver internal data structure.
- Do not use floating-point atomics for regret updates.
- Do not introduce nondeterministic merge order before tests are relaxed for sampled variance.
- Do not start with pure outcome sampling.
- Do not use AS before external-sampling correctness is locked down.
- Do not overload `HUNLConfig::use_pcs`; it is too narrow and underspecified for MCCFR.

## Greenfield Module Decision

The new solver should be treated as the next-generation HUNL solving engine. It can borrow proven code, but its internal architecture should be designed around sampling, sparse storage, lazy expansion, and multithreaded batches from day one.

Use the current exact solver for:

- correctness oracle on small graphs;
- exported strategy shape;
- action enumeration behavior;
- bucket map semantics;
- terminal utility conventions;
- memory-estimate reporting style;
- benchmark comparison.

Do not inherit these exact-solver constraints:

- full graph must exist before solving;
- every infoset row must be allocated before solving;
- current strategy must be stored densely;
- reach arrays must exist for every graph node;
- node/action values must exist for every graph node/edge;
- worker scheduling must be stage/depth based;
- one iteration must mean one complete tree pass.

Target mental model:

```text
old exact solver:
  build full graph -> allocate full table -> run full staged passes

new sampled solver:
  maintain sampled public-state cache -> allocate rows on first visit -> run many independent sampled trajectories -> merge sparse deltas
```

This is the difference between "sampling inside a full solver" and "a solver built for sampling."

## Recommended Solver Architecture

Add a separate solver instead of overloading exact `HUNLFlatDCFR`.

```text
include/solver/hunl_flat_mccfr.hpp
src/solver/hunl_flat_mccfr.cpp
tests/test_hunl_flat_mccfr.cpp
```

For a real new module, use this layout:

```text
include/solver/hunl_sampled_solver.hpp
src/solver/hunl_sampled_solver.cpp

include/solver/hunl_sampled_config.hpp
src/solver/hunl_sampled_config.cpp

include/solver/hunl_sampled_storage.hpp
src/solver/hunl_sampled_storage.cpp

include/solver/hunl_sampled_traversal.hpp
src/solver/hunl_sampled_traversal.cpp

include/solver/hunl_sampled_scheduler.hpp
src/solver/hunl_sampled_scheduler.cpp

include/solver/hunl_sampled_builder.hpp
src/solver/hunl_sampled_builder.cpp

include/solver/hunl_sampled_terminal.hpp
src/solver/hunl_sampled_terminal.cpp

include/solver/hunl_sampled_export.hpp
src/solver/hunl_sampled_export.cpp

tests/test_hunl_sampled_solver.cpp
tests/test_hunl_sampled_storage.cpp
tests/test_hunl_sampled_traversal.cpp
tests/test_hunl_sampled_scheduler.cpp
tests/test_hunl_sampled_builder.cpp
tests/test_hunl_sampled_terminal.cpp
```

`HUNLFlatMCCFR` can still be used as the class name if we want to emphasize relation to the flat solver. `HUNLSampledSolver` is better if we want to signal that this is a new era module, not only a variant of the old flat DCFR class.

Keep `HUNLFlatDCFR` exact and deterministic. The sampled solver should reuse or reference:

- `HUNLFlatSolveGraph` for the first prototype;
- `HUNLFlatInfosetTable` layout/index helpers;
- `PcsRng`;
- `HUNLFlatBucketMap`;
- `HUNLBucketTerminalTable`;
- memory estimation and benchmark output style.

But production internals should be new:

- `HUNLSampledPublicStateCache`
- `HUNLSampledInfosetTable`
- `HUNLSampledTraversal`
- `HUNLSampledWorker`
- `HUNLSampledDelta`
- `HUNLSampledScheduler`
- `HUNLSampledTerminalEvaluator`
- `HUNLSampledMemoryEstimate`
- `HUNLSampledStrategyExporter`

Add:

```cpp
enum class HUNLFlatSamplingMode : std::uint8_t {
    Exact = 0,
    PublicChance = 1,
    External = 2,
    AverageStrategy = 3,
};

struct HUNLFlatMCCFRConfig {
    HUNLFlatSamplingMode mode = HUNLFlatSamplingMode::External;
    std::uint64_t seed = 1;
    std::uint32_t traversals_per_iteration = 1024;
    std::uint32_t batch_size = 64;
    double as_epsilon = 0.05;
    double as_tau = 1000.0;
    double as_beta = 1e6;
    bool update_both_players = true;
    bool use_discounting = false;
};
```

`HUNLFlatMCCFR` public API should mirror the exact solver where possible:

```cpp
class HUNLFlatMCCFR {
public:
    HUNLFlatMCCFR(
        HUNLFlatSolveGraph graph,
        std::array<std::size_t, 2> bucket_count_per_player,
        HUNLFlatMCCFRConfig config,
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
        std::size_t workers = 1,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32);

    void run_iteration();
    void run_iterations(std::uint32_t iterations);
    HUNLFlatAverageStrategyTable export_average_strategy_table() const;
    std::unordered_map<std::string, std::vector<double>> export_average_strategy() const;
    HUNLFlatMemoryEstimate memory_estimate() const;
};
```

## New Module Responsibilities

The new solver module needs these subsystems.

### 1. Config And Presets

Purpose:

- make sampled mode explicit;
- keep exact mode as default;
- expose knobs needed for performance tuning without hiding them behind `use_pcs`.

Needed fields:

```cpp
struct HUNLSampledSolverConfig {
    HUNLFlatSamplingMode mode = HUNLFlatSamplingMode::External;
    std::uint64_t seed = 1;
    std::uint32_t iterations = 0;
    std::uint32_t traversals_per_iteration = 8192;
    std::uint32_t minibatch_size = 64;
    std::uint32_t max_cached_public_states = 0;
    HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;
    bool lazy_public_expansion = true;
    bool sparse_infosets = true;
    bool deterministic_merge = true;
    bool use_public_chance_isomorphism = true;
    bool use_average_strategy_sampling = false;
    double as_epsilon = 0.05;
    double as_tau = 1000.0;
    double as_beta = 1e6;
};
```

### 2. Public-State Cache And Lazy Builder

Purpose:

- avoid building all turn/river branches before solving;
- expand only states touched by sampled trajectories;
- keep enough metadata for legal actions, board, street, pot, history, and terminal utility.

Core structs:

```cpp
struct HUNLSampledStateKey {
    HUNLFlatBuilderMemoKey key;
};

struct HUNLSampledNode {
    HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
    HUNLSampledStateKey key;
    InfosetId infoset_id{};
    HUNLFlatPackedBoard board{};
    std::array<int, 2> contributions = {0, 0};
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint32_t first_child = 0;
    std::uint16_t child_count = 0;
    bool expanded = false;
};

class HUNLSampledPublicStateCache {
public:
    std::uint32_t find_or_create(const HUNLState& state);
    void expand(std::uint32_t node_id);
    const HUNLSampledNode& node(std::uint32_t node_id) const;
};
```

Implementation rules:

- expansion may allocate;
- traversal hot loops should reuse scratch buffers;
- node ids should be stable after allocation;
- cache keys should be compact and hashable;
- full debug strings should be optional/export-only.

### 3. Sparse Infoset Table

Purpose:

- allocate regret and strategy sum only when an infoset is visited;
- avoid dense `current_strategy_`;
- support bucketed rows without full upfront `V = sum(bucket_count * action_count)`.

Core structs:

```cpp
struct HUNLSampledInfosetMeta {
    InfosetId id{};
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
    std::uint32_t regret_offset = 0;
    std::uint32_t strategy_sum_offset = 0;
    std::uint32_t last_discount_iter = 0;
};

class HUNLSampledInfosetTable {
public:
    HUNLSampledInfosetView ensure(InfosetId id, const HUNLSampledInfosetShape& shape);
    HUNLSampledConstInfosetView view(InfosetId id) const;
    void compute_current_strategy(InfosetId id, double* out) const;
    void apply_delta(const HUNLSampledInfosetDelta& delta);
};
```

Storage:

- `float` for regret and strategy sum in production;
- `double` scratch for local arithmetic;
- optional `double` mode for validation;
- no dense current strategy array;
- row-local shape metadata.

### 4. Traversal Engine

Purpose:

- perform MCCFR walks without recursion-heavy object churn;
- support public chance, external sampling, and later AS.

Core API:

```cpp
struct HUNLSampledTraversalRequest {
    PlayerId traversing_player = 0;
    std::uint64_t trajectory_id = 0;
    std::uint32_t iteration = 0;
};

struct HUNLSampledTraversalResult {
    double value = 0.0;
    std::uint64_t nodes_visited = 0;
    std::uint64_t infosets_updated = 0;
};

class HUNLSampledTraversal {
public:
    HUNLSampledTraversalResult run(
        const HUNLSampledTraversalRequest& request,
        HUNLSampledWorkerScratch& scratch);
};
```

Implementation rules:

- start with recursive implementation if faster to land;
- move to explicit stack if profiling shows recursion/object copies matter;
- never write global regret directly from worker traversal;
- emit worker-local deltas only.

### 5. Worker-Local Deltas

Purpose:

- avoid locks and atomics in hot traversal;
- make multithreaded results deterministic.

Delta shape:

```cpp
struct HUNLSampledRowDelta {
    InfosetId id{};
    std::vector<float> regret_delta;
    std::vector<float> strategy_delta;
};

class HUNLSampledDeltaBuffer {
public:
    void add_regret(InfosetId id, std::size_t offset, double value);
    void add_strategy(InfosetId id, std::size_t offset, double value);
    void sort_for_merge();
    void clear();
};
```

First implementation can use vectors/maps. Later optimize with arenas and row-local contiguous scratch.

### 6. Batch Scheduler

Purpose:

- use all CPU threads with independent sampled trajectories;
- preserve deterministic results when requested.

Scheduling model:

```text
for iteration:
  for traversing_player:
    make trajectory ids [0, traversals_per_iteration)
    split ids into worker ranges
    workers run local traversals
    merge worker deltas in worker-index order
```

Later optional model:

- dynamic batch queue for better balance;
- deterministic merge by trajectory id if dynamic scheduling is used.

### 7. Terminal Evaluator

Purpose:

- compute terminal values without dense per-terminal-node matrices;
- reuse board-level caches.

Rules:

- fold terminal value is immediate from contributions;
- showdown terminal value should be board/cache based;
- bucketed mode should use bucket reach or sampled bucket weights;
- terminal cache key should be board + bucket shape + contribution class where needed;
- dense per-node showdown matrices should not return.

### 8. Strategy Export

Purpose:

- make sampled solver consumable by existing evaluation/benchmark paths;
- avoid forcing all sparse rows to become dense unless explicitly requested.

Exports:

- sparse average strategy iterator;
- `HUNLFlatAverageStrategyTable` materialization for tests and small benchmarks;
- string-key map for debug only;
- root strategy quick export.

### 9. Memory Estimator

Purpose:

- show that sampled mode actually solves memory;
- prevent accidental dense allocations.

Estimate categories:

```text
sampled public-state cache
sparse infoset rows
terminal cache
worker scratch
delta buffers
export scratch
```

Hard checks:

- fail if a sampled flop solve requests dense full graph plus dense full table by accident;
- warn if sparse rows approach dense table size;
- print rows allocated / possible rows when a validation full graph exists.

## 64 GB Memory Plan

The sampled solver must be designed to fit under 64 GB with room for OS, allocator overhead, exports, and evaluator caches. Use 56 GB as the practical hard ceiling and 48 GB as a warning ceiling.

Target budget:

```text
OS / allocator / safety headroom:        8 GB
sampled public-state cache:             8-12 GB max
sparse infoset regret + strategy_sum:   18-24 GB max
terminal / leaf / board caches:          4-8 GB max
worker scratch + deltas:                 4-8 GB max
export / diagnostics scratch:            2-4 GB max
emergency headroom:                      4 GB
```

Production memory rules:

- no full flop graph in sampled production mode;
- no dense full `current_strategy`;
- no dense full `node_values`;
- no dense full `action_values`;
- no dense full reach arrays;
- no per-worker graph-sized arrays;
- no per-terminal-node dense showdown matrix;
- no string infoset keys in hot storage;
- no duplicate board/history storage per node when a compact key is enough.

Sparse table memory:

```text
visited_values = sum_visited_infoset(bucket_count * action_count)
regret bytes   = visited_values * storage_bytes
avg bytes      = visited_values * storage_bytes
total rows     ~= 2 * visited_values * storage_bytes + metadata
```

With `Float32`:

```text
2 arrays * 4 bytes = 8 bytes per visited value
1 billion visited values ~= 8 GB before metadata
```

This is why we should not store dense current strategy. Current strategy should be computed into worker scratch from regret when a row is visited.

Worker memory:

```text
per worker:
  traversal stack: small, depth/action dependent
  action/value scratch: max_actions * max_buckets
  row scratch: max_bucket_count * max_action_count
  delta buffers: bounded by batch_size * visited rows per trajectory
```

Add hard caps:

- `max_delta_values_per_worker`
- `max_cached_public_states`
- `max_sparse_infoset_values`
- `max_terminal_cache_bytes`
- `max_export_bytes`

If a cap is exceeded:

1. flush and merge deltas;
2. reduce batch size;
3. reduce traversal count;
4. reduce buckets/action menu;
5. increase depth-limiting;
6. fail with a clear memory preflight error.

## Performance Plan For 10-15 Seconds

The RTA solver should maximize useful updates inside the decision window.

Primary target:

```text
startup/preflight:       < 1 second when cache is warm
first usable strategy:   < 3 seconds
normal solve budget:     10-15 seconds
export root strategy:    < 100 ms
```

The solver should support anytime behavior:

- after every batch, root strategy can be exported;
- stop cleanly when time budget expires;
- return best available average strategy;
- optionally spend final milliseconds normalizing/exporting only root and nearby infosets.

Runtime loop:

```text
deadline = now + budget
while now < deadline:
    run one sampled batch for player 0
    merge
    run one sampled batch for player 1
    merge
    if checkpoint interval hit:
        update root strategy snapshot
return latest root/average strategy
```

Performance optimizations:

- use `Float32` global storage and `double` local arithmetic;
- compute current strategy on demand;
- keep action rows contiguous for the preferred layout;
- use stack or arena allocation for traversal frames;
- keep row/delta buffers reusable per worker;
- avoid map lookups inside the innermost action loop;
- cache row handles after infoset lookup;
- cache legal actions per sampled public state;
- cache board-blocked bucket weights;
- cache terminal board equities by final board;
- use suit-isomorphic public chance classes;
- prune low-value/low-reach actions before solving;
- sort merge deltas by infoset id for linear merges;
- periodically compact sparse rows if many are abandoned by pruning.

Parallel performance notes:

- one sampled trajectory is serial, but many trajectories are independent;
- batch size should be large enough to amortize thread wakeup and merge cost;
- if merge exceeds 20 percent of solve time, increase local aggregation or batch size;
- if worker imbalance is high, switch from static ranges to deterministic work queue with merge sorted by trajectory id;
- if traversal is too shallow to saturate workers, increase trajectories per batch.

Practical first RTA settings:

```text
workers:                  8-16, depending on CPU
precision:                Float32 table, double scratch
sampling mode:            External
traversals per batch:     4096-16384
batch size:               64-256 trajectories per worker chunk
flop buckets:             64
turn buckets:             48
river buckets:            32
postflop raise cap:       1
all-in:                   only when SPR <= 2.5
depth limit:              street boundary or action-depth cap
terminal dense matrices:  off
public isomorphism:       on
```

## RTA Quality Strategy

We should optimize for useful decision quality, not purity.

Acceptable approximations:

- small action menus;
- bucket abstraction;
- public chance sampling;
- external sampling;
- depth-limited leaf values;
- online pruning;
- imperfect but stable average strategy;
- root-focused export.

Quality checks:

- root action probabilities are stable across nearby seeds;
- EV of chosen root action is stable across seeds;
- obvious dominated actions get low probability;
- strategy does not collapse to one action only because of sampling variance;
- turn/river subgames produce sensible lines before trusting flop;
- exploitability is measured on toy/small analogs, not full flop every time.

Use exploitative/practical evaluation:

- compare against known population heuristics or scripted opponents;
- compare root EV against exact small-tree baseline;
- run A/B tests across action abstractions;
- track regret magnitude and strategy entropy at the root;
- prefer stable high-EV choices over chasing tiny GTO differences.

## Future Blueprint Compatibility

The sampled solver should be expandable into blueprint-style generation later.

Design for two modes:

```text
online RTA mode:
  solve one current public state fast
  sparse/lazy cache
  time budget controls stopping

offline blueprint mode:
  run many public states
  persist sparse rows/checkpoints
  aggregate average strategies
  train value/leaf models
```

Required design hooks:

- serializable sampled infoset table;
- stable infoset/public-state ids;
- deterministic seeds and reproducible batches;
- ability to save/load terminal and leaf caches;
- ability to warm-start from previous solve or blueprint;
- ability to export training data for value networks:
  - public board;
  - pot/SPR/action history;
  - bucket range distributions;
  - leaf CFVs;
  - final average strategy.

Blueprint path:

1. Start with online solve only.
2. Add checkpointing for sparse tables.
3. Add batch solving of many sampled public states.
4. Train bucket/range-conditioned leaf values.
5. Use blueprint/value model as warm start for RTA.
6. Keep exact small games as regression tests.

## Real-Time CFR Notes For This Project

The Real-Time Parallel CFR paper is useful even though this plan uses MCCFR and lazy sampling.

Ideas to adopt:

- strict decision budget controls the solve;
- depth-limited solving is mandatory for large postflop trees;
- abstraction and pruning are multiplicative accelerators;
- leaf evaluation should eventually be batched;
- pipeline boundaries should be measurable;
- wall-clock exploitability/EV matters more than raw iteration count;
- CPU multithreading should be designed around the natural dependency structure.

Ideas to adapt:

- Their exact seven-stage pipeline parallelizes a full depth-limited tree by infoset and node. Our sampled solver should keep stage-level profiling, but the main parallel unit is trajectory batch.
- Their GPU leaf evaluation can become a future `HUNLSampledLeafEvaluator` backend. First version can use heuristic/table values.
- Their pruning step should become a pre-solve sampled action filter, then later a regret/value-bound pruning module.
- Their abstraction lesson maps directly to our bucket and action menus. RTA speed depends on choosing a practical action abstraction, not supporting every theoretical bet size.

Suggested sampled RTA pipeline:

```text
1. Preflight
   - validate config
   - estimate memory
   - choose exact/full-graph validation or lazy sampled mode

2. Root setup
   - build root public state
   - apply ranges
   - initialize/warm-start sparse rows
   - build legal action menu

3. Optional pruning
   - remove impossible actions
   - remove clearly dominated/low-value actions if bounds are available
   - keep fold/call/check safety actions

4. Sample batches until deadline
   - external-sampling MCCFR for both players
   - worker-local deltas
   - fixed-order merge

5. Depth-limited leaf evaluation
   - heuristic/table first
   - batched model later

6. Root export
   - normalize average strategy for root
   - include diagnostics and confidence/stability counters
```

## Programmer Hints And Traps

Implementation hints:

- Build the single-worker sampled traversal first.
- Keep sampled traversal deterministic before adding dynamic scheduling.
- Add counters from day one; otherwise variance bugs are painful.
- Keep exact `HUNLFlatDCFR` as a test oracle, not as a base class.
- Prefer row handles/views over repeated `unordered_map` lookup inside action loops.
- Store action ids compactly in sampled nodes.
- Keep debug strings behind debug/export flags.
- Make every large allocation visible in memory preflight.
- Keep `Float64` validation mode even if production is `Float32`.
- Add a `--time-budget-ms` benchmark flag early, not only `--iterations`.
- Root strategy export should not require exporting the whole sparse table.

Estimator traps:

- If chance/opponent actions are sampled, importance weights must be correct.
- Every terminal history that matters must have nonzero sampling probability.
- AS must enforce epsilon support and at least one sampled action.
- Sampling private buckets later requires careful card-blocking and range weighting.
- Updating average strategy only for visited nodes changes interpretation; document it and validate on small games.
- Comparing sampled iteration count to exact iteration count is misleading. Compare wall-clock and nodes visited.

Threading traps:

- Floating-point atomics will be nondeterministic and slow.
- Worker-local deltas can blow memory if the batch is too large.
- Dynamic scheduling can change merge order unless trajectory ids are sorted.
- RNG per worker is not enough for reproducibility; seed per trajectory.
- Thread startup can dominate tiny batches; reuse workers.

Memory traps:

- A "lazy" solver that still preallocates dense rows is not lazy.
- A "sampled" solver that builds the full flop graph first will still fail memory.
- Exporting full dense maps can exceed memory even if solving fits.
- Terminal caches can silently become the largest allocation.
- Debug strings and unordered maps are acceptable for validation, not hot production.

RTA traps:

- A perfect-looking strategy after 2 seconds may be sampling noise.
- Always track root strategy stability over recent batches.
- Do not spend the final second exporting every infoset.
- Do not chase large action menus before the core traversal is fast.
- If the solver cannot finish enough updates in 10-15 seconds, reduce actions/buckets/depth before micro-optimizing.

## Full-Graph Prototype Algorithm

The first implementation should reuse the existing full flat graph. This does not solve peak memory, but it lets us validate math against small exact games.

For each MCCFR iteration:

```text
for traversing_player in [0, 1]:
    run traversals_per_iteration sampled walks
    merge worker-local regret and average-strategy deltas
```

At a terminal node:

```text
return utility for traversing player
```

At a chance node:

```text
PublicChance mode:
    sample one chance outcome by outcome.probability
    recurse into that child

External/AS mode:
    sample one chance outcome by outcome.probability
    recurse into that child
```

At an opponent node:

```text
strategy = regret_matching(regret row)
add strategy_sum for the opponent row on this visited path
sample one opponent action by strategy
recurse into that child
```

At a traversing-player node:

```text
strategy = regret_matching(regret row)

External mode:
    evaluate every action

AverageStrategy mode:
    sample action a with rho(I, a)
    skipped action value is 0 in the sampled estimator
    sampled action values are importance corrected

node_value = sum_a strategy[a] * action_value[a]
regret[a] += counterfactual_reach * (action_value[a] - node_value)
return node_value
```

Bucket detail:

The exact solver stores each row as `bucket_count * action_count`. For the first sampled solver, do not sample private buckets. Keep bucket aggregation exact inside each visited public node:

- compute action strategy per bucket from the regret row;
- update regret per bucket using the sampled node/action values;
- update average strategy per bucket when the owner is visited;
- reuse `HUNLFlatInfosetTable::value_index()` for both layouts.

This keeps the estimator simpler and avoids adding private-hand sampling before public-chance sampling is proven.

## Production Memory Design

Full-graph MCCFR still needs:

```text
graph bytes + dense regret table + dense strategy_sum table + maybe current_strategy
```

That can still be too large for full flop.

For production large flop, add a sampled/lazy mode:

```text
include/solver/hunl_flat_sampled_state.hpp
src/solver/hunl_flat_sampled_state.cpp
include/games/hunl_flat_sampled_builder.hpp
src/games/hunl_flat_sampled_builder.cpp
```

Design:

- Build only the public skeleton needed for the current sampled trajectory batch.
- Allocate infoset rows lazily on first visit.
- Store regret and strategy sum only; compute current strategy on the fly from regret.
- Use compact `HUNLFlatBuilderMemoKey` or a new sampled key for node cache lookup.
- Evict or checkpoint rarely visited sampled public states only after correctness is stable.

Sampled memory target:

```text
global:
  graph/cache for visited public states
  sparse regret rows
  sparse strategy_sum rows
  terminal/evaluator cache by final board

per worker:
  traversal stack
  local sparse regret deltas
  local sparse strategy_sum deltas
  RNG state
  small bucket/action scratch
```

This can remove:

- dense `current_strategy_`;
- full `node_values_`;
- full `action_values_`;
- full reach arrays;
- unvisited public chance branches;
- unvisited infoset rows.

## Multithreading Plan

MCCFR parallelism should be trajectory-batch parallelism.

Do:

- pre-split each `(iteration, traversing_player)` into deterministic batch ids;
- give each worker a fixed contiguous batch range, or seed every trajectory by `(base_seed, iteration, player, trajectory_id)`;
- keep worker-local sparse deltas;
- merge deltas in fixed worker index order;
- avoid atomics for regrets and strategy sums;
- keep profiling at batch/stage level.

Worker scratch:

```cpp
struct HUNLFlatMCCFRWorkerScratch {
    PcsRng rng;
    std::vector<TraversalFrame> stack;
    std::vector<double> action_values;
    std::vector<double> strategy;
    std::vector<double> row_regrets;
    SparseInfosetDelta regrets;
    SparseInfosetDelta strategy_sum;
    HUNLFlatMCCFRWorkerProfile profile;
};
```

Merge:

```text
for worker_index in 0..workers-1:
    for delta row in worker.regrets sorted by infoset id:
        global.regret += delta
    for delta row in worker.strategy_sum sorted by infoset id:
        global.strategy_sum += delta
```

For deterministic tests, prefer static partitioning first. Dynamic work stealing can be added later if profiles show serious imbalance.

## File-By-File Implementation Map

### `include/games/hunl.hpp`

Add:

- `HUNLFlatSamplingMode`
- optional `HUNLFlatMCCFRConfig` if the config belongs at game level
- fields in `HUNLConfig` only for routing, not every solver knob:
  - `HUNLFlatSamplingMode sampling_mode = HUNLFlatSamplingMode::Exact`
  - `std::uint64_t sampling_seed = 1`

Do not reuse `use_pcs` as the main sampled mode.

### `include/util/pcs.hpp` and `src/util/pcs.cpp`

Extend the deterministic RNG utilities:

- `bool bernoulli(PcsRng&, double p)`
- `std::pair<std::size_t, double> sample_weighted_outcome(PcsRng&, const double* probs, std::size_t count)`
- `std::uint64_t mix_seed(base, iteration, player, trajectory)`

Keep existing tests in `tests/test_pcs.cpp` and add weighted sampling tests.

### `include/solver/hunl_flat_mccfr.hpp`

New solver API:

- config struct;
- profile struct;
- `run_iteration()`;
- `run_iterations()`;
- average strategy export;
- memory estimate.

### `include/solver/hunl_sampled_*.hpp` and `src/solver/hunl_sampled_*.cpp`

If we choose the new-era names, add these files instead of putting everything into one `hunl_flat_mccfr.cpp`:

- `hunl_sampled_config`: public config, defaults, validation.
- `hunl_sampled_storage`: sparse infoset rows, row views, precision handling, delta application.
- `hunl_sampled_builder`: lazy public-state expansion and state cache.
- `hunl_sampled_traversal`: public-chance, external-sampling, and AS traversal logic.
- `hunl_sampled_scheduler`: worker creation, batch partitioning, deterministic merge.
- `hunl_sampled_terminal`: fold/showdown/depth-limited sampled terminal values.
- `hunl_sampled_export`: sparse and dense average-strategy export.
- `hunl_sampled_solver`: top-level facade tying all pieces together.

Keep the interfaces narrow. The top-level solver should own the subsystems; traversal should receive references/views, not global mutable state.

### `src/solver/hunl_flat_mccfr.cpp`

Implement:

- regret matching on copied rows;
- sampled walk over `HUNLFlatSolveGraph`;
- public-chance sampling mode;
- external-sampling mode;
- worker pool or simple `std::thread` batch runner;
- fixed-order merge.

Keep it independent from `HUNLFlatDCFR::WorkerPool` at first. The exact worker pool is stage-oriented; MCCFR needs batch-oriented scheduling.

### `CMakeLists.txt`

Add new sources and headers to `texas_core`.

Add new tests to the existing test executable.

Do not remove or rename current exact solver files.

### `include/solver/hunl_flat_state.hpp` and `src/solver/hunl_flat_state.cpp`

Optional first prototype:

- reuse `HUNLFlatInfosetTable`.

Production sampled mode:

- add `HUNLFlatSparseInfosetTable` or `HUNLFlatSampledInfosetTable`;
- support row allocation by `(infoset id, bucket count, action count)`;
- store only regret and strategy sum;
- add memory estimation for sparse rows.

### `src/games/hunl_flat_builder.cpp`

For production memory mode:

- add a sampled graph builder or public-state cache;
- support lazy expansion from `HUNLState`;
- reuse existing public chance isomorphism logic;
- do not enumerate all future turn/river branches for sampled mode.

### `src/games/hunl_solver.cpp`

Route HUNL solves:

```text
sampling_mode == Exact -> HUNLFlatDCFR
sampling_mode != Exact -> HUNLSampledSolver or HUNLFlatMCCFR
```

Keep exact mode default.

### `examples/benchmarks/flat_scheduler_main.cpp`

Add:

- `--sampling exact|public-chance|external|as`
- `--sample-traversals N`
- `--sample-seed N`
- sampled memory preflight
- sampled profile output:
  - traversals/sec
  - nodes visited
  - sampled chance nodes
  - sampled opponent nodes
  - updated infosets
  - merge seconds

### `examples/benchmarks/hunl_random_flat_main.cpp`

Same CLI additions as above.

Replace the hard failure text saying sampled mode is not implemented once the sampled solver exists.

### `tests/test_hunl_flat_mccfr.cpp`

Add tests:

- deterministic seeded run gives identical average strategy for same worker count;
- deterministic seeded run gives same result for 1 vs 2 workers with static partitioning;
- public chance sampled estimate approaches exact chance value on a tiny graph;
- external sampling updates only visited infosets;
- AS keeps every action's sample probability at least epsilon;
- no negative/NaN strategy probabilities;
- export format matches exact solver shape.

### `tests/test_ranges_threading.cpp`

Add sampled worker-count tests after the core solver is stable.

## Step-By-Step Plan

### Phase -1: Greenfield Module Scaffold

1. Add the new sampled solver files:
   - `include/solver/hunl_sampled_solver.hpp`
   - `src/solver/hunl_sampled_solver.cpp`
   - `include/solver/hunl_sampled_config.hpp`
   - `src/solver/hunl_sampled_config.cpp`
   - `include/solver/hunl_sampled_storage.hpp`
   - `src/solver/hunl_sampled_storage.cpp`
   - `include/solver/hunl_sampled_traversal.hpp`
   - `src/solver/hunl_sampled_traversal.cpp`
   - `include/solver/hunl_sampled_scheduler.hpp`
   - `src/solver/hunl_sampled_scheduler.cpp`
   - `include/solver/hunl_sampled_builder.hpp`
   - `src/solver/hunl_sampled_builder.cpp`
   - `include/solver/hunl_sampled_terminal.hpp`
   - `src/solver/hunl_sampled_terminal.cpp`
   - `include/solver/hunl_sampled_export.hpp`
   - `src/solver/hunl_sampled_export.cpp`
2. Add empty or minimal classes with compile-time tests.
3. Register files in `CMakeLists.txt`.
4. Add initial tests:
   - sampled config validates defaults;
   - sampled storage can allocate one row;
   - sampled scheduler partitions trajectory ids deterministically.
5. Keep `HUNLFlatDCFR` untouched.

Acceptance:

- project builds;
- new empty-module tests pass;
- exact solver tests still pass;
- no solve path routes to sampled solver yet.

### Phase 0: Sampling Configuration And RNG

1. Add `HUNLFlatSamplingMode`.
2. Add `HUNLFlatMCCFRConfig`.
3. Extend `PcsRng` with Bernoulli, weighted sampling, and seed mixing.
4. Add unit tests for deterministic weighted sampling.
5. Add benchmark CLI flags but keep them returning "not implemented" until solver phase lands.

Acceptance:

- existing tests pass;
- new RNG tests pass;
- no exact solver behavior changes.

### Phase 1: Full-Graph Public-Chance Sampling Prototype

1. Add `HUNLFlatMCCFR` with single-worker execution.
2. Reuse `HUNLFlatSolveGraph` and `HUNLFlatInfosetTable`.
3. At chance nodes, sample one outcome by probability.
4. At player nodes, traverse all actions.
5. Update regret and strategy sums using existing row layout helpers.
6. Export average strategy through the same shape as exact solver.

Acceptance:

- tiny graph expected values converge toward exact with increasing samples;
- same seed gives identical output;
- no memory-lazy requirement yet.

### Phase 2: External-Sampling MCCFR

1. Add traversing-player walk.
2. At opponent nodes, sample one action from current strategy.
3. At traversing-player nodes, branch over all actions.
4. Run one batch for player 0 and one batch for player 1 per iteration.
5. Add counters for sampled opponent actions and traversing-player action expansions.

Acceptance:

- Kuhn/Leduc or tiny HUNL converges in direction of exact strategy;
- external sampling visits fewer nodes than public-chance sampling;
- no NaNs in regret/strategy rows.

### Phase 3: Multithreaded Sample Batches

1. Add worker scratch and profiles.
2. Split trajectory ids statically across workers.
3. Seed each trajectory from `(base_seed, iteration, traversing_player, trajectory_id)`.
4. Store worker-local sparse deltas.
5. Merge in worker index order.
6. Add benchmark profile output.

Acceptance:

- 1-worker and N-worker seeded runs match within strict tolerance when static partitioning is used;
- sampled solver scales with worker count on benchmark turn/flop presets;
- merge time is visible and not dominant.

### Phase 4: Sparse Infoset Storage

1. Add sparse sampled table with regret and strategy sum only.
2. Allocate rows on first visit.
3. Compute current strategy on demand.
4. Add memory estimation for sparse storage.
5. Keep dense `HUNLFlatInfosetTable` as an optional validation backend.

Acceptance:

- sampled memory is proportional to visited infosets;
- export works for sparse rows;
- repeated seeded runs are deterministic.

### Phase 5: Lazy Public-State Expansion

1. Add a sampled/lazy flat builder.
2. Cache nodes by compact state key.
3. Expand only children needed by the current sampled traversal.
4. Keep public chance isomorphism for sampled chance choices.
5. Do not build all turn/river branches upfront.

Acceptance:

- large flop sampled mode starts under the memory guardrail;
- graph/cache memory grows with samples, not full tree size;
- exact flat builder remains unchanged for exact mode.

### Phase 6: Average Strategy Sampling

1. Add AS mode after external sampling is stable.
2. Compute `rho(I, a)` from strategy sum:

```text
rho = max(epsilon, (as_beta + as_tau * strategy_sum[a]) / (as_beta + row_strategy_sum))
rho = min(1, rho)
```

3. Sample traversing-player action subset with Bernoulli draws.
4. Importance-correct sampled action values.
5. Guarantee at least one action is sampled per traversing-player infoset.
6. Add AS-specific profile counters:
   - actions considered;
   - actions sampled;
   - average sample ratio;
   - forced-at-least-one count.

Acceptance:

- AS never gives zero support to any action because epsilon is enforced;
- AS converges on toy games;
- AS improves nodes-per-update only when action count is meaningfully large.

### Phase 7: Sampled DCFR Discounting

1. Keep vanilla regret matching as the baseline.
2. Add optional sampled DCFR discounting.
3. Discount only visited rows first.
4. Add `last_discount_iter` handling for sparse rows.
5. Compare vanilla MCCFR vs sampled DCFR on small games.

Acceptance:

- sampled DCFR does not destabilize small tests;
- profile shows discounting is not a bottleneck;
- config defaults remain conservative.

### Phase 8: Variance Reduction

1. Add optional baseline values per node/infoset/action.
2. Start with simple moving-average baselines.
3. Later use depth-limited value tables or terminal board cache values.
4. Track estimator variance in benchmark output.

Acceptance:

- variance counters decrease;
- wall-clock-to-exploitability improves;
- baseline memory stays bounded.

### Phase 9: RTA Deadline Mode

1. Add `run_until(deadline)` or `solve_for(std::chrono::milliseconds budget)`.
2. Add `--time-budget-ms` to benchmarks.
3. Export root strategy without materializing the full strategy map.
4. Add periodic root-strategy snapshots.
5. Add stability diagnostics:
   - root action entropy;
   - root action probability delta over recent batches;
   - batches completed;
   - sampled nodes visited;
   - unique infosets touched;
   - memory used.
6. Return the latest complete average strategy snapshot if deadline hits mid-batch.

Acceptance:

- conservative turn/river solves return in <= 10 seconds;
- conservative flop solves return in <= 15 seconds;
- root export stays under 100 ms;
- timeout never corrupts global sparse storage.

### Phase 10: 64 GB Guardrails

1. Add sampled memory preflight.
2. Add live memory counters:
   - public states cached;
   - infoset rows allocated;
   - sparse values allocated;
   - terminal cache bytes;
   - worker delta bytes;
   - export bytes.
3. Add hard caps and fail-fast errors.
4. Add adaptive fallback:
   - reduce batch size;
   - reduce traversal count;
   - disable AS;
   - lower bucket preset;
   - increase depth limiting;
   - reject unsafe config.
5. Add benchmark output that prints the 64 GB budget categories.

Acceptance:

- sampled solver refuses configs estimated above 60 GB;
- sampled solver warns above 48 GB;
- production sampled mode never allocates full flop graph plus dense full table by accident;
- tests cover cap-triggered failures.

### Phase 11: Blueprint Expansion Hooks

1. Add save/load for sparse sampled infoset table.
2. Add deterministic public-state ids.
3. Add checkpoint metadata:
   - abstraction version;
   - action abstraction;
   - bucket counts;
   - seed schedule;
   - iteration/batch count.
4. Add offline batch public-state solving tool.
5. Export training rows for leaf/value model.
6. Add warm-start from saved blueprint rows.

Acceptance:

- online RTA mode remains fast;
- blueprint save/load roundtrips exactly in validation mode;
- warm-start improves first-second root strategy stability.

## Benchmark Plan

Run exact and sampled against the same small config first:

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R "pcs|hunl_flat_mccfr|hunl_flat_dcfr|ranges_threading" --output-on-failure
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 1 --iterations 10 --sampling public-chance --sample-traversals 1024 --seed 7 --precision float
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 8 --iterations 10 --sampling external --sample-traversals 8192 --seed 7 --precision float
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 8 --sampling external --time-budget-ms 15000 --seed 7 --precision float
```

Track:

- wall-clock per sampled iteration;
- traversals/sec;
- nodes visited/sec;
- unique infosets touched;
- regret rows allocated;
- memory peak/preflight;
- merge seconds;
- worker imbalance;
- exploitability on toy and small HUNL only.

For large flop, do not require exact exploitability at first. Use:

- root strategy stability across seeds;
- EV stability across seeds;
- exploitability on smaller structurally similar subgames;
- monotonic improvement in best-response proxy if exact BR is too expensive.

## Practical First Target

First working target:

```text
mode: External MCCFR
graph: full graph for validation
storage: dense HUNLFlatInfosetTable
workers: static trajectory batches
precision: Float32 table, double local arithmetic
traversals_per_iteration: 8192 or 16384
action sampling: off
private bucket sampling: off
```

First production target:

```text
mode: External MCCFR
graph: lazy sampled public-state expansion
storage: sparse regret + sparse strategy_sum
workers: static trajectory batches
precision: Float32 table, double local arithmetic
public chance: sampled with suit-isomorphic class probabilities
action sampling: optional AS only after external mode is validated
```

## Final Recommendation

Implement MCCFR, but do it as a new sampled flat solver. Use the existing exact `HUNLFlatDCFR` as the correctness oracle and keep it unchanged.

The best first implementation is not Average Strategy Sampling. It is external-sampling MCCFR with public chance sampling, deterministic worker batches, and dense-table validation. After that works, add sparse/lazy storage to actually solve the memory problem. Then add Average Strategy Sampling if the profile shows player action branching is still expensive enough to justify the extra estimator variance.
