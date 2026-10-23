Task: Reduce cold prepared-batch admission below 250 ms by batching catalog and sparse-row storage operations; Codex can implement source and tests, but an operator must run the build, tests, and Tracy captures.

# AD7 - Admission hot-path optimization

## Goal

Reduce `coordinator_admission_nanoseconds` for the captured cold batch from
the measured 1.338-1.487 seconds to below the AD5-AD6 target of 250 ms while
preserving strict validation, capacity safety, deterministic output, and
fixed/dynamic worker equivalence.

This is a focused data-structure and admission-path optimization. It does not
require changing traversal semantics or the scheduler.

## Evidence

The completed RelWithDebInfo Tracy captures on commit `89f0986` show:

- 69,194 unique public states and 31,070 unique sparse rows in the cold batch.
- 203,062 descriptor reconstructions and 607,327 parent-resolution probes.
- About 151-156 ms in public-catalog index insertion.
- About 163-169 ms in sparse-row admission.
- Identical fingerprints, checkpoint sizes, admitted counts, and zero
  discarded trajectories for fixed and dynamic scheduling.

The current implementation already merges worker discovery streams, but
`MultiwayPublicCatalog::admit`, `MultiwaySparseRowStorage::admit_row`, and
compact-storage admission still perform per-item sorted insertion and value
growth. `admit_prepared_batch` also repeats candidate lookup and descriptor
reconstruction work.

## Work breakdown

### AD7.1 - Add admission phase telemetry

- Split the admission timer into merge, parent-order, state preflight, row
  preflight, catalog commit, and storage commit zones.
- Report candidate lookup and reconstruction counts separately for existing
  epoch entries and new entries.
- Keep diagnostics outside the solver hot loops and preserve the existing
  report schema compatibility unless a schema bump is necessary.

### AD7.2 - Batch public-catalog commit

- Add an internal validated-batch commit operation to
  `MultiwayPublicCatalog`.
- Validate and reserve capacity before mutation.
- Pack new nodes contiguously and merge the old and new sorted index ranges
  once instead of inserting one index entry per state.
- Preserve menu deduplication, node-index correctness, state-ID conflict
  behavior, and strong failure semantics.
- Keep single-state admission for checkpoint restore and direct API callers.

### AD7.3 - Batch sparse-row commit

- Add internal sorted-batch commit operations for float storage and compact
  storage.
- Reserve metadata and value/page capacity once.
- Append new value ranges contiguously and merge sorted metadata/pages once.
- Preserve stable offsets, row-shape conflict checks, compact page width,
  capacity limits, and rollback-before-mutation behavior.

### AD7.4 - Reuse prepared candidate relationships

- Build parent candidate indexes once and reuse them in parent ordering and
  admission.
- Reuse the public-state candidate index for row validation instead of doing a
  binary search for every row.
- Avoid reconstructing the same admitted descriptor more than once per batch.
- Prefer sorted arrays and merge cursors over a new hash-map lookup in the
  admission loop.

### AD7.5 - Regression coverage

Add focused tests for:

- Sorted catalog and row output after a batch commit.
- Stable offsets and compact page metadata after a batch commit.
- Duplicate and conflicting IDs/shapes.
- Capacity rejection with no partial mutation.
- Existing-epoch filtering.
- Checkpoint byte and deterministic merge equivalence with the current path.
- Fixed/dynamic and one-worker/four-worker equivalence.

### AD7.6 - Operator qualification

The operator should run the focused tests and repeat the RelWithDebInfo Tracy
captures for cold and steady-state fixed/dynamic runs using Tracy 0.14.1.

## Implementation order

1. Add phase telemetry and capture a new baseline.
2. Implement public-catalog batch commit.
3. Implement float and compact sparse-storage batch commit.
4. Reuse candidate indexes and descriptor views.
5. Run focused tests and review the staged diff.
6. Run the operator Tracy matrix and compare correctness and timing.

Keep each stage in a small commit so regressions can be isolated.

## Acceptance

- Cold `coordinator_admission_nanoseconds` is below 250 ms.
- No partial mutation occurs after validation or capacity rejection.
- Fixed and dynamic runs produce identical deterministic merge fingerprints,
  checkpoint bytes, admitted sets, and policy results.
- Steady-state existing-state work remains filtered and produces zero
  discarded trajectories.
- No capacity exhaustion occurs in the qualification matrix.
- The optimization does not add allocations, locks, strings, or logging to
  traversal and merge inner loops.

## Dependencies

- AD2-AD4 discovery streams and AD5-AD6 prepared admission are complete.
- Operator build, test, and executable execution remain required for final
  qualification.
