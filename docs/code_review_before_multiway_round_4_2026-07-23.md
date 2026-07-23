# Fourth code review before integrated multiway solving

Date: 2026-07-23

Reviewed revision: `93e156d`

## Executive verdict

This static review found **one P0, seven P1, four P2, and two P3
findings** in the current tree.

The repository is still suitable for isolated helper hardening and
small-game/oracle development. It is **not ready to claim an enforced 60 GiB
sampled-solver limit or to connect the current multiway state/private helper
contracts unchanged to a production traversal**.

The most important findings are:

- sampled preflight omits the coordinator's full ordered-delta arena and still
  has no byte admission for sparse-row growth, so a request can pass the hard
  limit while its live peak is materially higher;
- the only remaining player who must answer an all-in can still be offered a
  raise or all-in, creating uncallable duplicate branches;
- a betting snapshot can omit players who have not acted, causing a restored
  state to skip required decisions;
- the private sampler's nominally bounded rejection loop is infinite when its
  public `uint32_t` attempt limit is `UINT32_MAX`;
- structured-root validation does not actually prove that the two ranges have
  any compatible joint deal;
- `depth_limit_plies_hint` is documented and used by adaptive preflight but is
  never enforced by sampled traversal.

The earlier fixes for multiway average-strategy reach, cumulative short-raise
reopening, side-pot binding, odd-chip order, non-finite external-sampling
deltas, deterministic delta ordering, worker exception capture, typed root
actions, and positive timed/structured-range fail-closed behavior are present.
They are not repeated as findings.

## Scope and method

This review covered:

- `README.md`, `AGENTS.md`, and the authoritative large-tree implementation
  plan;
- all prior review reports supplied for this pass;
- sampled HUNL config, builder, storage, traversal, scheduler, export,
  profiling, preflight, and solve orchestration;
- structured HUNL range validation and normalization;
- multiway betting snapshots/progression, terminal settlement, private
  sampling/showdown, CFR row math, and NashConv diagnostics;
- relevant regression tests and the recent remediation commits after
  `fce5de7`.

No build, test, benchmark, install, or solver command was run. No source file
was changed. The only file created by this review is this report.

Severity meanings:

- **P0**: defeats a hard resource-safety boundary or can make the intended
  production architecture unsafe.
- **P1**: can create an invalid game transition, skip required play, hang a
  bounded worker, accept an invalid solve request, or violate required solve
  failure semantics.
- **P2**: numerical, scale, diagnostic, or validation weakness that should be
  corrected before large or long-running solves.
- **P3**: low-risk contract/documentation or defensive-state issue.

## P0 findings

### P0-1: sampled memory preflight still undercounts live delta arenas and cannot enforce row-growth bytes

Evidence:

- Each worker reserves an aggregate arena of
  `trajectory_count * 4096` delta objects at
  `src/solver/hunl_sampled_solver.cpp:225-236`.
- After workers finish, their aggregate arenas remain live while the
  coordinator allocates another full-minibatch `ordered_deltas` arena at
  `src/solver/hunl_sampled_solver.cpp:251-261`.
- `estimate_worker_delta_bytes()` accounts for the worker aggregates and one
  per-worker trajectory scratch, but not the coordinator arena, at
  `src/solver/hunl_sampled_solver.cpp:472-483`.
- With one worker and a minibatch of `N`, the estimate covers approximately
  `(N + 1) * 4096` delta entries. The merge peak contains approximately
  `2 * N * 4096` entries because the worker aggregate and coordinator copy
  coexist. For the default minibatch of 64, this is 65 arenas' worth in the
  estimate versus 128 at the merge peak, excluding vector/thread overhead.
- Preflight estimates sparse row count as `minibatch_size * workers` at
  `src/solver/hunl_sampled_solver.cpp:373-387`, even when
  `max_cached_public_states` explicitly permits many more decision nodes.
- All rows are sized with the root action count in the same estimate, although
  deeper nodes may have a different action count.
- Runtime cache admission checks only the public-state count at
  `src/solver/hunl_sampled_builder.cpp:259-267`.
  `HUNLSampledStorage::ensure_row()` grows both central value arrays without a
  configured byte-budget check at
  `src/solver/hunl_sampled_storage.cpp:18-56`.
- Builder and storage memory estimates use unordered-map `size()` rather than
  retained bucket capacity at `src/solver/hunl_sampled_builder.cpp:167-184`
  and `src/solver/hunl_sampled_storage.cpp:121-132`.
- Final memory is recorded but never compared with `memory_fail_bytes`;
  `memory_rejected` is unconditionally recorded as false after work at
  `src/solver/hunl_sampled_solver.cpp:286-297`.

