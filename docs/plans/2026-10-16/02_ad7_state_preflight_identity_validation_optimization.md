Task: Reduce AD7 cold admission by optimizing schema-v2 history and public-state identity verification without weakening admission validation.

# AD7 state-preflight identity validation optimization

## Scope

This plan covers the next measured admission optimization after the pending-menu
index work. It does not change traversal, scheduling, public-state identity,
checkpoint layout, catalog layout, or rejection semantics.

The inspected capture is
`tmp/tracy/ad7-next-cold-fixed-1w-r2/report.json`. It was produced by commit
`4617ff8ac14ad6d8226a1bb63de8b1ddfb3abcb6` with Tracy `0.14.1`. The relevant
admission, traversal, builder, and test files have no committed diff between
that producer and the inspected `183d878` working-tree HEAD.

No build, test, executable, capture, analyzer, or solver-launching script was
run for this analysis.

## Conclusion

The best next target is `public_state_history_identity_validation_nanoseconds`,
not catalog menu lookup. It accounts for `74.3284 ms`, or `63.36%` of public
validation and `28.10%` of total admission. Every one of the `69,194` new
descriptors traverses its history three times in this bucket:

1. validate every history entry;
2. hash the complete history for `canonical_history_id`;
3. hash the same complete history again inside the public-state ID.

Implement one allocation-free schema-v2 identity inspector that validates
history entries while feeding the history bytes to two independent FNV-1a
accumulators in lockstep. The two hashes must retain their current distinct
prefix states and byte order. This removes two descriptor-history traversals,
exposes independent hash chains to instruction-level parallelism, and preserves
the exact schema-v2 results.

Do not trust the IDs generated during discovery, skip admission verification,
change the fingerprint schema, or move validation outside the measured
admission interval.

## Findings ranked by expected impact

### [R1] P2: history and identity are traversed three times per new descriptor

Expected impact: highest. Risk: medium because identity bytes are persistent.

`validate_public_state_descriptor` first scans every history entry at
`src/solver/multiway/engine/multiway_solver.cpp:389-397`. It then calls both
identity functions at `:398`. `stable_history_id` appends all history at
`src/solver/multiway/abstraction/multiway_public_builder.cpp:475-481`, and
`stable_public_state_id` appends the same history again at `:484-510`.
`append_history` serializes four 64-bit values per entry at `:198-207`.

The capture reports `74.3284 ms`, or `1,074.20 ns` per new state, for this
bucket. The current admission misses the `250 ms` gate by `14.5493 ms`; a
`19.57%` reduction of this bucket alone closes that gap if other phases remain
flat.

Recommended measurable target:

- history/identity validation at or below `59.0 ms` in the matching cold run;
- public-state validation below `102.0 ms`;
- total admission below `250.0 ms`.

Expected benefit is approximately `12-20 ms` on the reference capture. The
lower end may not pass the total gate. The report does not record total history
entries, so the exact gain cannot be inferred statically; it must be measured.

### [R2] P2: parent transition validation repeats history-prefix comparisons

Expected impact: second. Risk: medium-high because it enforces parent/child
semantics.

`validate_public_state_child_transition` reconstructs the parent fixed state
and compares full histories or the complete parent prefix at
`src/solver/multiway/engine/multiway_solver.cpp:157-222`. It costs `23.5065 ms`.
Do not combine this with the identity change. Reprofile it after R1 so rejection
semantics and performance effects remain attributable.

### [R3] P3: nested per-state telemetry has visible but insufficient overhead

Expected impact: low. Risk: low, but changing it would weaken diagnosis.

The trivial menu-key subphase at
`src/solver/multiway/engine/multiway_solver.cpp:404-410` reports `1.1255 ms`
and is nested inside the `74.3284 ms` history/identity timer. Its clock calls and
counter update are also included in the outer bucket. Removing or coarsening
telemetry cannot explain the remaining `14.5493 ms` gate gap and must not be the
next optimization.

### [R4] P3: catalog menu lookup is no longer the dominant phase

Expected impact: lower than R1. Risk: unnecessary scope expansion.

Menu lookup is `12.3459 ms`, including `1.1245 ms` index lookup and `4.4789 ms`
equality. It is nested inside the `24.0080 ms` catalog commit. It is slightly
above the earlier `10 ms` local gate, but optimizing it cannot be justified
before the `74.3284 ms` identity path is addressed.

## Telemetry topology

The report fields are not additive at every level.

