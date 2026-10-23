# Prompt: Prepare the next AD7 catalog-commit optimization plan

You are preparing a detailed implementation plan for the next TexasSolver
multiway admission optimization. Do not implement code in this task.

## Current problem

AD7 state-preflight optimization has been implemented and qualified by the
operator. Correctness is preserved, but the cold admission target is still
missed. The remaining dominant cost is catalog menu lookup during prepared
catalog commit.

The current cold target is coordinator admission below 250 ms. The latest
captures report:

- Cold admission: 327-352 ms.
- Steady admission: 2405-2562 ms.
- Cold state preflight: approximately 154-166 ms.
- Steady state preflight: approximately 578-616 ms.
- Cold catalog commit: approximately 62-69 ms.
- Steady catalog commit: approximately 1300-1380 ms.
- Cold public-state validation: approximately 115-120 ms.
- Steady public-state validation: approximately 410-427 ms.
- Steady catalog menu lookup: approximately 1219-1298 ms.
- Steady catalog index merge: approximately 2.2-2.5 ms.

The menu lookup phase is therefore the clear remaining catalog bottleneck. The
index merge is not currently material. The cold target also requires reducing
the validation path or another measured admission phase after menu lookup is
optimized.

## Evidence

Read the eight latest reports under:

`tmp/tracy/ad7-post-*/report.json`

The latest runs all reported:

- `failed=false`;
- zero discarded trajectories;
- cold fingerprint `9806774443936985566`;
- steady fingerprint `3496596242701454594`;
- cold checkpoint bytes `137380835`;
- steady checkpoint bytes `387676624`;
- capacity exhaustion stage `0`.

The new telemetry is under `admission_diagnostics`, including:

- `state_catalog_classification_nanoseconds`;
- `public_state_validation_nanoseconds`;
- `parent_transition_validation_nanoseconds`;
- `row_state_resolution_nanoseconds`;
- `row_shape_validation_nanoseconds`;
- `catalog_menu_lookup_nanoseconds`;
- `catalog_index_merge_nanoseconds`.

For the steady fixed one-worker run, the diagnostic counts include roughly:

- 12,309 existing public-state checks;
- 248,408 new public-state validations;
- 105,118 rows resolved from the current batch;
- 1 row resolved from the existing catalog;
- 1,219 ms spent in catalog menu lookup;
- 2.45 ms spent in catalog index merge.

## Code to inspect

Inspect the current implementation and its contracts before proposing a
solution:

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/plans/2026-10-15/11_ad7_state_preflight_catalog_optimization.md`
- `docs/PLURIBUS_LOG.md`

Pay particular attention to:

- `MultiwayPublicCatalog::find_menu`;
- `MultiwayPublicCatalog::admit_batch`;
- menu handle ownership and equality checks;
- catalog node packing;
- strong failure and capacity guarantees;
- deterministic catalog ordering and checkpoint export;
- `MultiwaySolverCoordinator::admit_prepared_batch`;
- the new admission telemetry fields.

The current implementation already uses one sorted merge to classify prepared
state IDs and reusable coordinator admission scratch. Existing descriptor
compatibility still uses reconstruction where full history comparison is
required. Do not undo those safeguards.

## What the next plan must determine

Prepare a detailed, staged remediation plan for catalog menu lookup and any
remaining cold admission cost. Evaluate, with explicit correctness reasoning,
options such as:

1. indexing canonical menu IDs to existing menu handles while retaining full
   menu equality as a collision or malformed-data check;
2. reusing menu handles for repeated menus within one prepared batch;
3. precomputing menu metadata during state preflight;
4. reserving menu and node capacity before mutation;
5. reducing temporary catalog commit copies without weakening strong failure
   semantics;
6. deciding whether direct packed-node compatibility is safe or whether the
   existing descriptor reconstruction fallback must remain;
7. identifying the next optimization target if menu lookup is no longer
   dominant.

The plan must preserve:

- public-state identity and full descriptor compatibility semantics;
- deterministic fingerprints and checkpoint bytes;
- sorted catalog index order;
- single-state admission and checkpoint restore behavior;
- row offsets, row-shape validation, compact storage layout, and capacity
  checks;
- the coordinator lock contract;
- strong failure behavior with no partial catalog or storage mutation;
- no changes to traversal, sampling, scheduler, or worker merge semantics.

## Required plan contents

Save the plan as a new Markdown document under:

`docs/plans/2026-10-15/`

Use a filename that clearly identifies the next AD7 catalog/menu optimization.
The first line must briefly state what the task does and whether operator
runtime validation is required.

The plan must include:

- measured baseline and acceptance thresholds;
- exact source and test files likely to change;
- data structures and ownership/lifetime rules;
- exception-safety and capacity-preflight reasoning;
- deterministic ordering and collision handling;
- focused regression tests;
- additive telemetry needed to prove the optimization;
- small commit boundaries;
- operator build/test/Tracy qualification steps;
- report locations and expected fingerprints/checkpoint sizes;
- fallback behavior if the 250 ms cold target remains unmet.

## Session constraints

- Do not build.
- Do not test.
- Do not run built `.exe` files.
- Do not run scripts that launch built executables.
- Use `scripts/` for read-only discovery when helpful.
- Use `project-navigation`, `cpp-code-review`, and `git-commit-hygiene` as
  applicable.
- Do not modify source code in this planning task.
- Preserve unrelated untracked plan files.
