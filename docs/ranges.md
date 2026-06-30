# Ranges in TexasSolver

## Short version

In poker, a `range` is the probability distribution over a player’s possible private hands.

TexasSolver already reasons about hidden information through infosets, chance reach, and per-hand or per-bucket states. What it does **not** yet have as a first-class concept is a dedicated, continuously updated villain-range object that flows through the solver as its own piece of state.

This document explains:

- what ranges are
- why we need them
- where they fit in this repository
- how to implement them cleanly
- how to precompute and reuse equilibrium ranges
- how to verify the whole pipeline with tests

## What a range is

A range is not a single hand.

It is a vector of weights over all hands that are still possible for a player.

Example:

- `AA`: 1.2%
- `AKs`: 2.3%
- `QQ`: 1.0%
- `72o`: 0.0%

The weights may be:

- exact combo weights
- hand-class weights
- bucket weights

So a range is just the solver’s belief about what the player could hold.

## Why we need ranges

Poker is hidden-information game theory.

Without ranges, the solver would behave as if every decision were made with full knowledge of private cards, which is wrong.

Ranges let us:

- weight expected value by the probability of each opponent hand
- compute counterfactual values correctly
- model how action history changes the opponent’s remaining hands
- prune impossible combos after board runout or blocked cards
- merge exact hands into buckets when the state space is too large

In short: ranges are how a solver turns “I do not know villain’s hand” into math.

## How TexasSolver works today

The current codebase already has the building blocks for hidden information:

- `DCFRSolver` traverses infosets keyed by game state and action history
- `HUNLFlatDCFR` stores reach, terminal values, node values, and action values explicitly
- `HUNLFlatSolveGraph` and `HUNLState` encode board and action history
- `HUNLFlatBucketMap` can map hands into buckets and attach weights
- `player_ranges` already appears in the flat path when a bucketed abstraction is loaded

Relevant code paths:

- [`include/solver/dcfr.hpp`](../include/solver/dcfr.hpp)
- [`src/solver/dcfr.cpp`](../src/solver/dcfr.cpp)
- [`include/solver/hunl_flat_dcfr.hpp`](../include/solver/hunl_flat_dcfr.hpp)
- [`src/solver/hunl_flat_dcfr.cpp`](../src/solver/hunl_flat_dcfr.cpp)
- [`include/games/hunl_solver.hpp`](../include/games/hunl_solver.hpp)
- [`src/games/hunl_solver.cpp`](../src/games/hunl_solver.cpp)
- [`src/solver/hunl_bucket_map.cpp`](../src/solver/hunl_bucket_map.cpp)
- [`src/solver/hunl_bucket_terminal.cpp`](../src/solver/hunl_bucket_terminal.cpp)

Important distinction:

- the solver already respects hidden information
- but it does not yet maintain a dedicated, first-class range state for each player across the tree
- bucketed flat solve mode is the closest current hook for range-aware solving

## What ranges should mean in this project

We should define ranges at three levels.

### 1. Exact-combo range

Probability for each legal private card combination.

Use this when:

- solving small subgames
- debugging
- preflop and simple postflop analysis

### 2. Hand-class range

Probability for each hand class, such as:

- `AA`
- `AKs`
- `KQo`

Use this when:

- exporting human-readable output
- precomputing charts
- storing coarse opponent priors

### 3. Bucket range

Probability for each abstraction bucket.

Use this when:

- the game is too large for exact combos
- running fast multi-threaded solve
- using abstraction-based postflop solving

## How the solver should use ranges

Ranges should feed the solver in four places.

### Pre-flop initialization

Start with a prior range.

Examples:

- uniform over all legal combos
- preflop chart range
- solver-generated equilibrium range
- villain-specific exploitative range

### Action filtering

After each action, remove impossible hands and renormalize.

Example:

- villain raises big
- some weak hands drop to zero
- suited broadways may remain
- value hands may gain weight depending on the model

### Reach-weighted evaluation

When evaluating a node, multiply action value by the opponent range mass that can actually reach that node.

That means the solver does not ask:

- “what is the EV of one hand?”

It asks:

- “what is the EV across the entire distribution of hands reaching this node?”

### Regret updates

Regret should be updated with reach-weighted range mass.

This is the key point:

- a hand with 0.001 weight should influence strategy less than a hand with 0.20 weight
- bucketed range mass should be used in the same way, just at bucket granularity

