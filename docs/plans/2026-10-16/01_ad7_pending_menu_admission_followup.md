Task: Explain the remaining AD7 pending-menu admission cost and select the smallest safe next optimization. No implementation or runtime validation was performed.

# AD7 pending-menu admission follow-up

## Verdict

The pending index removed the former linear scan, but
`catalog_menu_lookup_nanoseconds` is not a pure lookup metric. Its current
`12.1253 ms` contains:

- `1.1120 ms` of persistent-index timing on an empty persistent index;
- `4.3645 ms` combining the pending binary search, descriptor equality, an
  empty persistent-equality timing scope, and per-state clock overhead;
- `6.6488 ms` not assigned to a subphase. This includes new-menu vector
  copies/allocations, incremental sorted-vector insertion, handle/counter
  bookkeeping, and the outer per-state timer.

The largest avoidable first step is the empty persistent-menu path and its
nested timing overhead. The smallest safe optimization is to skip
`find_menu_by_key()` when `menu_index_` is empty. This preserves every catalog
contract and is projected to remove about `2.2 ms`, taking the measured lookup
to approximately `9.9 ms`. Because this is close to the `10 ms` gate, it must
be repeated. If it remains above the gate, optimize pending-index construction
before any validation work.

## Actionable review finding

Findings: 1.

[R1] P1 - Producer commit is not a self-contained build revision
Location: `include/solver/multiway/engine/multiway_solver.hpp:256-264` at `b4cd6ba6`; `src/solver/multiway/engine/multiway_public_catalog.cpp:47`, `:243`, `:509`
Evidence: Clean commit `b4cd6ba6` defines and calls `MultiwayPublicCatalog::validate_menu_index_debug()` but the class declaration proceeds directly from `menu_key()` to `classify_new_states()`. The current worktree has an unstaged declaration at header line 264. The report and manifest stamp `b4cd6ba6`, so the captured binary necessarily included uncommitted source or stale objects.
Impact: A clean checkout of the stamped producer revision does not compile, and the report cannot prove exact source provenance.
Fix: Commit the declaration, rebuild from a clean tree, and stamp that new commit in every qualification report.

## Evidence and impact ranking

### 1. Per-state timing and the empty persistent lookup

Expected impact: about `2.2-3.3 ms`; highest-confidence removable cost.

- The cold batch has `69,194` new states, zero persistent probes, and zero
  persistent hits (`report.json:81`). The persistent index remains empty until
  the post-resolution merge at
  `src/solver/multiway/engine/multiway_public_catalog.cpp:460-498`.
- Every state nevertheless calls `find_menu_by_key()` at lines 340-345.
  That function reads the clock around empty `lower_bound`/`upper_bound`
  operations at lines 65-75 and reads it again around an empty equality loop
  at lines 78-97.
- The empty persistent lookup alone reports `1.1120 ms`, or `16.1 ns` per
  state. The constant-time menu-key scope reports a similar `1.1529 ms` at
  `src/solver/multiway/engine/multiway_solver.cpp:404-410`. This is direct
  evidence of the two-clock per-state measurement floor.
- The empty persistent equality scope is included in
  `catalog_menu_equality_nanoseconds` but has no separate field. It should have
  roughly the same measurement floor as the empty persistent lookup.
- The outer lookup timer at catalog lines 323 and 351-356 adds another clock
  pair per state and is part of the `6.6488 ms` remainder.

### 2. Incremental sorted-vector insertion

Expected impact: about `1-2 ms` in this capture; it is the worst-scaling real
operation left in the lookup interval.

- Each of the `6,607` distinct menus performs a binary insertion into
  `batch_pending_menu_index_` at catalog lines 407-412.
- `reserve(new_count)` at lines 313-315 prevents reallocation but does not
  prevent `vector::insert` from shifting the suffix. Construction is therefore
  quadratic in the number and ordering of distinct keys.
- This work is outside the mixed equality timer but inside the outer menu timer.
  There is no insertion-specific timing field.
- Before the pending index, the unexplained lookup remainder was
  `28.4670 - 1.2176 - 22.0755 = 5.1739 ms`
  (`docs/plans/2026-10-15/13_ad7_pending_menu_index_optimization.md:26-34`).
  It is now `6.6488 ms`, an increase of `1.4749 ms`. The implementation change
  replaced append plus one final sort with per-new-menu sorted insertion.

### 3. Menu copying and allocation

Expected impact: material but not isolated by current telemetry.

- Catalog line 403 copies `states[index].legal_actions` into
  `batch_pending_menus_` once for every distinct menu.
- Reserving `batch_pending_menus_` reserves only the outer vector. Each
  non-empty copied action vector still allocates its own storage.
