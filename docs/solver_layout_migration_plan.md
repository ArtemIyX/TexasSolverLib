# Solver layout migration plan

## Goal

Replace the flat `include/solver` and `src/solver` layouts with mirrored
subdirectories. Preserve all namespaces, symbols, behavior, target names,
source classifications, and test coverage.

This is a path-only refactor. Do not rename files, classes, functions, CMake
targets, or namespaces in the same change.

## Scope

Move all 64 headers under `include/solver` and all 59 implementations under
`src/solver`. Update:

- quoted project includes in headers, implementations, tests, and examples;
- explicit CMake header and source lists;
- install destinations derived from public header paths;
- CMake boundary tests that read or match exact paths;
- public-header compilation fixtures;
- documentation that names an old path rather than only a symbol.

Do not move `core`, `games`, `preflop`, or `util` in this change. Their files
may need include edits when they depend on solver headers.

## Compatibility decision

Moving a public header changes its include path even when its namespace is
unchanged. The clean layout in this plan intentionally makes the new paths
canonical and removes the old files.

Before implementation, decide whether external source compatibility is
required:

1. Preferred for the current `0.1.0` project: make one explicit include-path
   migration and update every repository consumer.
2. If external consumers require a transition: leave forwarding headers at
   the old paths for one release. Each forwarding header must contain only
   `#pragma once` and the new include. Classify them in
   `TEXASSOLVER_COMPATIBILITY_HEADERS`; do not install them accidentally via
   `TEXASSOLVER_PUBLIC_HEADERS`.

Do not mix the two policies. The remainder of this plan assumes option 1.

## Target layout

Keep every existing basename. Mirror each header directory under `src` when a
matching implementation exists.

```text
include/solver/                    src/solver/
  generic/                          generic/
  hunl/                             hunl/
    bucket/                           bucket/
    flat/                             flat/
    sampled/                          sampled/
  multiway/                         multiway/
    abstraction/                      abstraction/
    blueprint/                        blueprint/
    continuation/                     continuation/
    engine/                           engine/
    evaluation/                       evaluation/
    resolver/                         resolver/
    session/                          session/
```

The directory names describe ownership only. All declarations remain in their
current namespace, primarily `texas` and existing nested detail namespaces.

## Complete move map

For each table, move `.hpp` from `include/solver` to the listed include
directory. Move the matching `.cpp`, when present, from `src/solver` to the
mirrored source directory.

### Generic solver

Destination: `solver/generic/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `dcfr` | `.hpp` | public header |
| `dcfr_vector` | `.hpp`, `.cpp` | research header, legacy research source |
| `exploit` | `.hpp`, `.cpp` | public header, stable source |
| `parallel_dcfr` | `.hpp`, `.cpp` | public header, stable source |
| `solver` | `.hpp`, `.cpp` | public header, stable source |

Example replacement:

```text
solver/dcfr.hpp -> solver/generic/dcfr.hpp
src/solver/exploit.cpp -> src/solver/generic/exploit.cpp
```

### HUNL common

Destination: `solver/hunl/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `hunl_leaf_evaluator` | `.hpp` | public header |

### HUNL bucket model

Destination: `solver/hunl/bucket/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `hunl_bucket_map` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_bucket_terminal` | `.hpp`, `.cpp` | public header, stable source |

### Exact flat HUNL

Destination: `solver/hunl/flat/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `hunl_flat_dcfr` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_flat_expected_value` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_flat_mccfr` | `.hpp`, `.cpp` | research header, flat MCCFR research source |
| `hunl_flat_pipeline` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_flat_state` | `.hpp`, `.cpp` | public header, stable source |

`HUNLFlatDCFR` behavior and ordering must remain unchanged.

### Sampled HUNL

Destination: `solver/hunl/sampled/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `hunl_sampled_builder` | `.hpp`, `.cpp` | research header, fixed research source |
| `hunl_sampled_config` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_export` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_profile` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_range` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_scheduler` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_simd` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_solver` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_storage` | `.hpp`, `.cpp` | public header, stable source |
| `hunl_sampled_terminal` | `.hpp`, `.cpp` | research header, fixed research source |
| `hunl_sampled_trajectory` | `.hpp`, `.cpp` | stable internal header, stable source |
| `hunl_sampled_traversal` | `.hpp`, `.cpp` | research header, fixed research source |

