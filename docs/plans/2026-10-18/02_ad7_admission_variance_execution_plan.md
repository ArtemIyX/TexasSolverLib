Task: Implement exclusive AD7 admission diagnostics, qualify them, and apply exactly one measured optimization without changing solver semantics.

# AD7 admission variance execution plan

## Current execution status

Phase 1 telemetry and Tracy instrumentation are implemented in local commit
`de825b1` (`[PB-4] (multiway-tracy) Add admission telemetry and hot zones`).
The implementation includes coordinator allocation accounting, discovery and
catalog copy/capacity counters, report serialization, focused regression
coverage, and scoped Tracy zones around the measured admission and traversal
phases.

The one-stream discovery fast path is already present from `8b890c0`, and its
full descriptor/nested-vector equivalence coverage is strengthened in
`144b413`. The schema-v2 identity out-parameter and explicitly unrolled
dual-lane encoder are already present in `0b59e99`, `60e93f4`, and `a6728c8`.

The CMake-wide Debug build completed successfully. The full Debug CTest set
also passed all 110 tests when `TEMP` and `TMP` were redirected to the
workspace; two atomic-publish tests hit `Access is denied` only under the
default Windows temp directory. Focused CMake Debug builds completed for all
five required targets, and the focused CTest expression passed all five tests.
The Tracy
`RelWithDebInfo` training target also built successfully. An isolated MSVC
`RelWithDebInfo` assembly compile of `multiway_public_builder.cpp` confirmed
that the schema-v2 history encoder has eight explicit byte steps with no call
to `append_u64` or `append_u64_bytes` in the identity function's code range;
the only branch in that region is the outer history-entry iteration. Four
cold-capture qualification remains pending. No candidate qualification result
is claimed.

## Repository state and boundary

- Planning head: `PB-3` at `eda4259`.
- Code baseline: `a6728c8`; the two later commits are documentation-only.
- Predecessor: `docs/plans/2026-10-18/01_ad7_admission_variance_followup.md`.
- This file refines the predecessor into exact field contracts, source edits,
  tests, commit boundaries, and operator qualification steps.
- Do not modify schema-v2 identity, traversal, sampling, scheduler behavior,
  checkpoint layout, catalog identity, row layout, or persistent strategy data.
- Do not combine discovery merge and parent-transition optimization in one
  implementation commit.
- Preserve unrelated untracked plans.

No build, test, benchmark, solver, capture, or Python helper was run while
preparing this plan.

## Independent code review

No actionable findings.

The current code satisfies its existing correctness contracts. The missing
exclusive telemetry and the proposed optimizations are new work, not current
correctness defects. Performance attribution remains unproven until the
telemetry-only captures are complete.

## Verified source behavior

### Discovery merge

`MultiwayRootBatchRunner::run()` prepares each worker stream on the coordinator
thread, then calls `detail::merge_multiway_discovery_streams()` inside the
existing `discovery_merge_nanoseconds` timer.

`MultiwayDiscoveryStream::prepare_for_merge()` already:

1. sorts public states and rows;
2. deduplicates each worker stream;
3. rejects conflicting same-ID public descriptors;
4. rejects conflicting same-infoset row shapes.

The generic merge then:

1. sums source sizes and reserves coordinator-owned output;
2. resets reusable cursors;
3. scans every stream to select the next ID;
4. copies one candidate into coordinator-owned output;
5. scans every stream again to consume and validate duplicates.

For one prepared stream, source order is already final and there are no
cross-stream conflicts. The output must still own deep copies because worker
scratch is rewound before the next batch. `MultiwayPublicStateDescriptor` owns
multiple vectors and is not a byte-copy candidate. `MultiwaySparseRowShape` is
flat, but both paths should use ordinary vector assignment unless an explicit
type-trait proof later justifies a lower-level transfer.

The reusable cursor postcondition must also be preserved. After the combined
merge returns, cursors reflect the row merge, because row merging is the second
operation.

### Parent-transition validation

Prepared admission validates each new descriptor before parent-transition
validation and before catalog or storage mutation. For each non-root child,
`validate_public_state_child_transition()`:

1. reconstructs `MultiwayFixedState` from the parent betting snapshot;
2. dispatches by typed incoming edge;
3. preserves history, board, runout, legal-action, and betting-transition
   invariants specific to that edge;
4. returns before the caller marks the state processed.

The action branch performs one history-prefix equality check and one linear
parent-menu search. The menu has a compact bounded size, but exact probe counts
can be collected without a second scan by changing `std::none_of` to an
equivalent `std::find_if` whose predicate increments a local counter.