Impact:

A solve can pass the advertised 60 GiB preflight and then allocate a
substantially larger delta peak. A large explicit public-state cap can also
grow more or wider rows than preflight modeled, with no runtime byte admission
to stop it. Retained hash buckets remain invisible on solver reuse. The
current hard limit is therefore a heuristic, not an enforceable resident
memory boundary.

Required fix:

1. Eliminate the second full-minibatch copy by merging already
   trajectory-ordered worker streams, or include its simultaneous capacity in
   peak preflight.
2. Project rows from the admitted public-state/infoset bound and per-node
   action shapes, not only the root menu and minibatch size.
3. Add checked coordinator-side byte admission before node, edge, row,
   value-array, and merge-arena growth.
4. Include retained unordered-map bucket/node capacity and all vector
   capacities in the conservative bound.
5. Track estimated, observed retained, and peak bytes separately. Stop before
   a growth that can cross the hard limit rather than merely recording the
   final number.

Regression gate:

- Compare reported worker/merge arenas with every simultaneous vector
  capacity times `sizeof(HUNLSampledValueDelta)` for one, two, and many
  workers.
- Use a public-state cap much larger than `minibatch_size * workers` and prove
  that row admission cannot exceed the configured byte limit.
- Reuse a solver after a large run and prove that retained hash/vector
  capacity is included.
- Exercise a deeper decision with more actions than the root and prove that
  its row is conservatively admitted.

## P1 findings

### P1-1: the lone final responder can make an uncallable raise

Status: **Fixed in the follow-up commit.** Aggressive actions now require an
additional actionable opponent. The final covering responder retains only the
meaningful fold/call decision; the regression enumerates 20 covering-stack
sizes.

Evidence:

- `refresh_round_completion()` deliberately keeps the sole actionable player
  active when that player faces the current wager at
  `src/games/multiway_state.cpp:232-250`.
- `legal_actions()` does not distinguish that final response from an ordinary
  multi-player decision. If `may_raise` is true and chips remain, it adds
  `Raise`/`Bet` and `AllIn` at
  `src/games/multiway_state.cpp:280-298`.
- A full all-in resets every other actionable seat's raise right at
  `src/games/multiway_state.cpp:261-267`.

Concrete sequence:

With flop stacks `{100, 100, 1000}`, seat 0 moves all-in for 100 and seat 1
calls all-in. Seat 2 is now the only actionable player and must be allowed to
fold or call. The current state leaves seat 2's `may_raise` true, so its legal
menu also contains a raise and an all-in.

Impact:

No opponent can answer the extra wager. The excess is later refunded, so the
raise/all-in branches are physically equivalent to a call but receive
separate CFR probability and regret mass. This reintroduces duplicate,
strategically meaningless branches immediately before board runout.

Required fix:

When only one actionable player remains and that player faces a wager, expose
only fold and call (where call may itself consume the remaining stack).
Aggressive actions require at least one other actionable opponent.

Regression gate:

- Assert that the final covering responder in the sequence above has exactly
  `{Fold, Call}`.
- Verify the heads-up and three-through-six-player variants, including a
  partial all-in call.

### P1-2: snapshot validation permits unacted seats to be omitted from the pending ring

Evidence:

- Snapshot validation requires a live seat below `current_bet` to be pending,
  but an equal-contribution live seat may be non-pending even when
  `has_acted[seat]` is false at
  `src/games/multiway_state.cpp:65-79`.
- It only requires some current player when `actionable_pending` is nonzero at
  `src/games/multiway_state.cpp:85-92`.
- Turn progression selects only from `pending_` at
  `src/games/multiway_state.cpp:216-230`.

Concrete snapshot:

On an unopened flop, use three live stacks, all street contributions zero,
`current_bet = 0`, seat 0 current/pending, and seats 1 and 2
`pending = false`, `has_acted = false`, `may_raise = false`. The snapshot
passes validation. After seat 0 checks, no pending seat remains and the round
ends without seats 1 or 2 acting.

Impact:

An externally supplied live subgame can silently skip legal decisions and
advance the board. Every strategy/value below that root then describes a
different game.

Required fix:

Derive or validate the full pending responder set. With more than one
actionable seat, every seat that has not acted in the round must remain
pending. More generally, round-trip validation should prove that the snapshot
represents a reachable betting state rather than only dimensionally
consistent vectors.

Regression gate:

- Reject the concrete omitted-unacted-seat snapshot.
- Enumerate small reachable betting rounds, round-trip every produced
  snapshot, and reject one-field mutations of pending/acted/raise metadata.

