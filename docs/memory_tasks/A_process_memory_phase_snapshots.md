# Task A: Process memory and phase snapshots

## Trigger

`Fully implement A.`

## Objective

Add a versioned, platform-aware process memory snapshot and record named phase
snapshots from bucket loading through training and checkpoint/export. This is
telemetry only. It must not change solver capacities, allocation policy,
artifact validation, checkpoint bytes, or training results.

## Dependencies

None.

## Read first

- `docs/multiway_memory_measurement_and_reduction_plan.md`
- `docs/memory_tasks/README.md`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`

## Required design

Add a small value type such as `MultiwayProcessMemorySnapshot`. Exact naming
may follow nearby conventions, but it must contain:

```text
phase_id
current_working_set_bytes
peak_working_set_bytes
private_committed_bytes
peak_private_committed_bytes, when available
virtual_address_bytes, when available
anonymous_resident_bytes, when available
file_resident_bytes, when available
availability flags
```

Use a compact enum for phase identity. Serialize stable string names only at
the report boundary.

Required phases:

1. `process_start`
2. `before_bucket_map`
3. `after_bucket_map_metadata`
4. `after_bucket_assignment_validation`
5. `before_memory_preflight`
6. `after_coordinator_construction`
7. `after_worker_pool_construction`
8. `after_cold_batch`
9. `after_steady_batch`
10. `before_checkpoint`
11. `after_checkpoint`
12. `before_export`
13. `after_export`

If current control flow cannot observe one boundary exactly, add the smallest
callback or explicit recorder needed. Do not move ownership across subsystems
only to obtain telemetry.

## Platform behavior

### Windows

Use `K32GetProcessMemoryInfo` and `PROCESS_MEMORY_COUNTERS_EX`:

- `WorkingSetSize` for current working set;
- `PeakWorkingSetSize` for peak working set;
- `PrivateUsage` for private committed bytes.

Do not label `PagefileUsage` as resident memory.

### Linux

Use `/proc/self/status` or `/proc/self/smaps_rollup` with bounded parsing.
Report availability explicitly. At minimum retain current RSS and high-water
RSS. Add anonymous/file resident splits when reliably available.

### Unsupported platforms

Return a snapshot with availability flags false. Do not invent zero as a valid
measurement.

## Required changes

1. Add the snapshot type and phase enum to the memory API.
2. Replace ambiguous one-value process sampling with the new snapshot while
   preserving existing report fields for compatibility.
3. Add a bounded snapshot collection to training status/report data.
4. Capture startup boundaries in `multiway_workflow_main.cpp`.
5. Capture construction and batch boundaries at their actual owners.
6. Capture checkpoint/export boundaries without adding work to inner loops.
7. Add report JSON serialization, validation, and backward compatibility.
8. Add Tracy plots for private bytes and current working set only at existing
   coarse telemetry boundaries.

Do not allocate or format in traversal, row update, terminal, or merge loops.

## Tests

Add or update tests that prove:

- phase names round-trip and remain ordered;
- duplicate or out-of-order required phases are rejected when a completed run
  claims them;
- supported process fields are nonzero in an integration-style sample;
- unavailable fields remain explicitly unavailable;
- report JSON contains the new memory schema and snapshots;
- old reports without snapshots remain readable;
- existing RSS fields retain their previous meaning;
- creating a snapshot is `noexcept` and does not mutate solver state.

Primary test files:

- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_blueprint_training.cpp` if trainer phase ownership needs
  coverage.

## Execute

Focused build:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\compact_build.py `
  --target test_multiway_memory `
  --target test_multiway_qualification_reports `
  --target test_multiway_blueprint_training `
  -o tmp\memory-task-a-build.log
```

Focused CTest:

```powershell
ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_memory|test_multiway_qualification_reports|test_multiway_blueprint_training)$"
```

Then run:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
git diff --check
```

No solver workload or Tracy capture is required for A.

## Acceptance

- Windows reports working set and private committed bytes separately.
- Required phase snapshots are represented in stable schema order.
- Existing report consumers and old report fixtures remain valid.
- No solver behavior, capacity, or artifact behavior changes.
- Focused tests and full build/CTest pass.
- C++ review finds no allocation or logging added to hot paths.
- The project log records Task A and its actual validation.

## Completion statement

Task A is complete when the new process/phase telemetry is implemented,
serialized, tested, fully built, and logged. Runtime memory conclusions are
out of scope and must not be claimed.

## Stop conditions

Stop and report blocked if:

- private committed bytes cannot be obtained on the Windows qualification
  host;
- the phase model requires changing training semantics;
- report compatibility cannot be retained without an explicit schema decision;
- required tests or the full build fail after three scoped repair attempts.