## Where to put range support in the repo

The cleanest architecture is to add a dedicated range layer instead of mixing range math into every solver.

Suggested modules:

- `include/ranges/range.hpp`
- `include/ranges/range_store.hpp`
- `include/ranges/range_builder.hpp`
- `src/ranges/range.cpp`
- `src/ranges/range_store.cpp`
- `src/ranges/range_builder.cpp`

Suggested responsibilities:

- `range.hpp`
  - range types, normalization helpers, renormalization, masking
- `range_store.hpp`
  - store priors, per-street ranges, and precomputed equilibrium ranges
- `range_builder.hpp`
  - build ranges from preflop charts, solver exports, or external data

Solver integration points:

- `HUNLConfig`
  - add optional initial player ranges
- `HUNLFlatBucketMap`
  - accept and apply range weights to buckets
- `HUNLFlatDCFR`
  - use range mass inside reach, regret, and average-strategy stages
- `solve_hunl_postflop`
  - accept range source selection and pass it down

## How to determine ranges

We should support several sources.

### A. Manual ranges

Good for:

- tests
- debugging
- single spot analysis

Bad for:

- continuous solving
- production automation

### B. Preflop charts

Good for:

- default starting ranges
- position-aware opening ranges
- simple training data

Typical workflow:

- load chart from file
- map chart entries to combos
- normalize by blockers and board cards
- apply action filters as the tree progresses

### C. Solver-generated equilibrium ranges

Good for:

- automated study
- repeating common nodes
- reducing manual work

Typical workflow:

- solve a smaller game or abstraction
- export average strategy and reach-weighted ranges
- cache ranges by node signature
- reuse them as priors in larger solves

### D. External open-source data

Good for:

- bootstrapping
- validating our own range generator

Sources can include:

- public preflop charts
- open-source poker trainers
- open-source GTO study tools
- hand-range libraries

Best practice:

- do not depend on a live external service at solve time
- import data once
- convert it into a local cache or binary asset

### E. Learned or heuristic ranges

Good for:

- exploitative play
- approximate solving
- opponent modeling

Examples:

- a small model predicts range after each action
- the model outputs bucket weights or combo weights
- the solver uses that as a prior, not as truth

## How to make ranges automatic

We should not manually set ranges every time.

Instead, use a pipeline:

1. Start with a prior range source.
2. Apply blockers from hole cards and board.
3. Apply action-based filtering.
4. Renormalize.
5. Convert to buckets if the current solve mode requires abstraction.
6. Cache the resulting range by node signature.

The node signature should include:

- street
- board
- acting player
- bet history
- stack state
- abstraction key if used

This gives us automatic range propagation.

## Equilibrium range precomputation

This project can precompute equilibrium ranges in two ways.

### Option 1: internal precompute app

Build a small tool in this repository that:

- solves a chosen game tree or subgame
- exports average strategy
- computes reach distributions for each node
- stores range tables on disk

Good for:

- reproducibility
- no external runtime dependency
- direct integration with the solver’s data model

Suggested location:

- `examples/precompute_ranges_main.cpp`
- or `tools/precompute_ranges.cpp`

### Option 2: import from external open-source tools

Use outside data only as an offline bootstrap.

Good workflow:

1. Solve or export data externally.
2. Convert to TexasSolver’s range format.
3. Store it in a local cache file.
4. Load it during solving.

This is useful if we want:

- quicker initial coverage
- cross-checking against known outputs
- faster prototyping

### Option 3: hybrid approach

Best practical option.

- use external/open-source data to seed priors
- use TexasSolver to refine and cache equilibrium ranges
- fall back to auto-generated ranges when no cached file exists

## Proposed implementation plan

### Step 1: define range data structures

Substeps:

- add a `RangeVector` type for combo or bucket probabilities
- add normalize, clamp, renormalize, and mask helpers
- add a `RangeMask` or validity mask for dead cards and blockers
- add serialization support for disk cache

### Step 2: define a range source API

Substeps:

- create an interface for loading ranges from:
  - uniform priors
  - preflop charts
  - solver exports
  - cached files
- make the API return a canonical normalized range
- keep the API independent from the solver engine

### Step 3: add node-aware range propagation

Substeps:

- compute child ranges from parent ranges and action filters
- fold out impossible combos
- renormalize after each action
- ensure chance nodes update legal combo sets correctly

