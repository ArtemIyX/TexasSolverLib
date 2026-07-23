# Fifth code review before integrated multiway solving

Date: 2026-07-23

Reviewed revision: `bcbb177`

## Executive verdict

This static review found **one P0, ten P1, four P2, and two P3
findings**.

The earlier round-four fixes are present, but two of their claimed guarantees
are incomplete:

- the sampled memory limit does not account for capacity growth, rehashes, or
  transient replacement allocations before allocating;
- the `UINT32_MAX` private-sampling regression exercises only successful early
  draws, while both rejection loops still wrap and become unbounded if all
  configured attempts are consumed.

The repository remains appropriate for oracle development and isolated
multiway helper hardening. It is not ready for integrated multiway traversal
or for treating range/cache inputs as production blueprint identities.

No build, test, benchmark, install, or solver command was run. The review was
performed by reading the governing documentation, current source, tests,
prior reviews, and remediation history.

Severity meanings:

- **P0**: defeats an advertised hard resource boundary.
- **P1**: can hang bounded work, admit an invalid game transition, bias or
  misidentify a strategy, access outside a public fixed-size representation,
  or consume corrupt external data unsafely.
- **P2**: weakens numerical parity, large-instance correctness, or defensive
  storage contracts.
- **P3**: lower-risk API/package robustness issue.

## P0 findings

### P0-1: sampled memory admission charges logical growth, not allocation peaks

Status: **Fixed in `f6cbc8a`; tests added but not executed.**

The sampled builder and sparse storage now project vector-capacity
replacement, map-entry/rehash, dynamic state/string, and logical row costs
before allocation with a conservative growth factor. They reserve admitted
capacity before logical mutation, verify retained capacity after allocation,
saturate aggregate byte totals, and roll back partially inserted rows,
nodes, and infosets. Edge arenas receive the same old-plus-new peak admission.
Sixty new boundary scenarios supplement the prior 40 row/node limit cases.

Evidence:

- `HUNLSampledStorage::ensure_row()` compares the current retained-capacity
  estimate with `estimate_row_storage_bytes(shape)`, then grows four separate
  containers. The estimate charges one logical row and its values, but a
  `std::vector` capacity jump or `unordered_map` rehash can allocate materially
  more.
- `HUNLSampledBuilder::find_or_create()` charges one node, one state, and the
  source state's dynamic capacities. It does not charge the node lookup
  rehash, vector capacity growth, the new infoset key/map entry, or allocator
  metadata.
- `HUNLSampledBuilder::ensure_expanded()` charges `edge_count * sizeof(edge)`
  even when `edges_.reserve()` replaces a large retained arena. The old and
  new arenas can coexist during allocation.
- Both mutable domains receive half the residual hard budget, but each local
  admission uses the incomplete estimates above.

Impact:

A solve can pass preflight and local admission while the next row, node,
infoset, edge, or map growth crosses the configured hard limit. Near 60 GiB,
even a normal capacity doubling can cause paging or allocation failure before
the code has a chance to observe the new capacity.

Required fix:

- Add checked projected-capacity and rehash accounting before every retained
  container growth.
- Include the transient old-plus-new allocation peak in admission.
- Reserve admitted capacities before mutating logical state.
- Make multi-container growth transactional.
- Keep a conservative allocator/entry overhead charge and verify the retained
  estimate after each coordinator growth.

Regression plan:

At least 20 boundary scenarios covering row values, metadata, lookup rehash,
node/state growth, infoset strings, edge growth, spare capacity, exact-limit
admission, one-byte rejection, and rollback after rejected growth.

## P1 findings

### P1-1: private rejection loops are still unbounded at `UINT32_MAX`

Status: **Fixed in `3da2d09`; tests added but not executed.**

Both samplers now use one inline 64-bit attempt cursor that publishes a
representable one-based `uint32_t` attempt and stops before incrementing past
the configured domain. Forty new cursor-boundary scenarios cover budgets
1–20 and each of the final 20 values ending at `UINT32_MAX`; the existing 20
maximum-budget sampling seeds remain.

Evidence:

- Both `sample_multiway_private_hands()` and
  `MultiwayCompiledPrivateRanges::try_sample_into()` use a `std::uint32_t`
  loop variable with `attempt_index < max_rejection_attempts`.
- When the limit is `UINT32_MAX` and the last attempt fails, incrementing
  `UINT32_MAX - 1` produces `UINT32_MAX`, the body runs, and the loop increment
  wraps to zero. The condition becomes true again.
