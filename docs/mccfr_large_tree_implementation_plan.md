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
- DeepStack: Expert-Level Artificial Intelligence in No-Limit Poker, Moravcik et al., 2017: https://arxiv.org/abs/1701.01724
- Depth-Limited Solving for Imperfect-Information Games, Brown/Sandholm/Amos, 2018: https://arxiv.org/abs/1805.08195
- Value Functions for Depth-Limited Solving in Zero-Sum Imperfect-Information Games, Kovarik et al., 2019: https://arxiv.org/abs/1906.06412
- DecisionHoldem: Safe Depth-Limited Solving With Diverse Opponents for Imperfect-Information Games, Zhou et al., 2022: https://arxiv.org/abs/2201.11580
- Pluribus / Superhuman AI for multiplayer poker summary: https://en.wikipedia.org/wiki/Pluribus_(poker_bot)
- Libratus summary and match details: https://en.wikipedia.org/wiki/Libratus
- OpenSpiel open-source game/research framework: https://github.com/google-deepmind/open_spiel
- PokerSkill public poker-AI repository and evaluation notes: https://github.com/lbn187/PokerSkill

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

## Product Context And Real Problem

The product is a high-performance HUNL postflop solving engine. It should be usable as:

- an offline research solver;
- a benchmark tool for comparing abstractions and algorithms;
- a real-time local re-solving engine for current public states;
- a data generator for future value networks and blueprint-style strategies.

This document intentionally scopes the work to solver technology, offline evaluation, controlled benchmarks, and research. It should not include client automation, screen scraping, stealth botting, online-room integration, anti-detection logic, or instructions for violating poker-site rules. The engineering target is still real-time strength: given a current public state, ranges, stack/pot/action context, and legal action abstraction, return a strong root strategy inside a strict time budget.

Real-time poker solving is hard because every decision is an imperfect-information subgame:

- the solver does not know private cards exactly;
- both players have ranges, not single hands;
- actions reveal information and alter ranges;
- bet sizing creates enormous branching;
- turn/river runouts create a public-chance explosion;
- exact GTO quality is far beyond a 10-15 second desktop budget;
- a solver that pages memory or touches the full flop tree loses the decision window.

The practical product is not "solve poker completely now." The product is:

```text
given one public poker state:
  build a bounded abstract lookahead
  preserve range and blocker correctness
  sample the most useful future branches
  update strategy as much as possible before deadline
  return a stable root action mix with diagnostics
```

This is why memory and performance are product requirements, not implementation details. A theoretically beautiful solver that allocates 90 GB or spends 18 seconds per exact iteration is not usable for real-time decisions on a 64 GB desktop.

## Real-Time Solver Lifecycle

In a controlled/offline setting, an RTA-style poker solver is just a strict-deadline re-solver.

Input:

- current public board;
- street;
- pot and stack state;
- action history;
- legal actions or action-abstraction preset;
- player to act;
- estimated ranges for both players;
- bucket abstraction;
- time budget;
- optional warm start.

Processing:

1. validate state and ranges;
2. build or find root public state;
3. project ranges through blockers;
4. choose action abstraction;
5. run sampled/depth-limited MCCFR batches until deadline;
6. periodically snapshot root strategy;
7. stop cleanly at budget;
8. export root action probabilities and diagnostics.

Output:

- root action mix;
- optional action values;
- confidence/stability metrics;
- memory and time profile;
- fallback/timeout status;
- optional training labels if running offline.

The product loop is therefore:

```text
state in -> bounded solve -> root strategy out
```

It is not:

```text
state in -> full game-tree solve -> full strategy export
```

This distinction should guide every implementation choice.

## Why Approximate Near-GTO Can Be Enough

We do not need a mathematically exact Nash equilibrium to build a strong poker engine.

Public evidence:

- DeepStack defeated professional HUNL players with statistical significance while using continual re-solving, sparse action lookahead, depth limits, and learned counterfactual value estimates rather than a full exact game solve.
- DeepStack used restricted actions and depth-limited lookahead; its paper reports sparse lookahead games around `10^7` decision points solved in under five seconds on a GTX 1080-class machine.
- DeepStack trained value networks from solved random poker situations: 10 million random turn games and 1 million random flop games. This supports our plan to generate many solved subgames for future neural leaf/value training.
- Libratus beat top HUNL professionals using blueprint solving, nested/endgame solving, and self-improvement; it was not simply a full exact solve of no-limit hold'em at decision time.
- Pluribus beat elite players in multiplayer no-limit hold'em with a blueprint and real-time search, and its public summaries emphasize that it used practical approximations rather than exact Nash guarantees for multiplayer poker.
- DecisionHoldem reports strong results using safe depth-limited subgame solving and explicit opponent range handling, including wins against Slumbot and OpenStack in its experiments.
- The Real-Time Parallel CFR paper explicitly frames strength as the number of useful CFR iterations completed inside a few-second decision budget, with pruning, abstraction, depth limiting, and parallelism as core ingredients.

Engineering conclusion:

```text
good abstraction + fast re-solving + stable ranges + enough samples
can beat many weaker strategies
without exact full-game Nash equilibrium
```

This does not mean approximation is free. Bad abstraction can be highly exploitable. The solver must track stability, avoid obviously dominated actions, preserve range/blocker semantics, and keep validation mode for small exact comparisons.

For lower-stakes or weaker-player environments, the bar is even more practical:

- avoid major strategic mistakes;
- avoid obvious overfold/overbluff patterns;
- select reasonable bet sizes;
- preserve mixed strategies enough to avoid being predictable;
- exploit population leaks only in explicit exploitative modes;
- return a robust root action under time pressure.

The strongest useful system is probably a hybrid:

```text
offline:
  solve many subgames
  train value/leaf networks
  build warm-start blueprint rows

online/current-state:
  load/warm-start
  run sampled depth-limited MCCFR
  export root strategy before deadline
```

The user's idea of generating a very large number of solved or near-solved subgames for a neural network is aligned with DeepStack-style value training. The document should treat "1 billion games" as an aspirational data scale, not a single monolithic solve. The path is many small bounded subgames, deterministic metadata, compressed training rows, and strict quality filters.

## Lessons From Public Poker AI And Open Repos

