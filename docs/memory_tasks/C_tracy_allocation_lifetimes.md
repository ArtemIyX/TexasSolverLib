# Task C: Tracy allocation lifetime capture

## Trigger

`Fully implement C.`

## Objective

Enable allocation/free lifetime events in Tracy diagnostic binaries while
leaving normal production binaries unchanged. Prove that the capture contains
memory events and does not recurse or corrupt aligned allocation paths.

## Dependencies

Task A must be complete. Task B is recommended but not required for code
implementation.

## Read first

- `docs/multiway_memory_measurement_and_reduction_plan.md`
- `docs/memory_tasks/A_process_memory_phase_snapshots.md`
- `include/util/tracy.hpp`
- `src/solver/multiway/engine/multiway_allocation_probe.cpp`
- `include/solver/multiway/engine/multiway_allocation_probe.hpp`
- `scripts/build_multiway_tracy.ps1`
- `scripts/run/run_multiway_tracy_training_capture.ps1`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/cmake/test_tracy_boundary.cmake`
- Tracy `0.14.1` memory macros under `external/tracy/public/tracy/Tracy.hpp`

## Required design

1. Enable Tracy memory events only on Tracy diagnostic targets.
2. Add wrapper macros or inline functions for allocation and free events.
3. Instrument scalar, array, aligned, and nothrow allocation paths exactly
   once per successful pointer.
4. Use the no-callstack form initially because the Tracy build currently has
   callstacks disabled.
5. Add a thread-local reentrancy guard if Tracy can allocate through the
   intercepted global allocator.
6. Never send a free event for null.
7. Keep current scoped allocation counters separate. Tracy memory support does
   not make `allocation_probe_valid` true.
8. Add manifest/report provenance such as `tracy_memory_events_enabled`.

Do not enable Tracy memory for the `texas` production target.

## Required changes

- Change `scripts/build_multiway_tracy.ps1` to configure memory support on.
- Extend `include/util/tracy.hpp` with disabled-build no-op wrappers.
- Instrument `multiway_allocation_probe.cpp` safely.
- Update report/manifest provenance and validation.
- Update the Tracy boundary test so target isolation remains enforced.
- Extend the capture wrapper to fail when memory events were requested but the
  resulting capture lacks them. Use an existing Tracy export capability when
  available; otherwise add a small deterministic capture-inspection step.

## Tests

Add coverage for:

- normal build macros compile to no-ops;
- Tracy target receives the required definitions;
- production target does not receive them;
- scalar/array/aligned/nothrow allocations execute successfully;
- null nothrow result does not emit an invalid event;
- delete paths match allocation paths;
- reentrancy guard does not suppress ordinary user allocations;
- report provenance rejects contradictory states;
- capture tooling rejects an empty or missing memory event stream.

Do not assert exact allocator byte totals across CRT versions.

## Execute

Focused normal build and tests:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\compact_build.py `
  --target test_multiway_qualification_reports `
  -o tmp\memory-task-c-normal-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_qualification_reports|test_tracy_boundary)$"
```

Tracy build:

```powershell
.\scripts\build_multiway_tracy.ps1 -Configure `
  -Configuration RelWithDebInfo -Parallel 4
```

Bounded capture:

```powershell
.\scripts\run\run_multiway_tracy_training_capture.ps1 `
  -Workers 1 -Batches 1 -TrajectoriesPerBatch 1000 `
  -Scheduler fixed `
  -CaptureId memory-task-c-smoke `
  -OutputDirectory tmp\memory-task-c-smoke
```

Then:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
git diff --check
```

## Acceptance

- Tracy build and capture complete.
- The `.tracy` artifact exists and contains allocation/free events.
- No connection failure, recursion, crash, or allocator mismatch occurs.
- Normal builds remain unaffected.
- Tracy-only overhead is not presented as production throughput.
- Focused tests and full build/CTest pass.
- The project log records capture ID, workload, and actual result.

## Completion statement

Task C is complete only after a real bounded Tracy capture proves memory events
are present. Building with memory support is insufficient.

## Stop conditions

Stop if the capture tool cannot connect, if profiler recursion occurs, if
allocation/free pairing is uncertain, or if the Tracy target change leaks into
the production library.

