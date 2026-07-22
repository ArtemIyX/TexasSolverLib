# Readiness review before multiway solving

Date: 2026-07-22

## Executive verdict

The repository is **not ready for production multiway solver implementation yet**. The standalone multiway betting, pot, private-card, and row-math helpers are useful foundations, but the heads-up production path still has correctness and memory blockers that would be inherited by a multiway implementation.

The highest-risk newly identified issue is in `HUNLFlatMCCFR::run_until`: deadline-based solving dispatches subbatches without rebuilding the current-strategy and average-policy caches. A timed solve can therefore report completed batches and export a plausible root strategy while deeper traversal continues to use the zero-filled caches created by the constructor. The second major blocker is that the full-graph MCCFR "sparse" mode allocates two dense `double` delta arrays for every infoset and every worker, then omits those arrays and other large caches from `memory_used_bytes()`.

The project also still lacks the contracts required by the stated product direction:

- the positive-work lazy/sparse solver is deliberately disabled;
- the public postflop solver rejects ranges and solves only two fixed private hands;
- there is no neural/value-table leaf-evaluator interface;
- depth-limited behavior differs materially between recursive DCFR, flat DCFR, flat MCCFR, and the lazy prototype;
- the multiway modules are not connected into a graph, traversal, storage, scheduler, export, or solve API.

Multiway work should proceed only after the P0 items below are either fixed or explicitly isolated behind fail-closed APIs. Adding player-indexed vectors to the current full-graph implementation would multiply its existing memory problem.

## Scope and method

This was a static review of the current working tree, including:

- public HUNL solve and configuration APIs;
- exact flat DCFR and full-graph MCCFR;
- sampled/lazy builder, traversal, storage, scheduler, export, profiling, and preflight code;
- range, bucket, showdown, and exploitability integration;
- multiway betting progression, side-pot settlement, private sampling, and CFR helper math;
- relevant tests and the previous review in `docs/code_review_before_multiway.md`.

No builds, tests, benchmarks, installs, or solver runs were performed, as required by `AGENTS.md`. The strategic file named by `AGENTS.md`, `docs/mccfr_large_tree_implementation_plan.md`, is not present in this working tree.

This document reports current problems rather than repeating resolved findings from the earlier review. A prior item marked "fixed" may still appear here when only a standalone helper was added and the production integration remains absent.

Severity definitions:

- **P0**: can silently produce an invalid strategy, defeats the 64 GB safety model, or blocks the core blueprint/neural/multiway architecture.
- **P1**: materially wrong or incomplete semantics that can corrupt convergence, continuation, terminal values, or diagnostics.
- **P2**: latent scale, API, test, or maintainability risk that should be resolved before large production trees make it expensive.

## P0 findings

### P0-1: deadline MCCFR did not refresh the strategy caches used by traversal

Status: **implemented on 2026-07-22; regression suite added, but not executed in this change.**

Original evidence before the 2026-07-22 fix:

- The constructor initializes `average_policy_cache_` rows to zero in `src/solver/hunl_flat_mccfr.cpp:224-227`.
- `compute_current_strategy_rows()` and `rebuild_average_policy_cache()` are called only by `run_player_batch()` at `src/solver/hunl_flat_mccfr.cpp:2034-2035`.
- Normal iteration solving uses `run_player_batch()` at `src/solver/hunl_flat_mccfr.cpp:251-253`.
- Deadline solving bypasses it and calls `run_player_subbatch()` directly at `src/solver/hunl_flat_mccfr.cpp:274-312`.
- Traversing-player expansion and opponent sampling read `average_policy_cache_` throughout `src/solver/hunl_flat_mccfr.cpp:1260-1515`.
- When a probability row has zero total, `sample_weighted_prefix()` selects action zero at `src/solver/hunl_flat_mccfr.cpp:108-116`.
- Existing deadline tests at `tests/test_hunl_flat_mccfr.cpp:1513-1553` cover only zero or negative budgets and therefore execute no batch.

Original impact:

A positive-budget `solve_for()`/`run_until()` request can count batches, touch rows, and export a normalized root strategy without running the policy that the current regrets imply. Opponent nodes are initially sampled as action zero, and their reach is multiplied by a cached probability of zero. Deeper regret updates can therefore disappear even though profiling reports traversal work. Root strategy sums can still change, which makes the result look more credible than a uniform placeholder.