| Parent | Nested children | Parent ms | Child sum ms | Exclusive or unattributed ms |
|---|---|---:|---:|---:|
| Coordinator admission | discovery merge, parent order, state preflight, row preflight, catalog commit, storage commit | 264.5493 | 264.3092 | 0.2401 |
| State preflight | catalog classification, public validation, parent transition validation | 149.3874 | 140.8708 | 8.5166 |
| Public validation | betting/board, menu, history/identity | 117.3147 | 112.5290 | 4.7857 |
| History/identity | catalog menu-key extraction | 74.3284 | 1.1255 | 73.2029 |
| Catalog commit | menu lookup and other catalog work | 24.0080 | not fully disjoint | do not subtract |
| Menu lookup | index lookup, equality, and remaining lookup work | 12.3459 | 5.6034 | 6.7425 |

The top-level admission timer starts at
`src/solver/multiway/engine/multiway_traversal.cpp:1679` and ends at `:1749-1752`.
The state-preflight timer starts at
`src/solver/multiway/engine/multiway_solver.cpp:960` and ends at `:1072-1076`.
Public validation and parent-transition validation are disjoint children of
state preflight, not additional top-level costs.

## Exact call path

1. `MultiwayRootExternalSamplingTraversal::traverse_decision` creates action
   children with `MultiwayPublicBuilder::make_action_child` at
   `src/solver/multiway/engine/multiway_traversal.cpp:939-977`.
2. `MultiwayRootExternalSamplingTraversal::traverse_public_chance` creates
   board-chance and street-transition children at `:1065-1090` and
   `:1121-1146`.
3. The builder computes both IDs before discovery:
   action child at
   `src/solver/multiway/abstraction/multiway_public_builder.cpp:338-360`, board
   chance at `:397-409` and `:426-438`, and street transition at `:455-467`.
   These producer hashes cannot replace independent admission validation.
4. `MultiwayDiscoveryStream::try_append_public_state` copies each descriptor at
   `src/solver/multiway/engine/multiway_traversal.cpp:383-391`.
   `prepare_for_merge` sorts and deduplicates by ID while checking complete
   descriptor compatibility at `:435-451`.
5. `MultiwayRootBatchRunner::run` merges discovery streams at
   `src/solver/multiway/engine/multiway_traversal.cpp:1698-1707`, builds the
   parent-first order at `:1715-1724`, and calls
   `MultiwaySolverCoordinator::admit_prepared_batch` at `:1726-1730`.
6. `admit_prepared_batch` classifies candidates at
   `src/solver/multiway/engine/multiway_solver.cpp:980-986`, then calls
   `validate_public_state_descriptor` for every new state at `:1019-1029`.
7. `validate_public_state_descriptor` validates betting/board at `:300-327`,
   validates the legal menu at `:329-387`, and performs history/identity work at
   `:389-420`.
8. Parent resolution and `validate_public_state_child_transition` follow at
   `src/solver/multiway/engine/multiway_solver.cpp:1033-1062`.
9. Catalog and storage mutation occur only after state and row preflight, at
   `src/solver/multiway/engine/multiway_solver.cpp:1188-1215`.

## Dominant repeated work

Schema-v2 starts each hash with the same schema field at
`src/solver/multiway/abstraction/multiway_public_builder.cpp:18-45` and
`:110-113`. The public-state hash then serializes every betting vector and
scalar, the board, history, and menu at `:490-510`. The history hash serializes
the schema and history at `:475-481`.

For a valid descriptor, short-circuit evaluation at
`src/solver/multiway/engine/multiway_solver.cpp:398` always executes both hash
functions. Therefore every history entry is:

- loaded and checked by the validation loop;
- loaded again and encoded as 32 FNV input bytes for the history ID;
- loaded again and encoded as the same 32 FNV input bytes for the public ID.

`core::fingerprint::append_u64` applies eight dependent FNV steps per value at
`include/core/fingerprint.hpp:17-23`. Running the two complete hashes
sequentially exposes one dependency chain at a time. A lockstep pair helper can
feed the same byte to two independent accumulators, preserving results while
allowing the processor to overlap the chains.

The builder also computes both identities when descriptors are created, but
skipping coordinator verification would admit malformed callers and violate
the public admission contract. This plan optimizes verification itself.

## Implementation plan

### 1. Add an exact identity-inspection result

In `include/solver/multiway/abstraction/multiway_public_builder.hpp`, add a
small value result containing:

- computed canonical history ID;
- computed public-state ID;
- whether every history entry has the currently required structural shape.