Public systems and repositories show useful patterns:

- OpenSpiel is a research framework with C++ core APIs and Python bindings. It is useful as a reference for clean game abstractions, CFR variants, tests, and reproducible experiments, but not as a drop-in high-performance HUNL RTA engine.
- DeepStack shows the importance of continual re-solving, sparse lookahead actions, depth limits, and learned counterfactual values.
- Libratus shows the strength of blueprint plus subgame/endgame solving and post-hoc improvement of weak lines.
- Pluribus shows that imperfect but strong approximations can beat elite humans in complex poker settings, especially when combined with blueprint strategy and real-time search.
- DecisionHoldem shows that safe depth-limited solving with opponent range modeling is a practical open research direction.
- PokerSkill is not a solver architecture to copy for this project, but it is evidence that even structured action grounding and reasonable poker abstractions can produce much stronger decisions than naive play.

What not to copy:

- generic Python-first loops for the production hot path;
- string-heavy state representations;
- per-node object graphs;
- broad framework abstractions inside inner traversal;
- code that is easy to read but allocates on every action or node.

What to copy conceptually:

- tiny reproducible game tests;
- explicit game/state/action APIs;
- solver variants hidden behind stable interfaces;
- benchmark scripts with reproducible seeds;
- clear separation of game rules, abstraction, solving, evaluation, and export.

## Core Hard Problems And Deep Rocks

These are the places where the implementation can quietly fail even if the code compiles.

### 1. State Explosion

No-limit hold'em explodes in three dimensions:

- public cards;
- private hand/range combinations;
- bet/action histories.

The solver must attack all three:

- public cards: sample chance and use suit isomorphism;
- private hands: use buckets/ranges and only consider private sampling later;
- actions: use tight action abstraction and progressive widening.

### 2. Memory Explosion

Memory fails before compute if we allocate:

- full graph;
- dense full infoset table;
- dense current strategy;
- dense reach arrays;
- dense terminal matrices;
- per-worker graph-sized scratch;
- full strategy export.

Every array must answer:

```text
is this needed for the current sampled batch?
can it be sparse?
can it be recomputed?
can it be per-worker scratch instead of global?
can it be root-only for RTA export?
```

### 3. Variance

MCCFR is noisy. A fast bad estimate is not useful.

Mitigations:

- external sampling before outcome sampling;
- enough trajectories per batch;
- stratified public chance;
- delayed averaging;
- root stability counters;
- seed A/B tests;
- conservative exploration floors;
- validation on small exact games.

### 4. Range Correctness

Ranges are the heart of poker solving. A fast solver with wrong blockers is garbage.

Rules:

- public board must zero impossible private combos;
- action updates must preserve bucket/range semantics;
- terminal equity must use valid blocked ranges;
- depth-limited leaf values must receive the correct range summary;
- any private bucket sampling must use importance correction.

### 5. Abstraction Leakage

Abstraction makes solving possible, but can create strategic holes.

Guardrails:

- keep safety actions;
- do not over-merge strategically different bet sizes;
- validate on small exact analogs;
- test multiple action menus;
- keep exploitative/population modes separate from unbiased validation mode.

### 6. Real-Time Deadline Pressure

Deadline behavior must be designed, not bolted on.

Rules:

- each batch has a bounded maximum time;
- export root strategy is cheap;
- timeout returns last complete snapshot;
- no final full-table normalization;
- no surprise allocation during the final second.

### 7. CPU Cache And Memory Bandwidth

The 64 GB RAM budget is not the performance budget. RAM is slow compared with cache. A solver that randomly walks tens of GB per second will bottleneck on memory bandwidth and cache misses.

Design for locality:

- flat arrays;
- action-major contiguous rows;
- compact ids;
- sorted merges;
- row handles cached inside traversal frames;
- per-worker scratch reused;
- no pointer-heavy object graph in hot traversal;
- no strings/hash allocations in inner loops.

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
- Do not spend the first implementation pass on SIMD while the solver is still graph-bound, allocation-bound, or map-lookup-bound.
- Do not claim a 64 GB target is met until peak memory is measured during lazy build, solve, merge, terminal evaluation, and root export.

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

include/solver/hunl_sampled_simd.hpp
src/solver/hunl_sampled_simd.cpp

include/solver/hunl_sampled_profile.hpp
src/solver/hunl_sampled_profile.cpp

tests/test_hunl_sampled_solver.cpp
tests/test_hunl_sampled_storage.cpp
tests/test_hunl_sampled_traversal.cpp
tests/test_hunl_sampled_scheduler.cpp
tests/test_hunl_sampled_builder.cpp
tests/test_hunl_sampled_terminal.cpp
tests/test_hunl_sampled_simd.cpp
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
- `HUNLSampledSimdKernels`
- `HUNLSampledProfiler`

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

### 10. SIMD Row Kernels

Purpose:

- accelerate the bucket/action row math after sampled architecture is working;
- keep scalar and SIMD implementations testable against each other;
- prevent intrinsics from leaking across traversal, storage, and terminal code.

Suggested files:

```text
include/solver/hunl_sampled_simd.hpp
src/solver/hunl_sampled_simd.cpp
tests/test_hunl_sampled_simd.cpp
```

Responsibilities:

- regret matching for action-major bucket rows;
- average strategy accumulation;
- regret delta application;
- row copy/fill/add/dot kernels;
- optional terminal bucket weighted sums;
- runtime or compile-time dispatch.

Rules:

- scalar reference path is mandatory;
- SIMD path must be optional;
- SIMD tests must compare against scalar;
- no solver correctness should depend on a specific CPU instruction set.

### 11. Profiling And Telemetry

Purpose:

- make memory and performance regressions obvious;
- make the 10-15 second target measurable;
- let us choose algorithmic optimization based on real bottlenecks.

Profiler should collect:

- build/cache time;
- traversal time;
- terminal/leaf time;
- merge time;
- export time;
- worker imbalance;
- sparse row/value counts;
- public-state cache count;
- worker delta peak bytes;
- terminal cache bytes;
- peak resident memory;
- root strategy stability.

The profiler should be cheap enough to leave enabled in benchmark/RTA builds.

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

## Target Hardware: Ryzen 9 9950X3D Class Desktop

