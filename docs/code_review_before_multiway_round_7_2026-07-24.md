# Seventh code review before integrated multiway solving

Date: 2026-07-24

## Executive verdict

The repository is **not ready to begin an integrated multiway solver**.
Standalone multiway betting, private-range, terminal-settlement, and CFR-row
helpers are useful foundations, but they do not yet form a solver boundary or
an unbiased external-sampling pipeline.

This review was performed statically by the primary reviewer and an
independent `code_reviewer` agent. No source files were changed. No build,
test, benchmark, install, or solver command was run, in accordance with
`AGENTS.md`.

The required next step is to complete the shared sampled-solver contracts and
the heads-up sampled-engine gates described in
`mccfr_large_tree_implementation_plan.md`, then introduce multiway traversal
on those contracts. Do not widen the full-graph MCCFR implementation.

Severity definitions:

- **P0**: blocks a correct integrated solver or can bias its updates.
- **P1**: leaves critical game-state semantics ambiguous between components.
- **P2**: makes validation, diagnostics, or large-root operation unreliable.

## P0 findings

### P0-1: there is no integrated multiway solver boundary

The public facade re-exports multiway helper types, but it does not expose a
multiway solve request/result, traversal, policy storage, scheduler, ordered
merge, root export, or policy-evaluation API.

Evidence:

- `include/core/lib.hpp:54-76` only re-exports standalone multiway types.
- `include/solver/multiway_cfr.hpp:23-129` defines configuration, update, and
  metric helper types, but no policy state or traversal interface.
- The production plan explicitly lists integrated traversal, typed export,
  policy evaluation, and NashConv estimator diagnostics as Phase C work in
  `docs/mccfr_large_tree_implementation_plan.md`.

Impact:

Adding player-indexed fields directly to heads-up structures would couple
multiway semantics to a solver that is not yet production-ready. It would also
leave storage ownership, deterministic worker merge order, terminal ownership,
and export semantics undefined.

Required fix:

Define a narrow multiway solver module before implementing traversal:

1. immutable structured root request and typed result;
2. stable per-seat public/infoset identifiers and action descriptors;
3. sparse row storage and worker-local bounded delta streams;
4. coordinator-owned public-state admission and fixed-order merge;
5. root-only export plus policy/value/quality diagnostics.

### P0-2: compatible private-deal sampling does not expose the proposal probability

`make_multiway_external_sampling_cfr_update()` requires a sampling reach, but
the two sampler APIs return only sampled holes and rejection attempts. A
compatible deal is drawn from independent per-seat ranges and accepted only
after conditioning on no card collisions. The required conditioned proposal
probability cannot be reliably reconstructed from those outputs.

Evidence:

- `MultiwayExternalSamplingRequest` requires `chance_reach` and
  `sampling_reach` in `include/solver/multiway_cfr.hpp:44-52`.
- `MultiwayJointPrivateSample` exposes only `holes` and `attempts` in
  `include/games/multiway_private.hpp:40-43`.
- `MultiwayCompiledPrivateRanges::try_sample_into()` exposes only success,
  sampled holes, and attempts in `include/games/multiway_private.hpp:59-64`.
- Independent per-seat draws and collision rejection are implemented in
  `src/games/multiway_private.cpp:76-109` and `:157-184`.

Impact:

An integrated MCCFR traversal can mistakenly use unconditioned product range
weights for a rejection-conditioned draw. That biases regret and average
strategy updates while still producing finite, normalized policies.

Required fix:

Compile a joint proposal contract that returns, for every accepted deal:

- the conditional deal probability or log-probability;
- explicit chance/proposal/inclusion reach components;
- a deterministic policy for bounded rejection exhaustion;
- diagnostic counters for accepted, rejected, and discarded trajectories.

The traversal must consume these values directly rather than recomputing them
from hole cards.

### P0-3: the production-facing structured HUNL sampled facade cannot perform positive work

The facade constructs a structured range request and calls the sampled solver,
but positive batches explicitly throw `HUNLSampledStructuredRangeNotReady`.
Positive timed solving also fails closed.

Evidence:

- `include/core/lib.hpp:85-97` presents
  `solve_hunl_postflop_sampled()` as the production-facing entry point.