The existing aggregate timer is updated only after successful validation.
Child timers and counters must follow the same success-only rule. Otherwise a
thrown transition could leave child values without a matching aggregate value.

### Reporting and accumulation

`MultiwayAdmissionDiagnostics::accumulate()` performs scalar addition and
merges per-worker vectors element-wise. The blueprint trainer calls it after
each batch. `save_multiway_training_report_atomic()` emits the diagnostics as a
flat JSON object under `admission_diagnostics`.

The new fields are additive runtime telemetry. Keep training report schema 7
and every existing field meaning unchanged. Existing readers tolerate extra
JSON members; no checkpoint or evidence identity includes these counters.

## Phase 1: telemetry-only implementation

### Step 1: define stable field semantics

Add these zero-initialized `std::uint64_t` fields to
`MultiwayAdmissionDiagnostics`:

| Field | Exact meaning |
|---|---|
| `discovery_public_state_merge_nanoseconds` | Successful public-state merge interval inside the existing aggregate merge interval |
| `discovery_row_merge_nanoseconds` | Successful row merge interval inside the existing aggregate merge interval |
| `discovery_public_state_source_elements` | Sum of prepared public-state counts supplied to successful merge calls |
| `discovery_row_source_elements` | Sum of prepared row counts supplied to successful merge calls |
| `discovery_public_state_duplicate_candidates` | Public-state source elements beyond the selected globally unique outputs |
| `discovery_row_duplicate_candidates` | Row source elements beyond the selected globally unique outputs |
| `discovery_single_stream_batches` | Successful merge calls receiving exactly one prepared worker stream |
| `discovery_one_stream_fast_path_uses` | Successful calls that actually execute Candidate A; remains zero in telemetry-only commits |
| `parent_transition_fixed_state_nanoseconds` | Successful parent fixed-state construction time |
| `parent_transition_action_edge_nanoseconds` | Successful action-edge checks after fixed-state construction |
| `parent_transition_board_chance_edge_nanoseconds` | Successful board-chance checks after fixed-state construction |
| `parent_transition_street_edge_nanoseconds` | Successful street-transition checks after fixed-state construction |
| `parent_transition_action_edges` | Successfully validated action edges |
| `parent_transition_board_chance_edges` | Successfully validated board-chance edges |
| `parent_transition_street_edges` | Successfully validated street-transition edges |
| `parent_transition_action_history_prefix_elements` | Parent history elements compared by successful action-edge prefix checks |
| `parent_transition_action_menu_probes` | Parent legal-action entries examined by successful action-edge searches |

Do not overload `discovery_single_stream_batches` to mean fast-path use. The
separate fields preserve meaning before and after Candidate A.

For successful merges:

```text
public duplicate candidates = public source elements - public output size
row duplicate candidates    = row source elements - row output size
```

This records cross-stream duplicates without increments in per-element loops.
Prepared streams are already locally unique, so the subtraction is exact.

For successful parent validation:

```text
edge validations = action edges + board-chance edges + street edges
child time = fixed-state time + selected edge-kind time
```

Counts exclude rejected transitions because the unchanged aggregate timer also
excludes them.

### Step 2: make merge diagnostics transactional

In `src/solver/multiway/engine/multiway_traversal.cpp`:

1. Add a private plain scalar `DiscoveryMergeDiagnosticsDelta`.
2. Extend `detail::merge_multiway_discovery_streams()` with an optional
   `MultiwayAdmissionDiagnostics*` defaulted to `nullptr`, preserving current
   direct test callers.
3. Time `merge_public_state_streams()` and `merge_row_streams()` separately
   inside the existing aggregate interval.
4. Reuse the existing source-size totals computed for `reserve()`; do not scan
   elements again.
5. Derive duplicate counts from source totals minus output sizes after each
   merge succeeds.
6. Record single-stream eligibility once per combined merge, not once per
   output kind.
7. Accumulate the local delta into the supplied diagnostics only after both
   merges succeed.
8. Pass `&result.admission_diagnostics` from
   `MultiwayRootBatchRunner::run()`.
9. Leave the outer `discovery_merge_nanoseconds` timer and Tracy zone exactly
   where they are.

The local delta prevents a row conflict or allocation exception from leaving
child counters when the aggregate merge timer is not committed.

### Step 3: split parent-transition validation

In `src/solver/multiway/engine/multiway_solver.cpp`:

1. Add an optional diagnostics pointer to the private
   `validate_public_state_child_transition()` helper.
2. In prepared admission, pass `admission_diagnostics_`; in direct
   `admit_public_state()`, pass `nullptr` so child fields are not recorded
   without the prepared-path aggregate timer.
