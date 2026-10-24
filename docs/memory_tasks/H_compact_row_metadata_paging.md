# Task H: Page compact row metadata

## Trigger

`Fully implement H.`

## Objective

Remove the eager `max_sparse_rows` metadata reserve from compact storage while
retaining deterministic ordering, bounded growth, transactional admission, and
allocation-free value updates.

## Evidence gate

Task D must select H. Compact row metadata retained capacity must be a material
private-memory owner at coordinator construction.

If D does not select H, do not implement this task.

## Required design

Use deterministic chunked growth or a paged metadata container. The design
must provide:

- stable logical row ordering;
- logarithmic or better lookup without hash-map access in hot row updates;
- reserve/preflight before coordinator mutation;
- a bounded growth transient included in memory preflight;
- unchanged checkpoint row order;
- no pointer/reference lifetime assumptions across growth unless guaranteed.

Choose chunk size from Task D evidence and element size. Make it an internal
constant unless configuration is required for correctness.

Do not replace one large reserve with repeated one-row reallocations.

## Required changes

1. Refactor compact row metadata ownership.
2. Update `metadata(...)`, row admission, page admission, checkpoint export,
   restore, and `memory_bytes()`/Task B snapshots.
3. Add growth preflight before logical mutation.
4. Preserve sorted row order and existing rejection ordering.
5. Update Task E estimate for chunk growth transient.
6. Keep regret and strategy-mass paging unchanged unless evidence requires it.

Likely files:

- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `tests/test_multiway_compact_storage.cpp`
- checkpoint/blueprint tests affected by storage export.

## Tests

Add coverage for:

- empty storage allocates at most one documented metadata chunk;
- admission below, at, and across chunk boundaries;
- sorted and out-of-order batch admission;
- duplicate and malformed rows;
- growth allocation failure preserves prior state;
- maximum row limit remains exact;
- metadata lookup before/after growth;
- page admission and cell updates across chunks;
- byte-identical checkpoint export;
- reverse-page checkpoint restore;
- `memory_bytes()` and Task B snapshot accuracy;
- no allocation in `apply_delta` or strategy lookup.

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\compact_build.py `
  --target test_multiway_compact_storage `
  --target test_multiway_blueprint_training `
  --target test_multiway_artifact `
  --target test_multiway_memory `
  -o tmp\memory-task-h-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_compact_storage|test_multiway_blueprint_training|test_multiway_artifact|test_multiway_memory)$"

.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
```

Rerun Task D A1 and A2 for three normal repeats and one A1 Tracy capture.

## Acceptance

- Construction no longer reserves five million row metadata entries.
- Growth is bounded, deterministic, and transactional.
- Checkpoint output and restored compact values match baseline exactly.
- No hot update path allocates.
- Coordinator-construction private bytes decrease beyond noise.
- Cold admission throughput does not materially regress.
- Full build/CTest and review pass.
- Project log records chunk size and before/after memory.

## Completion statement

Task H is complete only after checkpoint equivalence and bounded memory evidence
pass.

## Stop conditions

Stop if row ordering changes, checkpoint bytes change unexpectedly, growth can
partially mutate storage, or lookup/update hot paths regress structurally.

