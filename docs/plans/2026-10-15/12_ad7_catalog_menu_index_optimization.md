Task: Replace linear prepared-catalog menu lookup with collision-checked indexed reuse, then reduce the measured validation remainder; operator runtime validation is required.

# AD7 catalog menu index and cold-admission follow-up

## Scope

This task is limited to multiway coordinator admission. It does not change
traversal, sampling, scheduler, worker-delta merge, policy, row layout, or
checkpoint schemas. The implementation must preserve the existing sorted
state classification and the descriptor reconstruction used when full public
history compatibility is required.

Codex may implement and review the source and tests in a later task. The
operator must build, test, and run the Tracy qualification matrix.

## Measured baseline

The eight latest reports are:

- `tmp/tracy/ad7-post-cold-fixed-1w/report.json`
- `tmp/tracy/ad7-post-cold-fixed-4w/report.json`
- `tmp/tracy/ad7-post-cold-dynamic-1w/report.json`
- `tmp/tracy/ad7-post-cold-dynamic-4w/report.json`
- `tmp/tracy/ad7-post-steady-fixed-1w/report.json`
- `tmp/tracy/ad7-post-steady-fixed-4w/report.json`
- `tmp/tracy/ad7-post-steady-dynamic-1w/report.json`
- `tmp/tracy/ad7-post-steady-dynamic-4w/report.json`

### Cold captures

| Capture | Admission | Discovery merge | State preflight | Public validation | Parent validation | Catalog commit | Menu lookup | Index merge |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fixed, 1 worker | 327.009 ms | 83.170 ms | 154.412 ms | 114.723 ms | 28.562 ms | 62.096 ms | 47.672 ms | 0.155 ms |
| fixed, 4 workers | 347.962 ms | 87.770 ms | 161.616 ms | 118.089 ms | 31.274 ms | 69.107 ms | 54.407 ms | 0.133 ms |
| dynamic, 1 worker | 335.614 ms | 84.650 ms | 155.267 ms | 115.208 ms | 28.711 ms | 65.664 ms | 50.099 ms | 0.169 ms |
| dynamic, 4 workers | 352.110 ms | 90.310 ms | 165.954 ms | 120.150 ms | 32.077 ms | 64.104 ms | 45.720 ms | 0.156 ms |

Each cold capture contains 69,194 new public-state validations, 31,069 rows
resolved from the batch, and one row resolved from the catalog. There are no
existing-state compatibility checks in these cold captures.

### Steady captures

| Capture | Admission | Discovery merge | State preflight | Public validation | Parent validation | Catalog commit | Menu lookup | Index merge |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fixed, 1 worker | 2405.339 ms | 413.390 ms | 577.735 ms | 410.360 ms | 101.432 ms | 1299.880 ms | 1219.271 ms | 2.447 ms |
| fixed, 4 workers | 2562.027 ms | 454.470 ms | 611.648 ms | 424.337 ms | 112.125 ms | 1380.218 ms | 1297.859 ms | 2.201 ms |
| dynamic, 1 worker | 2471.927 ms | 404.460 ms | 602.511 ms | 422.703 ms | 111.322 ms | 1346.129 ms | 1261.840 ms | 2.236 ms |
| dynamic, 4 workers | 2521.960 ms | 437.760 ms | 615.525 ms | 426.774 ms | 113.648 ms | 1348.731 ms | 1264.938 ms | 2.218 ms |

Each steady capture contains 12,309 existing-state checks, 248,408 new-state
validations, 105,118 rows resolved from the batch, and one row resolved from
the catalog. Catalog parent reconstruction occurs only six times. The index
merge is already immaterial. Do not optimize it before menu resolution.

All eight reports have:

- `failed=false`;
- zero discarded trajectories;
- capacity exhaustion stage `0`;
- cold fingerprint `9806774443936985566`;
- steady fingerprint `3496596242701454594`;
- cold checkpoint bytes `137380835`;
- steady checkpoint bytes `387676624`.