The practical target machine is a Ryzen 9 9950X3D-class desktop:

```text
cores/threads:        16 cores / 32 threads
L3 cache:             about 128 MB total
RAM target:           64 GB
OS target:            Windows first
build target:         C++ Release, AVX2-capable
decision budget:      10-15 seconds
```

Important CPU reality:

- The L3 cache is not the same as RAM. It is a small hot working-set accelerator.
- 128 MB total L3 does not mean every worker can freely share 128 MB with uniform latency.
- X3D desktop CPUs can have asymmetric CCD/cache topology. Benchmark and optionally pin worker groups instead of assuming all cores behave identically.
- SMT threads can help when the solver stalls on memory, but can hurt if the hot loop is compute/SIMD bound. Benchmark physical cores first, then add SMT.
- Windows scheduling can move threads between CCDs. Add an optional affinity/worker-placement benchmark mode later.

9950X3D design goal:

```text
keep each worker's hot scratch tiny
keep shared hot rows contiguous
keep minibatch working set cache-resident when possible
stream sparse rows predictably
avoid random pointer chasing across tens of GB
```

Suggested first worker counts:

```text
validation:       1 worker
first parallel:   4 workers
cache-aware:      8 workers, one CCD-sized group
full CPU:         16 workers
SMT experiment:   24-32 workers only if benchmark improves
```

Do not assume "more threads = faster." For sampled MCCFR the common bottlenecks are:

- random sparse row lookup;
- terminal cache misses;
- worker delta memory growth;
- merge bandwidth;
- allocator contention;
- branch misprediction in traversal;
- cache misses from pointer-heavy state objects.

The solver should report speed by worker count and pick a default from data.

## C++ Hot-Loop Engineering Rules

The code should be clear and extensible, but the traversal and row-update hot paths must be data-oriented.

Strict hot-loop bans:

- no `std::string`;
- no string formatting;
- no heap allocation;
- no `new` / `delete`;
- no `std::shared_ptr`;
- no `std::function`;
- no virtual dispatch;
- no logging;
- no exceptions as control flow;
- no `unordered_map` lookup inside per-action/per-bucket loops;
- no growing `std::vector::push_back` unless capacity was reserved and the loop is not per-bucket/per-action critical.

Allowed in hot loops:

- raw pointers as non-owning views;
- `std::span` if available and optimized well;
- fixed-size stack arrays for small action counts;
- preallocated worker scratch buffers;
- index-based handles;
- pointer arithmetic inside tested row kernels;
- `std::array` for tiny fixed action menus;
- `std::vector` only as an owning buffer allocated/reserved outside the hot loop.

Ownership rule:

```text
own memory with RAII containers at subsystem boundaries
pass raw pointer/span views into hot kernels
never transfer ownership with raw pointers
avoid shared ownership in solver internals
```

Good pattern:

```cpp
struct RowView {
    float* regret = nullptr;
    float* strategy_sum = nullptr;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
};

void update_row(RowView row, WorkerScratch& scratch) {
    float* strategy = scratch.strategy.data();
    regret_matching_action_major_f32(
        row.regret,
        row.action_count,
        row.bucket_count,
        strategy);
}
```

Bad pattern:

```cpp
std::string key = make_infoset_key(state);
auto row = table[ key ];
std::vector<float> strategy;
for (...) {
    strategy.push_back(...);
    log_debug(...);
}
```

The bad pattern is readable, but it destroys the 10-15 second target.

### Flat Arrays And Handles

Flat arrays are preferred because the CPU can prefetch contiguous memory and SIMD can operate on adjacent values.

Prefer:

```text
std::vector<float> regrets;
std::vector<float> strategy_sum;
std::vector<RowMeta> rows;
RowId row_id;
offset = row_meta[row_id].regret_offset;
```

Avoid in production hot storage:

```text
std::vector<std::unique_ptr<Row>>
std::unordered_map<std::string, Row>
std::map<Key, Row>
nested vectors per row
per-node polymorphic objects
```

Use ids:

- `PublicStateId`;
- `InfosetId`;
- `RowId`;
- `ActionId`;
- `BucketId`;
- `TrajectoryId`.

Ids should be integer types, compact, serializable, and validatable.

### Allocation Strategy

Production allocation should happen in controlled places:

- solver construction;
- root setup;
- lazy public-state expansion;
- sparse row first-visit allocation;
- worker batch setup;
- checkpoint/export outside the deadline-critical path.

Use:

- reserved vectors for global append-only arrays;
- monotonic arenas for temporary public-state expansion if useful;
- worker-local scratch with fixed maximum capacity;
- delta buffers with hard byte caps;
- explicit `clear_keep_capacity()` behavior;
- allocation counters in profile.

Do not use a general allocator in the inner traversal. If allocation shows up in a profile after warmup, treat it as a bug.


### Padding And False Sharing

False sharing can erase multithreaded gains.

Use cache-line padding for:

- per-worker profile counters;
- per-worker RNG state;
- per-worker delta buffer metadata;
- frequently written stop/deadline flags;
- any hot atomic counters if unavoidable.

Avoid padding every row blindly. Padding sparse rows can waste gigabytes. Prefer aligned slab starts plus scalar tail handling.

Rule:

```text
pad worker-owned hot metadata
align large numeric arrays
do not pad millions of small sparse rows unless benchmark proves it
```

### Branches And Legal Actions

Branch-heavy action logic should be moved out of row loops.

Do:

- compute legal action menu once per public state;
- store compact action descriptors;
- precompute whether action is fold/call/check/bet/raise/all-in;
- precompute contribution deltas;
- keep terminal/fold checks outside bucket loops where possible.

Do not parse action strings or recompute legal action lists inside traversal.

### Hash Maps

Hash maps are acceptable for cold lookup and lazy expansion. They are not acceptable inside per-bucket row math.

Use a two-level model:

```text
cold path:
  key -> id lookup in hash map

hot path:
  id -> array offset
```

Cache row handles in traversal frames once resolved. If the same trajectory revisits the same public prefix, it should not repeat expensive key construction.

### Logging And Diagnostics

Diagnostics are required, but must be decoupled from hot loops.

Do:

- increment numeric counters;
- sample occasional debug events behind a compile-time or runtime flag;
- format logs after a batch;
- export detailed traces only in validation mode.