### Step 4: add bucket conversion

Substeps:

- map combo weights into bucket weights
- preserve total mass
- use the existing abstraction pipeline where possible
- make exact and bucketed paths produce comparable totals

### Step 5: wire ranges into HUNL solve config

Substeps:

- extend `HUNLConfig` with optional initial ranges
- support default range policy selection
- add validation for mismatched street/board/card counts
- surface friendly errors when ranges do not match the game state

### Step 6: integrate with flat DCFR

Substeps:

- feed range mass into reach computation
- use range-weighted values in regret updates
- include bucket mass in average strategy accumulation
- make range handling thread-safe and deterministic

### Step 7: add cache and export tools

Substeps:

- export equilibrium ranges to a local file format
- support import on later runs
- add cache invalidation by board, street, abstraction, and stack config
- optionally add a benchmark mode for precompute speed

### Step 8: validate against known outputs

Substeps:

- compare exact vs bucketed outputs on small games
- compare cached vs freshly computed ranges
- compare single-thread vs multi-thread outputs
- verify normalization and blocker logic

## Suggested file-level implementation map

This is a practical mapping, not a hard rule.

- `include/core/game.hpp`
  - add or extend range-related config fields if they are global
- `include/games/hunl.hpp`
  - store initial/player ranges in the game config
- `src/games/hunl.cpp`
  - validate range input against board and hole cards
- `include/solver/hunl_flat_dcfr.hpp`
  - add range-aware solver accessors if needed
- `src/solver/hunl_flat_dcfr.cpp`
  - apply range weights in reach and update stages
- `include/util/abstraction.hpp`
  - expose bucket-mapping helpers for range projection
- `src/util/abstraction.cpp`
  - implement combo-to-bucket projection helpers
- `src/preflop/*.cpp`
  - generate or load preflop priors
- `examples/*`
  - add precompute and export examples

## Suggested runtime flow

1. Load config.
2. Load or build prior range.
3. Mask illegal hands from board and blockers.
4. Convert to buckets if needed.
5. Solve the subgame.
6. Export average strategy.
7. Optionally export updated equilibrium ranges.
8. Cache the results for future runs.

## Tests to add

### Data validation tests

- range sums to 1 after normalization
- zero-mass input falls back to a valid uniform distribution
- illegal hands are removed by blocker masking
- board cards remove impossible combos
- identical inputs always produce identical normalized ranges

### Range propagation tests

- parent range splits correctly across actions
- child ranges preserve total mass after renormalization
- fold branches remove dead combos
- chance nodes update legal combos correctly

### Bucket mapping tests

- combo-to-bucket projection preserves total probability mass
- bucket weights match the sum of their member combos
- exact and bucketed totals agree on simple toy trees

### Solver integration tests

- flat solver can accept an explicit initial range
- recursive solver still works when ranges are absent
- average strategy export remains stable with range support
- exploitability computation remains finite and valid

### Multi-threading tests

- single-thread and multi-thread runs produce equivalent outputs within tolerance
- worker scheduling does not change normalized ranges
- no data race corrupts range tables
- repeated runs with the same seed/config produce the same result

### Cache and precompute tests

- exported range files round-trip correctly
- cached ranges reload exactly
- stale cache entries are rejected when board/street/config changes
- precompute output matches on-the-fly output for the same node signature

### Regression tests

- existing HUNL tests still pass
- existing DCFR tests still pass
- bucketed HUNL tests still pass
- preflop helpers still pass

## Acceptance criteria

We are done when:

- ranges can be loaded, built, cached, and validated
- the solver can consume ranges automatically
- exact and bucketed paths both use range mass correctly
- a precompute tool can export equilibrium ranges
- the full test suite passes
- the new behavior is documented and reproducible

## Recommended order of work

1. Add data structures and validation.
2. Add range loading and masking.
3. Wire range propagation into the HUNL path.
4. Add bucket projection.
5. Add export/import and cache support.
6. Add precompute tooling.
7. Add tests.
8. Benchmark and refine.

## Final note

If we want a solver that feels like a real poker engine instead of a pure game-tree evaluator, ranges should become a first-class concept.

The best path is not “manual ranges everywhere.”

It is:

- clean range types
- automatic propagation
- cacheable precompute output
- bucket-aware integration
- strong tests