## Acceptance thresholds

### Required correctness

- Every cold and steady capture retains the expected fingerprint and exact
  checkpoint byte count above.
- Every capture reports `failed=false`, zero discarded trajectories, and
  capacity exhaustion stage `0`.
- Fixed and dynamic scheduling remain equivalent for the same workload.
- Catalog export remains sorted by public-state ID and byte-identical after
  checkpoint serialization.
- Capacity, malformed input, and compatibility failures leave catalog and
  storage logical contents unchanged.

### Required performance

- Each of the four cold captures has
  `coordinator_admission_nanoseconds < 250000000`.
- Cold `catalog_menu_lookup_nanoseconds` is at most 10 ms and cold
  `catalog_commit_nanoseconds` is at most 25 ms.
- Steady `catalog_menu_lookup_nanoseconds` is at most 130 ms, a reduction of
  at least about 89% from the slowest baseline, and steady catalog commit is
  at most 250 ms.
- No unaffected admission subphase regresses by more than 10% in two repeated
  captures without an explained measurement tradeoff.
- Steady total admission must improve by at least 35% from its matching
  baseline and must not exceed 1600 ms in any qualification capture.

The catalog thresholds prove the index rather than accepting an accidental
total-time improvement. The cold total remains the release gate. Removing all
current cold menu lookup still projects about 273-302 ms, so validation work
is expected unless remeasurement shows a larger secondary effect.

## Current contracts

- `MultiwayPublicCatalog::menus_` owns every menu. Packed nodes own only a
  32-bit handle into that vector.
- Menu equality is full `std::vector<MultiwayActionDescriptor>` equality.
  `action`, `action_index`, target contribution, and `action_menu_id` all
  participate.
- Valid non-empty menus carry the same non-zero canonical `action_menu_id` in
  every action. Empty terminal menus need a separate key representation.
- `index_` is sorted by public-state ID. `nodes_` is append-only and index
  entries map sorted IDs to node positions.
- Prepared public states are sorted and unique by ID. `parent_order` remains
  parent-first. Existing-state compatibility reconstructs the descriptor and
  compares full history.
- The coordinator write lock covers classification, validation, catalog
  commit, and storage commit. Internal non-owning references are valid only
  during this epoch.
- Checkpoint export reconstructs descriptors in sorted index order. Restore
  clears the catalog and replays single-state admission.
- Row admission retains its existing sorted metadata, action-major offsets,
  compact pages, shape validation, and capacity checks.

## Design decisions

### Canonical menu index: implement

Add a private flat index to `MultiwayPublicCatalog`:

```cpp
struct MenuIndexEntry
{
    std::uint64_t canonical_menu_id = 0U;
    std::uint32_t menu_handle = 0U;
};

std::vector<MenuIndexEntry> menu_index_;
```

Keep it sorted by `(canonical_menu_id, menu_handle)`. A non-empty menu key is
its first action's `action_menu_id`; the empty-menu key is zero. Validated
non-empty menus cannot use zero. This extraction is constant-time and does
not recompute the stable menu hash during commit.

`find_menu` becomes an indexed equal-range lookup. It must compare the full
candidate menu against every indexed handle in the equal-key range. A key hit
is not equality. If the key collides, or malformed direct catalog input shares
the first action ID but differs elsewhere, append a distinct menu. This keeps
the current full-equality behavior and makes a 64-bit collision harmless.

The index is redundant metadata. `menus_` remains the owner. Index entries
store integer handles, never pointers or references, so `menus_` reallocation
cannot invalidate them. `clear`, single-state `admit`, batch admission, and
checkpoint restore must maintain the index together.

Do not replace `menus_` with an ordered map or serialize `menu_index_`.
Internal handle values and index iteration order must not enter fingerprints
or checkpoint bytes.

### Repeated menus in one prepared batch: implement

Resolve both existing and pending menus before catalog mutation. Use reusable
catalog batch scratch for:

- one menu key per new state;
- one resolved 32-bit handle per new state;
- menu requests keyed by canonical ID;
- distinct pending menus and their first state index;
- pending menu-index entries;
- pending public-state index entries;
- the merged public-state index.

Process states in sorted public-state order. New menu handles must be assigned
in first-occurrence state order, matching current behavior. Requests may be
sorted or grouped by canonical menu ID for lookup, but pending distinct menu
classes must be sorted back by their first state index before handles are
assigned. Every request first checks the persistent equal-key range by full
equality, then the pending equal-key classes by full equality. Repeated menus
within the batch reuse the same provisional handle.

Prefer flat vectors and sort/merge over a persistent unordered container. A
temporary hash table is acceptable only if profiling proves the flat grouping
material and tests prove that iteration order cannot affect handle assignment.
No hash lookup may be added to traversal, per-action, or per-bucket loops.

### Menu metadata during state preflight: implement

Extend the reusable coordinator admission scratch with one compact record per
prepared state:

```cpp
struct ValidatedAdmissionState
{
    std::uint64_t canonical_menu_id = 0U;
    std::uint32_t menu_handle = invalid_menu_handle;
    std::uint8_t action_count = 0U;
    std::uint8_t street = 0U;
    core::PlayerId current_player = -1;
    MultiwayNextNodeKind next_node_kind{};
};
```

The exact representation may be narrower, but it must be trivially owned by
the coordinator and valid only for the current locked admission epoch.
Populate it when `validate_public_state_descriptor` already has the fixed
betting state and validated menu. The catalog batch API receives the validated
menu keys for new states. Direct `MultiwayPublicCatalog::admit` and direct test
calls compute their keys locally so the existing public behavior remains.

Reuse `action_count`, `current_player`, street, and next-node kind during row
preflight. Do not introduce a long-lived descriptor cache or retain references
to caller vectors.

### Capacity reserve and transactional commit: implement

Perform all rejecting and allocating work before changing catalog sizes:

1. Validate state/flag sizes, sorted uniqueness, packed bounds, and betting
   array shapes.
2. Count new nodes and check `max_nodes_` with subtraction-based overflow-safe
   arithmetic.
3. Resolve all existing and pending menus by key plus full equality.
4. Check final menu count and every assigned handle against `uint32_t`.
5. Materialize copies of distinct pending menus and build complete merged
   public-state and menu indexes in scratch.
6. Reserve final capacity for nodes, menus, the state index, and menu index.
7. After all reserves succeed, move pending menus, pack/append nodes, and swap
   the prepared indexes using operations that are non-throwing for these
   element types.

Member-vector capacity may grow during successful reserve preflight, but a
failure must not change catalog sizes, descriptors, handle mappings, state
index contents, or future lookup results. If an implementation operation after
the first logical append can throw, add an explicit rollback to the saved
sizes and do not publish either prepared index.

Do not reserve `max_nodes_` eagerly. Reserve only the exact required batch
growth. Include persistent `menu_index_` and retained batch-scratch capacity
in catalog memory accounting so the optimization does not hide memory use.

### Temporary copy reduction: implement after the index

The current batch builds `pending_nodes`, then copies them into `nodes_`.
After validation, menu resolution, capacity checks, index construction, and
all reserves complete, pack directly into the reserved tail of `nodes_` in
sorted state order. `MultiwayPackedPublicNode` construction and assignment
must remain `noexcept`; enforce this with static assertions if relied upon.

Keep pending menu ownership because copying caller menus must finish before
mutation. Move those owned vectors into reserved `menus_`. Keep the existing
linear public-state index merge unless new telemetry contradicts the current
2.2-2.5 ms steady measurement.

Single-state admission may share the same internal preparation helper, but its
successful behavior, duplicate handling, sorted insertion, checkpoint restore
path, and public signature must remain unchanged.

### Direct packed-node compatibility: do not use as the sole check