- The existing regression uses easy compatible ranges and 20 seeds, so every
  case returns long before the boundary.

Impact:

A public configuration advertised as bounded can hang a worker forever on a
low-acceptance feasible distribution.

Required fix:

Use a wider loop counter or an explicit last-attempt break in both samplers,
and keep the reported attempt count representable.

Regression plan:

At least 20 attempt-budget and seed scenarios, including maximum budget,
success, exhaustion, reused scratch, and boundary-counter coverage through a
small testable attempt-range helper.

### P1-2: sampled merge results depend on worker count

Status: **Fixed in `2c0dd9e`; tests added but not executed.**

The coordinator now sorts each worker-local stream, performs a k-way merge in
cell and global trajectory order, validates the entire batch, and narrows each
central float exactly once. The merge uses one cursor stored in each existing
worker scratch and does not allocate a full coordinator delta copy. Twenty
adversarial cancellation partitions require bit-identical regret and strategy
rows for one through 20 workers.

Evidence:

- Each worker stream is sorted and merged separately.
- `merge_deltas()` sums a cell in `double`, adds it to the central `float`,
  and narrows after each worker.
- Changing worker count changes the number and positions of float roundings:
  one worker narrows once for the whole cell; N workers can narrow N times.
- Trajectory ordinals order deltas inside a worker but do not remove the
  worker-dependent aggregation boundary.

Impact:

The documented deterministic trajectory/merge contract is not invariant
across worker counts. Small rounding differences can compound over long
blueprint generation and invalidate reproducibility comparisons.

Required fix:

K-way merge already sorted worker streams into one global
row/action/bucket/trajectory order without allocating a coordinator copy.
Validate all affected cells transactionally, then narrow exactly once per
cell regardless of worker partition.

Regression plan:

At least 20 partitions of numerically adversarial delta sequences, comparing
bit-identical central rows for one through many worker streams.

### P1-3: snapshots can give an already-acted, fully matched seat another turn

Status: **Fixed in `3457262`; tests added but not executed.**

Snapshot validation now rejects an actionable pending seat when that seat has
already acted and its street contribution equals the current bet. Twenty
two-through-six-player invalid roots cover different offending seats, and 20
matching valid roots prove that acted players below the current bet remain
admissible and retain their turn.

Evidence:

- Snapshot validation requires unacted seats and seats facing a wager to be
  pending.
- It does not reject `pending == true` when a seat has already acted and its
  street contribution already equals `current_bet`.
- Such a seat can be selected as `current_player`; `legal_actions()` then
  offers another check before a real responder acts.

Impact:

An arbitrary live-root snapshot can duplicate a decision and change the game
tree, reach, pot, and strategy.

Required fix:

Reject an actionable pending seat that has already acted and is fully matched.
Preserve the valid cases: unacted checks, players below the current bet, and
cumulatively reopened players below the new bet.

Regression plan:

At least 20 seat-count, current-player, acted, contribution, and short/full
raise snapshot variants.

### P1-4: `HUNLState::apply()` accepts illegal player and chance transitions

Status: **Fixed in `7aa36d4`; tests added but not executed.**

`apply()` now rejects terminal-state actions, requires player actions to occur
in the current legal menu, and requires chance actions to match a current
enumerated outcome before narrowing the card identifier. Forty unknown,
invalid, or blocked action/card cases supplement semantic checks for
check/call/fold/bet/raise/terminal misuse. The old fold-utility regression now
uses a legal bet-fold sequence.

Evidence:

- `apply()` dispatches directly to `apply_player()` or `apply_chance()`.
- `apply_player()` recognizes action identifiers but never verifies that the
  action is in `legal_actions()`. It accepts check while facing a wager, call
  with nothing to call, capped raises, prohibited leads, and other illegal
  transitions.
- `apply_chance()` accepts any `ActionId` narrowed to a byte without checking
  validity, blockers, duplication, pending deal count, or membership in
  `chance_outcomes()`.

Impact:

Public callers can construct impossible game states that later enter builders,
infoset registries, evaluators, or caches.

Required fix:

Require player actions to be in the current legal menu and chance actions to
be one of the current chance outcomes before mutating a copied state.

Regression plan:

At least 20 legal/illegal action and card scenarios across preflop, all
postflop streets, folds, calls, raises, all-ins, blocked cards, duplicate
cards, and non-chance nodes.

