# Hand Buckets in TexasSolver

This document explains what "hand buckets" can mean in this repository, how the current code uses them, and how to extend the latest flat multithreaded SIMD solver to work with bucketed hand spaces.

## Two meanings of "buckets"

There are two different concepts that often get mixed together:

1. `Abstraction buckets`
   - These already exist in the repo.
   - They map a private hand to an abstract bucket ID for a specific board and street.
   - They are loaded from an external abstraction artifact and queried through `lookup_bucket(...)`.

2. `Solver-side bucketed hand space`
   - This is not fully implemented in the flat HUNL solver yet.
   - It would mean the solver stores regrets, strategies, reach, and averages over bucket IDs instead of explicit hands.
   - It is the optimization you want for faster flop, turn, and river solving, and it is also the foundation for range-based solving.

## What exists today

### Abstraction machinery

The repo already contains a complete abstraction lookup layer:

- `include/util/abstraction.hpp`
- `src/util/abstraction.cpp`

The abstraction artifact contains:

- canonical board indexing
- canonical hole-card indexing within a board
- bucket assignment arrays per street
- metadata such as schema version and bucket counts

The runtime lookup path is:

1. Canonicalize the board with suit isomorphism.
2. Canonicalize the hole cards under the same suit permutation.
3. Use board + hand keys to find the bucket ID for the street.

### Current flat solver behavior

The flat HUNL backend does not yet compress the game into bucket classes. It still stores values per explicit hand:

- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `src/solver/hunl_flat_dcfr.cpp`

Today the flat solver builds tables with:

- `1326` hands per player when ranges are open
- `1` hand per player when `initial_hole_cards` are fixed

That means the current solver is still operating on explicit hand rows, not bucket rows.

### Where bucket-aware keys already appear

The HUNL state can already include a bucket ID in the infoset key:

- `src/games/hunl.cpp`

That is useful for abstraction-aware tree building or keying, but it does not by itself change the solver storage layout.

## Why use buckets

Bucketed hand spaces are useful when the explicit hand space is too large for the target hardware budget.

Main benefits:

1. Lower memory use
   - Regret and strategy arrays shrink from `hands * actions` to `buckets * actions`.

2. Lower compute cost per iteration
   - Many loops become `O(bucket_count)` instead of `O(hand_count)`.

3. Better cache locality
   - A smaller dense state fits cache better and helps SIMD-friendly loops.

4. Easier range solving
   - Once the solver state is bucketed, a user's range is naturally represented as weights over bucket IDs.

The main tradeoff is approximation error:

- more buckets usually means better strategy fidelity
- fewer buckets usually means faster iterations

## What "building buckets" means

In practice, bucket building has three separate pieces:

1. `Feature extraction`
   - Decide what information describes a private hand on a given street.
   - Common examples: made-hand class, draw strength, blockers, equity quantile, nut potential, backdoor potential.

2. `Clustering / discretization`
   - Convert features into bucket IDs.
   - This can be hand-crafted rules, k-means-like clustering, quantile binning, or a hybrid.

3. `Artifact generation`
   - Write the resulting board and hand indices into an abstraction file the solver can load.

In this repo, the solver only consumes the artifact. The clustering logic lives outside the runtime solver.

## How the current abstraction artifact works

The loader expects an `.npz` archive with:

- `flop_assignments.npy`
- `turn_assignments.npy`
- `river_assignments.npy`
- `flop_board_index.npy`
- `turn_board_index.npy`
- `river_board_index.npy`
- `flop_hand_index.npy`
- `turn_hand_index.npy`
- `river_hand_index.npy`
- `metadata.npy`

The runtime side then uses:

- board canonicalization to choose the board key
- hole canonicalization to choose the hand key
- board+hand lookup to retrieve a bucket ID

This is exactly the right storage format to feed a bucketed solver.

## How to let users change bucket count

There are two user-facing knobs you will probably want.

### 1. Global bucket target

This is the simplest control:

- `num_buckets_flop`
- `num_buckets_turn`
- `num_buckets_river`

The abstraction builder uses these values when generating assignments.