Do not:

- format strings per node;
- write files per traversal;
- log from worker inner loops;
- store debug names in every node/row.

## Implementation Contract For Future Programmers

This section is the short contract a future implementer should treat as non-negotiable.

The new sampled solver is successful only if all of these are true:

- it can return a root decision from the current public state inside a strict wall-clock budget;
- it does not require building the full flop graph in production sampled mode;
- it does not require allocating dense full-tree regret, average strategy, reach, node value, or action value arrays;
- it keeps every major allocation visible in memory preflight and live diagnostics;
- it can run with multiple worker threads without floating-point atomics in the traversal hot path;
- it supports ranges, buckets, and depth-limited leaves from the first production path;
- it can export only the root strategy without materializing every sparse row;
- it keeps exact `HUNLFlatDCFR` available as a small-game oracle and regression baseline.

Definition of done for the first production RTA version:

```text
mode:                    external-sampling MCCFR
state expansion:         lazy sampled public-state cache
storage:                 sparse regret + sparse average strategy
threading:               worker-local trajectories + fixed-order merge
precision:               Float32 global values, double local scratch
deadline mode:           solve_for(10-15s)
root export:             no full dense export
memory hard ceiling:     56 GB resident memory
memory warning:          48 GB resident memory
fallback behavior:       reduce batch/actions/buckets/depth or fail clearly
```

Pull request acceptance gates:

- a sampled config that estimates above 60 GB must be rejected before allocation;
- a sampled config that estimates above 48 GB must print an explicit warning;
- deterministic mode must reproduce identical output for the same seed and worker count;
- static scheduling mode should match 1-worker vs N-worker results within a documented tolerance;
- sampled mode must report traversals/sec, nodes/sec, unique infosets, sparse values, merge time, terminal time, and peak memory;
- timeout must return the latest complete root snapshot and leave global storage consistent;
- benchmarks must include at least one 10-second and one 15-second wall-clock run.

## SIMD And Data-Oriented Optimization

SIMD is useful for this solver, but it is not the first fix. The first fix is algorithmic: lazy public-state expansion, sparse infoset rows, sampled traversal, and bounded exports. SIMD should be added after profiling shows the solver spends meaningful time in contiguous bucket/action row math.

Expected SIMD payoff:

- high payoff: regret matching over bucket rows, average strategy accumulation, regret delta application, terminal bucket equity loops, range projection over buckets;
- medium payoff: delta merge, row zero/fill/copy, dot products for action values, board-blocked bucket masks;
- low payoff: unordered map lookups, RNG, branch-heavy state expansion, legal action generation.

Rule of thumb:

```text
if row math is < 30 percent of runtime:
    fix architecture, caching, allocation, or batching first
if row math is >= 30 percent of runtime:
    add SIMD kernels for the hottest row operations
```

Preferred production row layout:

```text
row[action][bucket]
index = action * bucket_count + bucket
```

This matches an action-major layout such as `InfosetActionHand`. It lets a vector lane operate across consecutive buckets for the same action:

```text
load action 0 buckets b..b+lane
load action 1 buckets b..b+lane
load action 2 buckets b..b+lane
sum positive regrets per lane
write strategy[action][b..b+lane]
```

This is usually better than bucket-major layout because action count is small and bucket count is 32-128. Vectorizing across buckets gives more work per instruction.

Bucket counts should prefer SIMD-friendly values:

```text
good: 32, 48, 64, 96, 128
avoid: arbitrary odd counts unless required by abstraction quality
```

Do not add large per-row padding unless measured. Align row starts and use scalar tail handling. Current proposed bucket counts are already multiples of 8 or 16, which fits AVX2/AVX-512 well.

Add a small SIMD abstraction rather than scattering intrinsics everywhere:

```text
include/solver/hunl_sampled_simd.hpp
src/solver/hunl_sampled_simd.cpp
tests/test_hunl_sampled_simd.cpp
```

Core kernels:

```cpp
void regret_matching_action_major_f32(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy);

void accumulate_average_strategy_action_major_f32(
    const float* strategy,
    const float* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float scale,
    float* strategy_sum);

void add_regret_delta_action_major_f32(
    const float* action_values,
    const float* node_values,
    const float* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* regret);

void saxpy_f32(std::uint32_t n, float alpha, const float* x, float* y);
double dot_f32_f64_accum(std::uint32_t n, const float* x, const float* y);
```

Implementation order:

1. Keep scalar reference kernels.
2. Add tests that compare SIMD vs scalar bitwise or with tiny tolerance.
3. Add compile-time detection for available instruction sets.
4. Add runtime dispatch only if builds need to run on mixed CPUs.
5. Profile each kernel independently before enabling it in the solver.

Suggested instruction-set policy:

- baseline: scalar portable C++;
- first optimized target: AVX2 + FMA;
- optional later target: AVX-512 if the deployment CPU supports it and downclocking does not erase the gain.

Do not make AVX-512 required. Many consumer desktops either lack it or reduce frequency enough that AVX2 can be faster in wall-clock terms.

SIMD details that matter:

- use 64-byte alignment for large value arrays when practical;
- keep row starts aligned, but avoid wasteful padding for every small row;
- process bucket blocks in vector-width chunks and handle tails scalar;
- use `restrict`-style assumptions where the compiler supports them;
- avoid virtual calls inside row kernels;
- avoid denormals by clamping tiny probabilities or enabling flush-to-zero if profiling shows denormal stalls;
- keep local accumulation in `double` where value quality matters, but store global regret/average strategy in `float`;
- avoid false sharing when multiple workers write scratch buffers by aligning worker scratch to cache lines.

SIMD should also be used in terminal and leaf evaluation:

- vectorize bucket equity lookup and weighted sums;
- evaluate all buckets for one action in contiguous blocks;
- batch leaf requests by public board or evaluator key;
- keep terminal cache output in action-major bucket arrays when possible.

Expected realistic outcome:

```text
row-kernel speedup:      2x-8x depending on CPU and bucket count
whole-solver speedup:    limited by sampled traversal, cache misses, builder work, and merge cost
```

SIMD is a multiplier after the solver is sparse and cache-friendly. It is not a substitute for sampling and lazy allocation.