Implemented fix:

- `run_iteration()`, `run_iterations()`, `run_batches()`, and `run_until()` now use one resumable subbatch state machine.
- `begin_player_batch()` rebuilds the current-strategy rows and aggregate action-policy cache exactly once before dispatching that player's first subbatch. The snapshot is frozen for that player batch and retained across deadline interruption/resumption.
- `run_until()` performs deadline checks only between subbatches; it no longer hand-rolls dispatch orchestration or bypasses cache setup.
- The state machine retains target iteration, traversing player, and trajectory cursor. A partial call resumes the next uncommitted subbatch rather than replaying the preceding work.
- A strategy-snapshot generation invariant rejects a dispatch that has no prepared snapshot or refers to a different generation.

Regression coverage added:

- 24 deterministic normal-versus-work-bounded cases cover Exact, PublicChance, and External modes; dense and sparse storage; one and three workers; and one- or two-player update schedules. They compare completed iterations, traversal counters, snapshot rebuild counts, exported average strategies, and root exports.
- Targeted cases cover zero work, one rebuild per player batch, rebuilds for both traversing players, deep regret updates through a non-zero opponent action, partial-batch resume without a duplicate rebuild, resumption through `run_iteration()`, and starting a fresh snapshot only after an iteration completes.
- A positive `solve_for()` case exercises the public deadline API and asserts that it dispatches at least one cache-backed batch.

Remaining verification:

- Build and run the new tests before treating this item as fully verified. No build or test command was run while making this documentation update.

### P0-2: full-graph MCCFR sparse mode allocates dense graph-sized deltas per worker and hides them from memory reporting

Status: **memory reporting instrumentation and 72 constructor/snapshot regression cases added on 2026-07-22; the dense per-worker delta allocation remains unresolved.**

Evidence:

- Every worker calls `prepare_delta_rows(infoset_meta_)` in the constructor at `src/solver/hunl_flat_mccfr.cpp:233-239`.
- `prepare_delta_rows()` creates one row for every infoset and assigns two `std::vector<double>` arrays of `meta.value_count` at `src/solver/hunl_flat_mccfr.cpp:429-443`.
- This happens even when `config.use_sparse_storage` is true.
- The allocation is approximately `16 * total_infoset_values * worker_count` bytes, before vector/allocation overhead.
- `memory_used_bytes()` at `src/solver/hunl_flat_mccfr.cpp:2669-2681` counts the graph, baseline rows, central sparse/dense storage, and dense table metadata, but not worker delta rows, worker scratch, policy caches, sampling caches, traversal metadata, chance metadata, touched-infoset flags, or hash-table capacity.
- `RootStrategySnapshot::memory_used_bytes` exposes this incomplete number as a runtime diagnostic.
- The solver receives an already constructed `HUNLFlatSolveGraph`, so even its central sparse mode does not provide lazy public-state expansion.

Impact:

The implementation violates the repository rule against per-worker graph-sized scratch. At production bucket counts the worker deltas can exceed the central strategy store, and adding workers multiplies them. A reported memory value below the warning threshold can coexist with actual allocations far above it. This defeats the 48/56/60 GB guardrails and can cause paging or allocation failure before timed solving starts.

Required fix:

1. Replace per-worker dense row tables with a preallocated active-row arena or sorted sparse delta stream sized by minibatch preflight.
2. Keep stable integer row IDs, but allocate value storage only for rows touched by that worker's current batch.
3. Include all retained capacity in memory accounting, especially worker scratch and policy/baseline caches.
4. Perform checked preflight before graph-sized and per-worker allocations; reject configurations that cannot remain below the hard limit.
5. Treat `HUNLFlatMCCFR` as a small-game oracle only. The production solver must not require a full `HUNLFlatSolveGraph`.

Implemented coverage and reporting:

- `HUNLFlatMCCFR::memory_usage()` now reports graph, infoset metadata, central storage, policy caches, traversal metadata, worker scratch, and baseline components. `RootStrategySnapshot::memory_used_bytes` uses this expanded component total.
- 72 deterministic tests cover Exact, PublicChance, and External modes; dense and sparse central storage; one, two, and four workers; one- and eight-trajectory subbatches; and no-baseline or moving-average-baseline configurations.
- Each case checks that snapshot memory equals the component total and that graph, metadata, policy cache, traversal metadata, and worker scratch are all represented. Sparse cases also assert that construction alone does not create central sparse rows.

