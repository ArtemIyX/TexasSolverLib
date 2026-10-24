# Task D: Workflow integration and publication

## Objective

Switch the production training workflow to the streamed writer and apply Task C's exact gates immediately before temporary-file creation.

## Dependencies

Tasks A, B, and C.

## Scope

- Replace materialize-then-save in `examples/multiway_workflow_main.cpp` with `save_full_policy_atomic()`.
- Acquire the export lease, build the exact plan, run process/system gates, then create the temporary output only after admission.
- Report exact plan/result bytes, hash, duration, gate telemetry, and publication mode.
- Update `tests/cmake/test_multiway_workflow_boundary.cmake` to prove the workflow invokes the streaming path and preserves phase/report output.

## Required invariants

- Do not open or truncate a temporary file before plan validation and gates pass.
- Retain the previous destination on every rejected or failed export.
- Keep default workflow behavior, loader behavior, task capacities, and training results unchanged except for the bounded export ownership model.

## Validation

Run the focused workflow/artifact/training/memory/report targets and boundary test, then full build, compact CTest, `git diff --check`, and C++ review.

## Acceptance

- The production workflow has no `export_full_policy()` materialization path.
- Rejected gates leave no temp output and report the exact rejection category.
- The workflow test proves streamed export integration and report compatibility.

## Stop conditions

Stop if workflow publication can use a copy fallback, if gates execute after temp creation, or if the workflow retains an unbounded export owner.
