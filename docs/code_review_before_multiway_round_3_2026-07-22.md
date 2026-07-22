# Third code review before integrated multiway solving

Date: 2026-07-22

Reviewed revision: `fce5de7`

## Executive verdict

The repository has useful and substantially hardened foundations, but this
fresh review found **two P0, five P1, and three P2 findings** that should be
resolved before the standalone multiway helpers are connected to a production
traversal.

This is not a recommendation to stop forward development. The next work should
continue through the isolated Phase A/Phase B contracts in
`docs/mccfr_large_tree_implementation_plan.md`. It is, however, a no-go for
calling the sampled heads-up engine memory-bounded or using the current
multiway CFR/state contracts unchanged in an integrated solver.

The most important findings are:

- sampled worker scratch is budgeted as though 4,096 delta **entries** were
  4,096 **bytes**, so the 60 GiB preflight can substantially understate the
  actual allocation;
- structured range validation materializes an unbounded Cartesian product
  before memory preflight, including on a positive request that is supposed to
  fail closed;
- multiway average-strategy accumulation includes chance reach even though CFR
  average strategy is weighted by the acting player's own reach;
- a multiway state can simultaneously report `is_terminal()` and
  `requires_board_runout()`, and an arbitrary root with one covering stack can
  still expose unanswerable betting actions.

The earlier review fixes for card validation, sampled state reuse, typed
positive-work root export, side-pot binding, odd-chip order, external-sampling
importance ratios, and background-worker exception capture are present. They
are not repeated below except where the current implementation leaves a
separate gap.

## Scope and method

This was a static source review of the current tree, with emphasis on:

- the sampled/lazy heads-up solver, builder, traversal, storage, scheduler,
  export, profiling, and memory preflight;
- the structured HUNL range/blueprint boundary;
- multiway betting snapshots and progression;
- multiway private sampling, terminal settlement, CFR row math, and quality
  diagnostics;
- the full-graph MCCFR oracle where its behavior is used as a correctness or
  deadline/failure reference;
- relevant regression tests and all previous review documents.

No build, test, benchmark, install, or solver command was run, in accordance
with `AGENTS.md`. Findings are based on source inspection. Severity meanings:

- **P0**: can defeat the memory safety contract or fail before a documented
  fail-closed boundary.
- **P1**: can bias solver output, expose an invalid game transition/output, or
  break deterministic/failure semantics needed by integration.
- **P2**: scale, numerical, diagnostic, or boundary weakness that should be
  corrected before large production runs.

## P0 findings

### P0-1: sampled worker-delta memory is undercounted and the hard limit is not enforced against live capacity

Status: Implemented 2026-07-22. Worker arenas are now specified in delta
entries and converted with `sizeof(HUNLSampledValueDelta)`; preflight also
accounts for retained builder/storage capacity from a reused solver.

Evidence:

- `kWorkerDeltaBytesPerTraversal` is set to `4096` at
  `src/solver/hunl_sampled_solver.cpp:20` and is used as a byte count by
  `estimate_worker_delta_bytes()` at
  `src/solver/hunl_sampled_solver.cpp:468-478`.
- Execution reserves `range.size() * 4096` **objects** in each aggregate worker
  scratch and another 4,096 objects in each trajectory scratch at
  `src/solver/hunl_sampled_solver.cpp:227-238`.
- Each `HUNLSampledValueDelta` contains an infoset ID, bucket, action, and two
  `double` values at `include/solver/hunl_sampled_traversal.hpp:49-55`; one
  entry is therefore many bytes, not one byte.
- With one worker and the default minibatch of 64, the estimate is roughly 64
  times 4 KiB, while the aggregate reserve alone is 64 times 4,096 delta
  objects. The discrepancy is approximately an order of magnitude or more
  even before the temporary trajectory scratch and vector overhead are added.
- The worker vectors are local to `run_batches()` and are destroyed before the
  final call to `memory_estimate()` at
  `src/solver/hunl_sampled_solver.cpp:285-296`. The reported
  `worker_delta_bytes` is the same formula, not observed retained or peak
  capacity.
- Preflight estimates every public state as a fixed 512 bytes and sizes every
  row with the root action count at
  `src/solver/hunl_sampled_solver.cpp:361-395`, although deeper nodes can have
  a different action count and stored `HUNLState` histories own variable-sized
  vectors and strings.
