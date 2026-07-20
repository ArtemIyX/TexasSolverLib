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

Evidence:

- `src/solver/hunl_sampled_solver.cpp:52-56` converts every positive time budget into exactly one batch; it never measures elapsed time.
- `src/solver/hunl_sampled_solver.cpp:100-103` records the requested traversal count in the profile without executing traversals.
- `src/solver/hunl_sampled_solver.cpp:105` always exports a uniform root strategy.
- `src/solver/hunl_sampled_solver.cpp:110-111` reports the requested batches as completed and always sets `timed_out = false`.

Why this is dangerous:

A caller can request a 10-15 second solve, receive `batches_completed > 0`, a nonzero `profile.traversals`, and a normalized strategy, and reasonably treat it as solver output. The result is actually uniform regardless of state, seed, iterations, storage contents, or time budget. This is a silent correctness failure, which is more dangerous than an explicit “not implemented” error.

Recommended fix:

1. Until the engine is implemented, make positive-budget and positive-batch calls fail explicitly with a dedicated `not_ready` status or exception. Keep zero-budget uniform export only if the API documents it as an initialization query.
2. Implement a real batch loop that partitions trajectory IDs, invokes traversal, merges worker-local deltas in fixed order, applies discounting, and exports the root row.
3. Derive profile counters only from completed work.
4. In `solve_for`, check `steady_clock` between bounded minibatches and set `timed_out` from the actual stop condition.

Regression tests:

- A one-batch solve must change at least one regret or strategy-sum entry in a tiny nontrivial game.
- Different positive budgets must permit different completed batch counts; zero budget must perform none.
- Reported traversal count must equal the number of actual traversal invocations.
- A deliberately slow fake traversal must cause `timed_out = true`.

### P0-2: sampled traversal is not MCCFR and returns zero for every showdown

Evidence:

- `src/solver/hunl_sampled_traversal.cpp:30-35` only checks whether a row exists and increments `infosets_updated`; it does not update the row.
- `src/solver/hunl_sampled_traversal.cpp:49` passes the constant `0.0` to the showdown evaluator even though sampled nodes already contain `terminal_utility`.
- `src/solver/hunl_sampled_traversal.cpp:59` chooses `trajectory_id % edge_count` at every depth.
- The traversal never uses edge probability, current strategy, counterfactual reach, sampling reach, `request.iteration`, the configured seed, or the SIMD regret kernels.

Consequences:

- All showdown trajectories have value zero.
- A trajectory selects the same ordinal edge at every node where that ordinal exists, creating a strongly correlated and biased path distribution.
- Chance edges are not sampled according to `HUNLSampledEdge::probability`.
- Opponent actions are not sampled from regret-matched strategy.
- Traverser actions are not enumerated for external-sampling counterfactual values.
- `infosets_updated` means “row was present,” not “an update was made.”

Recommended fix:

Implement a scalar reference external-sampling traversal before optimizing it. Its inputs need explicit private-hand/bucket state, deterministic per-trajectory RNG, traverser identity, sampling probability, opponent reach, and worker-local delta storage. At a traverser node, enumerate every action; at opponent and public-chance nodes, sample from the correct distribution and carry importance weights where required. Terminal evaluation must use fold/showdown utilities for the sampled private state or the depth-limited value function.

Regression tests:

- Compare sampled mean values against exact enumeration on a tiny river tree.
- Verify empirical chance frequencies against edge probabilities with a fixed reproducible seed set.
- Verify that trajectory IDs do not force a constant edge ordinal down the entire path.
- Verify nonzero and correctly signed showdown values for win, loss, and tie.
- Compare one external-sampling regret update to a hand-computed toy game.

### P0-3: adaptive memory fallback can loop forever while increasing the estimate

Evidence:

- `src/solver/hunl_sampled_solver.cpp:229-241` loops while the estimate exceeds the hard limit and a configuration field changes.
- After other fallback levers are exhausted, `src/solver/hunl_sampled_solver.cpp:280` increments `depth_limit_plies_hint` on every iteration.
- `src/solver/hunl_sampled_solver.cpp:302-307` multiplies terminal-cache memory by `1 + depth_limit_plies_hint`.

The last fallback therefore increases estimated memory. Because the hint changes on every iteration, the “no progress” comparison never breaks the loop. A configuration still above the hard limit after minibatch, traversal, average-strategy, and bucket reductions can hang forever inside `preflight()`.

Recommended fix:

- Define the depth field unambiguously. If it means maximum plies searched, reducing it should reduce memory. If it means “cut off this many plies earlier,” rename it and make the estimator monotonic in the correct direction.
- Require every fallback step to produce `new_estimate < old_estimate`; otherwise stop and reject.
- Add a small fixed maximum number of fallback steps.
- Use saturating/checked arithmetic in all estimate products and sums so overflow becomes rejection, not a small estimate.

Regression tests:

- Construct a limit that remains impossible after all valid reductions and assert that preflight returns `Rejected` promptly.
- Assert that every recorded adaptive step strictly decreases the estimate.
- Exercise maximum `workers`, bucket hints, and cache sizes and assert overflow-safe rejection.

### P0-4: card inputs are not validated and can index fixed arrays out of bounds

Evidence:

- `HUNLConfig::validate` checks board length at `src/games/hunl.cpp:286-289`, but does not check card encoding, duplicate board cards, duplicate/overlapping fixed hole cards, or hole/board overlap.
- Range validation at `src/games/hunl.cpp:63-96` checks same-card holes and board blocking, but does not validate that a card encodes rank 2-14 and suit 0-3.
- `HUNLState::chance_outcomes` writes `held[c]` directly at `src/games/hunl.cpp:536-541` into `std::array<bool, 64>`.
- Similar unchecked writes exist in combo and bucket enumeration, for example `src/ranges/propagation.cpp:73-76` and `src/solver/hunl_bucket_map.cpp:24-27`.

Because cards are `uint8_t`, a value such as 255 passes configuration validation and produces an out-of-bounds write. Validly encoded duplicates instead produce impossible decks, incorrect chance denominators, and potentially duplicate cards at showdown.

Recommended fix:

Create one shared, allocation-free validation routine for a card, board, fixed private cards, and dead-card union. Call it at every public configuration/range boundary. Reject invalid rank/suit, duplicates, private-card overlap, and board overlap before constructing a state. Also validate finite range weights.

Regression tests:

- Reject card values below the encoding for rank 2 and above the encoding for ace/suit 3.
- Reject every duplicate combination across board and both players’ holes.
- Reject range entries with invalid cards, NaN, infinity, negative weight, and all-zero mass where a distribution is required.
- Run validation-focused tests under ASan/UBSan when sanitizer CI is available.

### P1-1: `initial_ranges` are accepted but never affect the solver

Evidence:

- `initial_ranges` appear in implementation code only in `resolve_range_policy` and validation (`src/games/hunl.cpp:237-319`).
- The actual bucket-map setup applies only `graph.config->player_ranges` at `src/solver/hunl_flat_dcfr.cpp:143-147`.
- `solve_hunl_postflop` requires fixed `initial_hole_cards` at `src/games/hunl_solver.cpp:130-134`.
- `tests/test_ranges_solver_integration.cpp` only checks that output remains finite after setting an `initial_ranges` entry. It never checks that changing the range changes reach, value, or strategy.

Impact:

The API implies that a caller can supply initial public-state ranges, but those ranges are dead configuration. A blueprint or subgame resolver can silently solve the fixed two hands from `initial_hole_cards` while ignoring the supplied distributions. Exploitability and game value are then computed for the fixed-hand state as well.

Recommended fix:

Define two separate solve contracts:

1. **Explicit-hand validation solve:** fixed private cards, one hand per player, no range fields accepted.
2. **Range/bucket solve:** public board plus a required range for every active player, no fixed opponent cards required.

Convert `initial_ranges` into root combo/bucket reach exactly once, apply board blockers jointly, normalize with an explicit zero-mass policy, and propagate them through chance and action transitions. Remove or clearly redefine the overlapping `player_ranges` field.

Regression tests:

- Two disjoint root ranges with different equities must produce different root values.
- A single-combo range must match the equivalent explicit-hand solve.
- `RequireExplicit` must require every active player’s range, not merely at least one entry as currently checked at `src/games/hunl.cpp:294-300`.
- Recursive and flat backends must either honor the same range contract or explicitly reject it.

### P1-2: sampled storage advertises precision modes that it does not implement

Evidence:

- The constructor stores any requested `HUNLFlatStoragePrecision` (`src/solver/hunl_sampled_storage.cpp:7-11`), and sampled config validation does not reject `Float64` or `Compressed16`.
- Both backing arrays are always `std::vector<float>` (`include/solver/hunl_sampled_storage.hpp:127-128`).
- Views always expose `float*`.
- Storage and preflight estimates always use `sizeof(float)` (`src/solver/hunl_sampled_storage.cpp:98-109,176` and `src/solver/hunl_sampled_solver.cpp:196`).

A caller requesting Float64 still receives Float32 behavior while `precision()` reports Float64. If true Float64 arrays are later introduced without fixing preflight, the guardrail will underestimate row memory by roughly two times. `Compressed16` is equally misleading.

