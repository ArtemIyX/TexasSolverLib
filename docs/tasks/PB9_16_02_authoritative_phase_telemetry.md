# PB9.16.02 Authoritative phase telemetry

Status: DONE
Depends on: PB9.16.01

Completed: 2026-10-25
Implementation commit: `6d85d5f`

## Scope

- Record distinct training, checkpoint, export, and total-wrapper durations.
- Record current working set, high-water mark, private commit, and page faults at phase boundaries.
- Correct or rename `peak_process_rss_bytes` if it is not a process-wide peak.
- Serialize and validate the new report fields.

## Implementation

- Added authoritative training, checkpoint, export, and total-wrapper wall durations.
- Added phase-boundary current working set, working-set high-water, private committed, and page-fault snapshots with explicit availability flags.
- Kept `peak_process_rss_bytes` as a compatibility field and populate it from process working-set HWM snapshots when available; expose the sampled peak separately.
- Preserved checkpoint bytes, artifact hashes, scheduler behavior, topology policy, merge order, and hot-path behavior.

## Files

- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- trainer, workflow, memory, and qualification-report tests

## Acceptance

- Training throughput excludes post-training checkpoint and export work.
- Reported peaks state their sampling scope truthfully.
- Unsupported counters are represented explicitly, not as fabricated values.

## Validation

- Focused multiway memory, qualification-report, blueprint-training, and workflow-boundary tests passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py` passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py` passed.
- `git diff --check` passed.
- C++ review: no actionable findings.

## Limitations

- Linux reports minor and major page faults; Windows reports the supported total page-fault counter. Unsupported counters remain unavailable.
- No qualification matrix, benchmark, or solver training was run.