### P1-3: arbitrary betting snapshots permit signed chip arithmetic overflow

Evidence:

- Snapshot validation accepts any individually non-negative `int` stack,
  contribution, street contribution, current bet, and full-raise size; it
  does not check their required sums at
  `src/games/multiway_state.cpp:42-93`.
- `legal_actions()` computes
  `street_contributions_[seat] + stacks_[seat]` and
  `current_bet_ + last_full_raise_size_` in signed `int` at
  `src/games/multiway_state.cpp:280-296`.
- `apply()` adds paid chips to total and street contributions in signed `int`
  at `src/games/multiway_state.cpp:308-316`.
- Terminal settlement rejects total contributions above `INT_MAX` at
  `src/games/multiway_terminal.cpp:24-27`, but the state layer can admit and
  traverse such roots first.

Concrete snapshot:

A current player with `stacks = INT_MAX`, `street_contribution = 100`, and
`current_bet = 100` passes the current per-field checks. Merely asking for
legal actions overflows the all-in target addition.

Impact:

Signed overflow is undefined behavior in C++, and less extreme accepted roots
can still run a tree that the terminal layer later refuses to settle.

Required fix:

Use checked 64-bit intermediate chip arithmetic, validate every snapshot's
per-seat original stack and table contribution total against the supported
domain, and narrow only after the bound is proven.

Regression gate:

- Boundary-test every sum at `INT_MAX`, `INT_MAX - 1`, and one above the
  supported total without executing overflowing arithmetic.

### P1-4: the bounded private rejection loop is infinite at `UINT32_MAX`

Status: **Fixed in the follow-up commit.** Both samplers now use a zero-based
`attempt_index < limit` loop, so `UINT32_MAX` remains a finite public budget.
The regression suite validates the full public limit with 20 deterministic
successful seeds; the loop form itself proves the final increment is never
evaluated.

Evidence:

- `max_rejection_attempts` is a public `uint32_t` and validation only requires
  it to be nonzero at
  `include/games/multiway_private.hpp:16-21` and
  `src/games/multiway_private.cpp:53-56`.
- Both the convenience sampler and compiled sampler iterate with
  `for (uint32_t attempt = 1; attempt <= max; ++attempt)` at
  `src/games/multiway_private.cpp:86-105` and
  `src/games/multiway_private.cpp:154-175`.
- When `max == UINT32_MAX`, incrementing the final attempt wraps to zero and
  the loop condition remains true forever.

Impact:

A valid public configuration can permanently hang a traversal worker and its
coordinator join, violating the bounded-worker and deadline contracts.

Required fix:

Use a zero-based `< max` loop or a wider loop counter, and validate/cap
unreasonable attempt budgets during compilation.

Regression gate:

- Prove bounded termination for `1`, `UINT32_MAX - 1`, and `UINT32_MAX`
  without iterating billions of attempts (factor the loop-bound calculation
  for a small deterministic boundary test).

### P1-5: structured-root validation does not establish joint private-range feasibility

Evidence:

- `HUNLStructuredRootRequest::validate()` delegates range feasibility to
  `validate_hunl_joint_range_feasibility()` at
  `src/games/hunl_solver.cpp:168-185`.
- That helper only proves that each marginal contains at least one positive
  valid hand at `src/games/hunl.cpp:355-374`; it never compares the two
  marginals for cross-player blockers.
- Actual joint normalization performs the cross-player distinct-card test and
  can later reject with no compatible deal at
  `src/games/hunl.cpp:324-351`.

Concrete request:

Give both players the same single positive-mass hand. `request.validate()`
passes, while `request.normalized_joint_range()` throws because no joint deal
exists.

Impact:

The public structured root can be declared valid even though its private state
space is empty. Positive sampled work currently fails closed for a different
reason, but an integrated range traversal would inherit an invalid admission
contract.

Required fix:

Perform a bounded compatibility search over canonical positive-mass combos
during validation, without constructing the joint-deal vector. Keep
materialization as a separate explicit operation.

Regression gate:

- Require `HUNLStructuredRootRequest::validate()` itself to reject identical
  single-combo ranges and accept a sparse compatible alternative.

### P1-6: sampled `depth_limit_plies_hint` is an estimate-only control

Evidence:

- The public config documents the field as the maximum plies cached/evaluated
  below a public state at
  `include/solver/hunl_sampled_config.hpp:25-35`.
- Adaptive preflight reduces it and uses it to shrink estimated terminal-cache
  bytes at `src/solver/hunl_sampled_solver.cpp:453-469` and
  `src/solver/hunl_sampled_solver.cpp:492-500`.