- This copy/allocation is inside `catalog_menu_lookup_nanoseconds`, although it
  is ownership preparation rather than lookup.
- The later move into `menus_` at lines 485-488 transfers the owned inner
  buffer after destination reserve. It does not repeat the descriptor copy and
  is outside the menu-lookup timer.

### 4. Pending lookup and descriptor equality

Expected impact: less than the reported `4.3645 ms` because that field includes
binary lookup and two timing scopes.

- The pending equal range is computed at catalog lines 369-374. Its timer
  starts before both bounds and ends after equality at lines 385-389.
- Full vector equality is retained at lines 375-383. Descriptor equality checks
  action, index, target contribution, and menu ID at
  `include/solver/multiway/engine/multiway_solver.hpp:67-79`.
- The report has `62,587` equality checks, exactly `62,587` pending reuse hits,
  `6,607` new menus, and zero collision misses (`report.json:81`). Thus every
  reuse matched the first equal-key candidate, while new menus had no
  unmatched equal-key range in this workload.
- No exact pending-lookup or pure equality duration can be recovered from the
  current fields. `catalog_menu_equality_nanoseconds` mixes both, plus the
  empty persistent-equality timer.

### 5. Catalog work attribution

Expected impact on the lookup gate: `6.6488 ms` is partly misnamed rather than
unexplained execution.

- New-menu copying, pending-index insertion, handle writes, collision
  bookkeeping, and the outer timer are all counted as menu lookup at catalog
  lines 323-413.
- Reserve (`0.0246 ms`) begins at lines 436-445 and is not in menu lookup.
- State/menu index merge (`0.1747 ms`) is at lines 447-484 and is not in menu
  lookup.
- Append/node packing (`8.5567/8.5566 ms`) is at lines 485-503 and is not in
  menu lookup. It is the other dominant catalog-commit path.
- `catalog_index_insertion_nanoseconds=8.7314 ms` overlaps merge and append at
  lines 447-508. It must not be added to those fields.
- Catalog commit is measured around the complete call at
  `src/solver/multiway/engine/multiway_solver.cpp:1188-1203`. The report's
  `23.3525 ms` commit is below its `25 ms` gate.

## Contract review

### Deterministic handles

- Input states must be sorted and unique at catalog lines 262-268.
- A distinct menu is appended in first-state order at lines 403-406.
- Sorting the pending index changes only lookup order; handles use the append
  index. Final ownership moves in that same order at lines 485-488.
- `tests/test_multiway_public_catalog.cpp:132-168` proves out-of-order keys do
  not change handles (`0, 1, 2, 0`).

### Collision safety and empty menus

- Persistent and pending equal-key candidates require full vector equality at
  catalog lines 79-91 and 375-383.
- Empty menus use key `0` at catalog line 44 and still use full equality.
- Existing tests cover empty reconstruction at
  `tests/test_multiway_public_catalog.cpp:102-114` and pending collision safety,
  reuse, and handle order at lines 132-168.

### Transactional rejection

- Shape, sort, duplicate, catalog-capacity, and already-admitted checks finish
  before scratch mutation at catalog lines 256-305.
- Menu copies and pending-index preparation occur in scratch at lines 307-420.
  Destination reserves and merged indexes are prepared before logical append
  at lines 436-484.
- Coordinator row capacity is checked before catalog commit at
  `src/solver/multiway/engine/multiway_solver.cpp:1176-1189`.
- Catalog and coordinator rejection coverage is at
  `tests/test_multiway_public_catalog.cpp:184-212` and
  `tests/test_multiway_solver.cpp:790-823`.

### Memory accounting

- `memory_bytes()` includes owned menus, retained pending menu buffers, key and
  handle arrays, both state indexes, the pending menu index, and the merged menu
  index at catalog lines 589-604.
- `clear()` retains capacities, and
  `tests/test_multiway_public_catalog.cpp:214-230` checks that retained scratch
  remains accounted.
- `report.json:84` reports the configured `2,048,000,000` byte public-catalog
  memory ledger allowance, not the live value from `memory_bytes()`. The report
  alone cannot prove scratch accounting.

## Smallest safe next optimization

Add one cold-path branch in `admit_batch()`:

1. If `menu_index_.empty()`, set the persistent result to not-found without
   calling `find_menu_by_key()`.
2. Otherwise preserve the current collision-safe persistent lookup unchanged.
3. Do not change the pending equal-range search, vector equality, menu ownership,
   handle assignment, index merge, serialization, or memory layout.