### P1-5: HUNL configuration permits unsafe chip and action-menu arithmetic

Status: **Fixed in `3ff1a86`, with the collision-free history-code bound
completed in the infoset remediation commit; tests added but not executed.**

Configuration validation now checks enum domains, forced-bet and maximum-pot
arithmetic in 64 bits, forced bets against stack, bet/force thresholds,
history-safe raise caps, menu widths, every sizing value, and automatic
all-in thresholds. Public action math now rejects non-finite or
unrepresentable rounding inputs and uses checked chip addition/multiplication.
Twenty-nine invalid configuration variants, 20 valid sizing boundaries, and
20 public overflow cases were added.

Evidence:

- Preflop validation computes `small_blind + ante`,
  `big_blind + ante`, and their sum in signed `int`.
- Postflop validation computes `c0 + c1` in signed `int`.
- Runtime action math multiplies `min_bet_bb * big_blind`,
  `force_allin_threshold * big_blind`, pot/fraction values, and contribution
  targets without a validated representable domain.
- Bet/raise menus and the automatic all-in threshold are not required to be
  finite and non-negative; blinds are not required to fit the stack.

Impact:

Malformed but accepted configs can overflow signed arithmetic, create
negative stacks, produce undefined conversions from non-finite doubles, or
emit impossible action targets.

Required fix:

Validate all chip totals with checked 64-bit arithmetic, require forced bets
to fit the stack, validate street and enum values, and require finite
non-negative bounded sizing controls before state construction.

Regression plan:

At least 20 boundary configs spanning blinds, antes, pots, contributions,
stack limits, NaN/infinity, negative and huge sizing values, and valid edge
values.

### P1-6: HUNL infoset encoding can index out of bounds or alias long histories

Status: **Fixed in `555f83b`; tests added but not executed.**

One canonical validator now checks cards, blockers, enum/dimension bounds,
street-length totals, history codes, and zeroed unused storage. Registry
insertion validates before hashing; the hash itself clamps public counts and
remains non-throwing. State encoding requires player 0/1 with private cards,
rejects more than four segments or 48 codes, and no longer truncates.
Opening bets use negative amount codes while raises use positive amount-plus-4
codes, removing the old one-million-chip category collision. Twenty invalid
players, 25 malformed encodings, exact/overflow history capacity, and a
1.2-million-chip bet are covered.

Evidence:

- `infoset_encoding()` and the abstraction/string overload index
  `hole_cards[player]` after converting `PlayerId` to `size_t` without checking
  that the player is 0 or 1.
- `infoset_encoding()` silently truncates history beyond
  `HUNL_MAX_HISTORY_CODES`.
- `hunl_infoset_key()` trusts public `board_count`, `history_count`, and street
  segment lengths, and indexes fixed arrays using those values.
- `HUNLInfosetEncodingHash` has the same trust boundary.

Impact:

Invalid public input can access outside fixed arrays. Long but otherwise
reachable configurations can collapse distinct betting histories into one
infoset and corrupt regret sharing.

Required fix:

Add one canonical encoding validator, reject invalid players and oversized or
inconsistent history/board metadata, and fail closed instead of truncating.

Regression plan:

At least 20 player, board-count, history-count, street-length, unused-field,
and exact-capacity cases.

### P1-7: range masking and propagation can resurrect impossible hands

Status: **Fixed in `3033105`; tests added but not executed.**

Standalone zero-prior normalization remains an explicit uniform-prior
operation. Masking now preflights finite non-negative weights and positive
surviving mass before mutation, then normalizes only enabled entries. Action
multipliers and bucket projection inputs are validated, and zero-mass
posteriors/projections fail closed. Twenty all-blocked masks, 20 sparse
single-survivor masks, 20 invalid priors, 20 zero-posterior filters, and 20
invalid multipliers are covered, plus 20 invalid chance probabilities.

Evidence:

- `apply_mask()` zeroes disabled entries and then calls `renormalize()`.
- `renormalize()` turns any non-empty zero-mass vector into a uniform
  distribution, including entries the mask just disabled.
- Action multipliers and source values are not consistently checked for
  finite non-negative values.

Impact:

An action or blocker filter with no surviving mass returns a uniform range
over impossible hands instead of failing closed. Negative or non-finite
multipliers can also produce invalid posterior reach.

Required fix:

Preserve zeroed masked entries, reject zero surviving mass, and validate every
input/multiplier before normalization. Keep the standalone explicit
zero-vector-to-uniform helper behavior only where a caller intentionally asks
for a prior.

