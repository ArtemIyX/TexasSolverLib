# Task E: Correct memory preflight accounting

## Trigger

`Fully implement E.`

## Objective

Replace overlapping memory-estimate categories with a non-overlapping model
that distinguishes existing resident memory, additional private commitment,
mapped virtual bytes, mapped resident allowance, and transient peaks.

This task corrects admission accuracy. It is not an actual RAM optimization.

## Dependencies

Task D must be complete so the new estimate can be compared with observed
working set and private bytes.

## Read first

- Task D evidence bundle under `tmp/memory-attribution/baseline/`;
- `include/solver/multiway/engine/multiway_memory.hpp`;
- `src/solver/multiway/engine/multiway_memory.cpp`;
- `examples/multiway_workflow_main.cpp`;
- `tests/test_multiway_memory.cpp`;
- current F1 configs under `configs/multiway/`.

## Required model

Represent these categories explicitly:

```text
existing_working_set_bytes
existing_private_committed_bytes
additional_private_capacity_bytes
mapped_virtual_bytes
mapped_resident_allowance_bytes
checkpoint_transient_bytes
export_transient_bytes
allocator_overhead_bytes
estimated_private_peak_bytes
estimated_working_set_peak_bytes
```

Rules:

1. Existing resident mapped pages must not be counted again as additional
   private commitment.
2. The full mapped artifact size may remain a conservative virtual-address or
   worst-case-residency bound, but it must be labeled separately.
3. Checkpoint and export peaks are mutually exclusive unless source evidence
   proves they overlap. Use the maximum, not the sum, by default.
4. Configured capacities remain safety inputs, but packed/current types must
   use correct element sizes.
5. Preserve warning, operating, and reject semantics.
6. Preserve deterministic admission stage and failure behavior.

## Required changes

1. Version or extend `MultiwayMemoryInputs`, `MultiwayMemoryLedger`, and
   `MultiwayMemoryEstimate` without silently changing serialized meanings.
2. Populate private and working-set baseline fields from Task A.
3. Use Task B capacity totals where they represent actual retained allocation;
   retain configured-capacity estimates for future growth separately.
4. Replace the unconditional baseline-plus-full-mapping sum.
5. Serialize both private and working-set estimates.
6. Update CLI error text to report the rejected estimate category and stage.
7. Update old report/config compatibility.

Do not lower configured capacity limits in Task E.

## Tests

Add table-driven tests for:

- zero mapping;
- cold mapping with no resident pages;
- fully resident mapping included in baseline;
- partially resident allowance;
- private capacity growth;
- checkpoint/export maximum counted once;
- allocator overhead;
- overflow saturation;
- warning, operating, and reject boundaries;
- every admission stage;
- old schema/config compatibility;
- expected F1 estimate using Task D fixture values.

Primary tests:

- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_artifact_preflight.cpp` when shared semantics change.

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\compact_build.py `
  --target test_multiway_memory `
  --target test_multiway_workflow_config `
  --target test_multiway_qualification_reports `
  --target test_multiway_artifact_preflight `
  -o tmp\memory-task-e-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_memory|test_multiway_workflow_config|test_multiway_qualification_reports|test_multiway_artifact_preflight)$"

.\scripts\codex_powershell.ps1 .\scripts\full_build.py
```

Then rerun normal Task D cases A1 and A3 and compare the estimate with the same
observed categories. Run `git diff --check`.

## Acceptance

- Estimate categories are non-overlapping and clearly named.
- Existing resident mapping is not silently added twice.
- Preflight still rejects configurations exceeding the process limit.
- Admission stage and transactional behavior remain deterministic.
- Task D A1/A3 complete with zero discards and matching fingerprints.
- The new estimate is reconciled against observed categories; differences are
  documented rather than forced to zero.
- Focused/full tests pass and review finds no weakened safety bound.
- The project log states that this is accounting correction, not RAM savings.

## Completion statement

Task E is complete when preflight semantics, tests, bounded comparison, review,
and log are complete.

## Stop conditions

Stop if the proposed model weakens the reject boundary without evidence, if
Task D lacks private-memory data, or if compatibility requires an unapproved
config/schema break.

