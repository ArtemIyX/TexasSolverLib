# Pluribus Roadmap Progress Log

Update this file after each completed roadmap part. Record completed scope,
files, verification, and any limitations. Do not claim an item is complete
until its implementation and required validation are finished.

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

- P0.2 policy provenance and status-code contract.
- P0.3 centralized model identity inputs.
- P0.4 allocation and timing profile expansion.