- Runtime admission in `HUNLSampledBuilder::find_or_create()` checks only the
  node count at `src/solver/hunl_sampled_builder.cpp:259-267`.
  `HUNLSampledStorage::ensure_row()` has no configured byte-budget admission.
- `builder_.clear()` and `storage_.clear_keep_capacity()` retain vector and
  hash-table capacity at `src/solver/hunl_sampled_solver.cpp:150-155`, but the
  next request's preflight is calculated from its model rather than the
  already-retained capacity. Hash-table bucket capacity is also omitted from
  the live estimates.
- If final observed/modelled memory exceeds the configured fail threshold, the
  solver only records the number; it does not reject or stop at
  `src/solver/hunl_sampled_solver.cpp:285-303`.

Impact:

A configuration can pass the nominal 60 GiB hard limit and then reserve much
more worker scratch than reported. Reusing a solver can also retain capacity
from a larger prior run while a smaller new preflight passes. The current
warning/rejection status is therefore not an enforceable resident-memory
property, and paging or allocation failure can occur inside a batch that was
declared safe.

Required fix:

1. Define the delta arena in entries and convert to bytes with checked
   `sizeof(HUNLSampledValueDelta)` arithmetic.
2. Size the aggregate arena by the exact per-worker trajectory partition and
   include each worker's trajectory scratch, thread state, and vector/hash
   overhead.
3. Preflight retained capacity already owned by a reused solver.
4. Add coordinator-side byte admission before builder, edge, infoset-row, and
   worker-arena growth.
5. Track peak and final observed capacity separately; abort before committing a
   batch if the next admitted growth can exceed the hard limit.

Regression gate:

- For one, two, and many workers, compare the reported worker arena with actual
  vector capacities multiplied by their element sizes.
- Reuse one solver after a large run and prove that a later small request still
  accounts for retained capacity.
- Exercise a deeper node with more actions than the root and prove that row
  admission remains below the configured bound.

### P0-2: structured-range validation materializes an unbounded Cartesian product before preflight

Status: Implemented 2026-07-22. Structured validation now performs bounded
canonical marginal checks without constructing joint deals; normalized deals
canonicalize duplicate hands first, and the unsupported sampled path never
materializes them.

Evidence:

- `normalize_hunl_joint_range()` reserves and constructs
  `first.size() * second.size()` joint deals at
  `src/games/hunl.cpp:300-329`. The multiplication is not checked and duplicate
  canonical hole entries are not combined first.
- `HUNLRangeInput::hand_weights` has no entry-count limit, so a caller can
  provide arbitrarily many duplicate valid hands.
- `HUNLStructuredRootRequest::validate()` calls
  `normalize_hunl_joint_range()` at `src/games/hunl_solver.cpp:168-186` merely
  to validate compatibility.
- `HUNLStructuredRootRequest::normalized_joint_range()` calls `validate()` and
  then calls `normalize_hunl_joint_range()` again at
  `src/games/hunl_solver.cpp:188-191`.
- A positive structured sampled request invokes `validate()` before throwing
  `HUNLSampledStructuredRangeNotReady` at
  `src/solver/hunl_sampled_solver.cpp:135-138`. It can therefore allocate the
  full product before reaching the advertised fail-closed result.
- A zero-work structured request materializes the product before sampled
  memory preflight begins at `src/solver/hunl_sampled_solver.cpp:139-157`, and
  the joint-deal vector is absent from the preflight estimate.

Impact:

An unsupported positive solve or nominally harmless validation request can
consume unbounded memory before the 64 GiB guardrails run. Canonical 1,326 by
1,326 ranges are already a large temporary; duplicated inputs can make this
arbitrarily worse. This contradicts the product architecture's requirement to
condition compatible private states without materializing the full Cartesian
product.

Required fix:

- Canonicalize and merge per-seat combos first.
- Separate cheap structural/feasibility validation from materialization.
- Use compiled marginal ranges plus compatible sampling/normalization mass, or
  a bounded sparse joint representation.
- Include every retained/temporary range table in preflight and perform that
  preflight before allocation.
- Positive unsupported work should reject after bounded validation only.

Regression gate:

- Large duplicate ranges must have memory proportional to the canonical 1,326
  combos per seat, not the input-vector product.
- Positive fail-closed requests must not construct joint deals.
- Overflow-sized products must reject deterministically before reserve or
  iteration.

## P1 findings

### P1-1: multiway CFR average-strategy weights incorrectly include chance reach

