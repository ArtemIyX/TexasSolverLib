# PB9.16.09 Qualification tooling

Status: DONE
Owner: Codex
Started: 2026-10-25
Completed: 2026-10-25
Depends on: PB9.16.02, PB9.16.04, PB9.16.05, PB9.16.06, PB9.16.07, PB9.16.08

## Scope

- Forward build configuration, binary path, scheduler chunk, placement policy, checkpoint mode, warm/cold order, and randomized case order through the worker matrix runner.
- Produce median, dispersion, provenance, phase, placement, migration, and page-fault summaries.
- Validate that one matrix uses one binary identity.

## Implementation

- Forwarded build configuration, binary path, fixed/static-block/dynamic scheduler mode, dynamic chunk size, placement policy, checkpoint mode, warmup order, and deterministic randomized case ordering through the worker matrix.
- Added explicit run-local configuration overrides and report gates for scheduler chunk, placement policy, checkpoint mode, full payload hash, and policy-content hash.
- Added warmup-aware result rows and a `worker_matrix_summary.json` manifest with one-binary provenance, protocol/order metadata, median/dispersion, phase, placement, migration, and page-fault summaries.
- Kept qualification tooling cold-path only. No benchmark or solver-training execution was added.

Implementation commit:

- `94e12d7 PB-9 Extend qualification tooling: Preserve comparable matrix evidence`

## Files

- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_worker_matrix.ps1`
- qualification-report and CMake boundary tests
- `docs/application_and_script_io.md`

## Acceptance

- The runner can execute the defined matrix without hidden defaults.
- Reports make non-comparable runs fail instead of silently entering summaries.
- Tooling does not start benchmarks as part of implementation tests.

## Validation

- PowerShell parser validation passed for both qualification runners.
- Focused `test_multiway_qualification_reports` and `test_multiway_workflow_boundary` passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed with the build-only `/FS` configuration.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed; all tests passed.
- `git diff --check`: passed.
- No C++ files changed; cpp-code-review boundary was empty.

## Limitations

- No worker matrix, benchmark, or solver training run was started.
