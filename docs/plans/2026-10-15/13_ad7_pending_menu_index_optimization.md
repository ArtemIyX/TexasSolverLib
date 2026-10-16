Task: Replace the remaining linear within-batch pending-menu search with a collision-safe flat index so cold admission can reach the 250 ms gate.

# AD7 pending-menu index optimization

## Scope

This task follows AD7 catalog menu indexing. It is limited to prepared
multiway catalog admission and the telemetry/tests needed to prove the change.
It does not change traversal, sampling, scheduling, worker merge ordering, row
layout, checkpoint schemas, or public-state compatibility semantics.

The persistent `MultiwayPublicCatalog::menu_index_` is already indexed, but a
cold batch has no persistent menu entries at the start. Repeated menus are
resolved against the newly discovered pending menus. The current pending path
still scans pending menus linearly and compares full vectors.

## Current evidence

The first rebuilt cold capture is:

- `tmp/tracy/ad7-menu-cold-fixed-1w/report.json`

Observed values:

- `coordinator_admission_nanoseconds`: `286537800` (`286.5 ms`);
- `catalog_commit_nanoseconds`: `39752700` (`39.8 ms`);
- `catalog_menu_lookup_nanoseconds`: `28467000` (`28.5 ms`);
- `catalog_menu_index_lookup_nanoseconds`: `1217600`;
- `catalog_menu_equality_nanoseconds`: `22075500`;
- `catalog_menu_index_probes`: `0`;
- `catalog_menu_equality_checks`: `62587`;
- `catalog_batch_menu_reuse_hits`: `62587`;
- `catalog_new_menus`: `6607`;
- `catalog_menu_id_collision_misses`: `0`.

Correctness passed for this capture:

- `failed=false`;
- discarded trajectories `0`;
- cold merge fingerprint `9806774443936985566`;
- checkpoint bytes `137380835`.

The capture is not qualified because the cold gates remain unmet:

- total admission must be below `250 ms`;
- catalog menu lookup must be at most `10 ms`;
- catalog commit must be at most `25 ms`.

The zero persistent-index probes are expected for a cold batch. The equality
count and timing identify the pending-menu resolution path as the next narrow
target. Do not begin validation optimization until this path is remeasured.

## Goal and acceptance thresholds

Reduce pending-menu resolution without changing menu handles, reconstructed
descriptors, fingerprints, checkpoint bytes, or deterministic ordering.

Required qualification after implementation:

- cold `catalog_menu_lookup_nanoseconds` is at most `10000000`;
- cold `catalog_commit_nanoseconds` is at most `25000000`;
- cold `coordinator_admission_nanoseconds` is below `250000000`;
- all cold and steady captures retain their expected fingerprints and exact
  checkpoint sizes;
- all captures report `failed=false`, zero discarded trajectories, and
  capacity exhaustion stage `0`;
- fixed and dynamic scheduling remain equivalent;
- no unaffected admission subphase regresses by more than 10% without an
  explained measurement tradeoff.

Expected fingerprints and checkpoint sizes:

- cold: fingerprint `9806774443936985566`, bytes `137380835`;
- steady: fingerprint `3496596242701454594`, bytes `387676624`.

## Design

### Pending menu index

Add reusable flat pending-menu index scratch to
`MultiwayPublicCatalog`. Each entry contains only:

```cpp
struct PendingMenuIndexEntry
{
    std::uint64_t canonical_menu_id = 0U;
    std::uint32_t pending_menu_index = 0U;
};
```

The pending index must be sorted by `(canonical_menu_id, pending_menu_index)`
before it is queried, or maintained in an equivalent deterministic order that
supports binary equal-range lookup.

For every new state:

1. Obtain the validated menu key supplied by coordinator preflight. Direct
   catalog calls compute the key locally.
2. Search the persistent menu index by key and compare every equal-key menu by
   full descriptor-vector equality.
3. Search the pending menu index by key and compare every equal-key pending
   menu by full descriptor-vector equality.
4. Reuse the existing or pending handle when equality succeeds.
5. If no equality succeeds, append one owned pending menu in first-state
   order and add one pending-index entry.

The canonical ID is only a lookup key. A key collision must never merge
different descriptor vectors. Empty menus continue to use key `0` and full
equality.

Do not use `std::unordered_map` in admission loops unless profiling proves a
flat index insufficient and deterministic handle assignment is separately
protected. Do not change persistent menu ownership or serialize the pending
index.

### Deterministic handle assignment

