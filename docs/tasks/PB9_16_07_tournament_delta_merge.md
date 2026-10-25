# PB9.16.07 Tournament delta merge

Status: DONE
Owner: Codex
Started: 2026-10-25
Completed: 2026-10-25
Implementation commit: `fac02ea PB-9 Add tournament delta merge: Preserve exact worker accumulation order`
Depends on: PB9.16.05

## Scope

- Optimize `MultiwaySolverCoordinator::merge_worker_streams_locked()` with a deterministic tournament path above a measured threshold.
- Keep a scalar linear reference path.
- Differential-test randomized bounded streams and duplicate rejection.

## Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_solver.cpp`

## Acceptance

- Ordering by infoset, action, bucket, trajectory ID, and value bits is unchanged.
- Worker-index tie-breaking, duplicate rejection, Float64 accumulation order, storage, policies, and fingerprint are bitwise identical.
- The optimized path adds no allocation in merge hot loops.

## Implementation

- Added explicit reference, tournament, and automatic delta merge paths.
- Selected tournament merging only above the four-stream threshold.
- Preserved full delta ordering, worker-index ties, duplicate diagnostics, Float64 accumulation, compact storage, policies, and fingerprints.
- Preallocated tournament, cursor, view, and pending-cell merge scratch.

## Validation

- Focused `test_multiway_solver`, `test_multiway_parallel_training`, and `test_multiway_traversal_boundary`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed with the build-only `/FS` configuration; build and tests reported OK.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed; all tests passed.
- `git diff --check`: passed.
- Independent C++ review: no actionable findings.

## Limitations

- No policy-content hashing, qualification matrices, benchmarks, or solver training were run.