All old includes matching `solver/hunl_sampled_*.hpp` become
`solver/hunl/sampled/hunl_sampled_*.hpp`.

### Multiway abstraction and models

Destination: `solver/multiway/abstraction/`

| Basename | Files |
|---|---|
| `multiway_action_abstraction` | `.hpp`, `.cpp` |
| `multiway_action_calibration` | `.hpp`, `.cpp` |
| `multiway_bucket_artifact` | `.hpp`, `.cpp` |
| `multiway_bucket_model` | `.hpp`, `.cpp` |
| `multiway_future_bucket` | `.hpp`, `.cpp` |
| `multiway_future_bucket_calibration` | `.hpp`, `.cpp` |
| `multiway_model_identity` | `.hpp`, `.cpp` |
| `multiway_public_builder` | `.hpp`, `.cpp` |
| `multiway_terminal_adapter` | `.hpp`, `.cpp` |

### Multiway blueprint and artifacts

Destination: `solver/multiway/blueprint/`

| Basename | Files |
|---|---|
| `multiway_artifact` | `.hpp`, `.cpp` |
| `multiway_blueprint_config` | `.hpp`, `.cpp` |
| `multiway_blueprint_policy_provider` | `.hpp`, `.cpp` |
| `multiway_blueprint_store` | `.hpp`, `.cpp` |
| `multiway_blueprint_trainer` | `.hpp`, `.cpp` |
| `multiway_checkpoint` | `.hpp`, `.cpp` |
| `multiway_export` | `.hpp`, `.cpp` |

### Multiway continuation

Destination: `solver/multiway/continuation/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `multiway_continuation_calibration` | `.hpp`, `.cpp` | public header, stable source |
| `multiway_continuation_policy` | `.hpp`, `.cpp` | public header, stable source |
| `multiway_continuation_policy_kind` | `.hpp` | public header |
| `multiway_continuation_selector` | `.hpp`, `.cpp` | public header, stable source |
| `multiway_leaf_evaluator` | `.hpp` | public header |
| `multiway_rollout_leaf` | `.hpp`, `.cpp` | public header, stable source |

### Multiway solver engine

Destination: `solver/multiway/engine/`

| Basename | Files |
|---|---|
| `multiway_cfr` | `.hpp`, `.cpp` |
| `multiway_memory` | `.hpp`, `.cpp` |
| `multiway_scheduler` | `.hpp`, `.cpp` |
| `multiway_solver` | `.hpp`, `.cpp` |
| `multiway_traversal` | `.hpp`, `.cpp` |

### Multiway evaluation

Destination: `solver/multiway/evaluation/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `multiway_baseline` | `.hpp`, `.cpp` | research header, test-support source |
| `multiway_evaluation` | `.hpp`, `.cpp` | public header, stable source |

### Multiway resolver

Destination: `solver/multiway/resolver/`

| Basename | Files | Existing CMake classification |
|---|---|---|
| `multiway_resolver` | `.hpp`, `.cpp` | public header, stable source |
| `multiway_resolver_budget` | `.hpp`, `.cpp` | public header, stable source |
| `multiway_resolver_evaluation` | `.hpp`, `.cpp` | public header, stable source |
| `multiway_resolver_policy` | `.hpp`, `.cpp` | stable internal header, stable source |

### Multiway sessions and range tracking

Destination: `solver/multiway/session/`

| Basename | Files |
|---|---|
| `multiway_decision_session` | `.hpp`, `.cpp` |
| `multiway_full_hand_session` | `.hpp`, `.cpp` |
| `multiway_range_belief` | `.hpp`, `.cpp` |
| `multiway_range_update` | `.hpp`, `.cpp` |
| `multiway_search_profile` | `.hpp` |
| `multiway_search_session` | `.hpp`, `.cpp` |

Except for the classifications called out above, all multiway headers remain
public and all multiway implementations remain stable sources.