`MultiwayPackedPublicNode` does not own full history. Reconstruction walks the
parent chain and rebuilds the betting-action history. Existing compatibility
also permits equivalent non-betting chance edges only when the complete
payload, including history and legal menu, matches.

Therefore a field-only packed comparison is not equivalent and must not
replace reconstruction for the 12,309 steady existing-state checks. A packed
comparison may be used only as an allocation-free negative precheck. A
candidate that passes it still requires the current reconstruction and
`public_state_candidates_compatible` comparison. Since all measured existing
candidates are compatible, this precheck is unlikely to improve the current
workload and should be omitted unless separately measured.

Keep reconstruction for checkpoint export, existing-state compatibility,
existing catalog parents, and the rare row resolved from the catalog.

## Staged implementation

### AD7G.1 - Add proof telemetry

Add these counters to `MultiwayAdmissionDiagnostics` and JSON output without
renaming or changing existing fields:

- `catalog_menu_key_nanoseconds`;
- `catalog_menu_index_lookup_nanoseconds`;
- `catalog_menu_equality_nanoseconds`;
- `catalog_node_pack_nanoseconds`;
- `catalog_reserve_nanoseconds`;
- `catalog_append_nanoseconds`;
- `catalog_menu_index_probes`;
- `catalog_menu_equality_checks`;
- `catalog_existing_menu_hits`;
- `catalog_batch_menu_reuse_hits`;
- `catalog_new_menus`;
- `catalog_menu_id_collision_misses`.

Retain `catalog_menu_lookup_nanoseconds` as the total indexed resolution cost,
including the collision equality checks. Retain `catalog_commit_nanoseconds`
as the complete catalog call. The new subphases may overlap their documented
parent total but must not overlap one another.

Also split public validation, before optimizing it, into:

- `public_state_betting_board_validation_nanoseconds`;
- `public_state_menu_validation_nanoseconds`;
- `public_state_history_identity_validation_nanoseconds`.

Instrumentation remains coordinator-owned and outside traversal inner loops.
Counts must be usable in unit tests without relying on wall-clock thresholds.

### AD7G.2 - Add and maintain the collision-safe menu index

- Add `MenuIndexEntry`, key extraction, equal-range lookup, and sorted merge.
- Initialize the index through normal root admission.
- Clear it with the catalog.
- Update it in single-state admission and batch admission.
- Keep menu append order based on first public-state occurrence.
- Account for the new retained memory.

At the end of each successful catalog mutation, assert in debug builds that
every menu-index handle is in range and the index is sorted. Do not scan these
invariants in release admission.

### AD7G.3 - Prepare batch menu handles before mutation

- Populate validated menu IDs in coordinator preflight.
- Resolve existing and within-batch repeated menus with full equality.
- Preflight menu-handle overflow and all destination capacities.
- Build both merged indexes before publishing catalog content.
- Direct-pack nodes into reserved storage after preparation succeeds.
- Preserve the existing `new_state_flags`; the catalog remains authoritative
  and rejects a flag that marks an already admitted ID as new.

Run a source review at this boundary specifically for exception paths between
the first append and the two index swaps.

### AD7G.4 - Requalify menu lookup before validation changes

The operator runs the focused build/tests and the eight captures. Continue to
AD7G.5 only if menu lookup meets its thresholds and cold admission still
misses 250 ms. If indexed lookup remains above threshold, inspect the new
probe, equality, collision, reserve, pack, and append fields before changing
any other phase.

### AD7G.5 - Reduce the measured validation remainder

Use the AD7G.1 split to select one narrow change:

1. If menu validation dominates, validate menu-invariant structure once per
   exact menu class in the batch. This reusable result may cover canonical ID,
   action indexes, ordering, duplicate targets, and common embedded menu IDs.
   Betting-dependent legal-action coverage and action execution must still run
   for every state.
2. Reuse the fixed-state metadata already produced during descriptor
   validation for row owner, street, next-node-kind, and action-count checks.