Recommended fix:

Either reject every precision except Float32 until implemented, or use precision-specific storage variants and typed kernels/views. The estimator must use the selected element size and include alignment/capacity overhead consistently.

Regression tests:

- Unsupported precision requests must fail during config validation.
- When Float64 is implemented, a value distinguishable from its Float32 rounding must round-trip.
- Estimated row bytes must match actual capacity bytes for each supported precision.

### P1-3: sampled public-chance isomorphism ignores private-range symmetry

Evidence:

- Sampled isomorphism is enabled by default (`include/solver/hunl_sampled_config.hpp:39`).
- `src/solver/hunl_sampled_builder.cpp:95-109` collapses outcomes using only `state.board` and the public outcomes.
- `canonicalize_public_chance_outcomes` groups by the public-board stabilizer (`src/util/suit_iso.cpp:92-116`); it does not verify that private hand sets and their reach weights are closed and invariant under the suit permutation.
- The older exact suit-isomorphism cache contains precisely those hand-permutation and reach-symmetry checks in `src/util/suit_iso.cpp:151-251`, but the sampled builder does not use them.

Public cards that are isomorphic relative to the board need not be equivalent relative to an asymmetric private range or fixed hole cards. Collapsing them and retaining only one representative can therefore change blockers, hand strength, bucket assignment, and value.

Recommended fix:

Disable sampled chance collapse by default until private-state remapping is implemented. A valid collapse must carry the relative suit permutation, prove closure and equal weights for every active player range, and remap sampled private hands/buckets and downstream rows. If any proof fails, expand the outcomes explicitly.

Regression tests:

- With an asymmetric suit-specific range, collapsed and uncollapsed exact values must match; until remapping exists, assert that collapse is disabled.
- With symmetric ranges, compare representative-plus-permutation evaluation to full enumeration.
- Include fixed-hole blocker cases where two board-isomorphic cards are not private-state-equivalent.

### P2-1: sampled memo keys silently truncate long betting histories

Evidence:

- Sampled keys have a fixed 48-code buffer (`include/solver/hunl_sampled_builder.hpp:21`, from `HUNL_MAX_HISTORY_CODES`).
- `src/solver/hunl_sampled_builder.cpp:230-249` caps each copied segment to remaining space and silently discards the suffix.
- Equality and hashing then use only the truncated representation.
- By contrast, the exact flat builder rejects history overflow in `src/games/hunl_flat_builder.cpp:181-222`.

Two distinct states sharing the first 48 codes can become the same sampled node. This can merge different action histories, infosets, stacks, or future semantics. Contributions and stacks reduce some collision cases, but do not make the history suffix redundant, especially after street transitions or when future multiway action order is added.

Recommended fix:

Reject overflow during the current implementation, matching the exact builder. For production sparse solving, use a collision-safe compact history identity: for example an interned immutable history node ID plus street metadata, or a sufficiently wide encoded key with full equality verification. Do not rely on a hash alone.

Regression tests:

- Construct two legal/high-cap states that share the first 48 codes and differ afterward; they must not memoize to the same node.
- Verify that overflow is explicit and deterministic until an unbounded representation is implemented.

### P2-2: sparse row reuse and offsets lack shape/size invariants

Evidence:

- `HUNLSampledStorage::ensure_row` returns an existing row solely by `InfosetId` at `src/solver/hunl_sampled_storage.cpp:14-18`; it does not verify player, street, bucket count, or action count.
- Offsets and `value_count()` are 32-bit (`include/solver/hunl_sampled_storage.hpp:25-35`).
- `src/solver/hunl_sampled_storage.cpp:26-27` narrows `vector::size()` to `uint32_t` before growing the vectors.

If an infoset ID is accidentally reused with a different abstraction or action menu, the caller receives a row with the old shape and may index it using the new shape. Large sparse stores can also wrap offsets beyond 2^32 float values (16 GiB per array), which is reachable below the repository’s 64 GB machine limit.

Recommended fix:

- On reuse, compare the complete shape and fail immediately on mismatch.
- Use `size_t` or checked 64-bit offsets and value counts.
- Check multiplication/addition before allocation and include the attempted allocation in memory preflight.
- Return stable row handles/offsets rather than raw pointers that become invalid after vector growth.

Regression tests:

- Reusing an ID with a different player/street/action/bucket shape must throw.
- Boundary tests must reject multiplication and offset overflow.
- A retained row handle must remain valid by contract, or the API must make invalidation explicit and tests must enforce correct reacquisition.

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
