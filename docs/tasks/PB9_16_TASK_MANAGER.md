# PB-9.16 worker scaling task manager

Source: `docs/tasks/pb9_16_worker_scaling_analysis_and_implementation_plan.md`
Status: PB9.16.06 complete; remaining PB9.16 tasks active planning
Last updated: 2026-10-25

## Goal

Make 16-worker multiway training measurable, provenance-safe, deterministic, and no more than 2% slower than 8 workers. Target a verified 16-worker improvement of at least 5% over 8 workers without reducing 8-worker throughput by more than 3%.

## State legend

`TODO` not started. `IN PROGRESS` has an owner. `BLOCKED` needs an external decision or prerequisite. `DONE` merged and validated.

## Task board

| ID | Task | State | Depends on | Blocker / exit condition |
|---|---|---|---|---|
| PB9.16.01 | [Compile-time provenance and runner gate](PB9_16_01_provenance_runner_gate.md) | DONE | None | Commit `66f6690eded19dc4002a9e0d1d122f75b8fc9be4`; runner rejects binary/source mismatch. |
| PB9.16.02 | [Authoritative phase telemetry](PB9_16_02_authoritative_phase_telemetry.md) | DONE | PB9.16.01 | Commit `6d85d5f`; phase timings and process snapshots serialize and validate. |
| PB9.16.03 | [Topology inventory](PB9_16_03_topology_inventory.md) | DONE | PB9.16.01 | Commit `1014b4d`; synthetic topology tests pass. |
| PB9.16.04 | [Safe placement policies](PB9_16_04_safe_placement_policies.md) | DONE | PB9.16.03 | Commit `c7f7a15`; OS-default stays unchanged; physical-core selection is unique. |
| PB9.16.05 | [Deterministic scheduler balance](PB9_16_05_deterministic_scheduler_balance.md) | DONE | PB9.16.01 | Commit `90e9510`; exactness holds for 1/2/4/8/16 workers. |
| PB9.16.06 | [Tournament discovery merge](PB9_16_06_tournament_discovery_merge.md) | DONE | PB9.16.05 | Commit `ca8cc80`; reference and tournament outputs match with deterministic duplicate diagnostics. |
| PB9.16.07 | [Tournament delta merge](PB9_16_07_tournament_delta_merge.md) | TODO | PB9.16.05 | Accumulation order and fingerprint are identical. |
| PB9.16.08 | [Policy-content hash](PB9_16_08_policy_content_hash.md) | TODO | PB9.16.01 | Full payload compatibility is preserved. |
| PB9.16.09 | [Qualification tooling](PB9_16_09_qualification_tooling.md) | TODO | PB9.16.02, PB9.16.04, PB9.16.05, PB9.16.06, PB9.16.07, PB9.16.08 | Matrix emits provenance, dispersion, phase, and placement evidence. |
| PB9.16.10 | [Authorized qualification experiments](PB9_16_10_authorized_qualification_experiments.md) | BLOCKED | PB9.16.09 | Explicit authorization is required before benchmarks or training runs. |

## Current blockers and risks

| Item | State | Required action |
|---|---|---|
| Fresh source qualification | Blocked | Complete provenance gate, rebuild, then authorize experiments. |
| Current PB-8 checkpoint memory evidence | Unverified | Do not treat stale-binary telemetry as current-source evidence. |
| 16-worker regression cause | Hypothesis validated by stale-binary evidence | Do not select a scheduler or placement policy before repeated qualified measurements. |
| Checkpoint compatibility | Constraint | Scheduler and identity changes must retain explicit incompatibility checks. |

## Invariants for every implementation task

- Preserve deterministic seeds, worker-index tie order, duplicate rejection, and exact Float64 accumulation order.
- Keep default scheduling and OS placement behavior unchanged unless an opt-in policy is selected.
- Keep hot paths allocation-free and free of strings, logging, virtual dispatch, and hash lookups.
- Preserve existing full payload hashing and checkpoint compatibility rules.
- Add focused tests before claiming completion. Do not run benchmarks or training without explicit authorization.

## Update procedure

When a task starts, set its state to `IN PROGRESS`, record owner and date in its file, and note a concrete blocker if one exists. When it completes, set `DONE` only after its listed validation passes, add the completed commit, and update this board and `docs/PLURIBUS_LOG.md`.

## Ready prompt

- [PB9.16.01 implementation prompt](PB9_16_01_AGENT_PROMPT.md)