3. Time only `make_multiway_fixed_state(parent.betting)` as the fixed-state
   child.
4. Start the selected edge timer after fixed-state construction.
5. Keep every validation and exception message unchanged.
6. For action history, add `parent.history.size()` to a local prefix count only
   when the size shape permits the existing full prefix comparison.
7. Replace the existing `std::none_of` menu check with an equivalent
   `std::find_if`; increment a local probe count in the predicate and use
   `it == end()` for the unchanged rejection.
8. For each valid edge, commit fixed time, selected edge time, selected edge
   count, and action-only counters immediately before return.
9. On any exception, commit nothing.
10. Keep the caller-owned aggregate timer unchanged.

Do not add a second menu scan, a per-history-element telemetry loop, a hash map,
or retained validation cache.

### Step 4: thread accumulation and JSON

In `include/solver/multiway/engine/multiway_solver.hpp`:

1. Add every new scalar to `MultiwayAdmissionDiagnostics`.
2. Add every scalar exactly once to `accumulate()`.
3. Do not change per-worker vector accumulation.

In `src/solver/multiway/workflow/multiway_training_report.cpp`:

1. Emit every new field in the existing `admission_diagnostics` object.
2. Keep existing names, nesting, ordering, and schema version unchanged.
3. Do not derive ratios or exclusive residuals in production JSON.

### Step 5: telemetry tests

Add focused tests in `tests/test_multiway_solver.cpp`:

1. `multiway_admission_diagnostics_accumulates_variance_fields`
   - seed two diagnostics objects with distinct values;
   - call `accumulate()`;
   - assert the exact sum of every new scalar.
2. `multiway_solver_discovery_merge_reports_source_and_duplicates`
   - prepare two streams with three public inputs yielding two outputs;
   - prepare two row inputs yielding one output;
   - assert source counts `3` and `2`;
   - assert duplicate counts `1` and `1`;
   - assert no single-stream batch and no fast-path use.
3. `multiway_solver_discovery_merge_reports_single_stream_eligibility`
   - merge one prepared stream;
   - assert one single-stream batch, zero fast-path uses, zero duplicates, and
     source counts equal output counts.
4. Extend the prepared-admission diagnostics test with one valid action child:
   - action count is one and other edge counts are zero;
   - menu probes equal the known action position plus one;
   - prefix elements equal the parent history size;
   - aggregate parent time is at least fixed plus action time;
   - no wall-clock threshold is asserted.
5. Add valid board-chance and street-transition fixtures built through
   `MultiwayPublicBuilder` and the existing terminal-transition helpers:
   - each increments only its own edge count;
   - the sum of edge counts matches the number of successful parent
     transitions in the fixture.
6. Add a malformed prepared transition whose descriptor identity remains
   valid but whose typed incoming edge is inconsistent:
   - rejection occurs before catalog/storage mutation;
   - child counters and the existing aggregate remain unchanged.

When mutating a field included in schema-v2 identity, recompute the descriptor
IDs before admission so the test reaches parent-transition validation. Mutating
only the incoming edge payload is preferable because incoming-edge payload is
not part of the public-state identity.

Extend `tests/test_multiway_qualification_reports.cpp`:

1. assign a unique non-zero sentinel to every new field;
2. save the report;
3. assert every exact JSON key/value pair is present;
4. retain the current cleanup and atomic-publication behavior.

Do not add timing limits to unit tests.

### Step 6: review the telemetry commits

Review the C++ telemetry commit and test commit independently using the C++
review checklist. Confirm:

- child timers are disjoint and nested under unchanged aggregates;
- failure paths do not commit partial child values;
- count definitions match JSON names;
- no worker traversal loop changed;
- no new allocation, string, logging, hash lookup, lock, or virtual dispatch
  was added to merge or validation inner loops;
- direct admission does not publish orphan child timing;
- report schema and checkpoint code are untouched.

## Phase 2: telemetry qualification

### Step 7: operator build and focused tests

The operator runs:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 `
  python scripts\full_build.py
```

If a narrow rerun is needed, use the compact build helper for the affected
targets, then CTest for:

```text
test_multiway_solver
test_multiway_terminal_adapter
test_multiway_qualification_reports
```

The telemetry commit is rejected on any build failure, test failure, changed
exception contract, or transactional mutation.

### Step 8: capture the telemetry-only baseline

Build the RelWithDebInfo Tracy training target, then run four cold captures at
the telemetry-only commit:

