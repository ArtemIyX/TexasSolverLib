# Follow-up code review before multiway solving

Date: 2026-07-22

Reviewed revision: `6a48334`

## Executive verdict

The repository is **not yet ready to begin an integrated production multiway solver**. The previous fixes improved the standalone multiway helpers and the sampled heads-up scaffolding, but this follow-up found new correctness defects in the newly exposed structured-range path and in multiway betting progression.

The highest-risk issue is that a structured range solve builds its public graph and infoset identities from the first joint private deal, sends every sampled deal through bucket zero, and substitutes the sampled cards only at terminal evaluation. This cannot learn blocker- or hand-dependent strategy. On flop and turn roots it also samples future boards using the first deal's blockers, so a trajectory can select a public card held by its own sampled private deal.

The second major issue is solver-session state. Every `run_batches()` call rebuilds public-state IDs from zero but retains the old regret/strategy storage and restarts iteration and trajectory IDs. Reusing a solver can therefore replay identical trajectories or attach an old row to a different infoset with the same numeric ID.

There are also concrete multiway state-machine defects: a covering stack receives meaningless betting actions after all opponents are all-in, all-in raises can be represented twice, and snapshots can validate while remaining stuck or granting inconsistent action rights.

Positive multiway implementation should wait until the P0 and P1 findings below are fixed or the affected public APIs fail closed.

## Scope and method

This was a static review of the current working tree, with emphasis on code added or changed after:

- `docs/code_review_before_multiway.md`;
- `docs/readiness_review_before_multiway_2026-07-22.md`;
- the contracts in `docs/mccfr_large_tree_implementation_plan.md` and `AGENTS.md`.

The review covered the structured HUNL root/range path, sampled builder/traversal/scheduler/storage/export integration, multiway betting snapshots and progression, private sampling/showdown, terminal settlement, CFR update helpers, and quality diagnostics. Resolved findings from the first two reviews are not repeated unless the current implementation exposes a related defect through a new path.

No build, test, benchmark, install, or solver command was run, as required by `AGENTS.md`. Findings are based on source inspection. Severity meanings:

- **P0**: can silently solve the wrong game, contaminate solver state, or make the production-facing bounded solve contract materially false.
- **P1**: creates an incorrect game tree, update schedule, or externally supplied root state.
- **P2**: diagnostic, numerical, API, or coverage weakness that should be fixed before scaling an integrated solver.

## P0 findings

### P0-1: structured range solving aliases every private deal and uses the wrong public-card distribution

Status: **fixed fail-closed on 2026-07-22.** Positive structured-range requests now validate then throw `HUNLSampledStructuredRangeNotReady` before builder/storage initialization or traversal. This prevents the prior first-deal/bucket-zero path from returning a policy. Full range solving remains blocked until the private-state-aware traversal and exports described below are implemented.

Evidence:

- `HUNLSampledSolver::run_batches()` materializes the normalized joint range but initializes the builder with `joint_deals.front().hole` at `src/solver/hunl_sampled_solver.cpp:136-140`.
- Every trajectory is created with `bucket = 0` at `src/solver/hunl_sampled_solver.cpp:192-200`; no hand-to-bucket projection occurs later.
- A trajectory's sampled deal is stored only in `HUNLSampledTraversalRequest::private_hole` at `src/solver/hunl_sampled_solver.cpp:201-204`.
- The builder creates chance edges from the cached state's `chance_outcomes()` at `src/solver/hunl_sampled_builder.cpp:91-105`. `HUNLState::chance_outcomes()` removes the cached hole cards from the deck, so these probabilities are conditional on the first deal, not the trajectory deal.
- Builder infoset IDs are made from the cached state's hole-card-bearing infoset key at `src/solver/hunl_sampled_builder.cpp:300-309`.
- `private_hole` is not consulted during chance or decision traversal. It is substituted only after reaching a terminal node at `src/solver/hunl_sampled_traversal.cpp:133-136`.
- Root export returns only one bucket's action vector, defaulting to bucket zero, rather than a hand/bucket-indexed strategy.