This removes a semantically empty binary lookup and its two nested per-state
timers. It introduces no allocation, scratch, serialization, or transactional
change. The projected `~9.9 ms` result is borderline, so require two cold
repetitions. If either repeated cold capture remains above `10 ms`, do not move
to validation. The next catalog-only step is to replace the `6,607` incremental
sorted inserts with an append-only, preallocated collision-safe pending lookup
and one final sort, while preserving first-occurrence ownership order.

## Required regression tests

1. Empty persistent catalog: a large repeated-menu batch reports zero
   persistent probes/hits, one new menu, `N-1` pending reuse hits, and unchanged
   reconstructed descriptors.
2. Non-empty persistent catalog: exact menus reuse the old handle; same-key
   different descriptors remain distinct after full equality.
3. Pending collisions and out-of-order keys: preserve the exact handle sequence
   already asserted at `tests/test_multiway_public_catalog.cpp:161-164`.
4. Empty menus: two pending empty menus reuse one handle; a later batch reuses
   the persistent empty-menu handle.
5. Transactional rejection: malformed and capacity-rejected batches preserve
   exported descriptors, sizes, handles, state index, and later successful
   lookup. Retained scratch remains included in `memory_bytes()`.
6. Prepared coordinator admission: preserve the telemetry relationships at
   `tests/test_multiway_solver.cpp:759-788` and the capacity transaction at
   lines 790-823.
7. If telemetry fields change, update serialization coverage at
   `tests/test_multiway_qualification_reports.cpp:93-104` and `:142-153`.

Do not add wall-clock assertions to unit tests.

## Operator commands

Run only after R1 is committed and the worktree used for the build is clean.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\full_build.py

build\Debug\test_multiway_public_catalog.exe
build\Debug\test_multiway_solver.exe
build\Debug\test_multiway_qualification_reports.exe

cmake --build build-tracy --config RelWithDebInfo --target texas_multiway_train_tracy --parallel
```

Run the following capture command once for every row in the matrix.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_multiway_tracy_training_capture.ps1 `
  -CaptureId <id> -Workers <workers> -Batches <batches> `
  -TrajectoriesPerBatch 1000 -Scheduler <scheduler> -CaptureSeconds 60
```

| ID | Workers | Batches | Scheduler | Expected fingerprint | Expected bytes |
|---|---:|---:|---|---:|---:|
| `ad7-next-cold-fixed-1w` | 1 | 1 | fixed | `9806774443936985566` | `137380835` |
| `ad7-next-cold-fixed-4w` | 4 | 1 | fixed | `9806774443936985566` | `137380835` |
| `ad7-next-cold-dynamic-1w` | 1 | 1 | dynamic | `9806774443936985566` | `137380835` |
| `ad7-next-cold-dynamic-4w` | 4 | 1 | dynamic | `9806774443936985566` | `137380835` |
| `ad7-next-steady-fixed-1w` | 1 | 4 | fixed | `3496596242701454594` | `387676624` |
| `ad7-next-steady-fixed-4w` | 4 | 4 | fixed | `3496596242701454594` | `387676624` |
| `ad7-next-steady-dynamic-1w` | 1 | 4 | dynamic | `3496596242701454594` | `387676624` |
| `ad7-next-steady-dynamic-4w` | 4 | 4 | dynamic | `3496596242701454594` | `387676624` |

Analyze every result with the matching fingerprint:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\analyze_multiway_checkpoint_telemetry.ps1 `
  -ReportPath tmp\tracy\<id>\report.json `
  -CsvPath tmp\tracy\<id>\<id>.csv `
  -ExpectedMergeFingerprint <expected-fingerprint>
```

## Acceptance measurements

For every cold report require:

- `coordinator_admission_nanoseconds < 250000000`;
- `catalog_menu_lookup_nanoseconds <= 10000000`;
- `catalog_commit_nanoseconds <= 25000000`.

For every report require:

- the exact fingerprint and checkpoint bytes in the matrix;
- `failed=false`, `discarded_trajectories=0`, and
  `capacity_exhaustion_stage=0`;
- fixed and dynamic runs with the same worker/batch count have identical
  fingerprints, checkpoint bytes, admitted-state/row counts, reuse/new-menu
  counts, equality checks, collision misses, and capacity stage;
- persistent probes remain zero for cold captures;
- `catalog_menu_equality_checks == catalog_batch_menu_reuse_hits` and
  `catalog_menu_id_collision_misses == 0` for this unchanged workload;
- no unaffected phase regresses by more than 10% in two repeated captures.

Repeat every cold result within `0.5 ms` of either lookup or commit threshold.
Do not select a validation optimization until the cold menu-lookup gate passes
and the pending lookup/equality versus construction remainder is accounted.

## Inspection performed

Read-only source, test, plan, report, manifest, CSV, and script inspection only.
No build, test, benchmark, executable, capture, analyzer, or solver-launching
script was run.