## Algorithmic Optimization Menu

These optimizations are more important than micro-optimizing instruction count. Add them in the order profiles justify them.

### 1. External Sampling First

External sampling is the baseline production estimator:

- sample chance;
- sample opponent actions;
- branch all traversing-player actions;
- update only visited traversing-player infosets;
- update average strategy along visited paths.

It gives a good balance between memory, variance, and implementation difficulty.

### 2. Public Chance Scheduling

Naive chance sampling can waste batches by revisiting the same runouts too often. Add chance scheduling after basic sampling works:

- sample public chance by suit-isomorphic outcome classes;
- stratify batches across turn/river classes;
- avoid duplicate public runouts inside one worker minibatch when possible;
- track runout coverage per solve;
- bias exploration toward high-probability and high-impact runouts, with importance correction when needed.

For RTA, stratified public chance sampling can improve stability faster than simply increasing traversal count.

### 3. Prefix Batching

Many trajectories share prefixes from the root. Group work to reuse cache:

- group trajectories by first sampled chance outcome;
- group by root action when traversing player branches;
- process minibatches that share public board and legal action menu;
- reuse row handles and terminal evaluator inputs within the group.

This reduces repeated lazy-builder lookups, board decoding, bucket projection, and terminal cache misses.

### 4. Root-Focused Resolving

The RTA solve only needs the current decision to be strong. It should not spend equal effort everywhere.

Do:

- focus updates near the current root;
- export root and immediate child strategy first;
- spend more samples on high-reach branches;
- reduce sample budget for lines that are unreachable under both current ranges;
- use smaller/deeper abstraction only where the current decision can realistically go.

Do not:

- export the full strategy map during a timed decision;
- spend time refining distant low-reach river branches if root action mix is still unstable.

### 5. Action Abstraction And Progressive Widening

Action count is a direct multiplier for traversing-player branches and row size.

RTA preset should start small:

```text
flop:  check/call, fold, small bet/raise, large bet/raise, all-in only at low SPR
turn:  check/call, fold, one or two sizes, all-in by SPR rule
river: check/call, fold, value size, bluff size, all-in by SPR rule
```

Progressive widening:

1. Start with a tiny safe action menu.
2. Run early batches.
3. Add extra sizes only if root EV/action regret suggests they matter.
4. Keep fold/check/call legal safety actions.

This is allowed because the goal is strong RTA play, not full dense-action GTO.

### 6. Regret-Based And Value-Based Pruning

Pruning can be a major speedup, but it must be conservative.

Candidate pruning rules:

- skip actions with very negative cumulative regret for a cooldown window;
- prune actions dominated by simple value bounds;
- prune raise sizes that are illegal, redundant, or nearly identical after abstraction rounding;
- keep a minimum exploration probability for actions that can become relevant again;
- periodically unprune to avoid permanent early-sample mistakes.

RTA-safe rule:

```text
never prune all aggressive actions
never prune fold/call/check safety actions
never permanently prune from the first few batches
```

### 7. Lazy Discounting For Sparse DCFR

If DCFR-style discounting is added, do it lazily:

```text
row stores last_discount_iter
when row is visited:
    apply all missed discount factors to that row
    update last_discount_iter
```

Never run a global discount pass over all sparse rows every iteration. That would recreate an exact-solver style full sweep.

### 8. Delayed Averaging And Linear Weighting

Average strategy can be noisy early. Add configurable averaging:

- no averaging for first `warmup_batches`;
- linear CFR-style weight after warmup;
- optional higher weight for later batches inside a 15-second solve;
- root snapshot should report how many batches contributed to average strategy.

This often improves RTA stability because early random samples do not dominate the exported strategy.

### 9. Variance Reduction Baselines

After correctness is stable, add baselines:

- moving-average value baseline per public state;
- baseline per infoset/action when memory allows;
- terminal board equity baseline;
- depth-limited leaf baseline from value table or future model.

Baselines can reduce sample variance, but they add memory and complexity. They should be optional and capped.

### 10. Importance Weight Control

Correct MCCFR needs correct importance weights. For pure validation mode, do not clip or bias weights.

For RTA exploitative mode, optional biased stabilization may be acceptable if it improves decisions:

- cap extreme weights;
- floor tiny sampling probabilities;
- mix current strategy with exploration policy;
- report that the mode is biased and not an unbiased MCCFR estimator.

Keep unbiased validation mode available.

### 11. Adaptive Bucket Resolution

Bucket count should depend on street, pot, and time budget:

- fewer buckets on flop, more where decisions are terminal/river-heavy;
- fewer buckets in low-reach branches;
- larger bucket count only near root or high-EV branches;
- always keep a stable mapping for rows already allocated.

Do not change bucket meaning inside a row after allocation. Adaptive abstraction must be encoded in the row shape/key.

### 12. Private Bucket Sampling Later

First production path should not sample private buckets. It should update all buckets inside a visited public infoset.

If bucket row math remains too expensive after SIMD:

- sample private buckets or bucket blocks;
- importance-correct sampled bucket updates;
- stratify by range mass so important buckets are seen often;
- keep exact bucket-update mode for validation.

This is a later optimization because mistakes here can silently break ranges and blockers.

### 13. Sparse Row Compression

For blueprint or long-running modes:

- compress cold rows to `float16` or quantized format only after testing;
- keep hot rows in `float32`;
- optionally store strategy sum less frequently than regret in RTA mode;
- checkpoint cold rows to disk in offline blueprint mode.

Do not use lossy compression in first RTA production path.

### 14. Merge Optimization

Worker-local deltas are correct but can become expensive.

Improve in stages:

1. Store deltas unsorted in worker-local buffers.
2. Sort by row id before merge.
3. Combine duplicate row deltas inside the worker before global merge.
4. Use row-local contiguous arrays for hot rows.
5. Merge workers in deterministic order.
6. Only add dynamic scheduling after deterministic static mode is tested.

If merge exceeds 20 percent of wall-clock time, it is a first-class bottleneck.

### 15. Warm Start And Blueprint Hooks

Warm start is one of the best RTA accelerators:

- initialize root strategy from a previous solve if the spot is close;
- initialize sparse rows from a saved blueprint;
- initialize leaf values from offline solved public states;
- keep abstraction/version metadata so old rows are not mixed with incompatible configs.