- `src/solver/hunl_sampled_solver.cpp:162-164` rejects positive structured
  range batches.
- `src/solver/hunl_sampled_solver.cpp:129-136` rejects positive timed solves.

Impact:

The shared range-aware, deadline-aware sampled foundation required by the
multiway design is not available yet. Multiway must not implement an unrelated
range and deadline model to work around this boundary.

Required fix:

Finish the heads-up sampled-engine Phase B gate first: private-state-aware
range propagation, resumable deadline batch state, exact-oracle comparison,
and shared leaf-evaluation support.

### P0-4: the full-graph MCCFR prototype still allocates graph-sized worker deltas

Every worker prepares delta rows for all infosets and allocates regret and
strategy arrays for every row value.

Evidence:

- Workers call `prepare_delta_rows(infoset_meta_)` in
  `src/solver/hunl_flat_mccfr.cpp:248-251`.
- `WorkerScratch::prepare_delta_rows()` resizes for every infoset and assigns
  both `regret_delta` and `strategy_delta` to `meta.value_count` in
  `src/solver/hunl_flat_mccfr.cpp:410-421`.

Impact:

Memory grows with the complete graph and again with worker count. Widening this
implementation to three through six players would multiply the already
disallowed graph-sized scratch allocation.

Required fix:

Keep `HUNLFlatMCCFR` as an oracle/prototype only. The production and multiway
path must use bounded active-row arenas or ordered sparse delta streams.

## P1 findings

### P1-1: the root snapshot does not define the complete multiway public contract

**Status: fixed (2026-07-24; static verification only).**

The integrated root boundary now carries explicit board/runout state, a
deterministic next-street seat, an explicit odd-chip rule, stable typed action
descriptors, and an action-abstraction identity. Validation derives the
expected board/runout state from the immutable betting snapshot, validates the
typed action menu against that snapshot, and preserves seat order, weighted
ranges, version metadata, and value units in the copied solve request.

Implementation evidence:

- `include/solver/multiway_solver.hpp` defines the completed root contract.
- `src/solver/multiway_solver.cpp` validates its board, action, seat, and
  odd-chip invariants.
- `tests/test_multiway_solver.cpp` covers complete valid roots, invalid
  board/runout and action metadata, invalid seat/odd-chip metadata, and the
  defensive request copy.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

`MultiwayBettingSnapshot` captures betting-round state but not canonical public
history/action-abstraction identity, board/chance state, positional metadata,
weighted ranges, odd-chip order, or version metadata. The state transition API
also asks the caller to choose the first player of the next street.

Evidence:

- Snapshot fields end at betting metadata in
  `include/games/multiway_state.hpp:45-63`.
- `begin_next_street()` accepts a caller-selected `first_player` in
  `src/games/multiway_state.cpp:427-449`.
- Showdown separately requires board, holes, contributions, folds, and
  odd-chip seat in `include/games/multiway_private.hpp:81-93`.

Required fix:

Introduce one immutable integrated root snapshot with a canonical public
history, board/runout state, seat order, stack/contribution/fold state,
odd-chip rule, weighted ranges, abstraction version, leaf-model version, and
value units. Define stable typed action descriptors from that snapshot.

### P1-2: board-runout and terminal transitions are labels, not shared traversal operations

**Status: fixed (2026-07-24; static verification only).**

`MultiwayTerminalAdapter` now owns the integrated boundary for public board
chance, root-owned street transition, and terminal resolution. It emits
card-id-ordered one-card chance edges that exclude the supplied compatible
private deal; transitions use the immutable root's next-street seat; and fold
and showdown outcomes delegate to the established terminal settlement and
showdown evaluators. The adapter validates board, state, transition, and
private-deal compatibility and introduces no traversal, pot, odd-chip, or hand
evaluation duplication.

Implementation evidence:

- `include/solver/multiway_terminal_adapter.hpp` defines the narrow adapter.
- `src/solver/multiway_terminal_adapter.cpp` implements chance, transition,
  and delegated settlement operations.
- `tests/test_multiway_terminal_adapter.cpp` covers canonical chance ordering
  and probability, root-owned transition order, all-in runout gating,
  fold/showdown settlement delegation including odd chips and side pots, and
  invalid inputs.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

