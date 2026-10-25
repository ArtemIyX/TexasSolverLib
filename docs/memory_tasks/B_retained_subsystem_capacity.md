# Task B: Retained subsystem capacity attribution

## Trigger

`Fully implement B.`

## Objective

Report actual live and retained capacity bytes for every major multiway-owned
memory subsystem. The result must explain solver-owned private memory without
double counting. This task changes telemetry only.

## Dependencies

Task A must be complete.

## Read first

- `docs/multiway_memory_measurement_and_reduction_plan.md`
- `docs/memory_tasks/A_process_memory_phase_snapshots.md`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `include/solver/multiway/continuation/multiway_continuation_selector.hpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_parallel_training.cpp`

## Required design

Add fixed-layout `noexcept` memory snapshot types. Every owning container must
report:

```text
live_element_bytes
retained_capacity_bytes
allocation_count, only if directly known
```

Aggregate these non-overlapping owners:

- bucket registry owned metadata;
- bucket mapping virtual bytes, grouping only;
- public catalog persistent nodes, indexes, menus, and scratch;
- compact row metadata, page metadata, regret cells, and strategy cells;
- worker regret-delta streams;
- worker continuation-delta streams;
- discovery public-state streams;
- discovery row streams;
- worker action-menu scratch;
- coordinator merge scratch;
- coordinator admission scratch;
- checkpoint/export retained buffers;
- continuation cache/scratch when present.

Nested totals must be marked grouping-only or omitted from the grand total.
One allocation must have one owner.

## Required changes

1. Add `memory_snapshot() const noexcept` or an equivalent narrow API at
   subsystem ownership boundaries.
2. Use `capacity() * sizeof(element)` with checked/saturating arithmetic.
3. Include nested menu/vector payloads where the owner is the catalog.
4. Add per-worker and aggregate stream fields.
5. Capture snapshots after coordinator construction, worker-pool construction,
   cold batch, and steady batch using Task A phases.
6. Serialize a versioned `memory_subsystems` object.
7. Add an `unattributed_private_bytes` analyzer field only as a derived value;
   never subtract when process private bytes are unavailable or smaller than
   attributed bytes.

Do not shrink, reserve, clear, or reallocate any container in Task B.

## Tests

Prove:

- empty containers report their retained reserves correctly;
- partially filled containers distinguish live bytes from capacity bytes;
- rewind/clear changes live bytes but not retained capacity;
- public catalog nested vectors are counted once;
- compact storage totals match row/page/value capacities;
- aggregate worker bytes equal the sum of workers;
- 1-worker and 4-worker fixtures scale only the worker-owned categories;
- arithmetic overflow saturates or reports invalid without wraparound;
- snapshot functions are `noexcept` and allocation-free;
- JSON serialization and report validation cover all categories.

Primary tests:

- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_public_catalog.cpp`
- `tests/test_multiway_qualification_reports.cpp`

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\compact_build.py `
  --target test_multiway_memory `
  --target test_multiway_parallel_training `
  --target test_multiway_compact_storage `
  --target test_multiway_public_catalog `
  --target test_multiway_qualification_reports `
  -o tmp\memory-task-b-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_memory|test_multiway_parallel_training|test_multiway_compact_storage|test_multiway_public_catalog|test_multiway_qualification_reports)$"

.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
git diff --check
```

No large workload is required.

## Acceptance

- All listed owners have live/capacity attribution.
- Grand totals have no known double counting.
- Snapshots allocate nothing and do not enter hot loops.
- Existing memory fields remain backward-compatible.
- Focused and full verification pass.
- Independent review confirms ownership and arithmetic.
- The project log records Task B and actual validation.

## Completion statement

Task B is complete when subsystem attribution is implemented, tested, built,
reviewed, and logged. No memory reduction may be claimed.

## Stop conditions

Stop if ownership cannot be assigned uniquely, if snapshot collection requires
hot-path allocation, or if Task A is incomplete.