Status: Implemented 2026-07-22. Average-strategy updates now use the acting
seat's reach only (or that reach divided by sampling reach for MCCFR).

Evidence:

- Full-tree updates set `average_strategy_weight` to
  `chance_reach * player_reaches[traverser]` at
  `src/solver/multiway_cfr.cpp:120-131`.
- External-sampling updates set it to
  `chance_reach * traverser_reach / sampling_reach` at
  `src/solver/multiway_cfr.cpp:161-178`.
- CFR average strategy at an infoset is weighted by the acting player's own
  reach. Chance and opponent reach belong in visitation/sampling and regret
  estimation, not in the target average-strategy numerator.
- The original review explicitly recorded the contract as “use only the
  traverser's own reach for average-strategy accumulation” in
  `docs/code_review_before_multiway.md`, but current tests encode the chance
  factor: `tests/test_multiway_cfr.cpp:86-108` expects 0.125 instead of 0.25
  for the full-tree example and 1.25 instead of 2.5 for the sampled example.
- The scalar heads-up sampled traversal uses
  `reach.player[acting_player] / reach.sampling` without multiplying chance in
  the numerator at `src/solver/hunl_sampled_traversal.cpp:184-200`, so the two
  reference paths disagree.

Impact:

When multiple chance/private histories map to one infoset or bucket, the
exported average policy is chance-weighted rather than the CFR average policy.
This can bias blueprint targets and makes a future multiway traversal disagree
with the heads-up reference even if regret updates are otherwise correct.

Required fix:

- Full tree: use only `player_reaches[traverser]`.
- External sampling: use `traverser_reach / sampling_reach` for the unbiased
  estimator under the stated node-visitation sampling contract.
- Add exhaustive two-player and three-player toy games with unequal chance
  branches that merge into the same infoset/bucket and compare the expected
  average strategy with enumeration.

### P1-2: multiway terminal/runout state is contradictory and arbitrary one-covering-stack roots still expose actions

Status: Implemented 2026-07-22. State progression is now exposed through one
`MultiwayNextNodeKind`; incomplete all-in runouts are chance-only rather than
terminal, and initialization closes one-actionable-seat betting roots.

Evidence:

- `MultiwayState::is_terminal()` is an alias for `is_hand_over()` at
  `include/games/multiway_state.hpp:75-79`.
- `is_hand_over()` returns true when betting is complete and at most one live
  seat remains actionable, even before the river, at
  `src/games/multiway_state.cpp:183-189`.
- `requires_board_runout()` returns true for that same state at
  `src/games/multiway_state.cpp:191-194`.
- The regression at `tests/test_multiway_state.cpp:213-226` therefore requires
  one object to report both `is_hand_over()` and `requires_board_runout()`.
- `MultiwayState::initial()` and `from_snapshot()` do not close betting merely
  because there is only one actionable live seat. If a valid arbitrary root
  starts with stacks `{900, 0, 0}`, three non-folded seats, and seat 0 pending,
  validation accepts a current player and `legal_actions()` offers check/bet
  and all-in even though no opponent can respond. The actionability-dependent
  termination at `src/games/multiway_state.cpp:187` only triggers after
  `current_player_` has already become negative.

Impact:

An integrated traversal that follows the conventional `is_terminal()` check
first can try to settle showdown with an incomplete board. Conversely, an
arbitrary online root on an all-in runout can create strategically meaningless
betting edges. The state does not provide one unambiguous next-node kind.

Required fix:

Represent the progression explicitly, for example:

- betting decision;
- street/public chance transition;
- chance-only board runout;
- fold terminal;
- showdown-ready terminal.

`is_terminal()` should be true only for fold resolution or a showdown with the
required public board. Initialization and snapshot construction should convert
states with at most one actionable live seat directly to chance-only runout.

Regression gate:

- Every valid state must select exactly one next-node kind.
- A flop/turn snapshot with one covering stack and multiple all-in opponents
  must expose no betting actions and must produce only public-chance children.
- Showdown settlement must be unreachable before five board cards exist.

### P1-3: zero-work sampled exports label every root action as fold

Status: Implemented 2026-07-22. Root descriptors are now built from the legal
root menu for both zero-work and expanded exports, so no valid action is used
as a placeholder.

Evidence:

- A zero-batch request initializes the root but does not expand it; newly
  created nodes have `edge_count == 0` and `expanded == false` in
  `src/solver/hunl_sampled_builder.cpp:259-297`.