`MultiwayState` can identify `BoardRunout` and `StreetTransition`, but there is
no common chance-edge API and no adapter that converts a state plus private
deal into fold/showdown utilities.

Evidence:

- `next_node_kind()` returns those labels in
  `src/games/multiway_state.cpp:219-239`.
- Side-pot and odd-chip settlement are implemented separately in
  `src/games/multiway_terminal.cpp`.

Required fix:

Provide one terminal/chance adapter owned by the integrated solver boundary.
It must create canonical public chance edges, apply street transitions, and
delegate every terminal to the validated side-pot settlement code. Do not
duplicate this logic in traversal, quality evaluation, and export paths.

### P1-3: duplicate public-state admission could silently change a stable state contract

**Status: fixed (2026-07-24; static verification only).**

The post-remediation architecture rescan found that a repeated public-state ID
was only partially compared. Coordinator admission now accepts a repeated ID
only when its complete public descriptor is semantically identical: parent and
history identities, betting snapshot, board/runout metadata, full public
history, and typed action menu.

Implementation and test evidence:

- `include/solver/multiway_solver.hpp` adds value equality for the relevant
  public contract components.
- `src/solver/multiway_solver.cpp` performs full descriptor comparison during
  duplicate admission.
- `tests/test_multiway_solver.cpp` covers an idempotent exact duplicate and
  rejected changes to parent, betting, runout, history, and action metadata.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-4: sparse infoset rows were not bound to their public action menus

**Status: fixed (2026-07-24; static verification only).**

The post-remediation architecture rescan found that coordinator row admission
could create or silently reuse a shape that did not correspond to the owning
public state's typed action menu. Admission now requires the public state to be
known and the action count to match exactly; repeated infoset rows are
idempotent only for an identical shape and reject bucket/action conflicts.

Implementation and test evidence:

- `src/solver/multiway_solver.cpp` validates the owning state/action menu and
  conflicting duplicate row shapes at admission time.
- `tests/test_multiway_solver.cpp` covers exact duplicate idempotency,
  conflicting shapes, unknown public states, and a non-root action-menu count
  mismatch.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-5: terminal adapter could combine a public state unrelated to its root

**Status: fixed (2026-07-24; static verification only).**

The post-remediation architecture rescan found that the adapter independently
validated a root, a betting snapshot, and a sampled deal, but did not prove the
snapshot was a valid descendant of that root. Adapter entry points now enforce
matching seat cardinality, per-seat total chip accounting, non-regressing
street progression, and canonical root-board lineage, including the special
chance-only runout shape.

Implementation and test evidence:

- `src/solver/multiway_terminal_adapter.cpp` validates root-consistent public
  inputs before chance, transition, or terminal work.
- `tests/test_multiway_terminal_adapter.cpp` covers seat/accounting/street and
  board-lineage rejection while retaining valid all-in and side-pot descendants.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-6: root bucket compatibility was deferred until policy export

**Status: fixed (2026-07-24; static verification only).**

The final architecture rescan found that a request could select a root bucket
outside the root sparse row's bucket count and fail only while exporting a
policy. Root-row admission now rejects that incompatible shape immediately,
while non-root rows remain unaffected.

Implementation and test evidence:

- `src/solver/multiway_solver.cpp` validates `root_bucket` against the root
  row's bucket count at admission.
- `tests/test_multiway_solver.cpp` covers rejected one-bucket admission and
  successful two-bucket admission/export for root bucket one.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-7: a row-admission regression expected an obsolete late export failure

**Status: fixed (2026-07-24; static verification only).**

After action-menu validation moved to row admission, one regression still
expected an invalid root row to be admitted and fail only at root export. The
test now asserts the intended immediate `std::invalid_argument` at admission.

Implementation and test evidence:

- `tests/test_multiway_solver.cpp` contains the admission-focused regression
  `multiway_solver_row_admission_rejects_a_shape_that_disagrees_with_root_actions`.

The test was updated but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-8: sparse infoset rows could be admitted for a nonacting seat

**Status: fixed (2026-07-24; static verification only).**

The final code review found that row admission checked the seat range but not
decision ownership. The coordinator now admits a sparse row only when the
public betting state is a decision and the infoset seat equals its current
acting seat.

