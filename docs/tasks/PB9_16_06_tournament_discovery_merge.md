# PB9.16.06 Tournament discovery merge

Status: DONE  
Owner: Codex  
Started: 2026-10-25  
Completed: 2026-10-25  
Implementation commit: `ca8cc80 PB-9 Add tournament discovery merge: Preserve deterministic public-state and row streams`
Depends on: PB9.16.05

## Scope

- Add preallocated reference and tournament merge paths for public-state and row streams.
- Select the optimized path only above a measured stream-count threshold.
- Preserve worker-index tie order and discovery diagnostics.

## Implementation

- Added caller-owned reference and heap-tournament scratch for public-state and row discovery streams.
- Selected tournament merging only above the four-stream threshold.
- Preserved worker-index ties, duplicate collapsing, descriptor selection, diagnostics, and output capacity reuse.
- Reserved production discovery outputs and merge scratch for immutable discovery epochs.

## Files

- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_solver.cpp`

## Acceptance

- Reference and optimized paths produce identical ordered state and row streams.
- Duplicate-heavy cases retain counts and deterministic ordering.
- Merge scratch is caller-owned and preallocated.

## Validation

- Focused `test_multiway_solver` and `test_multiway_parallel_training`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed, 111/111 tests.
- `git diff --check`: passed.
- Independent C++ review: no actionable findings.
- The requested `scripts/codex_powershell.ps1 python scripts/python/full_build.py` was attempted; MSVC reported PDB contention (`C1041`) across unrelated translation units, with no source diagnostic.

## Limitations

- No tournament delta merge, benchmarks, policy-content hashing, qualification matrices, or solver training were run.