- Sampled builder and traversal never read the field. Traversal continues
  until an already classified terminal/depth-limited node at
  `src/solver/hunl_sampled_traversal.cpp:119-255`.
- The sampled builder never creates a `DepthLimited` node based on traversal
  depth at `src/solver/hunl_sampled_builder.cpp:259-297`.

Impact:

A caller can request root-only or shallow work and receive a full terminal
traversal instead. Adaptive memory fallback reports that it reduced work even
though execution is unchanged. This also leaves no trustworthy boundary for
future value-network leaf evaluation.

Required fix:

Either remove the field and its adaptive step until a shared leaf evaluator
exists, or carry depth through coordinator preparation/traversal and fail
closed at the cutoff unless a typed evaluator is configured.

Regression gate:

- A depth of zero/one must measurably bound expanded nodes and invoke the
  configured leaf boundary, or validation must reject it before preflight.

### P1-7: a failed fresh solve destroys the last clean sampled result

Evidence:

- `run_batches()` clears builder, storage, profile, and root export before
  preflight at `src/solver/hunl_sampled_solver.cpp:153-160`.
- A rejected preflight then throws at
  `src/solver/hunl_sampled_solver.cpp:161-172`.
- Worker/preparation failures occur after the same destructive clear and
  rethrow at `src/solver/hunl_sampled_solver.cpp:201-261`.
- The authoritative implementation plan requires retaining the last clean
  snapshot on cancellation/failure.

Impact:

After a successful solve, a rejected or failed replacement request leaves the
solver without the previously usable policy. A bounded service cannot fall
back to its last clean root result.

Required fix:

Validate and preflight before mutating live session state. Build the next
batch/session in staging storage and commit builder/storage/export/profile
only after a clean merge, or retain the previous immutable snapshot
separately.

Regression gate:

- Solve one request successfully, inject preflight, preparation, and worker
  failures into a second request, and prove the first root export and profile
  remain available byte-for-byte.

## P2 findings

### P2-1: an explicit fixed deal can still allocate the configured global bucket domain

Evidence:

- `infer_bucket_count()` returns `bucket_count_hint` before checking whether
  the root has fixed hole cards at
  `src/solver/hunl_sampled_solver.cpp:51-59`.
- Every positive trajectory uses bucket zero at
  `src/solver/hunl_sampled_solver.cpp:204-214`.
- Coordinator preparation allocates each row with the inferred full bucket
  count at `src/solver/hunl_sampled_traversal.cpp:350-375`.

Impact:

Setting a production-sized bucket hint on an explicit-hand oracle allocates
every row for that domain while only bucket zero is updated or exported. This
can multiply memory, cause avoidable preflight rejection, and weaken the
small-game oracle's usefulness.

Required fix:

Fixed private deals must force `bucket_count = 1` regardless of the global
hint, or reject the contradictory configuration.

Regression gate:

- Run preflight/preparation for a fixed deal with a large hint and assert
  every row has one bucket and the estimate matches that domain.

### P2-2: sampled traversal can silently lose or poison updates at numerical extremes

Evidence:

- Strategy and importance weights divide by accumulated sampling reach without
  checking the resulting value at
  `src/solver/hunl_sampled_traversal.cpp:186-203` and
  `src/solver/hunl_sampled_traversal.cpp:238-251`.
- If sampling reach underflows to zero, both weights silently become zero. If
  a positive subnormal denominator produces an overflow, a non-finite delta
  is appended.
- Merge narrows double deltas to float and adds them without any finiteness or
  transactional precheck at `src/solver/hunl_sampled_traversal.cpp:257-271`.
- Average-strategy export assumes finite, non-negative sums at
  `src/solver/hunl_sampled_export.cpp:34-63`.

Impact:

Deep/low-probability trajectories can be silently dropped, or one overflow can
permanently store infinity/NaN and produce a non-normalized root export.
Multiway traversal multiplies more reach factors, so copying this path would
increase the risk.

Required fix:

Use scaled/log-domain reach or a documented bounded-weight policy. Validate
all deltas and their float conversions before mutating a row, and reject the
entire batch transactionally on non-finite output.

Regression gate:

- Cover zero-underflow, positive-subnormal, `FLT_MAX` conversion, and
  row-addition overflow. The row must remain unchanged on every rejection.

### P2-3: sampled runtime profiles do not measure runtime and mix live with hypothetical memory

Evidence:

- `HUNLSampledProfile` exposes traverse, merge, terminal, and export timers,
  but the solve path never calls any `add_*_seconds` method. Positive work at
  `src/solver/hunl_sampled_solver.cpp:201-283` therefore reports zero for all
  timing categories.
- `memory_estimate()` labels its total `total_bytes_live` while adding a
  modeled worker arena even after all worker vectors have been destroyed at
  `src/solver/hunl_sampled_solver.cpp:315-334`.
- Zero-batch profiles consequently report nonzero worker-delta bytes; the
  current regression test explicitly expects this at
  `tests/test_hunl_sampled_solver.cpp:567-579`.

Impact:

Merge cost, worker imbalance, and actual retained/peak memory cannot be
profiled, so the diagnostics cannot guide the required 10-15 second budget or
prove the 64 GiB guardrails.

Required fix:

Measure the real coordinator phases, report per-worker work/time, and separate
`preflight_estimate`, `observed_retained`, and `observed/modelled_peak`
categories.

### P2-4: sampled NashConv diagnostics still allow missing per-seat uncertainty

Evidence:

- Per-seat sample counts, standard errors, and confidence intervals are
  present in `MultiwayQualityDiagnostics` at
  `include/solver/multiway_cfr.hpp:66-77`.
- Validation checks only that the three vectors have matching sizes. Empty
  vectors pass even for `SampledEstimate` at
  `src/solver/multiway_cfr.cpp:48-74`.
- Seat-count matching is enforced only when the vectors are nonempty at
  `src/solver/multiway_cfr.cpp:225-237`.

Impact:

A sampled result can be labeled NashConv with no seat-level sample/error
contract, hiding a starved or high-variance seat behind an aggregate standard
error.

Required fix:

For `SampledEstimate`, require all per-seat diagnostic vectors to match the
player count and record the confidence level/definition used for each
interval.

## P3 findings

### P3-1: sampled-solver public comments and error text contradict current behavior

Evidence:

- The solver header says positive work accepts the blocker-normalized
  structured range root at
  `include/solver/hunl_sampled_solver.hpp:73-83`.
- Positive structured work always validates and throws
  `HUNLSampledStructuredRangeNotReady` at
  `src/solver/hunl_sampled_solver.cpp:139-142`.
- `HUNLSampledSolverNotReady` says positive work requires a structured root
  state at `src/solver/hunl_sampled_solver.cpp:101-104`, although bounded
  positive batches with an explicit `root_state` are implemented and positive
  timed requests fail even when that root is present.

Impact:

Callers receive the wrong remediation for a timed-solve failure and can infer
that structured positive work is supported when it is deliberately disabled.

Required fix:

Document bounded explicit-root batches, structured-range fail-closed status,
and timed-solve fail-closed status separately. Use distinct exception messages
for missing root and unavailable timed execution.

### P3-2: failed non-throwing private sampling leaves stale success metadata

Status: **Fixed in the follow-up commit.** `try_sample_into()` now clears the
seat count, attempts, and private-hole scratch before every sampling attempt,
so any failure leaves a defined empty result rather than stale metadata.

Evidence:

- On success, `try_sample_into()` writes `seat_count` and `attempts` at
  `src/games/multiway_private.cpp:168-171`.
- On rejection-budget exhaustion it returns false without clearing those
  fields at `src/games/multiway_private.cpp:173-175`.

Impact:

Reusing worker scratch after a previous success leaves valid-looking seat
count, attempt count, and holes after a failed sample. Correct callers should
branch on the boolean, but stale diagnostics make misuse harder to detect.

Required fix:

Set `seat_count = 0`, set a defined failure attempt count, and optionally
clear holes before beginning or before returning false.

## Recommended repair order

1. Make the sampled memory bound conservative and enforce byte admission
   before every large growth; remove the duplicate full-minibatch merge arena.
2. Fix final-responder action legality, snapshot reachability validation, and
   checked chip arithmetic.
3. Fix the rejection-loop overflow and structured joint-feasibility
   admission.
4. Remove or implement sampled depth limiting, then make failed solve
   replacement transactional.
5. Harden sampled numerical updates and restore one-bucket fixed-deal
   behavior.
6. Add real timing/peak-memory and mandatory per-seat sampled-quality
   diagnostics.
7. Correct the low-risk API text and scratch failure state.

## Go/no-go conclusion

**Go for isolated fixes and exhaustive helper regression work; no-go for
integrated production multiway traversal.**

The standalone multiway modules remain a useful base, and many earlier defects
are now closed. The current P0 memory gap and P1 state/sampling/admission
issues should be resolved before those helpers define the contracts of a
three-through-six-player sampled engine.