| Capture | Workers | Batches | Scheduler | Repeat |
|---|---:|---:|---|---:|
| `ad7-admission-split-cold-fixed-1w-r1` | 1 | 1 | fixed | 1 |
| `ad7-admission-split-cold-fixed-1w-r2` | 1 | 1 | fixed | 2 |
| `ad7-admission-split-cold-dynamic-1w-r1` | 1 | 1 | dynamic | 1 |
| `ad7-admission-split-cold-dynamic-1w-r2` | 1 | 1 | dynamic | 2 |

Use `scripts/run_multiway_tracy_training_capture.ps1` with 1,000 trajectories
per batch and a 60-second capture. Analyze each report with
`scripts/analyze_multiway_checkpoint_telemetry.ps1` and
`-AllowMissingArtifactValidation`.

For every capture require:

- fingerprint `9806774443936985566`;
- checkpoint bytes `137380835`;
- `failed=false`;
- discarded trajectories `0`;
- phase coverage at least `0.95`;
- 69,194 new and globally unique public states;
- 31,070 new and globally unique rows;
- 69,194 parent-resolution probes;
- six catalog-parent resolutions;
- 31,069 batch row resolutions and one catalog row resolution;
- 62,587 pending-menu reuse hits and 6,607 new menus;
- one single-stream batch and zero fast-path uses;
- fixed/dynamic equality for all deterministic counts and artifacts.

Record this matrix without summing nested timers:

| Field | Fixed r1 | Fixed r2 | Dynamic r1 | Dynamic r2 |
|---|---:|---:|---:|---:|
| Coordinator admission | fill | fill | fill | fill |
| Discovery merge aggregate | fill | fill | fill | fill |
| Public-state merge child | fill | fill | fill | fill |
| Row merge child | fill | fill | fill | fill |
| Public source elements | fill | fill | fill | fill |
| Row source elements | fill | fill | fill | fill |
| Public duplicate candidates | fill | fill | fill | fill |
| Row duplicate candidates | fill | fill | fill | fill |
| Parent-transition aggregate | fill | fill | fill | fill |
| Parent fixed-state child | fill | fill | fill | fill |
| Action-edge child/count | fill | fill | fill | fill |
| Board-chance child/count | fill | fill | fill | fill |
| Street-edge child/count | fill | fill | fill | fill |
| Action prefix elements | fill | fill | fill | fill |
| Action menu probes | fill | fill | fill | fill |
| Public-state validation | fill | fill | fill | fill |
| History/identity | fill | fill | fill | fill |
| Catalog commit | fill | fill | fill | fill |

Also calculate only these source-proven residuals:

```text
discovery_merge_residual = discovery_merge
  - public_state_merge_child
  - row_merge_child

parent_transition_residual = parent_transition_validation
  - parent_fixed_state_child
  - action_edge_child
  - board_chance_edge_child
  - street_edge_child
```

Small positive residuals are expected from timer and dispatch overhead. A
negative residual invalidates the instrumentation or extraction.

### Step 9: select exactly one candidate

Use all four captures, not the best capture.

Select Candidate A only when:

- `discovery_single_stream_batches == 1` in every capture;
- public and row duplicate counts are zero in the one-worker captures;
- public-state or row merge child time explains the dominant share of the
  discovery aggregate and tracks its repeated variance;
- the source-element counts are identical across runs.

Select Candidate B only when:

- one edge-kind child consistently explains the dominant parent-transition
  cost or its repeated variance;
- the fixed-state construction child is not the dominant cause;
- counts, prefix elements, and menu probes are identical across matched runs.

Select neither when child timers do not correlate with the variance. Record CPU
frequency, concurrent load, worker affinity, and Tracy collector state before
attributing the difference to source. Candidate C remains deferred.

## Phase 3A: Candidate A, one-stream copy fast path

### Step 10A: implement the narrow branch

Only after Candidate A qualifies:

1. In `merge_public_state_streams()`, branch only on `streams.size() == 1U`.
2. Copy the prepared source range into coordinator-owned output with
   `output.assign(source.begin(), source.end())`.
3. Set cursors to one entry containing the public-state source count.
4. In `merge_row_streams()`, perform the equivalent owned assignment and set
   the cursor to the row source count.
5. Commit `discovery_one_stream_fast_path_uses += 1` only after both assignments
   succeed.
6. Leave the current zero-stream and two-or-more-stream paths unchanged.
7. Do not alias worker vectors, move from worker storage, use `memcpy`, or skip
   `prepare_for_merge()`.
8. Preserve output capacity reuse and the final row-cursor postcondition.

### Step 11A: test Candidate A

Add tests that:

1. compare one-stream output with the generic path invoked using the same
   prepared stream plus an empty second stream;