Impact:

- Distinct private hands share one regret and average-strategy row, so the solver cannot learn blocker- or strength-dependent actions.
- The returned root mix is not a valid range strategy or blueprint training target.
- On flop and turn roots, board sampling is conditional on the wrong four private cards. A board card excluded by the first deal but available under the sampled deal is omitted; a card held by the sampled deal but not the first deal may be selected as public.
- When such an overlap reaches terminal evaluation, `clone_with_hole_cards()` rejects it. Depending on the random trajectory, the same valid structured request may therefore throw instead of completing.

Required fix:

1. Compile the range root into stable private-state or bucket IDs before traversal.
2. Sample a compatible private deal first, then sample public chance from the deck conditioned on that deal and current board, or apply a proven importance-weighted public-chance scheme.
3. Carry the trajectory's private/bucket identity through every decision lookup and delta.
4. Define whether root export is per combo, per bucket, or an explicitly weighted aggregate; do not present one bucket-zero row as the range solution.
5. Until this is implemented, make positive structured-range requests fail closed.

Minimum regression cases:

- Two river hands with opposite preferred actions must update different rows and export different policies.
- Swapping range weights must change the weighted aggregate without changing per-hand policies.
- On a turn root, no sampled river card may overlap the trajectory's private cards.
- The empirical river-card distribution must match exhaustive blocker-conditioned probabilities for a two-deal range.

### P0-2: sampled solver reuse retains old rows while resetting graph IDs and random-work cursors

Status: **fixed as an explicit fresh-run contract on 2026-07-22.** `run_batches()` now clears the builder, sparse storage, profile, and root export before each request. It therefore cannot silently continue or contaminate a prior solve. A resumable-session API remains future work and must introduce an explicit root identity plus monotonic cursor rather than changing this contract implicitly.

Evidence:

- `HUNLSampledBuilder::initialize()` calls `clear()` and recreates the root at `src/solver/hunl_sampled_builder.cpp:72-75`; `clear()` resets node and infoset lookup state at `src/solver/hunl_sampled_builder.cpp:195-203`.
- `HUNLSampledSolver::run_batches()` calls that initialization on every request at `src/solver/hunl_sampled_solver.cpp:165-171`.
- The same function never clears `storage_`. `HUNLSampledStorage::clear_keep_capacity()` exists at `src/solver/hunl_sampled_storage.cpp:199-204` but is not called by the solver.
- Storage is keyed only by numeric `InfosetId`; a reused ID with the same player/street/bucket/action shape is accepted without checking the original infoset identity.
- Batch, iteration, and trajectory numbering restart from zero/one on every call at `src/solver/hunl_sampled_solver.cpp:193-204`.

Impact:

- A new root can silently inherit regrets and strategy sums from an unrelated old root when row shapes happen to match.
- If shapes differ, an otherwise valid second solve can fail with a row-shape error caused by the preceding request.
- Repeating the same request replays identical trajectory seeds and overweights the same samples.
- There is no unambiguous distinction between "continue this solve" and "start a new solve."

Required fix:

Introduce an explicit solve session. Starting a session must clear or namespace graph/storage state and bind all rows to a root/config/abstraction identity. Continuing a session must preserve a monotonic iteration/player/trajectory cursor. Reject root or abstraction changes during continuation.

Minimum regression cases:

- Two different same-shaped roots solved sequentially with one solver must match results from two fresh solver instances.
- Two continuation calls must consume exactly the same global trajectory sequence as one combined call, without duplicates.
- Changing abstraction, bucket count, action menu, or blueprint identity must either create a fresh session or fail explicitly.

### P0-3: the structured heads-up root cannot represent an arbitrary live subgame

