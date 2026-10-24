# Multiway memory task tracker

**Updated:** 2026-10-24
**Overall status:** Complete through staged qualification gates
**Current task:** None
**Next action:** Seek explicit user approval and fresh disk, time, and memory gates before any 50M run.
**Latest problem:** Task K passed with bounded-matrix limitations; Task L adds staged gates, but no 50M-trajectory run is authorized by this tracker.

## Tasks

| Task | Status | Last result or problem, one sentence | Depends on | Task file | Evidence |
| --- | --- | --- | --- | --- | --- |
| A | Complete | Added versioned platform-aware process snapshots, ordered phase capture, report serialization, compatibility, and tests without changing solver behavior. | None | [Process memory and phase snapshots](A_process_memory_phase_snapshots.md) | [Project log](../PLURIBUS_LOG.md#pb-6-implement-task-a-process-memory-and-phase-snapshots) |
| B | Complete | Added allocation-free live and retained-capacity attribution, per-worker aggregates, phase snapshots, versioned JSON, validation, and tests without changing solver capacities. | A | [Retained subsystem capacity](B_retained_subsystem_capacity.md) | [Project log](../PLURIBUS_LOG.md#pb-6-implement-task-b-retained-subsystem-capacity) |
| C | Complete | Tracy-only allocation/free events were captured and inspected in a bounded diagnostic run without changing production targets. | A | [Tracy allocation lifetimes](C_tracy_allocation_lifetimes.md) | [Project log](../PLURIBUS_LOG.md#pb-6-implement-task-c-tracy-allocation-lifetimes) |
| D | Complete | A1-A5 normal and Tracy matrix passed; analyzer selected G from the one-to-four-worker private-memory and stream-capacity slope. | A, B, C | [Bounded attribution matrix](D_bounded_attribution_matrix.md) | `tmp/memory-attribution/baseline-final/evidence.md` |
| E | Complete | Corrected preflight accounting with non-overlapping private, working-set, mapped, transient, and allocator categories; focused/full validation and A1/A3 bounded runs passed. | D | [Preflight accounting](E_preflight_accounting.md) | [Project log](../PLURIBUS_LOG.md#pb-6-task-e-correct-memory-preflight-accounting) |
| F | Blocked | Bucket validation is a suspected working-set cause but must be selected by D before implementation. | D selects F | [Bucket validation residency](F_bucket_validation_residency.md) | Not selected |
| G | Complete | Replaced shared ceilings with four explicit capacities; 15 normal samples and A3/A4 Tracy passed with matching fingerprints and approximately 95.4% lower worker slopes. | D selects G | [Worker stream right-sizing](G_worker_stream_rightsizing.md) | `tmp/memory-attribution/task-g-explicit-analysis/evidence.md` |
| H | Blocked | Compact row metadata reserves the configured maximum, but its measured contribution is not yet known. | D selects H | [Compact row metadata paging](H_compact_row_metadata_paging.md) | Not selected |
| I | Blocked | Cold admission scratch may remain oversized in steady state, but no retained-capacity evidence exists yet. | D selects I | [Admission scratch retention](I_admission_scratch_retention.md) | Not selected |
| J | Blocked | Checkpoint/export transients are not proven to be the dominant remaining peak. | D selects J | [Checkpoint/export peak](J_checkpoint_export_peak.md) | Not selected |
| K | Complete | Five-repeat A1-A5 normal and Tracy qualification passed with explicit Task G configuration, zero discards, matching fingerprints, and final comparison recorded. | E and selected F-J tasks | [Final memory qualification](K_final_memory_qualification.md) | [Final evidence](../../tmp/memory-attribution/final/evidence.md) and [project log](../PLURIBUS_LOG.md#pb-6-task-k-final-bounded-memory-qualification) |
| L | Complete | Added staged 50K, 500K, 5M, and explicitly gated 50M qualification with hard preflight/runtime working-set safety gates and focused validation. | K | [Staged large-F1 qualification](L_staged_large_f1_qualification.md) | [Project log](../PLURIBUS_LOG.md#pb-6-task-l-staged-large-f1-qualification-with-hard-ram-safety-gates) |

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

