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

## P2 findings

### P2-1: NashConv diagnostics accept unknown value-unit enum values

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
