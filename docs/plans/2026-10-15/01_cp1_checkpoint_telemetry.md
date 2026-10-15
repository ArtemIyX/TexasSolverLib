Task: Split checkpoint timing into measurable phases; source changes can be prepared by Codex, but an operator must run the Tracy training capture and provide the timing results.

# CP1 - Checkpoint phase telemetry

## Goal

Explain the current 198-240 second checkpoint duration by measuring each major
operation independently.

## Scope

Add Tracy zones and report fields for:

- snapshot lock wait;
- public catalog export;
- compact storage export;
- canonicalization;
- validation;
- payload hashing;
- binary encoding and file write;
- close or flush;
- atomic publication.

Record bytes for public states, row shapes, regrets, strategy sums, and the
late-window baseline. The measured sub-phases should account for at least 95%
of the existing checkpoint duration.

## Likely files

- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`

## Agent work

- Trace the existing checkpoint call graph.
- Add phase timing and byte fields without changing checkpoint behavior.
- Preserve schema and deterministic identity.
- Add source-level validation for field propagation if applicable.

## Operator work

- Build the diagnostic target.
- Run the Tracy capture on the same bounded workload.
- Provide JSON, CSV, manifest, and checkpoint timing results.
- Run `scripts/analyze_multiway_checkpoint_telemetry.ps1` against the new
  `report.json`, passing the baseline merge fingerprint. The analyzer checks
  schema 7, all phase fields, the 95% timing gate, zero discarded trajectories,
  and the unchanged merge fingerprint.

## Acceptance

- All listed phases have timings.
- Byte counts reconcile with the checkpoint payload.
- At least 95% of the old duration is attributed.
- Zero discarded trajectories and unchanged merge fingerprint.

## Dependency

None. This task must precede checkpoint optimization.