## Include migration

### Rules

1. Use include paths rooted at `include`, never paths relative to the including
   file.
2. Keep quoted project includes, for example
   `#include "solver/hunl/flat/hunl_flat_state.hpp"`.
3. Do not change standard-library or third-party includes.
4. Do not change include order except where a path replacement naturally
   touches a line.
5. The first include of each `.cpp` must point to its moved matching header.
6. Apply replacements across `include`, `src`, `tests`, and `examples`, not
   only inside the moved files.

### Replacement prefixes

There is no safe single wildcard replacement for all multiway headers because
they split by ownership. Use the complete move map above as the source of
truth. The deterministic HUNL replacements are:

```text
solver/hunl_bucket_*       -> solver/hunl/bucket/hunl_bucket_*
solver/hunl_flat_*         -> solver/hunl/flat/hunl_flat_*
solver/hunl_sampled_*      -> solver/hunl/sampled/hunl_sampled_*
solver/hunl_leaf_evaluator -> solver/hunl/hunl_leaf_evaluator
```

Generic replacements are:

```text
solver/dcfr.hpp          -> solver/generic/dcfr.hpp
solver/dcfr_vector.hpp   -> solver/generic/dcfr_vector.hpp
solver/exploit.hpp       -> solver/generic/exploit.hpp
solver/parallel_dcfr.hpp -> solver/generic/parallel_dcfr.hpp
solver/solver.hpp        -> solver/generic/solver.hpp
```

### Repository consumers outside `solver`

The include inventory currently identifies these non-solver code files:

```text
include/core/lib.hpp
include/games/hunl_solver.hpp
include/preflop/preflop.hpp
include/preflop/preflop_rvr.hpp
include/util/suit_iso.hpp
src/games/hunl_solver.cpp
src/preflop/preflop.cpp
```

Re-run the inventory during implementation because this list can change.

### Examples

Update all eight example entry points:

```text
examples/solve_kuhn.cpp
examples/hunl_mccfr_postflop_main.cpp
examples/multiway_workflow_main.cpp
examples/benchmarks/flat_scheduler_main.cpp
examples/benchmarks/hunl_backend_compare_main.cpp
examples/benchmarks/hunl_mccfr_scaling_main.cpp
examples/benchmarks/hunl_random_flat_main.cpp
examples/benchmarks/main.cpp
```

## CMake changes

Edit `CMakeLists.txt` in the same commit as the moves.

### Header lists

Replace every solver path in these lists using the move map:

- `TEXASSOLVER_PUBLIC_HEADERS`
- `TEXASSOLVER_STABLE_INTERNAL_HEADERS`
- `TEXASSOLVER_RESEARCH_HEADERS`

Do not move a header between classifications. In particular:

- `hunl_sampled_trajectory.hpp` and `multiway_resolver_policy.hpp` remain
  stable internal;
- `dcfr_vector.hpp`, `hunl_flat_mccfr.hpp`, sampled `builder`, `terminal`, and
  `traversal`, and `multiway_baseline.hpp` remain research headers;
- all other moved solver headers retain their current public status.

`TEXASSOLVER_COMPATIBILITY_HEADERS` stays unchanged under the clean-break
policy.

### Source lists

Replace every solver path in:

- `TEXASSOLVER_SOURCES`
- `TEXASSOLVER_TEST_SUPPORT_SOURCES`
- `TEXASSOLVER_LEGACY_RESEARCH_SOURCES`
- `TEXASSOLVER_FIXED_RESEARCH_SOURCES`
- `TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES`

Preserve exact membership:

- `multiway_baseline.cpp` remains test support;
- `dcfr_vector.cpp` remains legacy research;
- sampled `builder`, `terminal`, and `traversal` remain fixed research;
- `hunl_flat_mccfr.cpp` remains flat MCCFR research;
- every other moved implementation remains in `TEXASSOLVER_SOURCES`.

The recursive classification checks already support nested directories. Do
not replace the explicit lists with globbed target sources. The globs are only
the completeness check.

### Include and install configuration

No change is required to either target include root:

```cmake
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
```

No change is required to the public-header install loop. It calculates each
destination relative to `include`, so moved public headers will install into
the new nested directories. Verify the resulting install tree after the
migration.

Target names, aliases, compile features, link libraries, and options must not
change.

## Test updates

### C++ test includes

Update all quoted solver includes in every `tests/test_*.cpp`. The current
inventory finds the following affected tests:

```text
test_br_walk_mode_contract.cpp
test_dcfr.cpp
test_dcfr_discount_contract.cpp
test_hunl_bucket_model.cpp
test_hunl_flat_dcfr.cpp
test_hunl_flat_expected_value.cpp
test_hunl_flat_mccfr.cpp
test_hunl_flat_pipeline.cpp
test_hunl_flat_state.cpp
test_hunl_p75_row_math.cpp
test_hunl_sampled_solver.cpp
test_hunl_sampled_stable.cpp
test_hunl_solver_storage.cpp
test_hunl_solver_storage_loader.cpp
test_multiway_action_abstraction.cpp
test_multiway_action_calibration.cpp
test_multiway_aivat_record.cpp
test_multiway_artifact.cpp
test_multiway_audit_round_4.cpp
test_multiway_baseline.cpp
test_multiway_blueprint_store.cpp
test_multiway_blueprint_training.cpp
test_multiway_bucket_artifact.cpp
test_multiway_bucket_model.cpp
test_multiway_cfr.cpp
test_multiway_continuation_cache.cpp
test_multiway_continuation_policy.cpp
test_multiway_continuation_selector.cpp
test_multiway_evaluation.cpp
test_multiway_f3_regressions.cpp
test_multiway_full_hand_session.cpp
test_multiway_future_bucket.cpp
test_multiway_leaf_evaluator.cpp
test_multiway_memory.cpp
test_multiway_model_identity.cpp
test_multiway_numerical_contract.cpp
test_multiway_p7_determinism_contracts.cpp
test_multiway_p7_hot_path_contracts.cpp
test_multiway_p7_memory_contracts.cpp
test_multiway_p7_profile_contracts.cpp
test_multiway_p8_differential.cpp
test_multiway_phase4.cpp
test_multiway_phase5_p53_p56.cpp
test_multiway_public_builder.cpp
test_multiway_public_chance_sampler.cpp
test_multiway_public_chance_traversal.cpp
test_multiway_range_belief.cpp
test_multiway_recursive_traversal.cpp
test_multiway_resolver.cpp
test_multiway_resolver_budget.cpp
test_multiway_resolver_policy.cpp
test_multiway_rollout_leaf.cpp
test_multiway_rules_contract.cpp
test_multiway_scheduler.cpp
test_multiway_search_session.cpp
test_multiway_solver.cpp
test_multiway_terminal_adapter.cpp
test_ranges_solver_integration.cpp
test_ranges_threading.cpp
test_simd.cpp
test_solver_exploitability_contract.cpp
```

Do not rename these tests or change their CMake target discovery.

### CMake boundary tests

Update exact paths and path-sensitive string checks in:

- `tests/cmake/test_multiway_traversal_boundary.cmake`
  - read traversal from `src/solver/multiway/engine/`;
  - read public builder from `include/solver/multiway/abstraction/`.
- `tests/cmake/test_legacy_vector_boundary.cmake`
  - require `src/solver/generic/dcfr_vector.cpp`;
  - update the old `include/solver/dcfr.hpp` substring to
    `include/solver/generic/dcfr.hpp` without weakening the classification
    assertion.
- `tests/cmake/test_legacy_namespace_boundary.cmake`
  - update every moved solver entry in `allowed_headers`.
- `tests/cmake/test_hunl_sampled_boundary.cmake`
  - read solver and config headers from `include/solver/hunl/sampled/`;
  - read the implementation from `src/solver/hunl/sampled/`;
  - update all three research-header path matches in the public-header block.
- `tests/cmake/test_decision_session_boundary.cmake`
  - read resolver files from `solver/multiway/resolver/`;
  - read decision session from `solver/multiway/session/`.
