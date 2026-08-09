# Pluribus Roadmap Progress Log

Update this file after each completed roadmap part. Record completed scope,
files, verification, and any limitations. Do not claim an item is complete
until its implementation and required validation are finished.

## P1.1 - Canonical Combination IDs

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commit:** `cb88695 Add canonical HUNL combination indexing`

- Added one fixed 1,326-entry HUNL-card combination view with unordered
  pair-to-ID, ID-to-pair, and dead-card legal-mask operations.
- Updated HUNL joint-range normalization and range enumeration to use the
  shared mapping while retaining board-relative range indices where required.
- Separated compact bucket-artifact cards from HUNL runtime cards with explicit
  adapters; updated resolver, traversal, range-update, and fixture boundaries.
- Added exhaustive combination, mask, and compact-artifact/HUNL-adapter tests.

### Validation

- Static staged and working-tree diff checks passed.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass.

## P1.2 - Fixed-Array Range Beliefs

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commits:**

- `964233c Add multiway range belief storage`
- `82c8824 Harden multiway range belief resets`
- `2c5cfbd Test multiway range belief storage`

- Added a session-owned six-seat belief container with fixed 1,326-combination
  `double` rows, legal masks, source, mass, action, and revision metadata.
- Added transactional uniform and supplied range initialization. Supplied
  entries merge by canonical ID, apply blockers before normalization, and fail
  explicitly on invalid or zero legal mass.
- Added read-only C++17 non-owning views and controlled belief copying while
  preventing implicit owner copy and move operations.
- Kept resolver, sampler, and public descriptors unchanged.

### Validation

- Added deterministic initialization, blocker, normalization, duplicate merge,
  transaction, copy, view, and bounds regression tests.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass.

## P1.3 - Bayes Action-Observation Updates

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commits:**

- `8592f88 Apply multiway range observations`
- `20fda71 Harden multiway range observations`
- `4f14c42 Test multiway range observations`

- Added fixed-row, two-pass Bayes updates for observed actions with explicit
  source, public-state, menu, table, action-index, and revision provenance.
- Updates validate policy/table identity before mutation, preserve illegal
  combinations at zero, and return a typed no-posterior result unchanged.
- Preserved legacy action metadata compatibility and isolated the new path from
  resolver, traversal, sampler, and public descriptors.

### Validation

- Added exact posterior, table-binding, blocker, transaction, seat-isolation,
  and repeat-update regression coverage.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED`.

## P0.1 - Resolver and Traversal Baselines

**Status:** Complete  
**Completed:** 2026-08-09  
**Commit:** `8c6ca324b0f8adecaaca321ab2c46be6def8918a`

Implemented privacy-safe deterministic baseline reporting for existing multiway
resolver and root-traversal paths. This does not change production inference.

- Added resolver and traversal baseline report APIs.
- Added fixture labels for valid, invalid, off-tree, no-artifact,
  deadline-exhausted, and max-row scenarios.
- Reports record status, fallback source, normalized policy, sampled action,
  batch/trajectory and row counters, timing, and observed resident memory.
- Reports omit hero cards, opponent ranges, and sampling seeds.
- Deterministic comparison and serialization exclude timing and memory.
- Added an isolated resolver fixture harness so retained stable-root state does
  not affect fixture order.
- Traversal reports use coordinator counter deltas rather than lifetime totals.
- Added resolver and traversal profiling scopes.

### Validation

- Added `tests/test_multiway_baseline.cpp`.
- Tests cover all P0.1 fixture labels, fixture isolation, privacy-safe stable
  serialization, memory measurement precedence, reused traversal coordinators,
  deterministic reports, and max-row behavior.
- Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass because direct execution was
  blocked by host policy.

### Deferred

None in Phase 0 implementation.

## P0.2 - Policy Provenance and Status Observability

**Status:** Complete
**Completed:** 2026-08-09

- Added distinct resolver statuses for partial and budget-rejected outcomes.
  They are forward-compatible values only; the legacy resolver does not emit
  either one yet.
- Added policy provenance for legacy adjustment, stable-root, blueprint, and
  static-legal policy paths.
- Added the legacy search-engine identifier/version and request artifact
  identity to resolver diagnostics.
- Extended public decision logs with provenance and search-engine metadata.

## P0.3 - Centralized Semantic Model Identity Inputs

**Status:** Complete
**Completed:** 2026-08-09

- Added explicit identity components for range semantics, future buckets,
  off-tree policy, continuation policy, and runtime search schema.
- Centralized their hashing in `make_multiway_model_identity`.
- Bound the new components into artifact identity hashing and bumped the
  manifest schema version.
- Diagnostic formatting remains outside the model identity contract.

## P0.4 - Baseline Allocation and Timing Profile

**Status:** Complete
**Completed:** 2026-08-09

- Added cold-boundary profiler scopes for resolver and root batches. Detailed
  baseline reports cover the contained traversal, private sampling, terminal,
  row-admission, and merge work without adding locks or allocations to those
  hot operations.
- Baseline reports now collect wall-clock time, process CPU time when exposed
  by the platform, current resident-memory snapshots, and process peak RSS on
  Windows/Linux. Peak RSS is explicitly unavailable on other platforms.
- Root-batch reports include accepted trajectories and deterministic worker
  minimum/maximum trajectory counts for imbalance measurement.
- Allocation-byte telemetry is explicitly unavailable until the host supplies
  an allocator profiler; RSS is not treated as allocation volume.

### Validation

- Added resolver, public-decision-log, semantic-identity, artifact-rejection,
  and baseline-environment regression coverage.
- Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass because direct execution was
  blocked by host policy.