Regression plan:

At least 20 all-blocked, one-survivor, sparse, negative, NaN, infinity,
wrong-shape, and normal posterior cases.

### P1-8: range-cache compatibility omits strategy-defining configuration

Status: **Fixed in `44858de`; tests added but not executed.**

Cache version 2 carries a deterministic fingerprint of the complete HUNL solve
contract: action menus/caps, all-in policy, rake, bucket/depth/mode/range
policy, PCS, private root inputs, and all original public-state fields. Matching
requires that fingerprint, and cache basenames include it to prevent
incompatible entries from overwriting one another. More than 20 one-field
contract mutations are covered.

Evidence:

- `RangeCacheKey` records stacks, blinds, ante, pot, board, contributions,
  abstraction path/version, player, and range kind.
- It omits bet menus, per-street menus, raise multipliers, raise caps, all-in
  policy, minimum/forced sizing, lead policy, depth limit, bucket counts, and
  other policy-defining controls.
- `load_range_cache_if_compatible()` therefore accepts a cache generated for a
  materially different game tree.

Impact:

Offline blueprint/range artifacts can be silently reused under the wrong
action abstraction or solve contract.

Required fix:

Add a stable, serialized solve-contract fingerprint covering every
strategy-defining field. Bump the cache format version and reject older or
incomplete identities.

Regression plan:

At least 20 one-field mutations plus stable round-trip and equivalent-config
cases.

### P1-9: binary range/cache decoders trust unbounded lengths and partially mutate output

Status: **Fixed in `29e4b76`; tests added but not executed.**

Range files cap value counts, validate kind/probability/mask domains, and
publish only a fully decoded temporary. Cache files cap strings at 1 MiB and
boards at five cards, require canonical public metadata and normalized aligned
range/mask payloads, validate diagnostics, reject trailing bytes, and likewise
publish transactionally. Optional-string presence bytes must be exactly zero
or one. Twenty range truncations, 20 invalid probabilities, 20 invalid mask
bytes, explicit oversized/enum cases, 20 cache truncations, and trailing-data
rollback are covered.

Evidence:

- Range value counts, cache strings, labels, and board sizes are resized
  directly from file-controlled `uint64_t` lengths.
- Enum values, card vectors, masks, probabilities, exploitability, and
  cross-object shapes are not fully validated after decoding.
- Decode functions write directly into the caller's object. Truncation or
  corruption can leave a partially replaced object even though the function
  returns false.

Impact:

A corrupt cache can request huge allocations, leave stale/mixed state, or
introduce invalid probabilities and metadata into an offline blueprint
pipeline.

Required fix:

Decode into bounded temporary objects, validate all enums, lengths, values,
cards, shapes, and finite diagnostics, require complete payload consumption
where applicable, and publish only on success.

Regression plan:

At least 20 malformed headers, lengths, enums, truncations, values, masks,
boards, strings, and transactional-output cases.

### P1-10: `ChartRangeSource` maps poker labels by implementation hash

Status: **Fixed in `c57e5f6`; tests added but not executed.**

The implementation-defined hash mapping has been removed. `load()` now always
fails with a directed error until an explicit label-to-combo/bucket mapping is
part of the API; callers can use indexed canonical values or range files.
Twenty representative labels with positive, negative, infinite, and NaN
weights plus empty and duplicate charts are covered.

Evidence:

- Each chart label is reduced to `std::hash<std::string>(label) % value_count`.
- The label is otherwise ignored.
- There is no combo index, bucket identity, chart grammar, collision check, or
  stable cross-platform mapping.

Impact:

A plausible preflop chart can silently assign weights to unrelated combos or
buckets. The artifact is neither semantically correct nor reproducible across
standard-library implementations.

Required fix:

Fail closed until the source receives an explicit label-to-combo/bucket
mapping, or replace the API with indexed entries. Never invent a poker mapping
with a general-purpose string hash.

Regression plan:

At least 20 representative labels/duplicates/invalid weights proving the
source rejects the unsupported contract without returning a range.

## P2 findings

### P2-1: sampled sparse storage admits invalid row shapes

Status: **Fixed in `100e3e3`; tests added but not executed.**

Storage construction now accepts only the two supported layouts and Float32.
Every row is validated before lookup or allocation: player 0/1, a decision
street, one to 14 actions, and one to one million buckets. Sampled config
validation enforces the same layout/bucket ceiling. Twenty invalid layouts,
more than 20 malformed shapes, and 20 valid player/street/size combinations
are covered.

