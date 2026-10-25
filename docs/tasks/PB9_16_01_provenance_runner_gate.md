# PB9.16.01 Compile-time provenance and runner gate

Status: DONE
Depends on: None

Completed: 2026-10-25
Implementation commit: `66f6690eded19dc4002a9e0d1d122f75b8fc9be4`

## Scope

- Embed source commit, dirty state, compiler, build configuration, and schema versions at compile time.
- Rename the runtime checkout field so it cannot be mistaken for binary provenance.
- Make runners verify embedded provenance and executable SHA-256 before a matrix run.
- Add workflow and report boundary tests.

## Files

- `CMakeLists.txt`
- `examples/multiway_workflow_main.cpp`
- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_worker_matrix.ps1`
- workflow/report tests

## Acceptance

- A stale executable cannot report the current checkout as its build identity.
- A matrix fails if executable SHA or embedded commit changes.
- Existing report consumers retain an explicit runtime-checkout field where needed.

## Risks

- Do not rely on launch-time environment variables for compile-time provenance.

## Validation

- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed after CMake reconfiguration.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- Focused `test_multiway_evidence` and `test_multiway_workflow_boundary`: passed.
- PowerShell parser checks for both changed runners: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

## Limitations

- No qualification matrix, benchmark, or solver training was run. Those remain explicitly gated by user authorization.