The solver should still be able to run cold from scratch for tests.

## 64 GB Fit Checklist

A future programmer should be able to answer these before running a big flop solve:

- How many public states can be cached before the hard cap?
- How many sparse values can be allocated before the hard cap?
- How many bytes can each worker delta buffer use?
- How large can terminal and leaf caches grow?
- How much memory does root export allocate?
- Does this config accidentally request dense validation storage?
- What adaptive fallback triggers first?

Use this approximate production budget:

```text
hard process target:        <= 56 GB
warning threshold:          >= 48 GB
reject threshold:           >= 60 GB estimated

sampled public cache:       <= 12 GB
sparse regret + average:    <= 24 GB
terminal/leaf caches:       <= 8 GB
all worker scratch/deltas:  <= 8 GB
exports/diagnostics:        <= 4 GB
remaining safety:           >= 4 GB
```

Example sparse table math:

```text
bucket_count = 64
action_count = 4
values_per_row = 256
two Float32 arrays = 256 * 8 bytes = 2048 bytes per row before metadata

5 million visited rows ~= 10.2 GB before metadata
10 million visited rows ~= 20.5 GB before metadata
```

This is why row count and action count matter as much as bucket count.

Memory fallback order for RTA:

1. reduce worker minibatch/delta size;
2. flush and merge deltas more often;
3. cap terminal cache;
4. reduce action menu;
5. reduce bucket preset;
6. increase depth limiting;
7. reduce public-state cache or evict cold states;
8. fail before allocation if the config is still unsafe.

Do not let Windows start paging. If the process pages, the 10-15 second target is gone.

## Profiling And Telemetry Requirements

Do not optimize blind. Every timed solve should print a compact profile line.

Minimum counters:

```text
time_total_ms
time_build_ms
time_traverse_ms
time_merge_ms
time_terminal_ms
time_export_ms
workers
traversals
traversals_per_sec
nodes_visited
nodes_per_sec
unique_public_states
unique_infosets
sparse_rows
sparse_values
worker_delta_bytes_peak
terminal_cache_bytes
rss_peak_bytes
root_strategy_delta
root_entropy
seed
```

Additional counters for tuning:

- sampled chance nodes;
- sampled opponent actions;
- traversing-player branches expanded;
- pruned actions;
- AS action sample ratio;
- leaf evaluations;
- terminal cache hit rate;
- public-state cache hit rate;
- row lookup cache hit rate;
- SIMD kernel time;
- scalar fallback time;
- merge duplicate row count;
- worker imbalance percent.

Use these thresholds:

```text
merge time > 20 percent:          improve delta aggregation or batch size
terminal time > 25 percent:       improve board/equity cache or batch leaf eval
builder/cache time > 15 percent:  group prefixes and cache legal actions
export time > 100 ms:             root-only export is still too heavy
worker imbalance > 20 percent:    use deterministic work queue or smaller chunks
rss > 48 GB:                      warn and adapt
rss > 56 GB:                      stop or fallback
```

Profile output should be machine-readable enough for CSV/JSON logs. RTA tuning will need many runs across boards, ranges, and seeds.

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

## Neural Training Data And Massive Self-Play Generation

The long-term path can include generating very large numbers of solved or near-solved poker situations for training a value model, policy model, or blueprint warm-start system.

Important framing:

```text
do not solve one impossible full game tree
solve many bounded public subgames
store compact labels and metadata
train models to generalize across public states/ranges/pots
use the model to accelerate future depth-limited RTA solving
```

DeepStack is the key precedent: it trained value networks from randomly generated poker situations whose targets were produced by solving restricted subgames. That is the right mental model for this project.

Potential generated examples:

- flop public state + ranges + pot/SPR + action context -> bucket counterfactual values;
- turn public state + ranges + pot/SPR + action context -> bucket counterfactual values;
- river public state + ranges + pot/SPR + action context -> terminal/near-terminal CFVs;
- root public state + ranges + action abstraction -> average strategy;
- public state + candidate actions -> action values and regret signs;
- leaf state -> value-network target.

Training row metadata must include:

- game/rules version;
- public board;
- street;
- pot size and stack size;
- SPR bucket;
- action history abstraction;
- legal action abstraction;
- bucket abstraction id;
- range encoding version;
- solver config;
- sampling mode;
- number of traversals/batches;
- convergence/stability metrics;
- seed;
- label quality score.

Do not train on unlabeled garbage. A bad fast solver can generate billions of bad examples. Data quality filters are required:

- minimum batches/traversals;
- root strategy stability threshold;
- no NaNs/infs;
- memory/cap status clean;
- no timeout-corrupted solve;
- terminal/leaf evaluator status clean;
- small-game validation periodically passes;
- duplicate/near-duplicate states downsampled.

Storage design:

- write append-only shards;
- prefer binary columnar or compact chunked format for large runs;
- compress cold shards;
- store config metadata once per shard;
- store numeric arrays as `float32` unless validation requires `float64`;
- keep enough metadata to reproduce the exact label generation.

Scale note:

```text
1 billion generated examples is a data pipeline project
not a single solver run
```

At that scale, every byte matters. A 1 KB row is already about 1 TB for 1 billion rows before indexing and metadata. The training writer must support compact row formats, sharding, and filtering.

Suggested roadmap:

1. Generate 10k tiny validated examples.
2. Generate 1M turn/river examples.
3. Train a simple value model or table.
4. Plug it into depth-limited leaves.
5. Compare RTA quality and speed.
6. Scale to 100M+ only after labels improve decisions.

The model should accelerate solving, not replace validation. Keep the sampled solver as the ground-truth generator and exact small games as sanity checks.

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
- `hunl_sampled_simd`: scalar/SIMD row kernels for regret matching, accumulation, deltas, and terminal bucket sums.
- `hunl_sampled_profile`: timing counters, memory counters, root stability metrics, and benchmark log formatting.
- `hunl_sampled_solver`: top-level facade tying all pieces together.

Keep the interfaces narrow. The top-level solver should own the subsystems; traversal should receive references/views, not global mutable state.

