# HUNL code review before multiway postflop work

Date: 2026-07-20

## Executive summary

The current code is not ready to serve as the correctness oracle for a blueprint plus online depth-limited solving system. The most urgent problems are in the new sampled/lazy path: its public solve methods do not run MCCFR, its traversal does not sample from strategy or update regrets, and showdown traversal always returns zero. There are also independent correctness and safety issues in memory fallback, card validation, range integration, sparse storage, and sampled state memoization.

Multiway support should not be added by widening the existing two-element arrays in place. Heads-up assumptions are embedded in turn selection, fold resolution, contribution matching, terminal utility, range storage, bucket tables, reach propagation, and solver updates. Fix and lock down the heads-up contracts first, then introduce a separate variable-player game-state and sampled-solver layer.

## Scope and method

This was a static review of the checked-in HUNL game, exact flat solver integration, range plumbing, suit isomorphism, and sampled/lazy solver modules. No build, tests, benchmarks, or long-running solver commands were run, in accordance with `AGENTS.md`.

The strategic document named by `AGENTS.md`, `docs/mccfr_large_tree_implementation_plan.md`, was not present in the working tree during the review. Consequently, this report uses the public headers, implementation, tests, and the architecture rules in `AGENTS.md` as the contract.

Severity meanings:

- **P0**: can return a strategy that looks valid but was not solved, can hang, or can invoke memory-unsafe behavior.
- **P1**: materially wrong solver semantics, probability model, or storage contract.
- **P2**: latent collision/overflow or missing invariant likely to become serious as trees grow.
- **Blocker**: not a defect for the current heads-up product, but must be redesigned before 3-6 player solving.

## Findings

### P0-1: `HUNLSampledSolver` reports completed work without performing any solve

Status: **API-level guard fixed on 2026-07-20; real solving remains guarded until the new traversal is integrated into scheduler/batch execution.**

Original evidence:

- The implementation converted every positive time budget into exactly one batch without measuring elapsed time.
- It recorded the requested traversal count without executing traversals.
- It always exported a uniform root strategy while reporting the requested batches as completed and `timed_out = false`.

Why this is dangerous:

A caller can request a 10-15 second solve, receive `batches_completed > 0`, a nonzero `profile.traversals`, and a normalized strategy, and reasonably treat it as solver output. The result is actually uniform regardless of state, seed, iterations, storage contents, or time budget. This is a silent correctness failure, which is more dangerous than an explicit “not implemented” error.

Implemented guard:

`HUNLSampledSolverNotReady` is now thrown for every positive-budget or positive-batch request before solver state or profile data is changed. Non-positive budgets and zero batches are explicitly initialization-only: they may initialize the root and export its uniform unsolved strategy, but return zero batches and zero traversals.

Remaining implementation work after P0-2:

1. Implement a real batch loop that partitions trajectory IDs, invokes traversal, merges worker-local deltas in fixed order, applies discounting, and exports the root row.
2. Derive profile counters only from completed work.
3. In `solve_for`, check `steady_clock` between bounded minibatches and set `timed_out` from the actual stop condition.

Guard regression tests added:

- Positive batch requests throw `HUNLSampledSolverNotReady` and preserve existing builder, profile, and exported-strategy state.
- Short and 15-second positive budgets both fail explicitly without reporting traversals or exporting a strategy.
- Zero-budget and zero-batch initialization report zero completed work and zero traversals.

The original behavioral tests for regret changes, budget-dependent batch counts, actual traversal counters, and timeouts remain required when P0-2 is implemented.

### P0-2: sampled traversal is not MCCFR and returns zero for every showdown

Status: **scalar external-sampling reference traversal implemented on 2026-07-20.**

Original evidence:

- `src/solver/hunl_sampled_traversal.cpp:30-35` only checks whether a row exists and increments `infosets_updated`; it does not update the row.
- `src/solver/hunl_sampled_traversal.cpp:49` passes the constant `0.0` to the showdown evaluator even though sampled nodes already contain `terminal_utility`.
- `src/solver/hunl_sampled_traversal.cpp:59` chooses `trajectory_id % edge_count` at every depth.
- The traversal never uses edge probability, current strategy, counterfactual reach, sampling reach, `request.iteration`, the configured seed, or the SIMD regret kernels.

Original consequences:

- All showdown trajectories have value zero.
- A trajectory selects the same ordinal edge at every node where that ordinal exists, creating a strongly correlated and biased path distribution.
- Chance edges are not sampled according to `HUNLSampledEdge::probability`.
- Opponent actions are not sampled from regret-matched strategy.
- Traverser actions are not enumerated for external-sampling counterfactual values.
- `infosets_updated` means “row was present,” not “an update was made.”

Implemented fix:

The traversal now accepts a deterministic base seed, trajectory ID, iteration, traverser, and explicit bucket selection. It maintains player, public-chance, and sampling reaches; enumerates all actions at traverser nodes; samples opponent strategy and chance edges from their actual probabilities with independent RNG draws; records regret and average-strategy changes in worker scratch; and merges those deltas in insertion order only after the traversal completes. Fold, showdown, and depth-limited nodes return their stored per-player terminal utility rather than a constant placeholder.

Regression tests added:

- A two-action river node checks the exact node value, regrets, and average-strategy update against hand calculations.
- Opponent sampling with a 75/25 regret-matched strategy checks empirical action frequencies and mean value.
- A nonuniform collapsed public-chance node checks empirical edge frequencies against stored probabilities.
- A multi-node trajectory checks that successive choices are independent RNG draws rather than repeated trajectory-ID modulo selection.
- Win, loss, and board-tie terminals check the stored utility from both player perspectives.

Remaining integration work belongs to P0-1: scheduler batches must invoke this traversal with stable per-trajectory IDs, keep separate scratch per worker, merge workers in fixed order, and derive solve profiles from completed traversals before positive solve requests can be enabled.

### P0-3: adaptive memory fallback can loop forever while increasing the estimate

Status: **fixed on 2026-07-20.**

Original evidence:

- `src/solver/hunl_sampled_solver.cpp:229-241` loops while the estimate exceeds the hard limit and a configuration field changes.
- After other fallback levers are exhausted, `src/solver/hunl_sampled_solver.cpp:280` increments `depth_limit_plies_hint` on every iteration.
- `src/solver/hunl_sampled_solver.cpp:302-307` multiplies terminal-cache memory by `1 + depth_limit_plies_hint`.

The last fallback therefore increases estimated memory. Because the hint changes on every iteration, the “no progress” comparison never breaks the loop. A configuration still above the hard limit after minibatch, traversal, average-strategy, and bucket reductions can hang forever inside `preflight()`.

Implemented fix:

- `depth_limit_plies_hint` now means the maximum plies cached/evaluated below a public state; zero is root-only. The fallback reduces it toward zero, reducing terminal-cache memory.
- Preflight evaluates each fallback candidate before committing it. It records and keeps a candidate only when `new_estimate < old_estimate`; otherwise it stops and rejects if the hard limit remains exceeded.
- Successful adaptive steps are capped at 128.
- Public-state, sparse-row, terminal-cache, worker-delta, export, and total estimates use saturating arithmetic. Overflow becomes `uint64_t` maximum and is rejected by a finite hard limit.

Original recommended fix:

- Define the depth field unambiguously. If it means maximum plies searched, reducing it should reduce memory. If it means “cut off this many plies earlier,” rename it and make the estimator monotonic in the correct direction.
- Require every fallback step to produce `new_estimate < old_estimate`; otherwise stop and reject.
- Add a small fixed maximum number of fallback steps.
- Use saturating/checked arithmetic in all estimate products and sums so overflow becomes rejection, not a small estimate.

Regression tests added:

- An impossible fixed-cache configuration rejects after all memory-reducing fallbacks are exhausted, rather than entering no-op fallbacks.
- Every recorded adaptive step is asserted to strictly decrease the estimate.
- A depth-hint-only configuration verifies that reducing the hint reduces memory until the limit fits.
- Maximum workers, bucket hints, cache sizes, depth hints, and action counts produce a saturated estimate and safe rejection.

Original regression recommendations:

- Construct a limit that remains impossible after all valid reductions and assert that preflight returns `Rejected` promptly.
- Assert that every recorded adaptive step strictly decreases the estimate.
- Exercise maximum `workers`, bucket hints, and cache sizes and assert overflow-safe rejection.

### P0-4: card inputs are not validated and can index fixed arrays out of bounds

Status: **fixed on 2026-07-20.**

Original evidence:

- `HUNLConfig::validate` checks board length at `src/games/hunl.cpp:286-289`, but does not check card encoding, duplicate board cards, duplicate/overlapping fixed hole cards, or hole/board overlap.
- Range validation at `src/games/hunl.cpp:63-96` checks same-card holes and board blocking, but does not validate that a card encodes rank 2-14 and suit 0-3.
- `HUNLState::chance_outcomes` writes `held[c]` directly at `src/games/hunl.cpp:536-541` into `std::array<bool, 64>`.
- Similar unchecked writes exist in combo and bucket enumeration, for example `src/ranges/propagation.cpp:73-76` and `src/solver/hunl_bucket_map.cpp:24-27`.

Because cards are `uint8_t`, a value such as 255 passes configuration validation and produces an out-of-bounds write. Validly encoded duplicates instead produce impossible decks, incorrect chance denominators, and potentially duplicate cards at showdown.

Implemented fix:

- Added shared allocation-free `is_valid_card` and `are_valid_and_distinct_cards` routines. Valid cards are exactly rank 2 through ace with a suit in the encoded low two bits.
- `HUNLConfig::validate` now rejects invalid or duplicate board cards, invalid or overlapping fixed private cards, and any board/private-card collision before state construction.
- Initial and player range inputs now reject invalid or duplicate hole cards, board-blocked hands, empty ranges, NaN, infinity, negative values, overflowing totals, and zero total mass.
- Combo enumeration, dead-card masks, sampled chance enumeration, private-card cloning, and bucket-map board/hand lookup validate card sets before indexing fixed 64-card arrays.

Original recommended fix:

Create one shared, allocation-free validation routine for a card, board, fixed private cards, and dead-card union. Call it at every public configuration/range boundary. Reject invalid rank/suit, duplicates, private-card overlap, and board overlap before constructing a state. Also validate finite range weights.

Regression tests added:

- A dedicated `test_hunl_card_validation` target covers low/high invalid encodings, duplicate boards, every fixed-hole collision class, range card validity, finite/non-negative/positive range mass, combo and dead-card utility boundaries, mutable-state chance safety, and cloned-hole validation.

Original regression recommendations:

- Reject card values below the encoding for rank 2 and above the encoding for ace/suit 3.
- Reject every duplicate combination across board and both players’ holes.
- Reject range entries with invalid cards, NaN, infinity, negative weight, and all-zero mass where a distribution is required.
- Run validation-focused tests under ASan/UBSan when sanitizer CI is available.

### P1-1: `initial_ranges` are accepted but never affect the solver

Status: **fixed on 2026-07-21 by making the unsupported range contract fail closed.**

Original evidence:

- `initial_ranges` appear in implementation code only in `resolve_range_policy` and validation (`src/games/hunl.cpp:237-319`).
- The actual bucket-map setup applies only `graph.config->player_ranges` at `src/solver/hunl_flat_dcfr.cpp:143-147`.
- `solve_hunl_postflop` requires fixed `initial_hole_cards` at `src/games/hunl_solver.cpp:130-134`.
- The previous integration test only checked that output remained finite after setting an `initial_ranges` entry. It never checked that changing the range changed reach, value, or strategy.

Impact:

The API implies that a caller can supply initial public-state ranges, but those ranges are dead configuration. A blueprint or subgame resolver can silently solve the fixed two hands from `initial_hole_cards` while ignoring the supplied distributions. Exploitability and game value are then computed for the fixed-hand state as well.

Implemented contract:

The current exact postflop implementations support only the explicit-hand contract. Range/bucket solving is not yet implemented, so range-bearing requests are rejected before graph construction or solver work:

1. Explicit-hand solving requires fixed private cards and accepts no range fields.
2. `RequireExplicit` and `UseInitialRanges` require an entry for both heads-up players and reject fixed private cards in the same configuration.
3. `player_ranges` is retired and rejected rather than being silently projected into bucket priors.
4. Recursive and flat entry points reject the range contract before solving; direct flat construction has the same guard.

The future range/bucket implementation still needs to convert `initial_ranges` into joint, blocker-aware root combo/bucket reach, normalize it under an explicit zero-mass policy, and propagate it through chance and action transitions. Until that pipeline exists, rejecting the request prevents a fixed-hand solve from being mislabeled as a range solve.

Regression tests:

- Explicit-hand configurations validate and enter the solver contract normally.
- Partial `RequireExplicit` and `UseInitialRanges` configurations are rejected.
- `RequireExplicit` now requires every active heads-up player’s range.
- Range policies and fixed private cards are mutually exclusive.
- `Uniform` rejects supplied `initial_ranges`, and the retired `player_ranges` field is rejected.
- Recursive and flat public entry points reject range requests before solving.
- Direct flat-backend construction rejects legacy bucket-prior input.
- Range validation covers invalid cards, blockers, duplicate cards, non-finite/negative weights, empty ranges, and zero mass.

The original value/equity equivalence tests remain future requirements for the actual range solver: they cannot be asserted while the supported behavior is explicit rejection.

### P1-2: sampled storage advertises precision modes that it does not implement

Status: **fixed on 2026-07-21 by rejecting unsupported sampled precisions.**

Original evidence:

- The constructor stores any requested `HUNLFlatStoragePrecision` (`src/solver/hunl_sampled_storage.cpp:7-11`), and sampled config validation does not reject `Float64` or `Compressed16`.
- Both backing arrays are always `std::vector<float>` (`include/solver/hunl_sampled_storage.hpp:127-128`).
- Views always expose `float*`.
- Storage and preflight estimates always use `sizeof(float)` (`src/solver/hunl_sampled_storage.cpp:98-109,176` and `src/solver/hunl_sampled_solver.cpp:196`).

A caller requesting Float64 still receives Float32 behavior while `precision()` reports Float64. If true Float64 arrays are later introduced without fixing preflight, the guardrail will underestimate row memory by roughly two times. `Compressed16` is equally misleading.

Implemented fix:

Sampled configuration validation now accepts only `Float32`, which matches the `std::vector<float>` backing arrays and `float*` row views. `HUNLSampledStorage` independently rejects `Float64` and `Compressed16`, so direct construction cannot advertise an unsupported representation. `HUNLSampledSolver` validates the configuration before constructing storage. The existing memory estimates therefore remain consistent with the only supported element type.

Regression tests:

- `Float64` requests fail during sampled config validation and solver construction.
- `Compressed16` requests fail during sampled config validation and solver construction.
- Direct sampled storage construction rejects both unsupported precisions.
- When Float64 is implemented, a value distinguishable from its Float32 rounding must round-trip.
- Estimated row bytes must match actual capacity bytes for each supported precision.

The last two items remain future requirements if additional precision modes are implemented.

### P1-3: sampled public-chance isomorphism ignores private-range symmetry

Status: **fixed on 2026-07-21 by disabling sampled public-chance collapse until private-state remapping exists.**

Original evidence:

- Sampled isomorphism is enabled by default (`include/solver/hunl_sampled_config.hpp:39`).
- `src/solver/hunl_sampled_builder.cpp:95-109` collapses outcomes using only `state.board` and the public outcomes.
- `canonicalize_public_chance_outcomes` groups by the public-board stabilizer (`src/util/suit_iso.cpp:92-116`); it does not verify that private hand sets and their reach weights are closed and invariant under the suit permutation.
- The older exact suit-isomorphism cache contains precisely those hand-permutation and reach-symmetry checks in `src/util/suit_iso.cpp:151-251`, but the sampled builder does not use them.

Public cards that are isomorphic relative to the board need not be equivalent relative to an asymmetric private range or fixed hole cards. Collapsing them and retaining only one representative can therefore change blockers, hand strength, bucket assignment, and value.

Implemented fix:

Sampled public-chance isomorphism is now disabled by default. The builder always expands every public chance outcome, even when the legacy request flag is explicitly set, because it cannot yet prove private-hand/range closure or remap the relative suit permutation through blockers, buckets, and downstream rows. The `chance_isomorphic` marker remains false for these complete expansions.

Regression tests:

- The default sampled configuration has public-chance isomorphism disabled.
- A builder with the legacy flag set still expands all outcomes and reports no isomorphic collapse.
- Fixed-hole chance states are covered by the no-collapse regression, preventing public-board symmetry from bypassing private-state blockers.
- Symmetric-range representative/permutation equivalence remains a future requirement once remapping is implemented.

### P2-1: sampled memo keys silently truncate long betting histories

Status: **fixed on 2026-07-21 by rejecting sampled history overflow.**

Original evidence:

- Sampled keys have a fixed 48-code buffer (`include/solver/hunl_sampled_builder.hpp:21`, from `HUNL_MAX_HISTORY_CODES`).
- `src/solver/hunl_sampled_builder.cpp:230-249` caps each copied segment to remaining space and silently discards the suffix.
- Equality and hashing then use only the truncated representation.
- By contrast, the exact flat builder rejects history overflow in `src/games/hunl_flat_builder.cpp:181-222`.

Two distinct states sharing the first 48 codes can become the same sampled node. This can merge different action histories, infosets, stacks, or future semantics. Contributions and stacks reduce some collision cases, but do not make the history suffix redundant, especially after street transitions or when future multiway action order is added.

Implemented fix:

Sampled key construction now rejects histories that exceed the fixed 48-code buffer, contain more street segments than the key can represent, or try to append current-street history after all four street slots are occupied. Segment and current-street copies are bounds-checked before writing, and `make_key` is no longer `noexcept`, so overflow becomes a deterministic `std::invalid_argument` instead of silently discarding a suffix. Equality and hashing continue to cover the complete represented key. A future production sparse solver may replace this bounded representation with an interned immutable history ID.

Regression tests:

- Overlong histories differing after the first 48 codes are rejected deterministically and cannot alias.
- Excess street segments are rejected rather than ignored.
- Overflow in each individual segment and cumulative overflow across segments are rejected.
- Exact-capacity histories, including split street/current layouts, remain valid.
- Distinct suffixes within capacity produce distinct keys.
- Builder initialization propagates the explicit overflow error.

### P2-2: sparse row reuse and offsets lack shape/size invariants

Status: **fixed on 2026-07-21 with shape validation and checked `size_t` row arithmetic.**

Original evidence:

- `HUNLSampledStorage::ensure_row` returns an existing row solely by `InfosetId` at `src/solver/hunl_sampled_storage.cpp:14-18`; it does not verify player, street, bucket count, or action count.
- Offsets and `value_count()` are 32-bit (`include/solver/hunl_sampled_storage.hpp:25-35`).
- `src/solver/hunl_sampled_storage.cpp:26-27` narrows `vector::size()` to `uint32_t` before growing the vectors.

If an infoset ID is accidentally reused with a different abstraction or action menu, the caller receives a row with the old shape and may index it using the new shape. Large sparse stores can also wrap offsets beyond 2^32 float values (16 GiB per array), which is reachable below the repository’s 64 GB machine limit.

Implemented fix:

`ensure_row` now compares player, street, bucket count, and action count whenever an `InfosetId` is reused, throwing immediately on mismatch. Row offsets use `size_t`; value-count multiplication is checked and allocation additions are validated before vector growth. The sampled storage memory estimate continues to account for vector capacity, while solver-level preflight remains responsible for rejecting unsafe total allocations. Row views are explicitly documented as invalidated by storage growth; callers must reacquire them after adding rows.

Regression tests:

- Reusing an ID with a different player, street, bucket, or action shape throws.
- Checked `size_t` value-count arithmetic has boundary coverage, including narrow-host overflow behavior.
- Exact row reuse preserves values, and reacquisition after row growth preserves the row contents.
- View invalidation after growth is explicit in the API contract and covered by reacquisition tests.

## Multiway-specific architecture blockers

These are not accidental heads-up bugs; they show why 3-6 player support needs a new game-state and traversal design rather than mechanical array widening.

### Blocker A: game state and action progression are intrinsically two-player

Examples include `std::array<..., 2>` for holes, stacks, contributions, folds, and all-ins in `include/games/hunl.hpp`; opponent selection with `1U - player` in `src/games/hunl.cpp:730,743,757,787`; and terminal detection as “either player folded” at `src/games/hunl.cpp:477-478`. Multiway poker needs an active-player ring, per-street pending responders, last full raise size, reopening rules, and termination when one player remains or all non-folded players have matched/all-in.

### Blocker B: terminal utilities and pots have no side-pot model

The current utility and sampled terminal interfaces store exactly two contributions and identify one folding player. Multiway all-ins require ordered side pots, eligibility sets, multiple winners/ties per pot, uncalled-bet refunds, and one utility per seat. Terminal evaluation should consume a precomputed pot/eligibility structure rather than infer everything in a hot loop.

### Blocker C: solver math is two-player zero/constant-sum CFR

The exact flat solver stores `player0_reach_` and `player1_reach_`, uses two-element bucket counts, and derives counterfactual reach from “the other player.” Multi-player CFR needs a product of all opponents’ reaches for each traverser and has weaker convergence guarantees in general-sum settings. Exploitability is also no longer the simple heads-up best-response sum. The project must define the target metric (for example NashConv) and acceptable algorithmic guarantees before implementation.

### Blocker D: private-state and abstraction interfaces are pairwise

Hole pairs, range arrays, suit permutations, showdown matrices, and exported infosets are all specialized to two players. A multiway blueprint needs blocker-correct joint sampling without materializing the full Cartesian product, per-seat ranges, and terminal evaluation that samples or factors compatible private hands across all surviving seats.

## Recommended repair order

1. Make unimplemented sampled solve operations fail explicitly so uniform output cannot be mistaken for a solve.
2. Fix card/deck validation and the nonterminating memory fallback.
3. Define and implement the root range contract; add semantic tests showing ranges change values and strategies.
4. Implement scalar, deterministic heads-up external-sampling MCCFR and compare it continuously with the exact solver on tiny games.
5. Fix precision, row-shape, offset, and isomorphism contracts before performance work.
6. Add depth-limited leaf callbacks/value-table interfaces and validate them against exact continuation values on small trees before training a network.
7. Design a separate multiway state/terminal/traversal module with variable seats, action rings, side pots, joint private sampling, and a defined convergence/quality metric.

## Minimum correctness gates before multiway development

- Sampled and exact values agree within statistical tolerance on tiny flop/turn/river games.
- Fixed seeds reproduce results across worker counts through per-trajectory seeding and fixed-order merges.
- Range/blocker tests cover asymmetric, zero-mass, suit-specific, and single-combo ranges.
- Collapsed and uncollapsed public chance agree whenever collapse is enabled.
- Memory preflight uses checked arithmetic, terminates, and upper-bounds measured live allocations.
- Root export comes from the solved row and reports the action IDs corresponding to the root legal-action menu.
- Depth-limited leaf values have an explicit perspective, units, pot convention, and error test against exact continuation.
- No public API can return a plausible normalized strategy while silently ignoring its solve request or range inputs.