Add one `noexcept` static builder function taking the same betting, board,
history, and legal-menu inputs as the existing public-state fingerprint. Derive
the seat count from `betting.stacks.size()`.

Do not add owning storage, callbacks, `std::function`, heap allocation, or a
long-lived cache.

### 2. Implement a dual-lane schema-v2 history append

In `src/solver/multiway/abstraction/multiway_public_builder.cpp`:

1. initialize the history and public-state accumulators independently;
2. append the current betting and board prefix only to the public accumulator;
3. append the exact current history field tag and byte length to both;
4. for each entry, perform the current actor/action/index/menu-ID/target checks;
5. encode actor, action, target, and menu ID once, then update both accumulators
   in explicit byte-by-byte lockstep;
6. append the legal menu only to the public accumulator;
7. apply the existing non-zero normalization to both results.

The emitted byte sequence for each accumulator must match the current
`append_history`, `stable_history_id`, and `stable_public_state_id` functions
exactly. Keep the existing functions as independent reference paths for the
first change; do not silently redefine schema-v2 through shared buggy code.

### 3. Use the inspector only in new-state descriptor validation

At `src/solver/multiway/engine/multiway_solver.cpp:389-400`, replace the
separate history validity loop and two sequential hash calls with one inspector
call. Preserve the two existing error classes and their ordering:

1. malformed history entry throws the existing history-descriptor error;
2. valid-shaped history with either mismatched ID throws the existing
   schema-v2 fingerprint error.

Leave betting/board validation, menu validation, metadata extraction, parent
transition validation, capacity checks, and commit order unchanged.

Do not use the optimized verifier for existing-state compatibility until a
separate profile proves that path material.

### 4. Preserve telemetry meaning

Keep `public_state_history_identity_validation_nanoseconds` around the same
semantic region. It must continue to include history structural validation,
both identity computations, and metadata extraction. Keep
`catalog_menu_key_nanoseconds` nested and document the nesting in tests.

Do not add per-entry clocks. If normalization is needed, add only an additive
integer count of inspected history entries and serialize it through:

- `include/solver/multiway/engine/multiway_solver.hpp:605-700`;
- `src/solver/multiway/workflow/multiway_training_report.cpp:300-333`;
- `tests/test_multiway_qualification_reports.cpp:87-156`.

The counter is optional. The optimization must not depend on it.

## Required regression tests

### `tests/test_multiway_public_builder.cpp`

1. Verify the new inspector returns exactly the existing
   `stable_history_id` and `stable_public_state_id` for:
   - empty root history;
   - one and multiple betting actions;
   - board-chance states;
   - street-transition states;
   - empty and non-empty legal menus.
2. Add fixed schema-v2 golden IDs captured from the pre-change implementation.
   A shared implementation agreeing with itself is insufficient.
3. Verify each structural mutation is reported invalid: negative/out-of-range
   actor, invalid action enum, non-zero history action index, zero menu ID, and
   negative target contribution.

### `tests/test_multiway_solver.cpp`

1. Retain and extend the rejection coverage at `:611-623` and `:648-703`.
2. Independently mutate `canonical_history_id` and public-state `id`; each must
   still be rejected.
3. Mutate history content while leaving both stored IDs stale; it must be
   rejected.
4. Submit a prepared batch whose late descriptor is malformed and verify the
   checkpoint and catalog are unchanged. Keep the capacity rejection coverage
   at `:790-823`.
5. Keep the telemetry nesting assertion at `:782`; add an assertion that the
   menu-key bucket is nested in history/identity when both are non-zero.

### Existing qualification coverage

Retain catalog reconstruction ordering, exact checkpoint round-trip,
fixed/dynamic scheduler equivalence, and deterministic merge tests. No test may
replace semantic assertions with timing thresholds.

## Correctness invariants

- The deterministic merge fingerprint remains exactly
  `9806774443936985566` for the cold workload and
  `3496596242701454594` for the established steady workload.
- Checkpoint bytes remain exactly `137380835` cold and `387676624` steady.
- Every descriptor accepted before the change remains accepted.
- Every malformed descriptor rejected before the change remains rejected with
  the same validation ordering.
- No valid state is rejected and no invalid state is admitted.
- Capacity or validation failure causes no catalog or storage mutation.
- Exact-mode fixed and dynamic scheduling remain deterministic and equivalent.
- `failed=false`, discarded trajectories `0`, and capacity exhaustion stage `0`
  hold in every qualification report.