Do not put SIMD directly into traversal first. Traversal should call row-kernel functions through a small interface so scalar validation, AVX2, and future AVX-512 paths stay interchangeable.

### `include/solver/hunl_sampled_simd.hpp` and `src/solver/hunl_sampled_simd.cpp`

Add:

- scalar reference kernels;
- optional AVX2/FMA kernels;
- optional AVX-512 kernels later;
- action-major row kernels;
- terminal bucket weighted-sum kernels;
- feature flags in profile output showing which kernel path was used.

Tests:

- compare scalar vs SIMD regret matching;
- compare scalar vs SIMD average strategy accumulation;
- compare scalar vs SIMD regret delta application;
- test bucket counts 32, 48, 64, 96, and a non-multiple tail case;
- verify no NaN/negative strategy probabilities.

### `include/solver/hunl_sampled_profile.hpp` and `src/solver/hunl_sampled_profile.cpp`

Add:

- low-overhead timers;
- counters for traversal/build/merge/terminal/export;
- memory counters for cache, sparse table, deltas, terminal cache, and export;
- root strategy snapshot deltas;
- CSV/JSON-ish benchmark line formatter.

The profile object should not allocate heavily during the hot loop.

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
   - `include/solver/hunl_sampled_simd.hpp`
   - `src/solver/hunl_sampled_simd.cpp`
   - `include/solver/hunl_sampled_profile.hpp`
   - `src/solver/hunl_sampled_profile.cpp`
2. Add empty or minimal classes with compile-time tests.
3. Register files in `CMakeLists.txt`.
4. Add initial tests:
   - sampled config validates defaults;
   - sampled storage can allocate one row;
   - sampled scheduler partitions trajectory ids deterministically.
   - sampled SIMD scalar reference kernels match simple hand-computed rows.
   - sampled profile counters can be formatted without allocation-heavy hot-loop work.
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

### Phase 8A: Data Layout And SIMD Pass

Do this after the sparse/lazy solver is working and profiling shows row math is a real bottleneck.

1. Confirm production rows use action-major contiguous bucket layout.
2. Add scalar reference row kernels in `hunl_sampled_simd`.
3. Replace ad hoc row loops with calls to scalar kernels.
4. Add tests for regret matching, average strategy accumulation, regret deltas, and terminal weighted sums.
5. Add AVX2/FMA kernels.
6. Add optional runtime dispatch or compile-time feature flags.
7. Add profile counters for scalar vs SIMD kernel time.
8. Benchmark with bucket counts 32, 48, 64, 96, and 128.

Acceptance:

- scalar and SIMD outputs match within tolerance;
- SIMD path is optional and safe to disable;
- row-kernel benchmark improves materially;
- whole-solver benchmark improves enough to justify keeping the complexity;
- no increase in peak memory except small alignment overhead.

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

### Phase 12: RTA Algorithmic Tuning

Do this after the baseline RTA solver returns correct timed decisions.

1. Add public chance scheduling:
   - suit-isomorphic class stratification;
   - runout coverage counters;
   - duplicate-runout reduction inside minibatches.
2. Add prefix batching:
   - group by early chance outcome;
   - group by root action where useful;
   - reuse public-state, legal-action, and bucket-projection caches.
3. Add conservative pruning:
   - regret-based cooldown pruning;
   - value-bound dominated-action pruning;
   - safety rules for fold/check/call and at least one aggressive option.
4. Add progressive action widening:
   - start with tiny action menu;
   - add sizes only when regret/EV says they matter.
5. Add delayed averaging:
   - skip early noisy batches;
   - linearly weight later batches;
   - expose average contribution count in diagnostics.
6. Add warm starts:
   - previous local solve;
   - saved sparse rows;
   - future blueprint rows;
   - compatible leaf/value cache.

Acceptance:

- each optimization can be toggled independently;
- unbiased validation mode remains available;
- RTA mode improves root stability or EV proxy under the same 10-15 second budget;
- memory caps still hold with all tuning enabled.

## Benchmark Plan

Run exact and sampled against the same small config first:

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R "pcs|hunl_flat_mccfr|hunl_flat_dcfr|ranges_threading" --output-on-failure
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 1 --iterations 10 --sampling public-chance --sample-traversals 1024 --seed 7 --precision float
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 8 --iterations 10 --sampling external --sample-traversals 8192 --seed 7 --precision float
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 8 --sampling external --time-budget-ms 15000 --seed 7 --precision float
build/Release/texas_solver_hunl_random_flat.exe --preset conservative --workers 8 --sampling external --time-budget-ms 15000 --seed 7 --precision float --profile-json sampled_profile.json
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
- builder/cache seconds;
- terminal/leaf seconds;
- export seconds;
- terminal cache hit rate;
- public-state cache hit rate;
- SIMD/scalar row-kernel seconds after SIMD phase lands;
- root strategy delta over recent batches;
- root entropy;
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
SIMD: off or scalar reference kernels only
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
SIMD: scalar kernels first, AVX2 after row math is profiled hot
```

## Final Future-Proofing Sections

These sections cover the remaining decisions future programmers usually need after reading an architecture plan. The goal is to avoid changing this document just because implementation moves from prototype to production.

### Public API Contract

The sampled solver should expose a small stable API. Internals can change, but these concepts should remain:

```cpp
class HUNLSampledSolver {
public:
    explicit HUNLSampledSolver(HUNLSampledSolverConfig config);

    HUNLSampledSolveResult solve_for(
        const HUNLSampledSolveRequest& request,
        std::chrono::milliseconds budget);

    HUNLSampledSolveResult run_batches(
        const HUNLSampledSolveRequest& request,
        std::uint32_t batches);

    HUNLRootStrategy export_root_strategy() const;
    HUNLSampledProfile profile() const;
    HUNLSampledMemoryEstimate memory_estimate() const;
};
```

Stable request fields:

- public board and street;
- pot, stacks, current bet, legal action context;
- player to act;
- ranges for both players;
- action abstraction preset;
- bucket abstraction preset;
- depth-limit config;
- optional warm-start/checkpoint path;
- wall-clock budget.

Stable result fields:

- root action probabilities;
- root action values if available;
- chosen recommended action;
- confidence/stability diagnostics;
- batches/traversals completed;
- memory peak;
- timeout/fallback status;
- profile counters.

### Configuration Preset Matrix

Production should use named presets instead of scattered flags.

```text
validation_small:
  dense validation allowed
  Float64 allowed
  exact oracle comparison enabled
  no deadline requirement

