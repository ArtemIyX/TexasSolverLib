# Task I: Release oversized admission scratch

## Trigger

`Fully implement I.`

## Objective

Reduce private memory retained after the cold admission batch when coordinator
or catalog scratch capacities remain far above steady-state requirements.

## Evidence gate

Task D must select I. The evidence must show a large cold-to-steady retained
capacity in admission/catalog/merge scratch and a matching private-memory
remainder.

If D does not select I, do not implement this task.

## Required design

Define a stable steady-state retention policy:

1. Record the high-water live size for each eligible scratch vector.
2. At a coordinator batch boundary after successful cold admission, compute an
   evidence-based retained target.
3. Retain enough capacity for normal steady batches plus a documented margin.
4. Release only scratch storage, never persistent catalog/storage ownership.
5. Perform any allocation/deallocation outside traversal, row update,
   terminal, and merge inner loops.
6. Preserve strong transactional behavior. A shrink failure must leave the
   existing capacity usable.

Prefer swapping with a pre-sized replacement at a safe boundary over calling
implementation-dependent `shrink_to_fit` and assuming it releases memory.

Eligible owners must be selected from Task B evidence. Likely candidates:

- discovery merge output vectors;
- parent-order scratch;
- coordinator admission classification arrays;
- public catalog batch pending/merged indexes and menu scratch;
- pending merge cells.

Do not shrink worker streams in I; that belongs to G.

## Tests

Add tests for:

- cold high-water followed by small steady batches;
- retained target calculation and safety margin;
- no release before successful cold admission;
- failed batch does not alter persistent state or lose required scratch;
- collision-heavy catalog menus retain correctness;
- deterministic handle and index order;
- later batch regrowth remains bounded and transactional;
- memory snapshots show the capacity reduction;
- fixed/dynamic equivalence;
- no allocations inside hot loops.

Primary tests:

- `tests/test_multiway_public_catalog.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_memory.cpp`

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\compact_build.py `
  --target test_multiway_public_catalog `
  --target test_multiway_solver `
  --target test_multiway_parallel_training `
  --target test_multiway_memory `
  -o tmp\memory-task-i-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_public_catalog|test_multiway_solver|test_multiway_parallel_training|test_multiway_memory)$"

.\scripts\codex_powershell.ps1 .\scripts\full_build.py
```

Rerun Task D A2, A4, and A5 for three normal repeats. Capture A4 with Tracy.

## Acceptance

- Only measured scratch owners are released.
- Persistent catalog, menu handles, row identities, and storage are unchanged.
- Steady private bytes fall beyond baseline noise.
- Regrowth remains safe and does not cause capacity exhaustion.
- Zero discards and matching fingerprints in all selected matrix cases.
- Steady throughput does not materially regress.
- Full build/CTest and C++ review pass.
- Project log records each released owner and before/after capacity.

## Completion statement

Task I is complete only after cold-to-steady retained capacity and private bytes
both show the expected reduction.

## Stop conditions

Stop if `shrink_to_fit` is the only proposed guarantee, if later batches
repeatedly reallocate, if persistent ownership is affected, or if the memory
change remains within noise.