Implementation and test evidence:

- `src/solver/multiway_solver.cpp` enforces decision-node and acting-seat
  ownership before admitting a row.
- `tests/test_multiway_solver.cpp` uses a real checked-action descendant for
  valid non-root admission and rejects a nonacting seat and a runout state.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-9: adapter lineage checks allowed root seat state to reverse

**Status: fixed (2026-07-24; static verification only).**

The final code review found that preserving a seat's total chips did not prove
that a candidate snapshot descended from the root: it could move committed
chips back into the stack or reverse a fold/all-in. The adapter now enforces
monotonic per-seat commitments and irreversible folded/all-in root state in
addition to its existing total, street, and board-lineage checks.

Implementation and test evidence:

- `src/solver/multiway_terminal_adapter.cpp` rejects reduced root
  contributions, unfolded root seats, and cleared root all-ins.
- `tests/test_multiway_terminal_adapter.cpp` constructs valid candidate
  snapshots and verifies each reversal is rejected.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

### P1-10: child public states were not bound to a replayable parent action

**Status: fixed (2026-07-24; static verification only).**

The final code review found that child admission only checked whether a parent
ID existed. Non-root public states now require a distinct history identity,
the complete parent history plus one appended legal action by the parent’s
acting seat, a replay-equivalent child betting snapshot, and unchanged
board/runout metadata for the action edge.

Implementation and test evidence:

- `src/solver/multiway_solver.cpp` replays and validates every admitted
  parent-to-child betting action.
- `tests/test_multiway_solver.cpp` uses real checked-action descendants and
  rejects missing/wrong history, invalid actor/menu, snapshot mismatch,
  altered board/runout, reused history ID, and broken grandchild history.

Those tests were added but not executed, per `AGENTS.md` and the requested
no-build/no-test constraint.

## P2 findings

### P2-1: NashConv diagnostics accept unknown value-unit enum values

**Status: fixed (2026-07-25; static verification only).**

The public NashConv diagnostics validator now rejects enum values outside the
four defined `MultiwayValueUnits` variants before result publication. A focused
regression passes an out-of-range enum value through
`compute_multiway_nash_conv()` and expects `std::invalid_argument`.

Implementation and test evidence:

- `src/solver/multiway_cfr.cpp` validates `diagnostics.units` at the public
  NashConv diagnostics boundary.
- `tests/test_multiway_cfr.cpp` covers rejection of an unknown value-unit
  enum value.

The regression was added but not executed; no build or test command was run,
per `AGENTS.md` and the requested no-build/no-test constraint.

The diagnostics validator checks metric method and uncertainty fields but does
not validate `MultiwayValueUnits`. `compute_multiway_nash_conv()` then returns
the unvalidated value.

Evidence:

- Validation occurs in `src/solver/multiway_cfr.cpp:48-84`.
- Result publication occurs in `src/solver/multiway_cfr.cpp:259-294`.

Required fix:

Reject unknown `MultiwayValueUnits` values at the public boundary and add enum
boundary regression tests.

### P2-2: compiled-range feasibility has an opaque fixed search budget

The compiled private-range constructor uses a bounded DFS with a fixed
one-million-node limit. Valid sparse six-player ranges can exceed that budget,
but callers receive no structured distinction between an impossible range and
an exhausted feasibility preflight.

Evidence:

- The hard limit and DFS are in `src/games/multiway_private.cpp:28-49`.
- Constructor use is in `src/games/multiway_private.cpp:143-154`.

Required fix:

Expose a coordinator-only feasibility/preflight result with status, visited
node count, budget, and reason. Do not execute feasibility search in worker or
trajectory hot paths.

## Recommended delivery gate

Before writing multiway traversal code:

1. Complete the sampled heads-up Phase B contract and its exact-oracle gates.
2. Define the integrated multiway root/result and canonical terminal/chance
   adapter.
3. Replace private rejection-only output with a conditioned joint-proposal
   sampling contract.
4. Implement sparse per-seat row storage and deterministic worker-local merge.
5. Validate three-player toy games against exhaustive enumeration before
   extending to four through six players.

Only then should the project begin the Phase C integrated multiway traversal.