- The solver creates a uniform strategy from the legal action count at
  `src/solver/hunl_sampled_solver.cpp:196`.
- `export_uniform()` initializes every descriptor with `ACTION_FOLD`, target
  zero, and menu ID zero at `src/solver/hunl_sampled_export.cpp:22-33`.
- Descriptors are attached only when `root.edge_count` equals the exported
  action count at `src/solver/hunl_sampled_solver.cpp:266-282`. For an
  unexpanded decision root the edge count is zero, so this condition is false.
- Existing zero-work tests assert normalization and action count but do not
  assert the action IDs, targets, or menu ID.

Impact:

The documented initialization-only API returns a plausible normalized typed
policy in which check, call, bet, raise, or all-in choices can all be labelled
as fold. A downstream UI, blueprint adapter, or diagnostic consumer can apply
the probabilities to the wrong physical actions.

Required fix:

Build typed descriptors directly from the root legal-action menu for zero-work
exports, without requiring full child expansion. Never use a valid poker action
as a placeholder; use an explicit invalid/unset state internally and reject it
at the public export boundary.

Regression gate:

- Zero-work and positive-work exports for the same root must contain identical
  ordered action IDs, targets, and action-menu identity.

### P1-4: sampled fixed-order merge is not reproducible across worker counts

Status: Implemented 2026-07-22. Deltas carry global trajectory ordinals and
each batch is reduced once in infoset/cell/trajectory order, independently of
worker partitioning.

Evidence:

- Worker aggregate deltas retain no trajectory ID or insertion sequence in
  `HUNLSampledValueDelta`.
- `merge_hunl_sampled_worker_deltas()` uses `std::sort` by infoset, bucket, and
  action at `src/solver/hunl_sampled_traversal.cpp:334-343`. The comparator
  treats all deltas for the same cell as equivalent, and `std::sort` is not
  stable.
- Workers own different contiguous trajectory partitions depending on worker
  count, then are merged by worker index at
  `src/solver/hunl_sampled_solver.cpp:218-259`.
- Float row updates are applied one delta at a time at
  `src/solver/hunl_sampled_traversal.cpp:253-267`, so changing the order can
  change the result through non-associative rounding.
- Tests compare repeated runs with the same worker configuration and verify
  sorting of different action keys, but do not compare one worker with N
  workers for identical trajectory IDs.

Impact:

The same seed and global trajectories can accumulate equal-key updates in a
different order when worker count changes. This violates the documented
cross-worker reproducibility gate and weakens exact-oracle statistical
comparisons.

Required fix:

Preserve global trajectory order for equal row cells, for example by carrying a
trajectory/sequence ordinal and using a stable deterministic reduction. Define
whether the contract is bitwise equality or a documented numerical tolerance,
then test it across worker counts and batch partitions.

### P1-5: an exception on MCCFR worker zero bypasses the coordinator wait/cleanup path

Status: Implemented 2026-07-22. The coordinator captures its own worker-zero
failure, waits for all dispatched workers, clears uncommitted scratch, then
rethrows transactionally.

Evidence:

- `HUNLFlatMCCFR::run_player_subbatch()` dispatches background workers, then
  calls `execute_worker_batch(0, ...)` directly at
  `src/solver/hunl_flat_mccfr.cpp:2089-2113`.
- The coordinator waits for background completion and rethrows captured worker
  errors only after the worker-zero call returns at
  `src/solver/hunl_flat_mccfr.cpp:2114-2121`.
- If worker zero throws, stack unwinding skips that wait while persistent
  background workers may still be writing their scratch and later incrementing
  `worker_completed_count_`.
- A retry resets the shared completion counter and starts another generation,
  so a late completion from the abandoned generation can corrupt the predicate
  or cause a wait for exact equality to hang.
- The regression at `tests/test_hunl_flat_mccfr.cpp:1729-1738` injects only a
  background-worker failure (`test_throw_worker_index = 1`), not a coordinator
  worker failure.

Impact:

An invariant, allocation, or injected failure on worker zero can leave the
oracle session with outstanding work. Retrying the solver is not safe, and its
deadline/failure behavior no longer guarantees a clean committed snapshot.

Required fix:

Catch worker-zero exceptions at the coordinator, mark the generation failed,
wait for every dispatched background worker, discard every worker delta for
the subbatch, reset generation state transactionally, and then rethrow. Add
worker-zero and mid-traversal fault-injection regressions followed by a safe
retry or explicit permanently-failed-session check.

## P2 findings

### P2-1: explicit-hand sampled solving allocates 1,326 buckets while updating only bucket zero

Status: Implemented 2026-07-22. Explicit fixed-deal solving now uses its single
private bucket; range domains remain an explicit future contract.

Evidence:

- `infer_bucket_count()` returns 1,326 whenever a root state has fixed hole
  cards at `src/solver/hunl_sampled_solver.cpp:51-59`.
- Every trajectory request is created with `bucket = 0` at
  `src/solver/hunl_sampled_solver.cpp:202-214`.
- Coordinator preparation allocates that full bucket count for every touched
  infoset at `src/solver/hunl_sampled_traversal.cpp:345-370`.

Impact:

The fixed-hand oracle path uses only one private state but pays the memory cost
of 1,326 buckets per row. This makes the experimental lazy path appear much
less sparse and can hit its cache/memory limits prematurely.

Required fix:

Use one bucket for an explicit fixed deal. Structured range solves should
derive their actual compiled combo/bucket domain explicitly rather than infer
it from the presence of hole cards.

### P2-2: sampled bucket-count configuration is narrowed without validation

Status: Implemented 2026-07-22. The public sampled bucket hint now uses the
same `uint32_t` domain as infoset rows, eliminating a lossy execution cast.

Evidence:

- `bucket_count_hint` is `std::size_t`, and sampled config validation does not
  bound it to the `std::uint32_t` row/storage domain at
  `src/solver/hunl_sampled_config.cpp:5-22`.
- Execution casts the inferred 64-bit count directly to `std::uint32_t` at
  `src/solver/hunl_sampled_solver.cpp:201`.
- A value such as 2^32 can therefore become zero if guardrails are disabled or
  thresholds are raised enough to let it reach execution.

Impact:

The accepted public configuration can turn into an invalid bucket domain or a
different bucket count at runtime rather than rejecting at validation.

Required fix:

Use one checked bucket-count type throughout and reject before any narrowing.

### P2-3: sampled NashConv diagnostics still lack per-seat uncertainty

Status: Implemented 2026-07-22. Quality diagnostics now carry per-seat sample
counts, standard errors, and confidence intervals; sampled production results
also require a policy identity.

Evidence:

- `MultiwayQualityDiagnostics` stores one aggregate `standard_error` at
  `include/solver/multiway_cfr.hpp:69-77`.
- `compute_multiway_nash_conv()` stores a signed unilateral improvement per
  seat but has no per-seat sample count, variance, or confidence interval at
  `src/solver/multiway_cfr.cpp:210-235`.
- Policy/model versions may remain empty even for a sampled estimate.
- The prior follow-up required sampled noise to remain visible **per seat as
  well as for the aggregate**, but current tests validate only the single
  aggregate field.

Impact:

A total standard error cannot describe differently noisy seat-specific best
response estimates or explain a negative unilateral improvement. Quality
dashboards can compare incompatible policies/models without an enforced
identity.

Required fix:

Record per-seat sample count, estimate variance/standard error, and confidence
interval alongside the aggregate method. Require policy and applicable model
identity for production sampled diagnostics.

## Recommended repair order

1. Correct sampled worker-arena accounting and add real runtime byte admission.
2. Replace Cartesian structured-range validation with compiled bounded range
   inputs that are preflighted before allocation.
3. Fix the multiway full-tree and external-sampling average-strategy weights
   and validate them by exhaustive expectation.
4. Replace the multiway terminal/runout booleans with one explicit next-node
   state and close arbitrary one-covering-stack roots.
5. Make typed zero-work root exports correct and make sampled merge ordering
   reproducible across worker counts.
6. Make worker-zero failure transactional in the full-graph oracle.
7. Correct bucket-domain inference/narrowing and complete per-seat NashConv
   confidence diagnostics.
8. Only then connect multiway state, private sampling, pots, CFR deltas,
   storage, traversal, typed export, and policy evaluation end to end.

## Go/no-go conclusion

**Go for isolated contract and regression work; no-go for integrated production
multiway traversal on the current contracts.**

The project does not need another broad rewrite. The defects above have narrow
repair surfaces, but the two P0 memory paths and the multiway average-policy and
runout semantics should be locked down before Phase C integration begins.