Good defaults:

- flop: high bucket count, because flop has the biggest strategic jump
- turn: medium bucket count
- river: medium or lower bucket count, depending on memory budget

### 2. Feature binning / custom bucket schema

For advanced users, expose a configuration object that controls:

- feature set
- per-feature bin count
- weighting between equity and texture
- whether buckets are street-specific or shared
- whether blocker-sensitive hands get special buckets

This is the path for custom abstractions.

## How to add user control in the codebase

Recommended places to expose bucket settings:

1. `HUNLConfig`
   - Add optional abstraction/bucket fields here.
   - This is the best place if bucket choice is part of solve configuration.

2. Abstraction artifact builder CLI or script
   - Add a generator tool that outputs the `.npz` abstraction file.
   - Keep the solver runtime focused on loading and using the artifact.

3. Flat solver constructor
   - Add a bucketed hand-count argument or a bucket table reference.
   - Do not hardcode bucket counts inside the solver.

Suggested config fields:

- `abstraction_path`
- `abstraction_version`
- `bucket_count_override`
- `bucket_count_flop`
- `bucket_count_turn`
- `bucket_count_river`
- `bucket_schema_name`

If you want custom buckets, the artifact builder should own the heavy logic and the runtime should only validate and load.

## Step-by-step plan to implement buckets in the flat solver

### Step 1. Define the bucketed solver contract

Decide what the solver state must hold per bucket:

- regret per action
- current strategy per action
- accumulated strategy per action
- optional reach statistics

For a bucketed HUNL solver, the unit of storage should become:

- `infoset x bucket x action`

instead of

- `infoset x hand x action`

### Step 2. Add a bucket map to the flat HUNL data model

Create a runtime table that maps each solver-relevant hand to a bucket index for the current street and board.

That table should support:

- board-specific lookup
- street-specific lookup
- fast dense iteration over bucket IDs
- optional weighted range inputs for future range solving

Likely placement:

- new type in `include/solver/`
- loaded from `AbstractionTables`
- cached inside the flat solver or its graph/state wrapper

### Step 3. Replace explicit hand storage with bucket storage

Update `HUNLFlatInfosetTable` so its row width is based on bucket count, not hand count.

This means:

- `hand_count` becomes `bucket_count`
- `value_count = bucket_count * action_count`
- value indexing uses bucket index instead of hand index

Keep a compatibility layer for fixed-hand or explicit-hand mode if you still need it for debugging.

### Step 4. Update strategy and regret updates

Current updates loop over hands.

Bucketed updates should:

- iterate over buckets
- aggregate all hand contributions mapped to that bucket
- write regret deltas for the bucket row
- compute the average strategy per bucket row

If the bucket represents multiple hands, decide on the aggregation rule:

- uniform over hands in the bucket
- weighted by range probability
- weighted by conditional reach

For range-aware solving, weighted aggregation is the long-term correct choice.

### Step 5. Update reach propagation

The forward pass should propagate reach to buckets instead of raw hands.

That requires:

- bucket reach accumulation
- hand-to-bucket compression at decision points
- optional bucket-to-hand expansion only when needed for evaluation or diagnostics

If a node's strategy is stored by bucket, then each child action can be evaluated using the bucket reach mass rather than a full hand row.

### Step 6. Update terminal value evaluation

For flop/turn/river solving, terminal utility can often be reduced by bucket pair interactions rather than explicit hand pair interactions.

You will likely want:

- bucket-vs-bucket terminal matrices
- board-specific hand strength aggregation
- deterministic mapping from bucket pair to showdown utility estimate

This is one of the largest speedups if the abstraction is good.

### Step 7. Make the parallel plan bucket-aware

The current parallel plan partitions:

- infosets
- nodes
- depth slices

After bucketing, make sure workers split the smaller bucket tables efficiently and avoid false sharing.

Useful rules:

- partition by infoset ranges first
- align hot bucket arrays to cache lines
- keep worker-local scratch buffers for bucket aggregation
- reduce cross-thread writes during the main iteration stages

### Step 8. Add a solver mode switch

Keep both modes available:

- explicit-hand mode for correctness and comparison
- bucketed mode for speed

This makes it easier to validate the new solver against the old solver on small game states.

### Step 9. Add range support as a first-class input

Bucketed solving becomes much more useful when the user can specify ranges instead of exact hands.

Plan the API so users can provide:

- per-hand weights
- per-bucket weights
- mixed range inputs

That will make future flop/turn/river range solving much easier.

### Step 10. Add export and diagnostics helpers

Users will want to inspect:

- bucket definitions
- bucket counts per street
- which hands were assigned to each bucket
- memory usage by table
- iteration timing by stage

This is especially important when bucket count is user-configurable.

## Recommended implementation order

If you want the fastest path to a working optimization, implement in this order:

1. Add bucket metadata loading and validation.
2. Add bucket-aware infoset table sizing.
3. Update strategy/regret storage to use bucket rows.
4. Update forward and backward passes to read bucketed strategies.
5. Add range-weighted aggregation.
6. Add terminal value compression.
7. Add performance profiling and tuning.

## What to expose to users

At minimum, users should be able to choose:

- abstraction file path
- abstraction version
- bucket count per street
- whether to use explicit-hand or bucketed solving
- whether range inputs are exact hands or weighted ranges

Nice-to-have controls:

- custom bucket schema name
- preset abstraction profiles like `fast`, `balanced`, `accurate`
- per-street bucket overrides

## How this helps flop, turn, and river

The payoff is largest on flop:

- large hand space
- large branching factor
- strong similarity between many hands after suit isomorphism

Turn and river also benefit, especially when:

- the range is broad
- the abstraction is carefully designed
- the solver is memory-bandwidth limited

The likely practical result is:

- fewer iterations per minute in absolute terms may or may not improve
- but each iteration becomes much cheaper
- so wall-clock convergence to a useful strategy should improve

## Tests to write

Write tests at three levels: loader, solver correctness, and performance invariants.

### Loader and abstraction tests

1. Load a known abstraction artifact successfully.
2. Reject a schema mismatch.
3. Reject a missing entry in the `.npz` archive.
4. Verify canonical board lookup is stable under suit permutations.
5. Verify canonical hole lookup changes only when the canonicalized hand changes.
6. Verify `lookup_bucket(...)` returns the expected bucket for a small fixture.

### Bucket model tests

1. Verify bucket counts match the artifact metadata.
2. Verify every valid hand on a street maps to a bucket.
3. Verify invalid board/hand combinations throw or fail cleanly.
4. Verify bucket IDs are dense or intentionally sparse, but documented either way.
5. Verify custom bucket schemas round-trip through load and lookup.

### Solver storage tests

1. Verify `HUNLFlatInfosetTable` allocates the expected `value_count`.
2. Verify bucketed row indexing is correct for both value layouts.
3. Verify strategy and regret buffers are sized from bucket count, not hand count.
4. Verify worker partitioning still covers all infosets and nodes.
5. Verify no stage writes out of bounds when bucket count is small.

### Solver behavior tests

1. Compare explicit-hand and bucketed solver outputs on a tiny toy game.
2. Verify a fixed abstraction produces deterministic results across runs.
3. Verify one iteration updates regrets and strategy sums in the expected bucket rows.
4. Verify average strategy normalization works for bucket rows.
5. Verify range-weighted inputs produce different results from uniform inputs when expected.

### Regression and performance tests

1. Verify the flat solver still handles current open-range and fixed-hole modes.
2. Verify flop solve memory usage decreases when bucket counts decrease.
3. Verify stage timing is recorded for bucketed mode.
4. Verify multithreaded runs match single-threaded runs within acceptable numerical tolerance.
5. Add a benchmark test that compares explicit-hand and bucketed runtimes on a flop subgame.

## Practical recommendation

If your goal is a fast flop solver that is ready for ranges, the best near-term design is:

- keep abstraction tables external
- make the flat solver consume bucket IDs directly
- store regrets/strategies by bucket
- use weighted bucket ranges for future range solving

That gives you the speedup path without forcing the runtime to reimplement clustering logic.

