# PB9.16.04 Safe placement policies

Status: DONE  
Depends on: PB9.16.03

Owner: Codex
Started: 2026-10-25
Completed: 2026-10-25
Implementation commit: `c7f7a15 PB-9 Add safe placement policies: Use topology-backed opt-in worker placement`

## Scope

- Fix coordinator reservation by physical core.
- Add opt-in OS-default, physical-compact, physical-spread, CPU-set-hint, and diagnostic hard-affinity policies.
- Report requested and resolved CPU-set, core, and cache-domain identities.
- Add optional amortized migration sampling.

## Implementation

- Added topology-backed OS-default, physical-compact, physical-spread, CPU-set-hint, and diagnostic hard-affinity policies.
- Reserved the coordinator's entire known physical core before physical-only selection and selected unique physical cores before SMT siblings.
- Applied Windows CPU Sets for desktop policies and retained hard affinity for diagnostic policy only.
- Added fallback selection when topology or placement APIs are unavailable, plus batch-boundary migration sampling.
- Reported requested and resolved CPU-set, physical-core, SMT-sibling, and cache-domain identities without changing scheduler, merge, checkpoint, or artifact identity behavior.

## Files

- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- blueprint config/trainer and workflow config/report files
- `examples/multiway_workflow_main.cpp`
- parallel-training and workflow-config tests

## Acceptance

- OS-default remains the default and does not set affinity.
- Physical-only policies use unique cores before any SMT sibling.
- A 16-worker placement never substitutes an SMT sibling solely because the coordinator consumed one primary logical processor.

## Validation

- Focused `test_multiway_parallel_training`, `test_multiway_workflow_config`, and `test_multiway_blueprint_training`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

## Risks

- CPU Sets are preferred for desktop policy. Hard affinity remains diagnostic only.

## Limitations

- Placement performance, qualification matrices, benchmarks, and solver training were not run.
- CPU-set and topology resolution remain dependent on operating-system APIs; fallback reports unavailable physical identities.
- Scheduler balance, tournament merges, policy-content hashing, and training qualification remain deferred to later PB9.16 tasks.