- Public IDs, history IDs, sorted merge order, menu handles, catalog contents,
  checkpoint telemetry fields, checkpoint section sizes, and memory-ledger
  values remain unchanged except for elapsed-time noise and any explicitly
  added diagnostic counter.
- The verifier uses stack scalars only. Persistent, checkpoint, and retained
  scratch memory accounting must not increase.

## Operator commands

Codex must not run these commands under the current task constraints. The
operator runs them after implementation.

### Build and unit tests

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\full_build.py
cmake --build build-tracy --config RelWithDebInfo --target texas_multiway_train_tracy --parallel
```

The first command must pass the complete Debug build and CTest suite. Review
the focused builder, solver, catalog, qualification-report, checkpoint, and
scheduler tests explicitly in the CTest output.

### Tracy capture matrix

Use this command template:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_multiway_tracy_training_capture.ps1 `
  -CaptureId <id> -Workers <workers> -Batches <batches> `
  -TrajectoriesPerBatch 1000 -Scheduler <scheduler> -CaptureSeconds 60
```

Run the following eight captures with Tracy `0.14.1`:

| ID | Workers | Batches | Scheduler | Expected fingerprint | Expected bytes |
|---|---:|---:|---|---:|---:|
| `ad7-identity-cold-fixed-1w` | 1 | 1 | fixed | `9806774443936985566` | `137380835` |
| `ad7-identity-cold-fixed-4w` | 4 | 1 | fixed | `9806774443936985566` | `137380835` |
| `ad7-identity-cold-dynamic-1w` | 1 | 1 | dynamic | `9806774443936985566` | `137380835` |
| `ad7-identity-cold-dynamic-4w` | 4 | 1 | dynamic | `9806774443936985566` | `137380835` |
| `ad7-identity-steady-fixed-1w` | 1 | 4 | fixed | `3496596242701454594` | `387676624` |
| `ad7-identity-steady-fixed-4w` | 4 | 4 | fixed | `3496596242701454594` | `387676624` |
| `ad7-identity-steady-dynamic-1w` | 1 | 4 | dynamic | `3496596242701454594` | `387676624` |
| `ad7-identity-steady-dynamic-4w` | 4 | 4 | dynamic | `3496596242701454594` | `387676624` |

Analyze each result:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\analyze_multiway_checkpoint_telemetry.ps1 `
  -ReportPath tmp\tracy\<id>\report.json `
  -CsvPath tmp\tracy\<id>\<id>.csv `
  -ExpectedMergeFingerprint <expected-fingerprint>
```

Repeat all four cold captures once. A single favorable result is insufficient
because only `14.5493 ms` separates the reference capture from the gate.

## Required measurements

For each report record:

- coordinator admission;
- discovery merge and parent order;
- state preflight;
- public-state validation and its three nested children;
- parent transition validation;
- row preflight;
- catalog commit, menu lookup, and storage commit;
- new-state validation count;
- failed/discarded/capacity status;
- deterministic merge fingerprint and checkpoint bytes;
- every checkpoint telemetry field and section byte count;
- memory ledger and peak compact-storage bytes.

Calculate only valid exclusive values:

```text
state_preflight_exclusive = state_preflight
  - state_catalog_classification
  - public_state_validation
  - parent_transition_validation

public_validation_exclusive = public_state_validation
  - public_state_betting_board_validation
  - public_state_menu_validation
  - public_state_history_identity_validation

history_identity_exclusive_of_menu_key =
  public_state_history_identity_validation - catalog_menu_key
```

Do not add nested values to their parents. Do not subtract catalog subphases
unless the source proves they are disjoint.

## Acceptance gates

Every cold capture must satisfy:

- `coordinator_admission_nanoseconds < 250000000`;
- `public_state_history_identity_validation_nanoseconds <= 59000000`;
- `public_state_validation_nanoseconds <= 102000000`;
- `catalog_commit_nanoseconds <= 25000000`;
- no unaffected top-level phase regresses by more than `10%` against its
  matching reference capture without an explanation.

Every cold and steady capture must satisfy all correctness invariants above.
Fixed and dynamic captures with the same worker and batch counts must have
identical admitted state/row counts, fingerprint, checkpoint bytes, and
checkpoint telemetry section bytes.

If identity validation improves by less than `10 ms`, inspect generated code
or add history-entry-count telemetry before changing another subsystem. If it
improves by `10-14 ms` but admission remains above the gate, reprofile parent
transition validation as the next isolated target. Do not combine that change
with this one.
