# Multiway memory task tracker

**Updated:** 2026-10-23
**Overall status:** In progress
**Current task:** A
**Next action:** Implement and verify process memory phase snapshots and report compatibility.
**Latest problem:** Current memory telemetry cannot separate private solver memory from file-backed working-set residency.

## Tasks

| Task | Status | Last result or problem, one sentence | Depends on | Task file | Evidence |
| --- | --- | --- | --- | --- | --- |
| A | In progress | Working set, private committed memory, and mapped residency are not reported as separate phase snapshots. | None | [Process memory and phase snapshots](A_process_memory_phase_snapshots.md) | Implementation pending |
| B | Blocked | Solver-owned live and retained container capacities cannot yet be reconciled with process private memory. | A | [Retained subsystem capacity](B_retained_subsystem_capacity.md) | Waiting for A |
| C | Blocked | Tracy memory events are disabled and Tracy reports mark allocation probes invalid. | A | [Tracy allocation lifetimes](C_tracy_allocation_lifetimes.md) | Waiting for A |
| D | Blocked | No matched normal/Tracy attribution matrix exists to select the dominant memory owner. | A, B, C | [Bounded attribution matrix](D_bounded_attribution_matrix.md) | Waiting for A-C |
| E | Blocked | Preflight can overlap resident baseline and full mapped-artifact accounting, but correction requires D evidence. | D | [Preflight accounting](E_preflight_accounting.md) | Waiting for D |
| F | Blocked | Bucket validation is a suspected working-set cause but must be selected by D before implementation. | D selects F | [Bucket validation residency](F_bucket_validation_residency.md) | Not selected |
| G | Blocked | Worker stream over-reservation is a suspected private-memory cause but must be selected by D. | D selects G | [Worker stream right-sizing](G_worker_stream_rightsizing.md) | Not selected |
| H | Blocked | Compact row metadata reserves the configured maximum, but its measured contribution is not yet known. | D selects H | [Compact row metadata paging](H_compact_row_metadata_paging.md) | Not selected |
| I | Blocked | Cold admission scratch may remain oversized in steady state, but no retained-capacity evidence exists yet. | D selects I | [Admission scratch retention](I_admission_scratch_retention.md) | Not selected |
| J | Blocked | Checkpoint/export transients are not proven to be the dominant remaining peak. | D selects J | [Checkpoint/export peak](J_checkpoint_export_peak.md) | Not selected |
| K | Blocked | Final qualification requires corrected accounting and every evidence-selected optimization. | E and selected F-J tasks | [Final memory qualification](K_final_memory_qualification.md) | Waiting for implementation |

## Status values

- `Ready`: dependencies are complete and the task may start.
- `In progress`: an agent is actively implementing or verifying the task.
- `Blocked`: a dependency, tool, artifact, failure, or decision prevents completion.
- `Complete`: every completion condition in the task file passed.
- `Failed`: attempted work produced a verified failure that needs a new decision.

## Update rules

Every agent working on this plan must update this file.

1. Before editing code, set the selected task to `In progress`, update the date,
   current task, next action, and one-sentence latest problem.
2. When blocked, set `Blocked` and replace the sentence with the exact blocker.
3. When complete, set `Complete`, summarize the measured result in one sentence,
   and link the evidence path or project-log entry.
4. Mark newly unblocked dependencies as `Ready`.
5. After D, set only evidence-selected F-J tasks to `Ready`; leave the others
   `Blocked` with `Not selected` evidence.
6. Keep every result/problem cell to one sentence. Put details in the task file,
   evidence bundle, or `docs/PLURIBUS_LOG.md`.
7. Do not mark a task complete when required tests, runtime evidence, review, or
   acceptance gates are missing.

## Reference

- [Task index](README.md)
- [Master memory plan](../multiway_memory_measurement_and_reduction_plan.md)
- [Project log](../PLURIBUS_LOG.md)