Remaining P0 work:

- Replace `WorkerScratch::prepare_delta_rows()`'s complete-graph `double` delta allocation with a bounded active-row arena or sorted sparse delta stream. The new reporting makes this multiplier visible but does not remove it.
- Account for the remaining retained capacities, including worker baseline rows/lookups, thread containers, and hash-table bucket/node overhead, before treating the report as a conservative resident-memory bound.
- Run the new regression suite before treating the reporting coverage as verified; no build or test command was run for this update.

Regression gate:

- Increasing workers from one to N must not multiply scratch by the complete graph value count.
- Measured live allocation/RSS must remain below a conservative reported upper bound on representative flop, turn, and river configurations.
- Sparse mode must demonstrate that untouched infosets consume no regret, strategy-sum, or worker-delta value arrays.

### P0-3: the intended positive-work lazy/sparse solver is still unavailable

Status: **single-worker scalar positive-work path implemented on 2026-07-22; multithreaded production integration remains blocked by P1-1.**

Implemented change:

- Positive `run_batches()` requests with a structured root state now execute deterministic scalar sampled traversals, update sparse storage, record traversal diagnostics, and export the root average strategy when the root is a decision node.
- Positive `solve_for()` performs one bounded batch and returns a timed-out partial result. Requests without a structured root state, or with more than one worker, remain fail-closed until the concurrency-safe batch/merge design is complete.
- The test suite now includes 64 mode/action-count/batch-budget cases covering positive work, sparse-state growth, profile updates, root export, and the remaining fail-closed boundaries.

Remaining work:

- Replace the single-worker bridge with scheduler-driven worker-local traversal deltas, deterministic merge, actual deadline minibatch scheduling, and runtime cache/memory enforcement before calling this production-ready.

Evidence:

- `HUNLSampledSolver::solve_for()` throws `HUNLSampledSolverNotReady` for every positive budget at `src/solver/hunl_sampled_solver.cpp:85-91`.
- `run_batches()` throws the same exception for every positive batch count at `src/solver/hunl_sampled_solver.cpp:94-99`.
- Zero-work calls only initialize the root and export a uniform strategy at `src/solver/hunl_sampled_solver.cpp:118-149`.
- The standalone `HUNLSampledTraversal` is not connected to `HUNLSampledScheduler`, time budgeting, worker-local fixed-order merge, DCFR discounting, root export, or solve profiling.
- The only positive-work MCCFR implementation, `HUNLFlatMCCFR`, requires the full graph and has the P0-1/P0-2 issues above.

Impact:

There is currently no callable solver matching the documented production direction: lazy public expansion, sparse rows, bounded worker scratch, deterministic trajectory batching, root-only timed export, and 64 GB guardrails. Multiway implementation cannot safely use the full-graph prototype as its base without cementing the wrong memory architecture.

Required fix:

Build the positive-work solver around an explicit batch state machine:

1. validate and preflight a structured root request;
2. assign stable global trajectory IDs;
3. sample private/public chance and actions with per-trajectory seeds;
4. keep traversal deltas worker-local;
5. expand/publicly intern states through a concurrency-safe boundary;
6. merge worker results in fixed order;
7. update the root export only after a clean merge;
8. stop between bounded minibatches and report actual completed work.

Regression gate:

- Positive batch and positive time requests must change regrets on a hand-computed tiny game.
- Results must reproduce across repeated runs and agree across worker counts to the documented reproducibility level.
- The public API must never return "completed work" when only initialization occurred.

### P0-4: the public postflop solver has no range/blueprint solve contract

Evidence:

- `solve_hunl_postflop()` rejects any `initial_ranges`, `player_ranges`, `UseInitialRanges`, or `RequireExplicit` request at `src/games/hunl_solver.cpp:119-142`.
- It then requires `initial_hole_cards` for both players at `src/games/hunl_solver.cpp:143-147`.
- Consequently, current HUNL solve output is for one fixed pair of private hands, not for blocker-aware player ranges.
- `MultiwayPrivateConfig` and `sample_multiway_private_hands()` are standalone helpers. Searchable call sites are limited to their implementation, tests, and facade aliases; no solver traversal consumes their samples or range weights.
- The sampled request contains only an optional `HUNLState` and a root action count in `include/solver/hunl_sampled_solver.hpp:16-19`.

