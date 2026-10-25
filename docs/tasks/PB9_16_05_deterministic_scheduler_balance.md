# PB9.16.05 Deterministic scheduler balance

Status: DONE  
Depends on: PB9.16.01

Owner: Codex  
Started: 2026-10-25
Completed: 2026-10-25  
Implementation commit: `90e9510 PB-9 Add deterministic scheduler balance: Version static blocks and expose dynamic chunk evidence`

## Scope

- Add versioned static-block scheduling.
- Expose dynamic chunk size through production config, CLI, identity, and report data.
- Preserve trajectory seeds and globally deterministic merge order.
- Cover fixed, static-block, and dynamic scheduling across 1/2/4/8/16 workers.

## Implementation

- Added versioned StaticBlock scheduling with deterministic contiguous worker partitions while preserving Fixed round-robin behavior and Dynamic chunk dispatch.
- Propagated scheduler mode and dynamic chunk size through workflow config, CLI overrides, training identity, checkpoints, run metadata, and JSON reports.
- Kept trajectory-derived seeds, worker-index stream merge order, exact storage, policy rows, fingerprints, and Float64 accumulation behavior unchanged across scheduler modes.
- Scheduler policy, partition, merge, and static-block versions are explicit identity and report evidence; scheduler changes remain checkpoint-incompatible.

## Files

- `include/solver/multiway/engine/multiway_scheduler.hpp`
- `src/solver/multiway/engine/multiway_scheduler.cpp`
- traversal, trainer, report, and CLI files
- `tests/test_multiway_scheduler.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_qualification_reports.cpp`

## Validation

- Focused scheduler, parallel-training, workflow-config, blueprint-training, and qualification-report tests passed.
- Fixed, static-block, and dynamic modes matched exact storage and policy rows across 1, 2, 4, 8, and 16 workers.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py` passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py` passed.
- `git diff --check` passed.
- C++ review: no actionable findings.

## Acceptance

- Partition coverage is complete, disjoint, deterministic, and fingerprinted.
- Chunk size is validated and visible in run evidence.
- Exact storage, policy rows, and merged fingerprint are preserved across supported modes.

## Risks

- Scheduling policy changes must remain checkpoint-incompatible unless compatibility is proven explicitly.

## Limitations

- No benchmarks, qualification matrix, tournament merges, policy-content hashing, or solver training were run.