2. cover both public states and rows, including empty ranges;
3. assert exact ordering, sizes, and cursor postconditions;
4. rewind and reuse the source stream, then prove output descriptors, nested
   vectors, and rows remain unchanged;
5. retain worker-local conflict rejection in `prepare_for_merge()`;
6. retain cross-stream conflict rejection and deterministic multi-stream order;
7. assert one fast-path use and unchanged source/duplicate counter semantics.

Review and qualify only this implementation before any parent-transition work.

## Phase 3B: Candidate B, parent-transition specialization

### Step 10B: require a design checkpoint

Only after Candidate B qualifies, identify the selected edge kind and write the
smallest context that removes measured repeated work. Do not add a general
cache speculatively.

The existing parent order is `(depth, state_id)`, not grouped by parent. A
"last parent" cache is therefore not a valid reuse design. A context indexed by
prepared parent would require batch scratch and memory accounting. Do not add a
full `MultiwayFixedState` vector without proving fanout benefit and bounded
memory cost.

Prefer existing prepared-admission metadata:

- `admission_state_next_node_kinds_`;
- `admission_state_current_players_`;
- the parent descriptor's legal-action view;
- the supplied `parent_indexes` relation.

For catalog-resolved parents, retain the current reconstruction and validation
path unless captures prove those six resolutions material.

### Step 11B: preserve every edge contract

For the selected edge kind, retain:

- action: exact history extension and prefix equality, changed history ID,
  board/runout preservation, acting seat, incoming-edge equality, parent-menu
  membership, exact `apply()` result, and fixed snapshot equality;
- board chance: valid parent node kind, unchanged history/betting, strict board
  growth, parent-board containment, card validity/distinctness, exact dealt-card
  payload, sorted payload, and exact runout metadata;
- street transition: valid parent node kind, unchanged history, exact transition
  board and street size, parent-board containment, exact runout metadata,
  `begin_next_street()` using root seat semantics, and fixed snapshot equality.

Do not trust producer IDs, weaken comparisons, reorder validation after
mutation, or cache across admission epochs.

### Step 12B: test Candidate B

Add valid and malformed fixtures for all three edge kinds even if only one is
specialized. Require:

1. prepared and catalog-resolved parent paths agree;
2. all current malformed-edge messages and rejection points remain;
3. a late transition failure leaves catalog, checkpoint descriptors, storage
   shapes, and admission totals unchanged;
4. mixed edge kinds retain deterministic descriptors and checkpoint bytes;
5. diagnostic counts remain identical to the telemetry-only baseline;
6. no unit timing threshold.

## Commit sequence

1. `PB-3 Multiway: Split admission variance telemetry`
   - diagnostics fields, merge and parent split, accumulation, JSON;
   - must compile with existing tests by preserving the optional merge helper
     parameter.
2. `PB-3 Tests: Cover admission variance diagnostics`
   - count relationships, accumulation, rejection accounting, JSON emission.
3. Run and record four telemetry-only captures.
4. Exactly one implementation commit:
   - `PB-3 Multiway: Fast-path one-worker discovery merge`, or
   - `PB-3 Multiway: Reuse parent transition context`.
5. `PB-3 Tests: Cover selected admission optimization`.
6. `PB-3 Docs: Record admission variance qualification`.

Review each C++ commit independently. Do not amend discovery and parent changes
into one patch.

## Final qualification

After the selected candidate passes focused tests, run two matched cold fixed
1w and two matched cold dynamic 1w captures at the implementation commit.
Proceed to the full cold/steady worker matrix only if all four pass.

Every cold capture must satisfy:

- coordinator admission `<250 ms`;
- history/identity `<=59 ms`;
- public-state validation `<102 ms`;
- catalog commit `<=25 ms`;
- fingerprint `9806774443936985566`;
- checkpoint bytes `137380835`;
- `failed=false`;
- discarded trajectories `0`;
- phase coverage `>=0.95`;
- telemetry count equality with the baseline;
- fixed/dynamic artifact and count equivalence.

The full matrix must also preserve the known steady fingerprint and checkpoint
bytes from the predecessor plan, exact-mode behavior, deterministic merge,
menu handles, row layout, checkpoint schema, and memory accounting.

Reject the candidate immediately on any identity, fingerprint, checkpoint-byte,
count, error-order, transactional, scheduler-equivalence, or memory-accounting
change. Revert only the isolated candidate commit; retain qualified telemetry.

## Stop rule

Stop after telemetry qualification if neither Candidate A nor Candidate B is
supported by all four captures. Do not combine them to force the admission
gate. Record environment data and prepare a separate Candidate C plan only if
the measured `12.5-13.7 ms` parent-order phase becomes the largest stable,
source-correlated remaining cost.
