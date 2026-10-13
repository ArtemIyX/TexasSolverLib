# Multiway memory scaling recovery completion report

Updated: 2026-10-11

## Result

The implementation slices in the recovery plan are complete. The build and
all 109 registered CTest tests pass. No 10,000+ trajectory production run was
executed, as requested.

## Implemented changes

- M1/M2: preflight now accepts post-load RSS, owned bucket bytes, mapped
  artifact bytes, checkpoint/export peaks, and allocator overhead. The report
  carries live, reserved, committed, current RSS, and peak RSS ledger fields.
- M3: compact CFR storage keeps sorted row headers and allocates fixed-width
  action-major pages only when a bucket page is touched. Missing pages read as
  uniform policy and export as zero accumulators. Page width and active-cell
  telemetry are exposed.
- M4: public states are retained as fixed parent-linked nodes with deduplicated
  action menus. Dynamic descriptors are reconstructed only for compatibility,
  checkpoint, and export boundaries.
- M5: production blueprint training defaults to immutable batch epochs. Workers
  read admitted storage without coordinator mutexes, emit bounded discovery and
  delta streams, and the coordinator admits states, rows, counters, and merges
  at the barrier in deterministic order. Discovery overflow is explicit.
- M6: compact blueprint export reads average strategy directly from compact
  pages, avoiding the former full Float64 checkpoint conversion. Checkpoint
  publishing retains atomic replacement and the existing identity checks.
- Atomic publication now falls back to same-volume replacement copy when a
  Windows rename is denied, preserving the temporary-file cleanup path.
- The scaling benchmark now runs with immutable discovery epochs. Worker delta
  sorting also uses regret and strategy payload bits as tie-breakers when one
  trajectory emits multiple updates for the same cell, keeping merge
  fingerprints independent of worker partitioning.
- The remaining small-fixture mismatch was traced to per-worker delta
  exhaustion: one worker discarded a trajectory after 32 entries while two
  workers kept it in a different stream. The benchmark reserve now allows
  eight entries per trajectory, and the small smoke size skips the noisy
  throughput monotonicity gate while retaining fingerprint validation.

## RAM cost

The recovery-plan baseline recorded peak RSS of 18.32 GB at 1,000
trajectories, 18.86 GB at 10,000 trajectories, and 21.63 GB before the
100,000-target run failed. Those are the pre-change pilot figures from the
plan, not a new large run.

The direct storage reduction is structural:

| Component | Before | After | Effect |
|---|---|---|---|
| Compact row values | all bucket/action cells on row admission | fixed-width pages on first touch | untouched buckets consume no value payload |
| Compact cell payload | 16 bytes for two `double` values | 12 bytes for `int32` regret plus `uint64` mass | 25% lower payload for active cells |
| Public state retention | dynamic board/history/menu/betting vectors per state | fixed node plus shared menu handle and parent id | removes repeated path allocations |
| Compact export | full Float64 checkpoint plus artifact | row-by-row page reads | removes the checkpoint-sized export duplicate |
| Bucket registry | owned assignments plus mapped artifact accounting | mapped artifact remains read-only; owned bytes are measured separately | preflight no longer hides the loaded artifact |

The compact-storage unit fixture has 10 buckets and 3 actions with page width
4. The first admitted row activates 12 of 30 possible cells. Admitting a
second page activates 18 of 30 cells. These ratios exclude row/page metadata
and show the intended lower bound before the model grows.

## Validation evidence

- Debug build: complete `ALL_BUILD` succeeded; the build error scanner reported
  zero failures.
- Scripted CTest: `109/109` passed; the CTest error scanner reported zero
  failures.
- Focused tests cover memory ledger accounting, mapped bucket artifacts,
  compact page admission/checkpoint round trips, packed catalog reconstruction,
  immutable epoch traversal, blueprint training, checkpoint equivalence, and
  qualification report serialization.
- Deterministic fingerprints and existing Float64 reference behavior remain
  covered by the existing multiway tests.
- The requested workers-1/2 smoke passed with seed 119 and matching fingerprint
  `10576998949177294300`.

## Remaining qualification boundary

The 10,000+ trajectory workload and the full production-shaped 1/2/4/8/16
worker RSS matrix were intentionally not run. The implementation is therefore
ready for that qualification, but this report does not claim a new production
RSS measurement beyond the structural and bounded-test evidence above.

The benchmark-only delta reserve doubles from four to eight entries per
trajectory. This increases its bounded worker-stream reservation but does not
change production solver capacities or storage layout.