Impact:

A blueprint continuation or real-time subgame resolver starts from public state plus per-seat ranges/reaches, not a globally fixed deal. Without joint blocker-aware root reach, private sampling, bucket mapping, and posterior propagation, the current solver cannot represent the intended input. The fixed-hand exact solver also cannot serve as a semantic oracle for range-weighted terminal or root values.

Required fix:

1. Define one structured root contract containing public state, active seats, stacks/contributions, per-seat weighted ranges or blueprint reaches, abstraction version, and value units.
2. Normalize and condition ranges jointly on board and cross-seat blockers.
3. Sample or factor compatible private states without materializing the full Cartesian product.
4. Carry sampled hand/bucket IDs and correct reach/importance weights through traversal.
5. Define how a blueprint strategy seeds regrets, strategy sums, priors, or leaf continuation values.
6. Keep the current explicit-hand API fail-closed and label it as a test/oracle mode.

Regression gate:

- Single-combo ranges must agree with explicit-hand solves.
- Range permutation and suit-remapping tests must preserve value when symmetry conditions hold.
- Changing an asymmetric range must change blocker frequencies, value, and root strategy in expected directions.

### P0-5: depth-limited and neural leaf semantics are inconsistent or absent across solver paths

Evidence:

- Automatic backend selection sends every river solve and a default single-worker flop solve to recursive DCFR at `src/games/hunl_solver.cpp:39-51,177-181`; an explicit environment override can still force the flat backend.
- Recursive `DCFRSolver<HUNLState>` traverses `HUNLState` directly; `HUNLState` contains no traversal depth/cutoff state, so `HUNLConfig::depth_limit_plies` is ignored in that backend.
- Flat graph construction creates `DepthLimited` nodes with the default `{0, 0}` terminal utility at `src/games/hunl_flat_builder.cpp:358-364`.
- Flat DCFR overwrites player-zero cutoff values with `heuristic_depth_limited_value_p0()` at `src/solver/hunl_flat_dcfr.cpp:972-981`.
- That heuristic is based on pot size, contribution pressure, street, and a board-texture score; it has no private range, hand, bucket, blueprint, or neural input at `src/solver/hunl_bucket_terminal.cpp:410-443`.
- Flat MCCFR returns `meta.terminal_utility[traversing_player]` for `DepthLimited` nodes at `src/solver/hunl_flat_mccfr.cpp:641-644` and equivalent traversal kernels, which means the graph's zero placeholder is used.
- The lazy builder does not create depth-limited nodes at all; `depth_limit_plies_hint` is used by memory estimation but not by builder/traversal cutoff logic.
- No leaf-evaluator/value-network callback interface exists in `include/` or `src/`.

Impact:

The same configuration can mean full continuation, heuristic continuation, or zero continuation depending on backend. Strategies and reported values are therefore not comparable. A zero-valued cutoff can make all continuation actions look equivalent; the heuristic can inject arbitrary board/position bias; the recursive path can unexpectedly build/traverse far beyond the requested depth. Neural-network integration cannot be added safely until perspective, units, range inputs, batching, and failure behavior are explicit.

Required fix:

Define a common leaf contract before adding multiway traversal. At minimum it must specify:

- public-state and betting metadata;
- per-seat range/bucket reach and blocker conditioning;
- one value per active seat;
- perspective and units (chips, big blinds, pot fraction, or normalized stack fraction);
- whether committed chips and initial pot are included;
- constant-sum/zero-sum conservation rules;
- batched inference lifetime and thread ownership;
- deterministic fallback and timeout/error behavior;
- model and abstraction version identifiers.

All backends must either consume the same evaluator or reject depth limiting. Remove the heuristic from production paths or label it test-only.

Regression gate:

- Exact continuation values on tiny trees must be injectable as leaves and reproduce the untruncated root value/strategy.
- Every backend must produce the same cutoff-node values for the same evaluator.
- A mock batched evaluator must verify seat perspective, pot/contribution convention, and deterministic request ordering.

## P1 findings

### P1-1: the current sampled traversal cannot be safely parallelized as written