Status: **fixed fail-closed on 2026-07-22.** Balanced low-SPR postflop roots are now valid even when historical contributions exceed chips behind. Structured roots with unequal contributions are rejected before solving because the legacy scalar-stack state would otherwise invent the bettor's remaining chips. A complete heads-up betting snapshot remains the required design before enabling arbitrary online roots.

Evidence:

- `HUNLStructuredRootRequest` embeds `HUNLConfig` rather than a complete betting snapshot at `include/games/hunl_solver.hpp:19-28`.
- `HUNLConfig` has one scalar `starting_stack` and two initial contributions at `include/games/hunl.hpp:134-142`.
- Postflop initialization assigns the same remaining stack to both seats regardless of unequal contributions at `src/games/hunl.cpp:466-488`.
- Validation rejects either initial contribution above that scalar stack at `src/games/hunl.cpp:445-448`.
- The root contract has no pending responder set, amount faced when last acting, raise rights, last full raise size, or canonical action-history identity.

Impact:

An online or offline resolver cannot faithfully reconstruct common live roots. If one player has bet on the current street, the two remaining stacks are normally different, but the state gives both players the same stack. Low-SPR roots can also be rejected merely because historical contributions exceed chips remaining behind. These errors change legal bet sizes, all-in thresholds, terminal utility, and the strategy being solved.

Required fix:

Replace the scalar postflop root fields with a validated heads-up betting snapshot, analogous to the intended multiway snapshot but with explicit remaining stacks, total/street contributions, pending actor, raise metadata, history/menu identity, board, ranges, and value convention. Keep the legacy `HUNLConfig` constructor only for simple oracle roots.

### P0-4: `solve_for()` does not implement a timed solve

Status: **fixed fail-closed on 2026-07-22.** Positive time budgets now throw `HUNLSampledSolverNotReady`; only non-positive initialization/export requests remain available. This removes the one-batch pseudo-timeout behavior until a resumable deadline state machine exists.

Evidence:

- A positive budget is explicitly discarded at `src/solver/hunl_sampled_solver.cpp:122`.
- The method always runs exactly one batch at `src/solver/hunl_sampled_solver.cpp:123` and then unconditionally sets `timed_out = true`.
- It has no clock, deadline cursor, cancellation check, or partial-batch state.

Impact:

A 1 ms and a 15 second request do the same amount of work. This is incompatible with the stated bounded online sub-solving contract and makes timeout/batch diagnostics unsuitable for service control.

Required fix:

Use a resumable minibatch state machine with `steady_clock` checks at bounded coordinator boundaries. Report actual completed work and distinguish deadline exhaustion, requested-work completion, cancellation, memory stop, and worker failure. Until then, remove or fail closed the positive timed entry point.

## P1 findings

### P1-1: the sampled traverser schedule can permanently starve a player

Status: **fixed for bounded batch calls on 2026-07-22.** Traverser selection now derives from the batch-global trajectory ID, so odd minibatches alternate their extra traverser across batches. A future resumable session must continue the same global sequence.

Evidence:

- The traversing player is `trajectory & 1U`, using the trajectory's index inside the current minibatch at `src/solver/hunl_sampled_solver.cpp:197-200`.
- `minibatch_size` accepts every positive value, including one.
- Each new batch restarts the local trajectory index at zero.

Impact:

With `minibatch_size == 1`, only player 0 ever receives regret updates. With any odd minibatch, player 0 is updated more often in every batch. Existing small regression helpers use minibatch one, so normalization and completion assertions can pass without ever solving player 1.

Required fix:

Make traverser selection part of the persistent global work cursor, or run an explicit balanced player-update schedule. Profile and export per-seat traversal/update counts and require both seats before calling an iteration complete.

### P1-2: sampled configuration advertises modes and controls that execution ignores

Status: **fixed by removing unsupported sampled-solver controls on 2026-07-22.** The sampled config now exposes only the implemented external-sampling, lazy/sparse, deterministic behavior. The richer sampling-mode controls remain solely on the full-graph oracle config.

Evidence:

- `HUNLSampledSolverConfig` exposes `Exact`, `PublicChance`, `External`, and `AverageStrategy` modes plus iterations, traversals per iteration, sparse/lazy flags, and deterministic merge controls at `include/solver/hunl_sampled_config.hpp:11-48`.
- Positive execution always calls the same external-sampling traversal; `config_.mode` is not read in the run path.
- `iterations` and `traversals_per_iteration` do not determine executed work. The latter is validated and adaptively reduced even though `run_batches()` uses only `minibatch_size`.
- `deterministic_merge`, `lazy_public_expansion`, and `sparse_infosets` do not select alternate execution behavior.

Impact:

Callers and tests can believe they exercised exact, public-chance, or average-strategy sampling while all modes execute the same algorithm. Memory fallback can report a work reduction that does not reduce actual work.

Required fix:

Reduce the public config to implemented behavior and reject every unsupported value, or implement each mode with mode-specific statistical oracle tests. Every adaptive lever must measurably change both runtime behavior and its estimate.

### P1-3: betting continues when one covering stack is the only player able to act

Status: **fixed on 2026-07-22.** A completed round with at most one actionable live seat is betting-terminal and exposes `requires_board_runout()` when public cards still need to be dealt. It cannot advance to a fresh street that would give the covering seat unanswerable actions.

Evidence:

- `MultiwayState::is_hand_over()` stops for one live player or when every live player is all-in, but not when betting has completed and only one live player has chips at `src/games/multiway_state.cpp:156-167`.
- `begin_next_street()` marks every non-folded, non-all-in seat pending at `src/games/multiway_state.cpp:341-362`.

Concrete sequence:

On the flop with stacks `{100, 100, 1000}`, seat 0 moves all-in for 100, seat 1 calls all-in, and seat 2 calls while retaining 900. The round completes with seat 2 as the only actionable seat. The state is neither hand-over nor terminal, so the caller advances the street. Seat 2 is then offered check, bet, and all-in actions even though no opponent can respond.

Impact:

The tree contains strategically meaningless actions, including chips that will only be refunded. This duplicates equivalent continuations and distorts strategy probabilities and traversal cost.

Required fix:

Represent "betting closed; board runout still pending" separately from showdown/fold terminal state. After the last required call, if at most one live seat can act, transition through chance-only board runout with no decision nodes.

### P1-4: `Bet`/`Raise` and `AllIn` can encode the same physical action

Status: **fixed on 2026-07-22.** Non-all-in bet/raise targets must now leave chips behind; an all-in target is accepted only through `MultiwayAction::AllIn`. The legal menu suppresses Bet/Raise when no non-all-in full raise exists.

Evidence:

- Whenever a seat may raise and has more than the call amount, `legal_actions()` appends both `Bet`/`Raise` and `AllIn` at `src/games/multiway_state.cpp:228-241`.
- `apply(Bet/raise, target)` permits the target to consume the entire stack; the minimum-raise rejection is skipped when the resulting seat is all-in at `src/games/multiway_state.cpp:283-306`.

Concrete sequence:

Seat 0 bets 100 and seat 1 has 150 behind. Seat 1's menu contains both `Raise` and `AllIn`. `Raise` to 150 is accepted as a short all-in, and `AllIn` produces the same child state.

Impact:

An integrated graph can contain duplicate edges for one poker action. CFR then assigns separate regret mass to equivalent choices, and typed exports can present two labels for the same target.

Required fix:

Make non-all-in bet/raise targets strictly below the all-in target when `AllIn` is a separate semantic action, canonicalize child actions by physical target, and reject duplicate targets in action-menu construction.

### P1-5: snapshot validation accepts stuck and semantically inconsistent betting rounds

Status: **fixed on 2026-07-22.** Snapshot validation now requires a current player whenever actionable pending work exists, requires every live seat facing a wager to remain pending, and verifies raise rights against the recorded action/reopen metadata.

Evidence:

- `MultiwayBettingSnapshot::validate()` checks the current seat only when `current_player >= 0` at `src/games/multiway_state.cpp:69-73`.
- A snapshot with `current_player == -1` and an actionable pending seat is accepted.
- `from_snapshot()` calls `refresh_round_completion()`, but that function only clears the current player when no pending seat exists; it never selects a player when pending work exists at `src/games/multiway_state.cpp:194-207`.
- Validation does not derive `may_raise` from `has_acted`, `bet_faced_when_acted`, `current_bet`, and `last_full_raise_size`, and does not require every live seat below `current_bet` to remain pending.

Impact:

An external structured root can validate with pending action but expose no legal actions, causing premature street completion. It can also omit a required caller or grant/remove a raise right inconsistent with the supplied wager history.

Required fix:

Define and validate cross-field invariants, not only vector dimensions. Require exactly one current player whenever actionable pending work exists, validate the cyclic turn cursor, derive or verify pending responders and raise rights, and reject snapshots that change when round-tripped through `MultiwayState`.

## P2 findings

### P2-1: sampled solve profiles report pre-work storage and memory as if they were final

Status: **fixed on 2026-07-22.** The solver refreshes sparse storage and memory categories after merge/export, so the returned profile reflects final retained state rather than initialization-only state.

Evidence:

- Sparse storage and memory are recorded before trajectory preparation and row/public-state growth at `src/solver/hunl_sampled_solver.cpp:173-187`.
- After batches merge, the solver exports strategy but never refreshes `record_sparse_storage()` or `record_memory_budget()`.

Impact:

A successful result can report zero or stale sparse rows, values, and public states even though the solver retained substantially more memory. This weakens the runtime guardrail and any blueprint-generation telemetry.

Required fix:

Record peak and final retained capacity after coordinator preparation, worker allocation, merge, and export. Keep preflight estimates separate from observed live/peak metrics.

### P2-2: the showdown convenience API drops the configured odd-chip order

Status: **fixed on 2026-07-22.** `MultiwayShowdownInput` now carries `odd_chip_first_seat`, validates it, and passes it into terminal settlement. Real-hand showdown tests cover a two-way tied odd pot with nonzero positional order.

Evidence:

- `MultiwayTerminalInput` has explicit `odd_chip_first_seat` at `include/games/multiway_terminal.hpp:10-19`.
- `MultiwayShowdownInput` has no corresponding field at `include/games/multiway_private.hpp:60-66`.
- `evaluate_multiway_showdown()` creates a default terminal input and never sets odd-chip order at `src/games/multiway_private.cpp:155-168`.

Impact:

All showdown calls through this helper silently use seat 0 as the first odd-chip seat. Tied pots with a remainder can therefore violate the table's positional rule and introduce seat-ID bias despite the lower terminal layer supporting the correct policy.

Required fix:

Carry the odd-chip policy/order through `MultiwayShowdownInput`, or use fractional chip EV for solver utilities and reserve integer positional settlement for presentation.

### P2-3: exact NashConv accepts mathematically impossible negative improvements

Status: **fixed on 2026-07-22.** Exact-enumeration diagnostics now reject a best-response value materially below the profile value. Sampled estimates retain signed improvements because sampling error remains observable diagnostic information.

Evidence:

- Diagnostics default to `ExactEnumeration` at `include/solver/multiway_cfr.hpp:65-74`.
- `compute_multiway_nash_conv()` stores `best_response - profile` without checking its sign at `src/solver/multiway_cfr.cpp:195-213`.
- The regression named `multiway_nash_conv_sums_unilateral_improvements` supplies an exact-method best-response value below the profile value and expects a negative improvement.

Impact:

For exact evaluation a best response cannot be worse than following the profile. Accepting a material negative value hides evaluator, seat-order, unit, or policy-version mismatches and can understate NashConv.

Required fix:

Reject negative exact improvements outside a tight numerical tolerance. Preserve signed noise only for `SampledEstimate`, and report its uncertainty per seat as well as for the aggregate.