- `tests/cmake/fixtures/public_headers/main.cpp`
  - include the new sampled solver and multiway resolver paths.

Keep every assertion and failure condition unchanged apart from path text.

## Implementation sequence

1. Record a clean inventory with repository scripts and `rg`.
2. Create all destination directories.
3. Move headers and matching sources as one mechanical batch. Preserve file
   contents and basenames.
4. Update each moved `.cpp` self-include first.
5. Apply the complete header path map across `include`, `src`, `tests`, and
   `examples`.
6. Update the CMake classification lists without changing membership.
7. Update CMake boundary tests and the public-header fixture.
8. Update path references in `README.md`, `docs`, `.agents`, and scripts only
   when they refer to actual filesystem/include paths. Do not rewrite prose
   that refers only to symbols such as `multiway_solver`.
9. Run the static audits below. Fix every stale old path before compiling.
10. In a separately authorized verification step, configure, build, and test.

Avoid mixing code cleanup, include-order normalization, namespace edits, or
API changes into the migration commit. Mechanical moves must remain easy to
review with rename detection.

## Scripted inventory and static audits

Run commands through the repository PowerShell wrapper.

### Before moving

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\repo_summary.py .
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\cmake_summary.py --target texas .
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\extract_includes.py --modules --top 100 .
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\extract_symbols.py . --module solver --compact --top 2000
```

Save the counts conceptually, not as generated files:

```text
include/solver: 64 headers before and after
src/solver:     59 implementations before and after
```

### After moving, before any build

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 rg --files include\solver src\solver
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\extract_includes.py --missing .
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\cmake_summary.py --target texas .
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 rg -n "include/solver/(hunl_|multiway_|dcfr|exploit|parallel_dcfr|solver)|src/solver/(hunl_|multiway_|dcfr|exploit|parallel_dcfr|solver)" CMakeLists.txt tests cmake docs README.md .agents scripts
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 rg -n "#include [^\r\n]*solver/(hunl_|multiway_|dcfr|exploit|parallel_dcfr|solver)" include src tests examples
```

Both stale-path searches must return no old flat solver paths. Review matches
carefully because valid new basenames still contain `hunl_` and `multiway_`;
the forbidden form is a basename directly below `solver/`.

Also verify:

- every `.cpp` listed by `rg --files src/solver` appears exactly once in a
  CMake source classification;
- every `.hpp` listed by `rg --files include/solver` appears exactly once in a
  CMake header classification;
- `extract_includes.py --missing` reports no unresolved quoted include;
- `git diff --summary` reports moves rather than delete/add pairs where Git can
  detect them;
- `git diff --word-diff=porcelain` shows only path changes in moved code.

## Later build and test verification

Do not run this section as part of planning or without explicit authorization.
After authorization, verify in this order:

1. Configure with tests enabled. This executes the source/header
   classification function and catches missing or duplicate CMake entries.
2. Build the library and tests.
3. Run the compact test suite.
4. Configure or build examples and research targets so moved research headers
   and sources are covered.
5. Perform an install-tree smoke check and compile the public-header fixture
   against the installed include tree.

Recommended commands:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 cmake --build build --config Debug -- /nologo /v:q "/clp:ErrorsOnly;NoSummary"
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\compact_ctest.py
```

Research coverage must include the configurations that own:

- `TEXASSOLVER_BUILD_LEGACY_RESEARCH`;
- `TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH`;
- `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES`.

## Acceptance criteria

- `include/solver` and `src/solver` contain only the planned subdirectories,
  with no flat solver files.
- Exactly 64 solver headers and 59 solver implementations remain.
- Header/source paths mirror each other when both exist.
- No namespace, symbol, signature, behavior, or target name changes.
- Every repository include uses a canonical new path.
- Every moved file retains exactly one CMake classification.
- Public, internal, research, compatibility, and test-support boundaries are
  unchanged.
- All path-sensitive CMake tests and fixtures use new paths without weakened
  assertions.
- Public headers install under the new nested directory tree.
- Static include and stale-path audits are clean.
- Once separately authorized, all stable, research, example, install, and test
  verification passes.

