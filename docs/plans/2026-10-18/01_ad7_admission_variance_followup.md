Task: Isolate the remaining cold-admission variance after the AD7 schema-v2 identity redesign and implement one measured bottleneck reduction at a time.

# AD7 admission variance follow-up

## Scope

This follows `docs/plans/2026-10-17/01_ad7_state_identity_validation_redesign.md`.
It excludes schema-v2 identity, traversal semantics, sampling, scheduler
semantics, checkpoint layout, catalog identity, and persistent memory layout.
Do not combine discovery merge and parent-transition changes in one commit.

## Evidence

Current source: `PB-3` at `a6728c8`.

The inline identity encoder meets its own cold gate in all four isolation
captures: `55.280-58.817 ms` versus `<=59 ms`. Correctness counters are
identical in every isolation capture:

- 69,194 new and globally unique public states;
- 31,070 new and globally unique rows;
- 69,194 parent-resolution probes;
- six catalog-parent resolutions;
- 31,069 batch row resolutions and one catalog row resolution;
- 62,587 pending-menu reuse hits and 6,607 new menus.

| Capture | Admission ms | Discovery merge ms | Public validation ms | Parent transition ms | Catalog ms |
|---|---:|---:|---:|---:|---:|
| dynamic 1w r1 | 278.804 | 83.986 | 108.323 | 29.444 | 23.975 |
| dynamic 1w r2 | 259.490 | 78.321 | 100.900 | 25.803 | 23.813 |
| fixed 1w r1 | 250.814 | 77.891 | 98.391 | 22.982 | 23.924 |
| fixed 1w r2 | 274.235 | 83.431 | 103.553 | 28.161 | 26.761 |

All four reports retain the cold fingerprint `9806774443936985566`, checkpoint
bytes `137380835`, `failed=false`, and zero discarded trajectories. Therefore
the repeated gate misses are not explained by an output change.

The prior full matrix is consistent with this result. Cold discovery merge is
`73.493-82.700 ms`; fixed 1w r2 also had `28.483 ms` parent-transition work.
The variance moves with discovery merge, public validation, parent transition,
and occasionally catalog commit. No current report splits discovery merge or
parent transition into exclusive internal costs, so selecting a source rewrite
from a single aggregate timer would be speculative.

## Source findings

### F1: one-worker discovery merge still performs per-element merge work

`merge_public_state_streams` and `merge_row_streams` in
`src/solver/multiway/engine/multiway_traversal.cpp` scan every stream to select
the next item, append each element to coordinator-owned output, then scan every
stream again to consume duplicates. With one stream, the outcome is already
sorted and locally deduplicated by `MultiwayDiscoveryStream::prepare_for_merge`.
The cold bucket still copies 69,194 descriptors and 31,070 shapes through the
generic merge path. Its `77.891-83.986 ms` aggregate is the largest non-identity
top-level admission child.

This is a candidate, not proof that copying dominates. The timer also contains
capacity checks, cursor resets, and the generic state/row loops. Add exclusive
telemetry before changing data ownership.

### F2: every new non-root state reconstructs and applies a fixed betting state

`validate_public_state_child_transition` in
`src/solver/multiway/engine/multiway_solver.cpp` calls
`make_multiway_fixed_state(parent.betting)` for every child, then validates one
of the action, board-chance, or street-transition edges. This is invoked once
per new non-root candidate after public-state validation. The aggregate costs
`22.982-29.444 ms` and varies by `6.462 ms` on identical counts.

The action path also performs a full parent-history prefix comparison and a
linear parent-menu search. Board and street paths compare history and board
arrays. These checks are admission invariants. No comparison or fixed-state
construction may be removed merely because discovery generated the candidate.

### F3: parent ordering is separate and currently bounded

`build_parent_first_order` resolves 69,194 parents by binary search, computes
depths, then sorts `(depth, state_id)`. It is `12.536-13.747 ms`, stable enough
to remain outside the first implementation change. It must be retained because
prepared admission requires parent-first order.

## Required diagnostic change

First add additive, non-semantic counters and timers. They must be outside
worker traversal loops and must not change existing field meanings.

For `discovery_merge_nanoseconds`, record disjoint children:

- public-state output merge;
- row output merge;
- total source elements scanned for each;
- duplicate candidates compared for each;
- a one-stream fast-path count.

For `parent_transition_validation_nanoseconds`, record disjoint children:

- parent fixed-state construction;
- action-edge validation;
- board-chance validation;
- street-transition validation;
- counts by edge kind;
- action history-prefix bytes/elements compared and parent-menu probes.

Thread these diagnostics through `MultiwayAdmissionDiagnostics::accumulate()`
and report JSON. Keep the aggregate parent and merge timers unchanged. Unit
tests must assert count relationships and JSON emission, never timing limits.

Run two cold fixed 1w and two cold dynamic 1w captures after this telemetry
only change. Require the existing fingerprint, bytes, failure/discard,
candidate counts, and fixed/dynamic equivalence before using the split to pick
an implementation candidate.