### P2-4: external-sampling importance ratios can become non-finite without rejection

Status: **fixed on 2026-07-22.** External-sampling updates reject non-finite importance weights/deltas, and row application preflights every resulting value before mutating the row. This makes a failed update transactional at the row level.

Evidence:

- Every input probability is only checked to be finite and in `[0, 1]`.
- The helper divides counterfactual and average-strategy weights by `sampling_reach` at `src/solver/multiway_cfr.cpp:162-171`.
- A positive subnormal sampling reach is accepted; the resulting ratio or deltas can overflow to infinity, and `apply_multiway_cfr_update()` adds them without a finiteness check.

Impact:

Multiway trajectories multiply more reach factors than heads-up trajectories, so underflow/overflow arrives sooner. One non-finite update can poison a sparse row permanently.

Required fix:

Use scaled/log-domain reach bookkeeping or a bounded importance-weight policy with a documented estimator tradeoff. Validate every produced delta before merge and abort the batch transactionally on non-finite values.

### P2-5: compiled private sampling still has no deterministic feasibility gate

Status: **fixed on 2026-07-22.** Compilation now removes zero-mass combos and performs bounded deterministic compatibility search before workers begin. Worker code can use `try_sample_into()` to receive a non-throwing rejection-budget result; the existing throwing method remains only as an API-boundary wrapper.

Evidence:

- `MultiwayCompiledPrivateRanges` compiles canonical cumulative per-seat tables, but `sample_into()` still uses whole-deal rejection up to `max_rejection_attempts` and throws from the sampling path at `src/games/multiway_private.cpp:116-134`.
- Construction validates each range independently but does not prove that any joint compatible deal exists or estimate acceptance probability.

Impact:

A feasible but low-acceptance range set can fail a worker nondeterministically, while an impossible set is discovered only during traversal. The method is allocation-free but still violates the intended no-exception hot-path contract.

Required fix:

Perform joint feasibility and acceptance preflight during compilation. Prefer sequential conditional sampling with precomputed compatibility mass, or return a non-throwing status that the coordinator handles before committing a batch.

## Missing regression gates exposed by this review

Before integrated multiway traversal begins, add tests for:

- structured range policies separated by private combo/bucket;
- blocker-conditioned turn/river chance distributions;
- fresh-session isolation and exact continuation cursor equivalence;
- balanced per-seat traversal and regret-update counts for minibatches 1, 3, and 5;
- rejection of unsupported sampled modes/config fields;
- one covering stack after all opponents are all-in, with chance-only runout;
- no duplicate physical targets across bet, raise, and all-in actions;
- snapshot rejection for `current_player == -1` with pending work and for inconsistent raise rights;
- odd-chip showdown order propagated through real hand evaluation;
- exact NashConv rejecting a materially negative unilateral improvement;
- transactional rejection of non-finite importance-weighted deltas;
- deterministic rejection or successful sampling of very low-acceptance joint ranges.

## Recommended repair order

1. Fail closed the current positive structured-range sampled path, or implement correct per-private-state chance, bucket, and infoset propagation.
2. Introduce explicit sampled solve sessions with root identity and a resumable global work cursor.
3. Replace the heads-up structured root's scalar stack/config representation with a complete validated live-state snapshot.
4. Make the timed API real and restrict sampled configuration to implemented semantics.
5. Correct the four multiway betting-state defects and strengthen snapshot invariants.
6. Lock these contracts down with exhaustive two- and three-player toy games before connecting multiway traversal/storage.
7. Then integrate multiway private sampling, chance, side pots, CFR deltas, deterministic scheduling, typed per-seat export, and quality evaluation.

## Go/no-go conclusion

**No-go for integrated production multiway solving at revision `6a48334`.** The standalone components remain useful reference helpers, but the current structured-range solver can solve the wrong private/public probability model, solver reuse is not state-safe, and the multiway betting layer admits incorrect trees. These are implementation blockers, not merely missing optimizations or diagnostics.