New menu handles must remain assigned in the first occurrence order of sorted
`MultiwayPublicStateId` input. Sorting pending index entries must not reorder
pending menu ownership or node packing. Keep the current first-occurrence
`menus_` append order and use the index only for lookup.

### Transactional behavior

Pending-index allocation and sorting must complete before logical catalog
mutation. A failure during validation, menu copying, capacity checks, reserve,
or index preparation must leave catalog sizes, exported descriptors, menu
handles, and state-index contents unchanged. Retained scratch capacity may
change after a rejection and must remain included in `memory_bytes()`.

After all reserves succeed, moving pending menus, packing nodes, and swapping
the prepared state/menu indexes must remain non-throwing for the element types.
Keep the existing catalog-before-storage commit order.

## Telemetry

Retain all existing fields and make the following counters distinguish the two
lookup domains:

- `catalog_menu_index_probes`: persistent-index equal-range candidates;
- `catalog_menu_equality_checks`: full comparisons for persistent and pending
  candidates;
- `catalog_existing_menu_hits`: persistent menu reuse;
- `catalog_batch_menu_reuse_hits`: pending-menu reuse;
- `catalog_new_menus`: distinct pending menus created;
- `catalog_menu_id_collision_misses`: equal-key ranges with no full-equality
  match.

If useful for proof, add separate pending counters rather than changing the
meaning of existing counters. Any new fields must be additive in
`MultiwayAdmissionDiagnostics::accumulate()` and training-report JSON.

Keep `catalog_menu_lookup_nanoseconds` as the complete persistent-plus-pending
menu resolution total. Keep `catalog_commit_nanoseconds` as the complete
catalog admission call. Do not use wall-clock thresholds in unit tests;
counter relationships and deterministic results should be tested instead.

## Focused tests

Extend `tests/test_multiway_public_catalog.cpp` with:

- repeated exact menus in a batch use one pending menu owner;
- pending menus with the same canonical ID but different descriptors remain
  distinct;
- empty pending menus are reused safely;
- mixed persistent and pending matches resolve to the correct handles;
- pending-index sorting does not change first-occurrence append order;
- capacity and malformed-input rejection preserve catalog logical contents;
- clear/rebuild and memory accounting include retained pending-index scratch.

Extend `tests/test_multiway_solver.cpp` with:

- a prepared batch reports pending reuse and new-menu counters;
- a mixed existing/new batch preserves reconstruction-based compatibility
  rejection;
- row owner, action count, street, offsets, and capacity behavior remain
  unchanged;
- telemetry accumulates across multiple admission epochs;
- deterministic checkpoint export remains descriptor-equivalent and sorted.

Extend `tests/test_multiway_qualification_reports.cpp` if new JSON fields are
added, verifying every field is emitted and additive.

## Implementation files

Expected source files:

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`, only if coordinator
  metadata or telemetry plumbing changes;
- `src/solver/multiway/workflow/multiway_training_report.cpp`, only if new
  telemetry fields are added.

Expected tests:

- `tests/test_multiway_public_catalog.cpp`;
- `tests/test_multiway_solver.cpp`;
- `tests/test_multiway_qualification_reports.cpp`, if applicable.

Do not change CMake or traversal files unless implementation proves a separate
dependency is required.

## Small commits

1. `PB-3 Multiway: Index pending catalog menus`.
2. `PB-3 Tests: Cover pending menu index collisions and reuse`.
3. `PB-3 Docs: Record pending menu qualification`, only after operator results.

Review each C++ commit before the next one. Do not stage unrelated untracked
plans.

## Operator qualification

The operator must perform these actions after implementation:

1. Rebuild `texas_multiway_train_tracy` in `RelWithDebInfo`.
2. Run the focused catalog, solver, and qualification-report tests.
3. Run the four cold captures and four steady captures using Tracy `0.14.1`:
   `ad7-menu-cold-*` and `ad7-menu-steady-*`.
4. Analyze each report and CSV with
   `scripts/analyze_multiway_checkpoint_telemetry.ps1`.
5. Compare old admission phases, pending/persistent menu counters, menu
   lookup, catalog commit, total admission, memory ledger, fingerprints, and
   checkpoint bytes.
6. Repeat any threshold-borderline capture before accepting the result.

Codex must not build, test, run executables, or run solver-launching scripts
under the current session rules.

## Next decision

If pending menu lookup reaches the threshold but cold admission remains above
`250 ms`, use the already-added validation split to select one measured
validation optimization. If pending lookup remains above `10 ms`, inspect
pending equality checks, key grouping, reserve time, and append time before
touching validation or discovery merge.
