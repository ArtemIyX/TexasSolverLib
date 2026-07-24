# Sixth code review before integrated multiway solving

Date: 2026-07-24

## Executive verdict

The experimental sampled heads-up path and standalone multiway helpers are
substantially safer than they were in the first five review rounds, but the
repository is not yet a trustworthy base for blueprint generation or integrated
multiway solving.

This review found three P0, three P1, two P2, and one P3 problems. The highest
risk is outside the recently reviewed sampled storage code:

- two public preflop RVR APIs report completed work without solving the
  requested game;
- public preflop-equity boundaries permit out-of-bounds array/vector access;
- several thread owners can terminate the hosting process if worker creation
  fails after an earlier worker was launched;
- generic and vector DCFR paths either ignore the requested discount parameters
  or accept non-finite parameters;
- the class-169 preflop tree ignores the configured raise menu and can create
  raises that no opponent can answer;
- direct solver outputs use a different exploitability definition than the
  public wrapper;
- finite extreme inputs can make standalone multiway CFR helpers return
  non-finite strategies or metrics;
- remaining 32-bit narrowing can alias very large exact-oracle tables;
- `BrWalkMode::Vector` is accepted but ignored.

Multiway integration should remain gated until the P0 and P1 findings are
closed. The existing sampled and multiway modules remain useful experimental
components, but the preflop and generic-oracle surfaces cannot currently be
treated as correctness references.

## Scope and method

This was a static review of:

- `README.md`, `AGENTS.md`, and every document in `docs/`;
- public headers in `include/`;
- game, range, preflop, exact DCFR, sampled HUNL, exploitability, and multiway
  implementations in `src/`;
- CMake packaging and the existing tests.

No build, test, benchmark, install, or long-running solver command was run.

Severity:

- **P0**: can return a plausible result for work that was not performed, invoke
  memory-unsafe behavior, or terminate the host process.
- **P1**: materially wrong game, solver, action-abstraction, or public metric
  semantics.
- **P2**: latent numerical/index failure at supported large-scale boundaries.
- **P3**: misleading or internally inconsistent public behavior without an
  immediate strategy-corruption path.

## Findings

### P0-1: public preflop RVR APIs report work that they do not perform

Status: **Fixed fail-closed in the P0-1 remediation commit; tests added but
not executed.**

Evidence:

- `solve_hunl_preflop_rvr()` discards the supplied equity table, iteration
  count, and all three DCFR parameters in
  `src/preflop/preflop_rvr.cpp:524-529`.
- It returns `solve_kuhn(1, 1.5, 0.0, 2.0)` and fabricates a decision count in
  `src/preflop/preflop_rvr.cpp:530-538`.
- The public `Class169VectorDCFR::solve(decision_node_count, ...)` overload
  discards both root reaches, creates no infosets, only increments its iteration
  counter, and returns normally for every positive node count in
  `src/preflop/preflop_rvr.cpp:493-505`.

Impact:

A caller can request thousands of HUNL preflop iterations, receive a finite
strategy/metric object and matching iteration count, and publish it as a
blueprint even though the result came from one Kuhn iteration or no traversal
at all.

Required fix:

Remove these incomplete overloads or make them fail closed with a dedicated
not-ready exception. Keep the tree/cache-aware class-169 overload as the only
positive-work RVR entry until the legacy facade has a real conversion and
quality-metric implementation.

Required regression coverage:

At least 20 calls spanning iteration counts, DCFR parameters, node counts, and
root-reach shapes must prove that the incomplete surfaces always fail before
mutating solver state or returning output.

Implemented fix:

- Added `PreflopRvrNotReady`.
- `solve_hunl_preflop_rvr()` now always fails explicitly instead of returning a
  one-iteration Kuhn result.
- The count-only `Class169VectorDCFR::solve()` overload now fails before
  changing its iteration counter or infoset state.
- The tree/cache-aware class-169 overload remains available for real work.

Regression coverage added:

`tests/test_preflop_rvr_not_ready.cpp` contains 20 independent test cases
covering zero through blueprint-scale iteration counts, alternate exponent
values, zero through large node counts, and empty/short/exact/oversized reach
vectors. The stateful overload is checked for unchanged iteration and empty
strategy state after every failure.

### P0-2: preflop-equity public inputs can index fixed storage out of bounds

Status: **Fixed in the P0-2 remediation commit; tests added but not executed.**

Evidence:

- `class_index()` accepts ranks outside 2-14; `rank_pos()` then returns the end
  position and index arithmetic produces an invalid class.
- `class_decode()` maps every class index above 168 to `22` instead of
  rejecting it.
- `hole_to_class()`, `enumerate_pair_equity()`, and
  `monte_carlo_pair_equity()` do not validate card encodings, duplicate private
  cards, or cross-player overlap.
- Both equity functions write `used[card]` directly in
  `src/preflop/preflop_equity.cpp:123-130,180-186`; a value such as 255 is an
  out-of-bounds write.
- Monte Carlo equity accepts zero samples and returns `0 / 0`.
- `PreflopEquityTable::at()` uses unchecked flattened indexing, and the public
  mutable `data()` accessor can invalidate the table-size invariant.

Impact:

Malformed imported ranges, class IDs, or caller indices can corrupt memory or
return NaN/aliased equity values. These are public library boundaries used by
blueprint and training-data generation.

Required fix:

Add one allocation-free validation path for ranks, class IDs, variants, private
deals, sample counts, and table coordinates. Reject invalid input before any
fixed-array write or flattened-index calculation. Make table access safe even
if a caller has changed the mutable backing vector size.

Required regression coverage:

At least 20 boundary cases must cover invalid low/high ranks, invalid class
IDs, invalid variants, every private-card collision class, zero Monte Carlo
samples, table coordinate overflow, and a damaged backing-vector shape.

Implemented fix:

- Rank, class, variant, two-card hand, and four-card private-deal validators now
  reject malformed inputs before array access.
- Exact and Monte Carlo equity require four valid distinct cards.
- Monte Carlo equity rejects a zero sample count.
- Both const and mutable `PreflopEquityTable::at()` overloads validate all
  coordinates before flattening and verify the backing-vector shape before
  indexing.
- Invalid class decoding no longer aliases to pocket deuces.

Regression coverage added:

`tests/test_preflop_equity_validation.cpp` contains 32 independent test cases
covering rank and class boundaries, variants, low/high invalid cards, same-hand
and cross-player collisions, zero samples, every table coordinate, and cleared,
shortened, or extended mutable backing storage.

### P0-3: partial worker-launch failure can terminate the process

Status: **Fixed in the P0-3 remediation commit; tests added but not executed.**

Evidence:

- The sampled solver launches a local vector of `std::thread` objects without a
  launch guard in `src/solver/hunl_sampled_solver.cpp:263-294`.
- `HUNLFlatDCFR::WorkerPool` launches persistent workers in its constructor in
  `src/solver/hunl_flat_dcfr.cpp:181-190`; its destructor is not invoked if the
  constructor throws.
- Parallel recursive DCFR launches persistent workers without a guarded startup
  in `src/solver/parallel_dcfr.cpp:781-785`.
- Parallel preflop equity has the same unguarded local launch loop in
  `src/preflop/preflop_equity.cpp:270-289`.
- Destroying a joinable `std::thread` calls `std::terminate`.

Impact:

Thread-resource exhaustion or an injected/OS launch error after one successful
launch can kill an offline generation job or online host process instead of
returning an exception and preserving the last clean strategy.

Required fix:

Make every launch loop transactional. On startup failure, signal persistent
workers to stop, join every successfully launched worker, and rethrow. Finite
worker groups must join all launched work before propagating the error.

Required regression coverage:

At least 20 deterministic launch-failure points across the four owners must
prove exception propagation, joining, no deadlock, and unchanged published
solver state.

Implemented fix:

- Added a scoped, C++17-compatible `ThreadJoinGuard`.
- The guard invokes the owner's stop/cancellation callback and joins every
  successfully created thread during stack unwinding.
- Sampled solving and parallel equity use cancellation flags so partial launch
  cleanup does not finish an unnecessary full batch/table.
- Parallel equity also captures worker exceptions and rethrows them on the
  coordinator after every worker has joined.
- `HUNLFlatDCFR::WorkerPool` signals its persistent workers before joining when
  construction is interrupted.
- Parallel recursive DCFR now owns its persistent pool through the same guard
  for startup and all later coordinator exceptions.

Regression coverage added:

`tests/test_thread_join_guard.cpp` contains 20 independent injected failure
points. Each case launches zero through nineteen workers, throws before the
next launch, and verifies that the original exception propagates only after
every started worker observes cancellation and exits.

## P1 findings

### P1-1: several advertised DCFR paths do not apply DCFR

Status: **Open.**

Evidence:

- `DCFRSolver` stores and validates `alpha`, `beta`, and `gamma`, but its solve
  loop never discounts regrets or average-strategy sums.
- `ParallelDCFRSolver` likewise never uses the three values after validation.
- `VectorDCFR::discount()` exists but is never called by its solve loop.
- Recursive, parallel, vector, and flat constructors check `beta < 0` and
  `gamma < 0` without rejecting NaN or infinity. The MCCFR config has the same
  non-finite gap.

Impact:

Changing the documented solver parameters does not change several solver
outputs. Comparisons against the flat exact oracle are mislabeled, convergence
expectations are wrong, and NaN exponents can silently poison stored rows.

Required fix:

Apply the same iteration-boundary DCFR discount schedule used by
`HUNLFlatDCFR`: discount existing rows once before each iteration's strategy is
computed, initialize newly discovered rows at the current discount cursor, and
then perform both player updates. Reject every non-finite or out-of-domain
exponent at all public constructors/facades.

Required regression coverage:

At least 20 cases must cover recursive, parallel, generic vector, class-169,
flat, and MCCFR parameter validation; exact row-scale identities; and observable
parameter sensitivity after multiple iterations.

### P1-2: the class-169 preflop tree ignores its action abstraction and permits unanswerable raises

Status: **Open.**

Evidence:

- `enumerate_actions()` iterates `config.raise_size_xs` only to discard every
  value, then hardcodes `{2,3,4,5}` for both opens and raises in
  `src/preflop/preflop_rvr.cpp:74-94`.
- `apply_action()` does not refresh `all_in` after calls, raises, or all-ins.
- Action enumeration does not require an opponent with chips behind before
  offering a raise.
- A player facing an all-in can therefore raise into an opponent who cannot
  respond; the tree recurses to a zero-action decision node rather than a
  terminal equity leaf.
- `PreflopBettingTree::build()` does not validate `HUNLConfig`.

Impact:

The generated blueprint tree can contain the wrong action menu, ignore the
requested abstraction version, and terminate on a structurally invalid
zero-action decision. Strategy rows and action ordinals then no longer
correspond to the requested poker game.

Required fix:

Validate the config, consume the configured sizing menu deterministically,
maintain all-in flags after every chip movement, suppress aggression without an
actionable opponent, and reject any attempted zero-action decision node.

Required regression coverage:

At least 20 tree cases must cover custom sizing menus, deduplication/clamping,
short stacks, calls, all-ins, no-covering-stack states, raise caps, and the
absence of empty decision nodes.

### P1-3: direct solver exploitability uses a different metric than wrapper output

Status: **Open.**

Evidence:

- `DCFRSolver::solve()` reports `br0 + br1`.
- `ParallelDCFRSolver::solve()` reports the same raw sum.
- `detail::exploitability()` and HUNL exploitability report the sum of unilateral
  improvements divided by two.
- In a constant-sum game with a non-zero initial pot, raw `br0 + br1` also
  includes the game constant.

Impact:

The same strategy receives different convergence numbers depending only on
whether the caller uses a solver object or a facade/backend wrapper. This makes
blueprint quality gates and future multiway NashConv comparisons unreliable.

Required fix:

Define `SolveOutput::exploitability` consistently as mean unilateral
improvement for every heads-up backend:

`((BR0 - EV0) + (BR1 - EV1)) / 2`.

Required regression coverage:

At least 20 constant-sum and zero-sum cases across direct sequential, direct
parallel, and wrapper entry points must agree on value and exploitability.

## P2 findings

### P2-1: finite extreme multiway CFR inputs can produce non-finite outputs

Status: **Open.**

Evidence:

- `multiway_regret_matching()` sums finite positive regrets without checking
  whether the sum overflowed; two `DBL_MAX` regrets produce an infinite
  denominator and a zero-sum strategy.
- Full-tree multiway CFR does not validate the finiteness of its accumulated
  node value or generated deltas.
- `compute_multiway_nash_conv()` validates each input value but not the
  accumulated total, so finite per-seat improvements can produce an infinite
  metric.

Impact:

Numerical overflow can cross a public helper boundary as a plausible result and
poison later sparse-row merges or diagnostics.

Required fix:

Use scale-stable regret matching and checked finite accumulation. Reject an
update or metric transactionally if any derived value is non-finite.

Required regression coverage:

At least 20 cases must cover maximum finite values, mixed scales, cancellation,
overflowing node values/deltas, NashConv accumulation, and unchanged output
rows after rejection.

### P2-2: exact-oracle IDs and pipeline counts still narrow without checks

Status: **Open.**

Evidence:

- `InfosetRegistry::intern()` narrows `id_to_key_.size()` to `uint32_t`.
- `detail::InfosetAccum` stores offsets in `uint32_t` and narrows a double-arena
  size without checking.
- `HUNLFlatPipelinePlan::build()` narrows graph, bucket, and value counts to
  `uint32_t` without checking.

Impact:

At 64 GB-class exact-oracle configurations, row offsets or diagnostic buffer
counts can wrap and alias unrelated storage. The intended production solver is
lazy/sparse, but these exact components are still the correctness oracle and
source of copied contracts.

Required fix:

Use `size_t`/64-bit offsets internally and checked narrowing for every
explicitly 32-bit public field. Reject before constructing an aliased table.

Required regression coverage:

At least 20 checked-boundary cases must cover exact maximum values, first
overflowing values, multiplication/addition overflow, and unchanged state on
rejection without allocating multi-gigabyte buffers.

## P3 findings

### P3-1: `BrWalkMode::Vector` is accepted but ignored

Status: **Open.**

Evidence:

`compute_exploitability_and_value_with_mode()` explicitly discards `mode` in
`src/solver/exploit.cpp:584-589`. Unknown enum values are also accepted.

Impact:

Callers can believe they selected and regression-tested an independent vector
best-response engine when every request uses the per-combo implementation.

Required fix:

Until a distinct vector engine exists, reject `Vector` and unknown modes
explicitly, or remove the mode-taking API. Do not silently treat an unavailable
engine as another implementation.

Required regression coverage:

At least 20 mode-boundary calls must prove that `PerCombo` remains functional
and unsupported/unknown values fail before tree construction.

## Repair order

1. P0-1: fail closed on fake preflop RVR work.
2. P0-2: harden every preflop-equity boundary.
3. P0-3: make all thread-launch paths exception-safe.
4. P1-1: implement the advertised DCFR discount schedule and finite validation.
5. P1-2: correct class-169 preflop action/state progression.
6. P1-3: unify heads-up exploitability units.
7. P2-1: harden standalone multiway numerical accumulation.
8. P2-2: remove unchecked exact-oracle narrowing.
9. P3-1: fail closed on the unavailable best-response walk mode.

Each repair is to receive a dedicated test file or clearly isolated test
section with at least 20 regression scenarios, a status/evidence update in this
document, and a separate git commit. Tests will be written but not executed,
per the user instruction and `AGENTS.md`.