3. If fixed-state conversion in parent validation is measured material, cache
   it only for prepared parents referenced by `parent_indexes`. Do not allocate
   a full fixed-state copy for every candidate without memory measurements.
4. Preserve stable history ID, stable public-state ID, board/runout, betting,
   action executability, and parent-transition checks. Do not trust discovery
   provenance as proof that a descriptor is valid.

Target cold public-state validation at or below 80 ms. Stop once all cold
captures pass 250 ms. Do not broaden into traversal or scheduler code in this
task.

### AD7G.6 - Select the next target if cold admission still fails

Rank only measured exclusive subphases after AD7G.5:

1. remaining public-state validation;
2. parent transition validation;
3. discovery merge, currently 83-90 ms cold;
4. parent ordering, currently about 13 ms cold;
5. row preflight, currently about 10-13 ms cold.

Discovery merge is outside this catalog task. If it becomes dominant, write a
separate plan that preserves worker stream ordering and merge semantics. Do
not hide a miss by weakening validation or moving work outside the measured
admission interval.

## Exception safety and lock contract

- The coordinator continues to acquire one write lock per prepared batch.
- All non-owning catalog views, if added for metadata access, are valid only
  while that lock excludes catalog mutation.
- Validation, state/row capacity arithmetic, menu resolution, menu copies,
  and index allocation complete before logical catalog mutation.
- Catalog commit finishes before storage commit as today. Catalog commit must
  itself be strongly safe. Row-capacity preflight must finish before catalog
  commit so a row-capacity failure cannot leave newly admitted states.
- Storage commit remains non-throwing after its own reserve/preflight boundary,
  or retains its existing rollback guarantee. Do not change row offsets or
  compact page placement.
- Scratch may be resized or retain capacity after rejection; it is not catalog
  state and must be cleared before the next epoch. It must never retain caller
  references.
- Do not release the coordinator lock between catalog preparation and storage
  commit, and do not add a second lock acquisition.

## Determinism and ordering

- Prepared state order remains sorted by `MultiwayPublicStateId`.
- Nodes and newly owned menus are appended in the same first-state order as
  the current implementation.
- The state index remains sorted and is produced by one deterministic merge.
- The menu index is sorted by canonical ID and handle. It is not serialized.
- Hash or key collisions are resolved only by full descriptor-vector equality.
- Checkpoint export continues to follow the state index, not node or menu-index
  storage order.
- No telemetry value contributes to fingerprints or checkpoint payloads.

## Focused regression tests

Extend `tests/test_multiway_public_catalog.cpp` with:

- repeated exact menus across single admissions reuse one logical handle;
- repeated exact menus within one prepared batch reuse the pending handle;
- an empty legal menu is indexed and reused;
- two different menus with the same supplied canonical ID remain distinct and
  reconstruct exactly, proving the full-equality collision path;
- mixed existing and new menus resolve correctly after several batches;
- `clear` followed by single admission rebuilds the index;
- capacity rejection leaves exported descriptors, size, and later lookup
  behavior unchanged;
- batch export remains sorted and all reconstructed menus match inputs;
- catalog memory accounting includes the menu index and retained scratch while
  retaining the fixed-node repeated-menu advantage.

Extend `tests/test_multiway_solver.cpp` with:

- a prepared batch containing many states with one repeated menu reports one
  new menu and batch-reuse hits;
- mixed existing/new public states preserve reconstruction-based conflict
  rejection and do not mutate the checkpoint on failure;
- conflicting history with otherwise matching packed fields is rejected,
  guarding against accidental packed-only compatibility;
- a prepared parent and an existing catalog parent retain parent-first and
  transition validation behavior;
- row action count, owner, street, offsets, compact layout, and capacity
  rejection are unchanged when precomputed metadata is used;
- checkpoint export before and after a restore remains descriptor-equivalent
  and sorted;
- telemetry totals accumulate across batches and all additive JSON fields are
  present in the report serialization test or nearest report test fixture.