## Implementation candidates, in decision order

### Candidate A: one-stream discovery-copy fast path

Choose only if the new split shows element-wise one-stream output transfer is
the dominant discovery cost.

For exactly one prepared stream, copy its sorted/deduplicated public-state and
row ranges to the reusable coordinator output with bulk vector assignment or
equivalent contiguous transfer. Do not alias worker storage because it is
rewound and reused after the batch. Preserve output ownership, ordering,
capacity accounting, and the duplicate/conflict checks already performed by
`prepare_for_merge`.

Keep the current k-way merge untouched for two or more streams. Do not use
`memcpy` until type traits prove the element type is trivially copyable.

Tests:

- one-stream output equals the existing merge output for states and rows;
- duplicate/conflicting descriptors still fail during worker preparation;
- one-stream and multi-stream ordering remain deterministic;
- output remains independent after worker rewind/reuse;
- fixed/dynamic checkpoint equivalence remains unchanged.

### Candidate B: specialize parent-transition validation by edge kind

Choose only if the split shows a specific edge-kind validator, rather than
fixed-state construction shared by all kinds, dominates.

Factor a narrow parent validation context built once per admitted parent in
parent-first order. It may retain only values already derived from
`MultiwayFixedState`, such as next-node kind, current player, legal menu view,
and the computed action transition when required. Children must still receive
the exact current checks for history/board preservation, typed edge payload,
legal parent action, runout metadata, and exact resulting betting snapshot.

Do not cache across admission epochs, weaken equality comparisons, trust
producer IDs, or change parent ordering. Prefer stack/local context or existing
admission scratch with explicitly bounded lifetime.

Tests:

- action, board-chance, and street-transition valid fixtures remain accepted;
- each existing malformed-edge rejection still occurs before mutation;
- prepared and catalog-resolved parents agree;
- mixed parent edge kinds retain deterministic descriptors and checkpoints;
- late parent-transition failure leaves catalog and storage unchanged.

### Candidate C: parent-order lookup improvement

Consider only if new telemetry disproves A and B or after both are qualified.
Replace the 69,194 `lower_bound` calls only with an equivalent sorted-ID index
or parent-id merge that preserves cycle detection, missing-parent rejection,
depth computation, and `(depth, state_id)` output order. This candidate is
deferred because its current `12.5-13.7 ms` cost does not explain the observed
variance alone.

## Non-goals and guardrails

- Do not alter `MultiwayPublicBuilder` identity inspection or its reference
  hash functions.
- Do not remove coordinator validation or move it after catalog/storage mutation.
- Do not add hash maps, allocations, logging, strings, virtual dispatch, or
  locks in merge or validation inner loops.
- Do not make timing threshold changes, hide diagnostics, or accept one
  favorable capture as qualification.
- Preserve one-write-lock admission, transactional failure behavior, fixed and
  dynamic determinism, menu handles, row layout, checkpoint schema, and memory
  accounting.

## Files and small commits

Expected telemetry files:

- `include/solver/multiway/engine/multiway_solver.hpp`;
- `src/solver/multiway/engine/multiway_traversal.cpp`;
- `src/solver/multiway/engine/multiway_solver.cpp`;
- `src/solver/multiway/workflow/multiway_training_report.cpp`;
- `tests/test_multiway_solver.cpp`;
- `tests/test_multiway_qualification_reports.cpp`.

Potential implementation also touches
`include/solver/multiway/engine/multiway_traversal.hpp` for a private helper or
scratch declaration. No CMake changes are expected.

Commit sequence:

1. `PB-3 Multiway: Split admission variance telemetry`.
2. `PB-3 Tests: Cover admission variance diagnostics`.
3. One of `PB-3 Multiway: Fast-path one-worker discovery merge` or
   `PB-3 Multiway: Reuse parent transition context`.
4. `PB-3 Tests: Cover selected admission optimization`.
5. `PB-3 Docs: Record admission variance qualification`.

Review every C++ commit independently. Preserve existing unrelated untracked
plans.

## Qualification

After each implementation candidate, the operator rebuilds and runs focused
multiway tests, then runs matched cold fixed/dynamic 1w captures twice. The
full cold/steady worker matrix follows only if all repeated 1w runs pass.

Cold gates:

- coordinator admission `<250 ms`;
- history/identity `<=59 ms`;
- public-state validation `<102 ms`;
- catalog commit `<=25 ms`;
- fingerprint `9806774443936985566`;
- checkpoint bytes `137380835`;
- `failed=false`, discarded trajectories `0`, phase coverage `>=0.95`.

If repeated captures still vary materially after the exclusive splits and no
source phase correlates with the variance, record processor frequency,
concurrent load, and Tracy collector state as qualification environment data.
Do not attribute scheduler or operating-system noise to a source change without
matching source telemetry.