Evidence:

`ensure_row()` accepts zero actions, zero buckets, invalid player/street
metadata, and unsupported layout enum values. An empty arena view can then
perform pointer arithmetic on a null `data()` result.

Impact:

Malformed coordinator input can create unusable metadata, ambiguous uniform
strategies, or undefined pointer behavior.

Required fix:

Validate the complete row shape and layout before lookup or allocation.

Regression plan:

At least 20 valid/invalid shapes across players, streets, action counts,
buckets, layouts, and shape reuse.

### P2-2: `FlatInfosetStore` narrows row offsets to 32 bits

Status: **Fixed in the flat-layout arithmetic remediation commit; tests added
but not executed.**

`RowMeta::offset` is now `size_t`. Row-id, offset, required-size, vector-limit,
and 64-row block-rounding arithmetic is checked before allocation. Row width
and action metadata must fit `uint16_t`, reused keys must keep the original
action shape, and arena growth reserves before transactional logical mutation.
Twenty widths spanning a block boundary, 20 reused-key mismatches, and
invalid width/action boundaries are covered.

Evidence:

- `RowMeta::offset` is `uint32_t`.
- `intern()` narrows `meta_.size() * row_width_` before calculating the needed
  arena size.
- Block rounding arithmetic is unchecked.

Impact:

Large stores can wrap offsets and alias regret/strategy rows before the host
necessarily exhausts the 64 GiB design envelope.

Required fix:

Use checked `size_t` offsets and checked block rounding throughout.

Regression plan:

At least 20 row-width, block-boundary, reuse, and checked-overflow scenarios.

### P2-3: SIMD regret discounting does not match scalar NaN behavior

Status: **Open.**

Evidence:

Scalar discounting leaves a NaN unchanged because neither sign comparison is
true. SSE2/AVX2 mask only positive and negative lanes and replace unordered
lanes with zero.

Impact:

The active backend changes numerical failure behavior. A poisoned exact row
can be hidden on x86 instead of remaining observable.

Required fix:

Preserve unordered lanes in vector kernels and keep scalar/SSE2/AVX2 parity.

Regression plan:

At least 20 lengths, alignments, NaN positions, infinities, signed zeros, and
ordinary values.

### P2-4: inclusive `uint32_t` discount loops wrap at the maximum iteration

Status: **Open.**

Evidence:

Several DCFR/MCCFR/preflop paths use
`for (uint32_t tt = last + 1; tt <= target; ++tt)`. Both `last + 1` and the
post-body increment wrap at `UINT32_MAX`.

Impact:

Long-running/offline iteration metadata can repeat discount epochs or hang.
The same pattern is easy to copy into future multiway storage.

Required fix:

Use a checked/wider closed-range iterator or an explicit last-element break,
and define saturation/exhaustion behavior for public iteration counters.

Regression plan:

At least 20 closed ranges near zero and `UINT32_MAX`, including empty ranges
and one-element maximum ranges.

## P3 findings

### P3-1: the public sampled scheduler can allocate arbitrarily many empty batches

Status: **Open.**

`partition_deterministic(0, very_large_worker_count)` reserves and returns one
empty batch per requested worker. Clamp workers to useful trajectories, while
preserving one empty batch for the existing zero-worker convenience contract.

Regression plan: at least 20 zero/small trajectory and oversized-worker cases.

### P3-2: the installed CMake package does not discover its thread dependency

Status: **Open.**

`TexasSolverConfig.cmake.in` imports `TexasSolverTargets.cmake` without
including `CMakeFindDependencyMacro` and calling `find_dependency(Threads)`.
Consumers can see an unresolved `Threads::Threads` target.

Required fix:

Discover dependencies before importing the exported targets.

Regression plan:

Add a static package-contract test with at least 20 required ordering/token
checks; no configure/install command is run in this task.

## Repair order

1. Close P0 memory admission and verify all coordinator growth is
   transactional by inspection.
2. Close bounded-work and deterministic-merge findings.
3. Close multiway snapshot and HUNL state/input correctness findings.
4. Close range propagation, cache identity, and decoder findings before using
   range artifacts for blueprints.
5. Close storage/numerical/iteration findings.
6. Close scheduler and package robustness findings.

For every finding, this report will be updated with the fixing commit,
implementation summary, regression scenario count, and the explicit note that
tests were added but not executed.