Retain all existing admission, capacity, ordering, float storage, compact
storage, checkpoint, and deterministic fixture tests.

## Files likely to change

Implementation:

- `include/solver/multiway/engine/multiway_solver.hpp`
  - menu index, batch scratch, validated admission metadata, diagnostics.
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
  - indexed lookup, pending-menu reuse, preflight, direct node append, index
    maintenance, memory accounting.
- `src/solver/multiway/engine/multiway_solver.cpp`
  - metadata production/reuse and validation subphase telemetry.
- `src/solver/multiway/workflow/multiway_training_report.cpp`
  - additive JSON telemetry fields.

Tests:

- `tests/test_multiway_public_catalog.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_qualification_reports.cpp`
  - additive admission-diagnostics JSON fields.

Documentation after operator qualification:

- `docs/PLURIBUS_LOG.md`
- this plan only if measured thresholds or commands require a factual update.

No CMake change or new translation unit is expected. Do not edit
`multiway_traversal.cpp` unless a separate post-AD7G plan is approved.

## Small commit boundaries

1. `PB-3 Multiway: Add catalog menu proof telemetry`
2. `PB-3 Multiway: Index canonical catalog menus`
3. `PB-3 Multiway: Reuse prepared batch menu handles`
4. `PB-3 Multiway: Reuse validated admission metadata`
5. `PB-3 Tests: Cover indexed catalog admission`
6. `PB-3 Multiway: Reduce measured validation remainder`, only if AD7G.5 is
   required and its telemetry supports the selected change.
7. `PB-3 Docs: Record AD7 catalog qualification`, after operator results.

Each implementation commit receives a focused C++ review before the next one.
Do not stage or commit unrelated untracked plans.

## Operator qualification

1. Build the Debug test target using the repository `scripts/full_build.py`
   workflow through `scripts/codex_powershell.ps1`.
2. Run the focused public-catalog, multiway-solver, and report serialization
   tests.
3. Build the RelWithDebInfo Tracy 0.14.1 target.
4. Run four cold captures: fixed/dynamic with one/four workers.
5. Run four steady captures: fixed/dynamic with one/four workers.
6. Store each raw report under `tmp/tracy/<capture-id>/report.json`; recommended
   IDs are `ad7-menu-cold-fixed-1w`, `ad7-menu-cold-fixed-4w`,
   `ad7-menu-cold-dynamic-1w`, `ad7-menu-cold-dynamic-4w`, and matching
   `ad7-menu-steady-*` IDs.
7. Compare total admission, every old admission phase, every new menu phase,
   probe/equality/reuse/collision counts, and memory ledger values.
8. Verify the expected fingerprints and checkpoint sizes exactly:

   - cold: fingerprint `9806774443936985566`, bytes `137380835`;
   - steady: fingerprint `3496596242701454594`, bytes `387676624`.

9. Verify `failed=false`, discarded trajectories `0`, and capacity exhaustion
   stage `0` in all eight reports.
10. Repeat any threshold-borderline capture once before accepting or rejecting
    the optimization.

Codex must not run these builds, tests, executables, or solver-launching
scripts during implementation unless the session constraints are changed.

## Fallback

- If indexed lookup misses its threshold, keep the correctness-preserving
  index only if probe counts show expected logarithmic/grouped behavior and
  the remaining cost is isolated to equality, key preparation, allocation,
  packing, or append. Optimize that measured subphase in a small follow-up.
- If collision or malformed-input handling cannot retain full equality, do not
  ship the index. Revert to linear lookup rather than treating the canonical
  ID as unique.
- If cold admission remains at or above 250 ms after catalog and validation
  thresholds pass, preserve the reports and open a separate discovery-merge
  plan. Its current 83-90 ms cold cost is then the likely next boundary.
- If any fingerprint, checkpoint size, catalog export, row offset, compact
  page, or policy result changes, stop qualification at the first offending
  commit and fix correctness before continuing performance work.