rta_river_fast:
  External MCCFR
  river buckets 64
  small action menu
  5-10 second budget
  strict root-only export

rta_turn_fast:
  External MCCFR
  turn buckets 64, river buckets 48
  depth limit allowed
  10 second budget

rta_flop_conservative:
  External MCCFR
  flop 64, turn 48, river 32
  raise cap 1
  depth limit mandatory
  15 second budget

rta_flop_balanced:
  External MCCFR
  flop 96, turn 64, river 48
  only if memory preflight passes
  15 second budget

offline_blueprint:
  no strict 15 second deadline
  checkpointing enabled
  broader public-state coverage
  optional larger buckets/action menus
```

Every preset should print its resolved config at solve start. Benchmark logs should record preset name and full resolved values so results can be reproduced.

### Checkpoint And Blueprint Format

Future blueprint support should not require redesigning storage. Use versioned checkpoint metadata from the start.

Checkpoint metadata:

- file format version;
- solver version/git commit when available;
- abstraction version;
- action abstraction id;
- bucket abstraction id;
- storage precision;
- sampling mode;
- depth-limit mode;
- public-state id scheme;
- infoset id scheme;
- seed schedule;
- completed batches/traversals;
- row compression mode;
- endian/architecture marker if binary.

Checkpoint payload:

- sparse infoset metadata;
- regret rows;
- average strategy rows;
- optional baseline rows;
- optional terminal/leaf cache;
- optional root snapshots;
- profile summary.

Rules:

- incompatible checkpoints must fail clearly;
- loading a checkpoint must not silently change bucket/action meaning;
- online RTA can load a warm start but must still be able to solve cold;
- blueprint mode can use larger files and slower IO, RTA mode cannot block inside the hot decision loop.

### Determinism And Reproducibility

Determinism is required for debugging and benchmarking.

Rules:

- seed every trajectory by `(base_seed, solve_id, iteration_or_batch, traversing_player, trajectory_id)`;
- do not rely on thread scheduling order for RNG;
- merge in fixed order in deterministic mode;
- log all config values and seed values;
- log CPU feature path: scalar, AVX2, or AVX-512;
- log compiler/build mode because floating-point behavior can differ;
- keep a scalar deterministic validation path.

Expected behavior:

- same seed + same worker count + same deterministic mode should reproduce the same root strategy within strict tolerance;
- different worker counts may have tiny floating differences unless merge is fully normalized by trajectory id;
- fast RTA mode may trade strict determinism for throughput only behind an explicit flag.

### Error Handling And Fallback Policy

The solver should fail before damaging the decision window.

Hard errors:

- invalid ranges;
- no legal actions;
- impossible public board;
- unsupported street/preset combination;
- checkpoint abstraction mismatch;
- estimated memory above reject threshold;
- allocation failure;
- NaN or infinite strategy/value.

Fallbacks:

- reduce batch size;
- flush deltas earlier;
- cap terminal cache;
- switch off AS or expensive variance reduction;
- reduce action menu;
- reduce bucket preset;
- increase depth limiting;
- disable full dense export;
- return last stable root snapshot if deadline expires.

Do not silently return uniform strategy after an internal failure. If a fallback strategy is needed, result status must say so.

### Testing Matrix

Minimum tests before production use:

```text
unit:
  RNG weighted sampling
  sparse row allocation
  regret matching
  SIMD vs scalar kernels
  deterministic scheduler partitioning
  memory estimator caps

integration:
  tiny exact-vs-sampled convergence
  single-worker external MCCFR
  multi-worker deterministic merge
  lazy public-state expansion
  root-only export
  timeout during batch
  checkpoint save/load

performance:
  10s river preset
  10s turn preset
  15s conservative flop preset
  memory cap rejection
  terminal cache stress
  merge stress

quality:
  seed stability
  root EV proxy stability
  dominated-action suppression
  small-game exploitability
```

Tests should include both validation mode and production-like RTA mode. A fast sampled solver that only works in one benchmark board is not enough.

### Deployment And Build Assumptions

Target environment:

- Windows desktop first;
- 64 GB RAM target machine;
- CPU multithreading required;
- AVX2 expected for optimized builds but not required for correctness;
- AVX-512 optional only;
- Release build required for performance numbers;
- debug builds are for correctness only.

Build flags should expose:

- scalar-only mode;
- AVX2 mode;
- optional AVX-512 mode;
- deterministic mode;
- profile mode;
- assertions/sanitizers for development.

Never quote RTA performance from debug builds.

### Rollout Plan

Implementation should roll out in gates:

1. Build greenfield empty module.
2. Pass scalar single-worker validation.
3. Pass external MCCFR small-game convergence.
4. Pass deterministic multithreaded merge.
5. Pass sparse storage tests.
6. Pass lazy public-state expansion.
7. Pass root-only timed export.
8. Pass 64 GB preflight and cap tests.
9. Pass 10-second turn/river benchmark.
10. Pass 15-second conservative flop benchmark.
11. Add SIMD only after profiling.
12. Add advanced RTA tuning one toggle at a time.

Each gate should preserve exact solver behavior.

### Documentation Ownership

After this document lands, future updates should be rare and should only happen when one of these changes:

- solver architecture changes away from sampled/lazy sparse MCCFR;
- memory budget changes;
- RTA deadline target changes;
- checkpoint format changes incompatibly;
- production preset definitions change;
- validation shows a recommended algorithm is wrong for this codebase.

Routine implementation details should go into code comments, tests, benchmark notes, or a changelog, not into this architecture plan.

## Final Recommendation

Implement MCCFR, but do it as a new sampled flat solver. Use the existing exact `HUNLFlatDCFR` as the correctness oracle and keep it unchanged.

The best first implementation is not Average Strategy Sampling. It is external-sampling MCCFR with public chance sampling, deterministic worker batches, and dense-table validation. After that works, add sparse/lazy storage to actually solve the memory problem. Then add Average Strategy Sampling if the profile shows player action branching is still expensive enough to justify the extra estimator variance.
