# Task J: Reduce checkpoint and export peak memory

## Trigger

`Fully implement J.`

## Objective

Reduce transient memory during checkpoint construction or blueprint export
without changing canonical bytes, payload hashes, resume behavior, or atomic
publication.

## Evidence gate

Task D or a checkpoint-enabled extension of D must select J. Named snapshots
must show checkpoint/export as the dominant remaining peak.

If checkpoint/export was disabled in D, first add matched checkpoint-enabled
bounded evidence. Do not infer J from the preflight estimate alone.

## Required design

Choose one measured transient owner:

- canonical public-state ordering;
- compact regret/strategy export;
- payload serialization buffer;
- hash input staging;
- blueprint export duplication;
- atomic publication copy/fallback.

Preferred techniques:

- stream canonical sections in deterministic order;
- reuse one bounded serialization buffer;
- write/hash the same byte stream once;
- avoid holding logical and canonical full copies simultaneously;
- use existing page-order metadata for direct compact export.

Do not change schema-2 bytes or atomic replacement guarantees.

## Required invariants

- byte-identical checkpoint for identical solver state;
- identical payload hash and section sizes;
- continuous and split/resumed training equivalence;
- reverse-page exact-value restore remains correct;
- output and input checkpoint paths never alias unexpectedly;
- Windows atomic publication fallback remains checked and reported;
- failure leaves the previous published destination valid.

## Likely files

- checkpoint artifact header/source under
  `solver/multiway/blueprint/`;
- `multiway_compact_storage.cpp` export path;
- `multiway_solver.cpp` checkpoint/canonicalization;
- `core/atomic_publish.*` only if measured publication memory is selected;
- blueprint trainer/report telemetry;
- checkpoint analyzers and tests.

## Tests

Add coverage for:

- byte-identical old/new checkpoint output;
- section sizes and payload hash;
- empty and populated compact pages;
- reverse-page restore;
- continuous versus split/resumed result;
- write failure and destination preservation;
- separate resume input/output paths;
- bounded buffer capacity independent of checkpoint size;
- Task B transient-capacity reporting.

Primary tests:

- `tests/test_multiway_artifact.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_compact_storage.cpp`
- atomic-publication cases in `tests/test_multiway_artifact.cpp` when
  publication changes.

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\compact_build.py `
  --target test_multiway_artifact `
  --target test_multiway_blueprint_training `
  --target test_multiway_compact_storage `
  -o tmp\memory-task-j-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_artifact|test_multiway_blueprint_training|test_multiway_compact_storage)$"

.\scripts\codex_powershell.ps1 .\scripts\full_build.py
```

Run at least three matched bounded checkpoint-enabled normal runs before and
after the change, plus one Tracy capture. Run the existing bounded checkpoint
equivalence workflow used by the latest F1 log entry.

## Acceptance

- Checkpoint bytes and payload hashes are identical.
- Split/resumed output remains byte-identical to continuous output.
- Peak private bytes decrease beyond baseline noise at the selected phase.
- No extra full-size buffer appears in another phase.
- Checkpoint wall time does not materially regress.
- Atomic publication behavior remains correct.
- Full build/CTest and review pass.
- Project log records checkpoint size, hashes, peak memory, and timing.

## Completion statement

Task J is complete only after byte-equivalence and matched peak-memory evidence
pass.

## Stop conditions

Stop if output bytes change, resume diverges, publication safety weakens, the
selected phase is not dominant, or memory is merely shifted to another full
buffer.