Evidence:

- `HUNLSampledTraversal::run()` receives shared mutable builder and storage references.
- Traversal calls `builder.ensure_expanded()` at `src/solver/hunl_sampled_traversal.cpp:139`.
- It creates central rows with `storage.ensure_row()` at `src/solver/hunl_sampled_traversal.cpp:171-177`.
- It merges the worker scratch back into central storage inside `run()` at `src/solver/hunl_sampled_traversal.cpp:320`.
- Builder expansion mutates several `std::vector` and `std::unordered_map` members, and storage growth can invalidate all row views.

Impact:

Calling this object concurrently from the deterministic scheduler would create data races, iterator/pointer invalidation, and non-deterministic merge order. The presence of `HUNLSampledWorkerScratch` does not make the traversal worker-local because row creation, graph expansion, and final writes are global.

Required fix:

- Separate traversal into a read-mostly snapshot/view plus a returned worker delta batch.
- Route new-state/information-set requests through a serialized, sharded, or deterministic post-batch intern/expansion phase.
- Perform all central row creation before exposing row views for the batch.
- Merge only on the coordinator in stable worker/row/action/bucket order.

### P1-2: sampled memory preflight assumes a cache size that runtime does not enforce

Evidence:

- With default `max_cached_public_states == 0`, preflight estimates public states as roughly `minibatch_size * workers` at `src/solver/hunl_sampled_solver.cpp:212-221`.
- Zero means no actual builder cap; `HUNLSampledBuilder::find_or_create()` retains every new state.
- The only runtime cap check is immediately after root initialization at `src/solver/hunl_sampled_solver.cpp:124-127`.
- `ensure_expanded()` and standalone traversal do not check the cap or live memory after growth.
- Builder memory estimates approximate unordered-map entries but do not account for bucket arrays and allocator overhead.

Impact:

Once positive work is enabled, a preflight that passes on tens of estimated states can operate an unbounded cache across many batches. The warning/hard-limit policy is therefore not an enforceable safety property.

Required fix:

Make zero mean an explicit bounded default or reject it in production. Reserve/cache-admit against a checked byte budget, sample live memory at bounded intervals, and stop cleanly before a growth operation can exceed the hard limit.

### P1-3: multiway CFR is labeled external-sampling but exposes only full-tree CFR weights

Evidence:

- `MultiwayCFRConfig` exposes only `ExternalSamplingMCCFR` in `include/solver/multiway_cfr.hpp:17-25`.
- `make_multiway_cfr_update()` is documented as a full-tree CFR update at `include/solver/multiway_cfr.hpp:55-63`.
- It accepts player reaches and chance reach, but no sampling reach or inclusion probability.
- It multiplies regret by chance reach and every opponent's reach at `src/solver/multiway_cfr.cpp:98-109`.
- No multiway trajectory traversal calls this helper.

Impact:

The helper is valid as isolated full-tree counterfactual row math, but it is not an external-sampling estimator. In external sampling, sampled opponent/chance paths require an explicit sampling probability and the appropriate importance ratio. Wiring raw sampled continuation values into this helper would generally apply the reach factor again and bias updates. Average-strategy accumulation has the same missing sampling contract.

Required fix:

Rename the existing helper as full-tree CFR math, then define a separate external-sampling update taking true reach, sampling reach/inclusion probability, traverser reach, and sampled continuation estimator. Validate it first on two-player toy games against the heads-up reference, then on three-player toy games with exhaustively enumerated expectations.

### P1-4: multiway betting cannot represent every legal reopening sequence or an arbitrary live subgame root

Evidence:

- After a seat checks/calls, `may_raise_` is set false at `src/games/multiway_state.cpp:181-189`.
- A single short all-in correctly does not reopen action, but successive short all-ins only set pending responders; they never reopen a prior actor when the cumulative amount faced reaches a full raise at `src/games/multiway_state.cpp:207-215,233-239`.
- `MultiwayGameConfig` has starting stacks, contributions, street contributions, first player, blind, and street, but no pending responders, last full raise size, amount faced when each player last acted, raise rights, folded/all-in seats, or last aggressor.
- Initialization marks every actionable seat pending and allowed to raise at `src/games/multiway_state.cpp:51-65`.

Impact:

Example: seat 0 bets 100, seat 1 calls, seat 2 moves all-in to 150, and seat 3 moves all-in to 200. Seat 0 now faces a cumulative 100-chip increase, equal to the last full raise, but remains `may_raise == false`. Separately, a real-time resolver cannot reconstruct an arbitrary in-progress state from observed/blueprint state without replaying the exact history, and replay is not part of the structured input contract.

Required fix:

Track the current bet faced when each seat last acted, derive reopening from cumulative increase, and add a validated snapshot constructor containing the full betting-round state. Add canonical action history/raise metadata needed by infoset keys and blueprint lookup.

### P1-5: precomputed multiway pot layouts can conserve chips and still settle the wrong winners

Evidence:

- The public overload `settle_multiway_terminal(input, layout)` validates refund size, non-negative amounts, eligible seat bounds/fold status, and total chip conservation at `src/games/multiway_terminal.cpp:78-105`.
- It does not verify that pot amounts, caps, or eligibility sets correspond to `input.contributions` and `input.folded`.
- It does not reject duplicate eligible seats.

Impact:

For contributions `{100,100,100}`, a supplied layout containing one 300-chip pot eligible only to seats `{1,2}` passes conservation and seat validation while silently excluding seat 0, even if seat 0 has the best hand. A stale cached layout or construction bug can therefore produce a plausible, zero-sum, but incorrect terminal result.

Required fix:

Make precomputed layouts opaque and construct them only through the validated builder, or store and verify an exact contribution/fold signature. In debug/validation mode, rebuild the expected layout and compare caps, amounts, unique sorted eligibility, and refunds.

### P1-6: reported heads-up exploitability changes definition and offset with backend selection

Evidence:

- Recursive `DCFRSolver` reports `br0 + br1` at `include/solver/dcfr.hpp:451-454`; parallel recursive DCFR does the same at `src/solver/parallel_dcfr.cpp:673-676,862-865`.
- The flat branch of `solve_hunl_postflop()` overwrites the value using `detail::exploitability<HUNLState>()` at `src/games/hunl_solver.cpp:221-224`.
- `detail::exploitability()` divides the total unilateral improvement by the player count at `include/solver/solver.hpp:255-261`.
- HUNL utility is constant-sum when `initial_pot` is non-zero: the two utilities sum to `initial_pot / big_blind` under the convention in `src/games/hunl.cpp:554-593`. The recursive `br0 + br1` expression does not subtract that on-policy constant before reporting the metric.
- Backend selection changes with street, worker count, force flag, and environment at `src/games/hunl_solver.cpp:39-51,177-181`.

Impact:

Equivalent strategies can be reported with both a player-count scaling difference and an additive initial-pot offset solely because a different backend was selected. For a constant-sum game, total unilateral improvement is `br0 + br1 - game_constant`, and per-player exploitability divides that result by two. This corrupts convergence dashboards, regression thresholds, and comparisons between exact, sampled, blueprint, and future multiway metrics.

Required fix:

Define named metrics: total NashConv and per-player exploitability (or the project's chosen convention). Store the name/units in output diagnostics and compute it once in the wrapper for every backend.

### P1-7: interrupted deadline solves replay deterministic trajectories when resumed

Evidence:

- `run_until()` starts each call at `target_iteration = iterations_ + 1` and `trajectory_begin = 0` at `src/solver/hunl_flat_mccfr.cpp:274-283`.
- Partial player/subbatch deltas are merged immediately.
- If the deadline interrupts an iteration, counters are accumulated but `iterations_` is not incremented at `src/solver/hunl_flat_mccfr.cpp:313-321`.
- Trajectory seeds include target iteration, traversing player, and trajectory ID at `src/solver/hunl_flat_mccfr.cpp:2234-2241`.

Impact:

A later `solve_for()` call repeats already merged player batches and trajectory IDs with identical seeds. This overweights early trajectories and may repeat player zero work before player one receives an update. Timed continuation is therefore not equivalent to a longer uninterrupted solve.

Required fix:

Persist an iteration/player/trajectory cursor and resume exactly after the last committed subbatch, or make partial iterations transactional and discard their deltas. Include the cursor in diagnostics/checkpoints.

### P1-8: exceptions escaping MCCFR worker threads terminate the process

Evidence:

- `worker_loop()` calls `execute_worker_batch()` without a `try/catch` at `src/solver/hunl_flat_mccfr.cpp:2167-2203`.
- Traversal contains explicit throws for graph/infoset invariants, and moving-average baseline observation allocates vectors and hash entries inside worker traversal at `src/solver/hunl_flat_mccfr.cpp:503-530`.
- An exception escaping a `std::thread` entry function invokes `std::terminate`.

Impact:

A bad graph invariant or allocation failure can kill the hosting application instead of returning a failed solve with the last clean root snapshot. This is unacceptable for a bounded real-time service and makes memory-pressure behavior especially dangerous.

Required fix:

Catch all worker exceptions, store the first `exception_ptr`, cancel the batch, wait/join safely, and rethrow on the coordinator. Preallocate baseline scratch or move allocation outside the hot path.

### P1-9: root exports contain ordinal indices, not stable poker action identities

Evidence:

- `HUNLSampledActionProbability` contains only `action_index` and probability in `include/solver/hunl_sampled_export.hpp:11-14`.
- `HUNLFlatMCCFR::export_root_average_strategy()` exports loop ordinals at `src/solver/hunl_flat_mccfr.cpp:2560-2589`.
- The snapshot does not include the root legal `ActionId` values, bet/raise target amounts, or an action-menu version.

Impact:

A consumer must retain the exact graph and infer that ordinal N still refers to the same fold/check/call/bet size. This is fragile across abstractions, blueprint versions, canonicalization, and multiway action menus. A normalized strategy can be applied to the wrong physical action without any shape error.

Required fix:

Export a structured action descriptor for every probability: semantic action kind, `ActionId`, target contribution or size, and stable abstraction/menu ID. Verify that the export order matches the root edge order.

## P2 findings

### P2-1: full-graph MCCFR metadata still uses unchecked 32-bit value offsets

Evidence:

- `build_infoset_meta()` accumulates `value_offset`, `bucket_offset`, and per-row `value_count` in `std::uint32_t` at `src/solver/hunl_flat_mccfr.cpp:82-103`.
- Row value count multiplies bucket count by action count without checked arithmetic.
- The project's 64 GB target can reach more than 2^32 float values across central arrays before every aggregate allocation necessarily exceeds the hard limit.

Impact:

Large abstractions can wrap offsets and alias rows. This is less likely in the intended lazy production path but remains dangerous in the full-graph oracle and any code copied into multiway storage.

Required fix:

Use checked `size_t`/64-bit offsets end to end and reject before narrowing any graph or export index.

### P2-2: multiway private sampling is correct as a helper but unsuitable for the traversal hot path

Evidence:

- Every call rebuilds nested weight vectors at `src/games/multiway_private.cpp:51-56`.
- Every attempt constructs/resizes a sample vector and performs per-seat weighted scans at `src/games/multiway_private.cpp:57-74`.
- Compatibility uses whole-deal rejection with a fixed attempt cap and throws on exhaustion.
- Range entries are not canonicalized/deduplicated, so `{As,Ks}` and `{Ks,As}` can appear as separate weighted entries.

Impact:

Six wide overlapping ranges can make rejection expensive and unpredictable, while heap allocation and exceptions violate hot-path rules. A feasible joint distribution can still fail a particular call after the configured cap.

Required fix:

Compile ranges once into canonical combos, cumulative/alias tables, and compatibility metadata. Use allocation-free worker scratch and either sequential conditional sampling or a measured rejection strategy with a feasibility/acceptance preflight.

### P2-3: odd-chip settlement is seat-ID biased and lacks button/order context

Evidence:

- Tied-pot remainders are assigned in ascending eligible-player order at `src/games/multiway_terminal.cpp:122-129`.
- The terminal input has no button or "first seat left of button" context.

Impact:

Integer odd chips create a systematic seat-ID advantage and break rotational symmetry. Real table rules usually use positional order, while solver chip-EV may prefer fractional utility to avoid arbitrary bias.

Required fix:

Choose and document one policy: fractional chip-EV for solving, or a positional odd-chip order included in terminal state. Do not use raw seat ID as an implicit game rule.

### P2-4: quality diagnostics are names without engines or confidence contracts

Evidence:

- `compute_multiway_nash_conv()` only combines caller-provided profile and best-response values at `src/solver/multiway_cfr.cpp:129-148`.
- There is no multiway best-response traversal, estimator confidence interval, or policy-evaluation engine.
- Negative estimated improvements are clamped to zero, which can hide evaluator inconsistency or sampling error.
- `BrWalkMode` is ignored in the heads-up exploitability implementation at `src/solver/exploit.cpp:584-589`.

Impact:

Future dashboards can label an arbitrary or noisy input as NashConv without recording how it was estimated. Comparisons against heads-up exploitability will be ambiguous.

Required fix:

Attach metric method, units, sample count, seed, standard error/confidence interval, and policy/model versions. Keep exact toy-game best responses as the correctness oracle.

### P2-5: critical regression coverage is missing around the newly exposed production interfaces

Missing gates include:

- positive-budget deadline MCCFR versus equivalent normal batches;
- depth-limited equivalence across recursive DCFR, flat DCFR, flat MCCFR, and lazy traversal;
- exact continuation injected through a mock leaf evaluator;
- memory accounting versus actual retained capacity/RSS, including all worker scratch;
- deadline pause/resume without repeated trajectory IDs;
- worker exception propagation without process termination;
- cumulative short-all-in reopening;
- rejection of a conserving but semantically incorrect precomputed pot layout;
- structured root action IDs matching graph edges;
- end-to-end three-player traversal and NashConv on an exhaustively enumerable game.

The current tests are broad, but several assert only finiteness, normalization, or deterministic repetition. Those properties do not prove that the requested policy was traversed or that the reported metric has the intended semantics.

### P2-6: the repository's governing large-tree plan is missing and the earlier review overstates integration status

Evidence:

- `AGENTS.md` requires reading `docs/mccfr_large_tree_implementation_plan.md` before major HUNL work, but the file is absent.
- The earlier review marks multiway state, terminal, CFR, and private blockers as "fixed" after standalone helpers were added. There is still no integrated multiway solve path.

Impact:

Without one current architecture document, contributors can treat the full-graph MCCFR prototype as the production base or assume standalone multiway helpers have settled traversal/storage contracts.

Required fix:

Restore or replace the plan and make it authoritative. Record module maturity explicitly: oracle/prototype, standalone validated helper, integrated experimental solver, or production-ready.

## Recommended repair order

1. Run and verify the P0-1 regression suite; retain `HUNLFlatMCCFR` as a small-game oracle while the remaining P0 memory work is unresolved.
2. Redesign worker deltas and complete memory accounting/preflight; keep full-graph MCCFR explicitly oracle-only.
3. Define the structured range/blueprint root request and the batched leaf-evaluator contract.
4. Make cutoff semantics identical across backends, or reject unsupported backend/configuration combinations.
5. Integrate the positive-work lazy heads-up solver with worker-local deltas, deterministic merge, enforced memory caps, and root-only export.
6. Validate lazy heads-up values and strategies against exact tiny games and injected exact leaf values.
7. Correct the multiway external-sampling estimator contract and betting/terminal edge cases.
8. Only then connect multiway state, joint private sampling, side pots, traversal, sparse storage, scheduling, export, and NashConv diagnostics.

## Minimum go/no-go gates before production multiway traversal

The project is ready to start the production multiway traversal only when all of these are true:

- Positive timed heads-up solving follows the same policy-update schedule as batch solving. **Implemented for `HUNLFlatMCCFR`; pending build/test verification.**
- No production mode builds the full flop graph or allocates graph-sized scratch per worker.
- Preflight plus runtime checks conservatively bound every large retained allocation below the configured hard limit.
- A structured public-state-plus-ranges request changes blocker-aware root reach and strategy correctly.
- Exact leaf values injected at a cutoff reproduce untruncated tiny-game results.
- Fixed global trajectory IDs and fixed-order merge give the documented reproducibility across worker counts and pause/resume.
- Worker failure returns an error and last clean snapshot rather than terminating the process.
- Root output identifies actual poker actions and bet sizes, not only ordinals.
- Multiway betting passes cumulative short-raise, fold, all-in, reopening, and arbitrary-root reconstruction tests.
- Side-pot layouts are bound to contributions/folds and conserve payouts/utilities.
- The external-sampling update is validated in exhaustive two-player and three-player toy games.
- NashConv diagnostics state method, units, sampling error, seed, and policy/model versions.
