# Pluribus Roadmap Progress Log

## Struct layout evidence review

**Status:** Complete
**Completed:** 2026-10-31

- Reordered `HUNLSampledInfosetMeta` fields under pack 8 to remove padding. Static layout analysis predicts 48 bytes to 40 bytes without changing field types or names.
- Preserved `HUNLSampledNode` state copies, `HUNLFlatNodeMeta` terminal fields, `MultiwayPackedPublicNode` record fields, and multiway IDs because ownership, traversal, exact-mode, external-format, or bounds evidence was insufficient for a safe reduction.
- Added focused compile-time size, alignment, standard-layout, trivial-copyability, and member-offset contracts for the sampled metadata, sampled row shape, multiway IDs, row shape, and packed record candidates.

### Files

- `include/solver/hunl/sampled/hunl_sampled_storage.hpp`
- `tests/test_struct_layout_contract.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review only. CMake configuration was run before the user requested no further build or test execution; the build was interrupted and no tests were run.

### Limitations

- Runtime sizeof, vector-capacity, resident-memory, and representative-runtime before/after measurements remain outstanding.
- AVX2 dispatch remains compile-time selected in `src/util/simd.cpp`; no SIMD code was changed because runtime CPU measurements and a broader dispatch change were outside this static-only continuation.

## Explicit struct packing policy

**Status:** Complete
**Completed:** 2026-10-31

- Added balanced 8-byte packing scopes around project-owned struct definitions in public headers, implementation files, examples, and tests. The packing restoration probe intentionally uses 1-byte packing.
- Preserved cache-line aligned worker types and added compile-time checks for their alignment and packed synchronization members. Documented the policy, ABI rebuild requirement, and serialization boundaries.

### Files

- `AGENTS.md`, `docs/PROJECT_GUIDE.md`, `docs/PLURIBUS_LOG.md`
- Struct-bearing headers under `include/` and implementation files under `src/`
- Struct-bearing examples under `examples/` and fixtures under `tests/`
- `tests/test_packing_contract.cpp`

### Validation

- Static source audit checked named struct declarations, balanced packing scopes, preprocessor branch boundaries, and includes within scopes. The only intentionally uncovered declaration is the packing-restoration probe.
- Reviewed explicit over-alignment declarations, representative layout and serialization code, and the whitespace diff. Build and tests were not run as requested.

### Limitations

- The new compile-time assertions have not been compiled. ABI compatibility with existing binaries and layout behavior across compilers and platforms remain unverified; rebuild consumers with the library.

The user reported that the build and tests passed after the following fixes; commands and test scope were not specified.

## Multiway boundary checks after source splits

**Status:** Complete
**Completed:** 2026-10-31

- Updated traversal and batch-runner boundary checks for their current source files.
- Updated workflow export ordering and report field checks for the split implementation.

### Files

- `tests/cmake/test_multiway_traversal_boundary.cmake`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review of checked source locations, call ordering, and report fields.
- Configuration, builds, and tests not run as requested.

### Limitations

- Boundary scripts were not executed.

## Multiway source compilation fixes

**Status:** Complete
**Completed:** 2026-10-31

- Fixed bucket codec symbol lookup, AIVAT confidence score lookup, tournament comparator deduction, and local solver detail includes.

### Files

- `src/solver/multiway/abstraction/multiway_bucket_artifact_codec.cpp`
- `src/solver/multiway/evaluation/multiway_aivat.cpp`
- `src/solver/multiway/engine/multiway_discovery.cpp`, `multiway_solver.cpp`, `multiway_coordinator_admission.cpp`, `multiway_coordinator_merge.cpp`, `multiway_coordinator_checkpoint.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review of declarations, call sites, comparator semantics, confidence levels, and include paths; `git diff --check` passed.
- Build and tests not run as requested.

### Limitations

- Compilation and runtime behavior were not verified.

## Blueprint training compact storage compile fix

**Status:** Complete
**Completed:** 2026-10-31

- Included the complete compact storage declaration used by training diagnostics.

### Files

- `src/solver/multiway/blueprint/multiway_blueprint_training_loop.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review of storage declarations, value count types, and the affected maximum calculation.
- Build and tests not run as requested.

### Limitations

- Compilation and runtime behavior were not verified.

## Root policy artifact declaration compile fix

**Status:** Complete
**Completed:** 2026-10-31

- Included the checkpoint header that declares the root policy artifact used by blueprint serialization.

### Files

- `src/solver/multiway/blueprint/multiway_root_policy_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review of the declaration, namespace, and call signatures.
- Build and tests not run as requested.

### Limitations

- Compilation and runtime behavior were not verified.

## Multiway blueprint identity serialization compile fix

**Status:** Complete
**Completed:** 2026-10-31

- Restored file-local identity serialization helpers for blueprint artifacts, matching the existing portable field order and truncated-input handling.

### Files

- `src/solver/multiway/blueprint/multiway_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review of helper visibility, field order, and error handling.
- Build and tests not run as requested.

### Limitations

- Compilation and runtime behavior were not verified.

## Standalone public types extraction

**Status:** Complete
**Completed:** 2026-10-31
**Implementation commits:** `e20aa9c6` Types: Extract standalone value headers; `0ef3b50a` Types: Extract remaining planned structs; `1c6751e7` Types: Isolate the lightweight action ID; `85418045` Build: Export the standalone types target; `2f937bee` Types: Document extracted configuration fields

- Extracted the plan’s value structs, enums and supporting identifiers into subsystem `*_types.hpp` headers, retaining namespaces, member order, defaults and old include paths.
- Added `TexasSolver::types` as a C++17 interface target and linked `TexasSolver::texas` publicly to it without adding solver dependencies to the types target.
- Kept the shared package export and Threads lookup; out-of-line operations still require the implementation library.

### Files

- `CMakeLists.txt`, `cmake/TexasSolverTargets.cmake`, `tests/cmake/test_cmake_structure.cmake`
- `docs/PROJECT_GUIDE.md`, `docs/PLURIBUS_LOG.md`
- `include/core/action_id.hpp`, `include/core/poker.hpp`, `include/core/street_types.hpp`, `include/core/types.hpp`, `include/core/value_types.hpp`
- `include/games/hunl.hpp`, `include/games/hunl_action_types.hpp`, `include/games/hunl_flat_graph.hpp`, `include/games/hunl_flat_graph_types.hpp`, `include/games/hunl_terminal_types.hpp`, `include/games/hunl_tree.hpp`
- `include/games/multiway_rake.hpp`, `include/games/multiway_rake_types.hpp`, `include/games/multiway_state.hpp`, `include/games/multiway_state_types.hpp`
- `include/solver/hunl/flat/hunl_flat_state.hpp`, `include/solver/hunl/flat/hunl_flat_types.hpp`
- `include/solver/hunl/sampled/hunl_sampled_config.hpp`, `include/solver/hunl/sampled/hunl_sampled_config_types.hpp`, `include/solver/hunl/sampled/hunl_sampled_export.hpp`, `include/solver/hunl/sampled/hunl_sampled_export_types.hpp`, `include/solver/hunl/sampled/hunl_sampled_profile.hpp`, `include/solver/hunl/sampled/hunl_sampled_profile_types.hpp`
- `include/solver/multiway/abstraction/multiway_future_bucket.hpp`, `include/solver/multiway/abstraction/multiway_future_bucket_types.hpp`, `include/solver/multiway/abstraction/multiway_model_identity.hpp`, `include/solver/multiway/abstraction/multiway_model_identity_types.hpp`
- `include/solver/multiway/blueprint/multiway_blueprint_config.hpp`, `include/solver/multiway/blueprint/multiway_blueprint_config_types.hpp`

### Validation

- Static review only: inspected includes and type dependencies, declaration names/defaults/member order, compatibility entry points, target usage requirements, header installation registration, package export and dependency order, and final diffs.
- `git diff --cached --check` passed for each staged change set.
- No configuration, compilation, tests, installation, benchmarks, ABI comparison or solver jobs were run, as requested.

### Limitations

- Header compilation, installed consumer use and relocatability remain unverified. The package still finds Threads for types-only consumers, and calling out-of-line methods requires `TexasSolver::texas`.
## CTest failure scanner detection

**Status:** Complete
**Completed:** 2026-10-31

- Updated the CTest error scanner to recognize harness `[FAIL]` records with or without source locations and to fall back to CTest's failed-test summary. Missing or unreadable logs now fail closed.
- Fixed the row-index regression setup by compacting its runs before ordered iteration, preserving the configured fan-in limit.

### Files

- `scripts/python/scan_ctest_errors.py`
- `scripts/python/test_scan_ctest_errors.py`
- `tests/test_multiway_external_storage.cpp`
- `docs/application_and_script_io.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `& .\scripts\codex_powershell.ps1 python .\scripts\python\test_scan_ctest_errors.py` passed (5 tests).
- Scanning `tmp/ctest_debug.log` reported the row-index failure and failed CTest target, with exit code 1.
- Focused build of `test_multiway_external_storage` passed; focused CTest passed (1/1).

### Limitations

- Full CTest was not rerun.

## F1 50K bounded scale-up gate

**Status:** Complete
**Completed:** 2026-10-30

- Defined and prepared a 50-batch, 50,000-trajectory F1 12/12/12 profile with 5,000,000 public-state/sparse-row caps and a 16 GiB disk reservation. Kept memory, disk-amplification, and P99 hard stops unchanged.
- The focused monitor test passed and the 50K training attempt stopped at the P99 lower-bound gate after 6 checkpoints and 5 cycles. The first two complete cohort P99 values were 66,039.4304 ms and 67,013.8412 ms; cycle 5 at 80,958.331 ms established a strictly higher lower bound for the third cohort. Inspection confirmed this is the intended stop condition, not a monitor defect.
- Memory and disk gates remained within limits: 16.92 GiB peak working set, 2.68 GiB peak private commit, 26.71 GiB minimum available memory, and 1.84x maximum disk amplification. The 50K profile did not qualify; no resume was run.

### Files

- `configs/multiway/f1_checkpoint_qualification_50k_v1.cfg`
- `scripts/run/prepare_f1_50k_checkpoint_qualification.ps1`
- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/resume_f1_10k_checkpoint_qualification.ps1`
- `docs/f1_10k_checkpoint_qualification.md`
- `docs/f1_50k_checkpoint_qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `powershell.exe -NoProfile -File .\scripts\run\test_pb12_diagnostic_monitor.ps1` passed.
- PowerShell AST parsing passed for the preparation, monitor runner, and resume scripts.
- Prepared `pb12_diagnostic_f1_50k_20261030_gate1`; the recorded trainer and bucket hashes matched retry3.
- `& .\scripts\run\run_pb12_diagnostic.ps1 -RunName 'pb12_diagnostic_f1_50k_20261030_gate1'` exited 1 because the P99 stop fired. The monitor recorded 6 checkpoints, 5 cycles, and the stop reason. No build or checkpoint-only resume was run.

### Limitations

- The 50K profile is not qualified. Investigate the increasing cycle durations before another bounded run; do not weaken the P99 gate or advance to 50M.

## F1 10K checkpoint-only resume qualification

**Status:** Complete
**Completed:** 2026-10-30

- Passed the focused PB-12 monitor test and retry3's checkpoint-only resume in a separate named attempt, preserving the earlier root-level resume evidence.
- Retry3 training evidence passed: exit 0, 10,000 accepted trajectories, zero discarded, 10 checkpoints, 9 cycles, P99 pass, 22.40 GiB peak working set, 8.28 GiB peak private commit, 21.46 GiB minimum available memory, and 1.83x maximum disk amplification.
- The resume exited 0 with 10,000 accepted and zero discarded trajectories. Model identity matched (`14334205690234538347`), and the original and resumed policy SHA-256 values matched (`D6421B352A1181F10B96C781396327266C84527F8EB3CD51A08C29B6036B7A6A`). Resume resource peaks were 16.30 GiB working set, 2.04 GiB private commit, and 27.64 GiB minimum available memory.

### Files

- `docs/f1_10k_checkpoint_qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `powershell.exe -NoProfile -File .\scripts\run\test_pb12_diagnostic_monitor.ps1` passed.
- `$runName = 'pb12_diagnostic_f1_10k_20261030_retry3'; & .\scripts\run\resume_f1_10k_checkpoint_qualification.ps1 -RunName $runName -ResumeName 'retry1'` exited 0.
- Inspected `tmp/pb12_diagnostic_f1_10k_20261030_retry3/monitor.jsonl`, `report.json`, and `resume_attempt_retry1/resume_report.json` plus `resume_equivalence.json`.
- No build or training job was run; only the authorized monitor test and zero-batch resume were executed.

### Limitations

- This qualifies the 10K profile only. The 50M run remains unqualified.

## F1 checkpoint resume exit evidence

**Status:** Complete
**Completed:** 2026-10-30

- The retry3 resume logged completed export and produced matching policy SHA-256 values and report identity and trajectory counts, but the wrapper reported a blank exit code and did not persist resource evidence.
- Retain the launched process handle and support fresh named resume attempts without overwriting the original evidence. Add exit-code regression coverage and document the human retry.

### Files

- `scripts/run/resume_f1_10k_checkpoint_qualification.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/f1_10k_checkpoint_qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Inspected the resume logs and reports and compared policy SHA-256 hashes. Builds, tests, compiled programs, and training were not run by the agent at the user's direction.

### Limitations

- The missing exit-code cause and handle fix require human verification. The first resume lacks persisted exit and resource evidence, so a fresh resume must pass all gates.

## F1 10K retry2 checkpoint investigation

**Status:** Complete
**Completed:** 2026-10-30

- Traced the P99 hard stop to increasing sliding cohorts and live-byte samples; checkpoint writes were much shorter than batch-to-checkpoint cycles. The journal does not establish a single performance root cause.
- Record the triggering checkpoint and cycle before evaluating the unchanged gate, and add the preserved retry2 sequence to monitor regression coverage. Document both retries, measurement limits, and a fresh human-run name.

### Files

- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/f1_10k_checkpoint_qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Inspected the preserved monitor, journal, logs, configuration, qualification scripts, and checkpoint timestamp. No builds, tests, compiled programs, or training were run, as requested.

### Limitations

- The regression and future qualification require human execution. Stable storage scans are not explicitly tied to the checkpoint generation; the recorded evidence does not establish whether this affected retry2.

## F1 10K manifest publication investigation

**Status:** Complete
**Completed:** 2026-10-30

- The preserved 10K attempt failed publishing the aggregate training manifest during batch 7 with Windows access denied, after six completed batches. The monitor recorded no hard stop; its P99 result was incomplete after the trainer exit.
- Allow atomic replacement during live monitor reads and add a Windows replacement regression. Keep stable-sample and qualification gates intact.
- Update the human-run qualification instructions with a fresh run name and the failed attempt's evidence.

### Files

- `scripts/run/pb12_diagnostic_storage.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/f1_10k_checkpoint_qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Source and preserved run evidence inspected; `git diff --check` passed. The human-run monitor test failed because Windows PowerShell passed an invalid null backup path to `File.Replace`; the test now supplies a backup path. The corrected test has not been run. Builds, compiled programs, and training were not run by the agent at the user's direction.

### Limitations

- The logs do not identify the handle that blocked publication. The regression and fresh qualification, including checkpoint-only resume, require human execution.

## F1 10K checkpoint qualification preparation

**Status:** Complete
**Completed:** 2026-10-30

- Added train-time bucket artifact identity and per-table street bucket-count validation, with inspection output exposing the profile counts.
- Prepared a bounded 10K F1 12/12/12 qualification and checkpoint-only resume workflow. It records checkpoint write time separately from batch-to-checkpoint cycles and enforces memory, disk, and P99 gates.
- Kept the monitor compatible with Windows PowerShell numeric literal syntax.
- Existing Stage 9.4 and 3K results used 96/128/192 buckets, while the F1 artifact contains 12/12/12. Those runs do not qualify the target profile. Current evidence does not justify a 50M run.

### Files

- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `tests/test_multiway_bucket_artifact.cpp`
- `configs/multiway/f1_checkpoint_qualification_10k_v1.cfg`
- `scripts/run/prepare_f1_10k_checkpoint_qualification.ps1`
- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/resume_f1_10k_checkpoint_qualification.ps1`
- `docs/f1_10k_checkpoint_qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- Windows PowerShell 5.1 AST parsing passed for all three qualification scripts.
- Builds, tests, training, and compiled programs were not run; the user reserved those for human execution.

### Limitations

- The 10K run and checkpoint-only resume gate remain pending human execution. Do not start the 50M run until both pass.

## Root README and AI workflow guide

**Status:** Complete
**Completed:** 2026-10-30

- Added a general-audience README covering project purpose, maturity, the multiway workflow, onboarding, dependencies, research, and documentation.
- Added a guide to repository agent instructions, skills, and project evidence.

### Files

- `README.md`
- `docs/AI_WORKFLOW.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Inspected current documentation, CMake definitions, example source, dependency licenses, and workflow configuration.
- Checked local link targets and Markdown code-fence balance.

### Limitations

- Builds, tests, examples, documentation generation, and rendering were not run for this documentation task.
- GitHub Pages deployment and live CI status were not verified.

## Linux Debug compiler diagnostics

**Status:** Complete
**Completed:** 2026-10-29

- Removed the duplicate multiway snapshot validation declaration that stopped Linux Debug compilation.
- Addressed the reported dangling-reference and compiler warnings with explicit initialization, bounded insertion sorting, unused-code cleanup, and warning-safe expressions.
- Added the missing showdown case and removed an unused resolver parameter.

### Files

- `include/games/multiway_state.hpp`
- `include/solver/generic/solver.hpp`
- `src/games/hunl_flat_builder.cpp`
- `src/games/multiway_private.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/resolver/multiway_resolver.cpp`
- `src/solver/multiway/workflow/multiway_checkpoint_equivalence.cpp`
- `src/util/pcs.cpp`
- `src/util/simd.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- The Linux build was not rerun because the repository's C++ implementation and review skills prohibit builds and tests unless explicitly requested.

### Limitations

- A successful Linux Debug build has not yet confirmed these fixes.

## Doxygen GitHub Pages workflow

**Status:** Complete
**Completed:** 2026-10-29

- Added a Doxygen configuration for project-owned `.h` and `.hpp` files in `include/`, `src/`, and `tests/`, excluding `external/`.
- Added a GitHub Actions workflow that regenerates HTML on pushes to `main` and deploys the generated Pages artifact.
- Pinned GitHub Actions to full commit SHAs.

### Files

- `Doxyfile`
- `.github/workflows/docs.yml`
- `docs/PLURIBUS_LOG.md`

### Validation

- Inspected the workflow and Doxygen configuration.
- Doxygen generation and GitHub Pages deployment were not run because this task only added their configuration.

### Limitations

- GitHub Pages must be enabled with the repository's publishing source set to GitHub Actions before deployment can succeed.
- The workflow and generated site have not been exercised on GitHub Actions.

## C++ header documentation coverage

**Status:** Complete
**Completed:** 2026-10-29

- Added Doxygen file briefs and API documentation to all 134 tracked project-owned `.h` and `.hpp` headers, including test-support headers.
- Based documentation on the declarations and existing implementation behavior; excluded vendored `external/` headers.

### Files

- `include/core/*.hpp` (12 headers)
- `include/games/*.hpp` (15 headers)
- `include/logging/*.hpp` (3 headers)
- `include/preflop/*.hpp` (3 headers)
- `include/solver/generic/*.hpp` (5 headers)
- `include/solver/hunl/*.hpp` (1 header)
- `include/solver/hunl/bucket/*.hpp` (2 headers)
- `include/solver/hunl/flat/*.hpp` (5 headers)
- `include/solver/hunl/sampled/*.hpp` (12 headers)
- `include/solver/multiway/abstraction/*.hpp` (12 headers)
- `include/solver/multiway/blueprint/*.hpp` (8 headers)
- `include/solver/multiway/continuation/*.hpp` (6 headers)
- `include/solver/multiway/engine/*.hpp` (10 headers)
- `include/solver/multiway/evaluation/*.hpp` (2 headers)
- `include/solver/multiway/resolver/*.hpp` (4 headers)
- `include/solver/multiway/session/*.hpp` (6 headers)
- `include/solver/multiway/workflow/*.hpp` (13 headers)
- `include/util/*.hpp` (12 headers)
- `src/logging/terminal.hpp`
- `tests/test_abstraction_fixture.hpp`
- `tests/test_harness.hpp`

### Validation

- Tracked-header inventory found 134 in-scope files; every file contains a Doxygen `@file` brief.
- `git diff --check -- include src tests` passed.
- No builds, tests, or generated documentation were run because this task only changed documentation.

### Limitations

- Full-worktree `git diff --check` could not inspect the inaccessible `external/pokerHandEvaluator` submodule; the changed source, include, and test paths passed the scoped check.
- Documentation rendering and compilation were not evaluated.

## C++ documentation skill

**Status:** Complete
**Completed:** 2026-10-29

- Added scoped C++ documentation coverage rules, examples, writing guidance, and future automation references.
- Checked project examples against current declarations and implementations; no source or CI behavior changed.

### Files

- `.agents/skills/cpp-documentation/SKILL.md`
- `.agents/skills/cpp-documentation/references/coverage.md`
- `.agents/skills/cpp-documentation/references/writing-examples.md`
- `.agents/skills/cpp-documentation/references/automation.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Checked expected frontmatter, local reference targets, and balanced Markdown code fences.
- The skill-creator validator could not run because the available Python runtimes lack PyYAML.
- No project builds, tests, documentation generation, benchmarks, installs, or solver jobs were run because they were not requested.

### Limitations

- Generator rendering and documentation deployment were not evaluated.

## TexasSolver guide skill

**Status:** Complete
**Completed:** 2026-10-29

- Added a project-wide agent skill with focused architecture, CMake, tooling, and task-example references.
- Routed repository work to the skill in `AGENTS.md` while retaining specialized skill instructions and execution boundaries.

### Files

- `AGENTS.md`
- `.agents/skills/texas-guide/SKILL.md`
- `.agents/skills/texas-guide/references/architecture.md`
- `.agents/skills/texas-guide/references/cmake.md`
- `.agents/skills/texas-guide/references/tools.md`
- `.agents/skills/texas-guide/references/examples.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Skill creator `quick_validate.py` passed using the installed Python runtime.
- Checked skill reference links and cited project and example paths.
- No project builds, tests, benchmarks, installs, or solver jobs were run because they were not requested.

### Limitations

- Project paths and build options must be rechecked as the repository evolves.

## Project guidance report

**Status:** Complete
**Completed:** 2026-10-29

- Prepared a detailed external guide to the current architecture, CMake configuration, scripts, workflows, and project practices using static repository inspection.
- Identified outdated navigation and source-discovery guidance against the current checkout.

### Files

- `docs/PLURIBUS_LOG.md`
- External workspace artifact: `TexasSolver-project-guide.md`

### Validation

- Inspected repository configuration, guidance, selected source locations, CI, scripts, and local commit history.
- Builds, tests, benchmarks, and solver jobs were not run because they were not requested.

### Limitations

- The linked GitHub pull request pages and the evaluator submodule worktree could not be inspected in this environment.

## CMake build structure refactor

**Status:** Complete
**Completed:** 2026-10-29

- Kept project setup, options, and source/header classification in the root; moved target and package rules to `cmake/TexasSolverTargets.cmake`, examples to `examples/CMakeLists.txt`, and test declarations to `tests/CMakeLists.txt`.
- Preserved target names, C++17 and CMake 3.20 requirements, dependency visibility, platform source selection, test gates, and independent optional components.
- Corrected the parent install rule to install the vendored evaluator's public headers from their actual source paths without editing vendored files.
- Added a CMake structure contract and updated boundary checks for the new ownership files.

### Files

- `CMakeLists.txt`
- `cmake/TexasSolverTargets.cmake`
- `examples/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `tests/cmake/test_cmake_structure.cmake`
- `tests/cmake/test_example_boundary.cmake`
- `tests/cmake/test_legacy_vector_boundary.cmake`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `tests/cmake/test_tracy_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Configured six independent Windows/MSVC variants: defaults, research examples, legacy research, fixed HUNL research, logging, and Tracy diagnostics.
- `scripts/python/full_build.py` passed the full build and test suite before the final install-path adjustment. After that adjustment, `compact_build.py --target texas`, all CTest tests, and seven focused CMake boundary tests passed.
- The first post-adjustment parallel CTest run hit an MSVC program-database mismatch in `test_public_header_boundary`; that test passed alone and the subsequent full CTest run passed.
- Installed the package, then configured and linked a separate `TexasSolver::texas` consumer before and after relocating the install prefix.
- `git diff --check` passed for the task changes.

### Limitations

- Verification used Windows/MSVC with CMake 4.4.0-rc2; exact CMake 3.20, Linux, and macOS runs were unavailable.

## Project skill - CMake maintenance

**Status:** Complete
**Completed:** 2026-10-29

- Added a project-local CMake skill with implicit invocation enabled and an AGENTS.md requirement to read it before every CMakeLists.txt edit or related CMake change.
- Added five focused guides covering targets/modules, dependencies/packages, tests/verification, platforms/presets, and authoritative CMake plus LLVM/VTK/curl references. Included C++17, package-config, test-registration, and CMake 3.20 preset examples.
- Preserved execution authorization boundaries. No CMake files, build behavior, or solver code changed.

### Files

- `AGENTS.md`
- `.agents/skills/cmake-maintenance/SKILL.md`
- `.agents/skills/cmake-maintenance/agents/openai.yaml`
- `.agents/skills/cmake-maintenance/references/targets-and-modules.md`
- `.agents/skills/cmake-maintenance/references/dependencies-and-packages.md`
- `.agents/skills/cmake-maintenance/references/testing-and-verification.md`
- `.agents/skills/cmake-maintenance/references/platforms-and-presets.md`
- `.agents/skills/cmake-maintenance/references/sources-and-projects.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static checks passed for required frontmatter, all five local reference links, the JSON preset example, implicit invocation metadata, and AGENTS.md routing.
- `git diff --check` passed. No project builds or tests ran for this instruction-only change.
- Attempted the bundled skill-creator `quick_validate.py` through the project Python wrapper using both available runtime paths; both lacked the required PyYAML module.

### Limitations

- Full skill-creator validation remains unavailable without PyYAML. Automatic use is instructed through skill metadata and AGENTS.md; this is not a filesystem hook or proof of future agent behavior.

## PB-11 CI follow-up: Force page-sub-batch rollback coverage

**Status:** Test fixture corrected; CI rerun pending
**Completed:** 2026-10-29

- User-reported Linux CTest failure: `multiway_solver_external_bounded_page_merge_rolls_back_flushed_subbatches` observed no page publication before the intentionally invalid delta.
- A 2,048-byte attempt was rejected because its remaining page-cache budget could not hold one committable page. The initial 3,000-byte fixture update still failed on Linux: `MultiwayWorkerDelta` aggregate fields are bucket then action, but the attempted updates varied action while keeping bucket zero, so only the root and one child page were dirtied. The fixture now keeps the valid 3,000-byte cache and updates child buckets 0 through 3 at action 0 before the invalid action 255. This dirties five distinct pages, exceeding the two-page cache capacity and forcing a subbatch publication. Production code is unchanged.

### Files

- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- User-provided CI results confirm the original no-publication failure, the 2,048-byte attempt failed with `external compact page cache cannot hold a committable page`, and the first 3,000-byte attempt still had no page publication because its aggregate delta initializers addressed one child bucket.
- `git diff --check` passed after the change. Builds and tests were not run by the assistant, per repository instructions.

### Limitations

- CI rerun is required to confirm the test fixture passes on Linux.

## Stage 9.4 post-stop: Correct the first bounded append admission

**Status:** Complete, bounded diagnostic verified; P99 inconclusive
**Completed:** 2026-10-29

- The prepared PB-12 run failed on its first catalog append with `external storage disk amplification budget cannot admit a record`; its two scans recorded 397 live bytes and 581 stored bytes, below the 3x hard stop. The run produced no batch/checkpoint and is not a completed-run result.
- Synchronous bounded aggregate training now relies on its existing durable transaction WAL and manifest backups for append rollback/recovery, avoiding a duplicate component payload WAL. Append preflight counts only the component's segment/index and manifest staging; aggregate-manifest staging is checked at commit against the updated live-byte denominator and disk reserve. Generic stores retain component WAL behavior.
- The monitor now preserves trainer failure as the primary completion reason instead of overwriting it with missing-observation status. Added first-record admission, outer-journal rollback/recovery, commit-peak rejection, and monitor reason-precedence regressions.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `scripts/run/pb12_diagnostic_process.ps1`
- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `docs/PLURIBUS_LOG.md`
- `tmp/pb12_diagnostic_20261029_aggregatewalfix/training.conf`
- `tmp/pb12_diagnostic_20261029_aggregatewalfix/plan.json`
- `tmp/pb12_diagnostic_20261029_aggregatewalfix/preparation_evidence.json`
- `tmp/pb12_diagnostic_20261029_verifiedprep/training.conf`
- `tmp/pb12_diagnostic_20261029_verifiedprep/plan.json`
- `tmp/pb12_diagnostic_20261029_verifiedprep/preparation_evidence.json`
- `tmp/pb12_diagnostic_20261029_verifiedprep/monitor.jsonl`
- `tmp/pb12_diagnostic_20261029_verifiedprep/training.log`
- `tmp/pb12_diagnostic_20261029_memretry/training.conf`
- `tmp/pb12_diagnostic_20261029_memretry/plan.json`
- `tmp/pb12_diagnostic_20261029_memretry/preparation_evidence.json`
- `tmp/pb12_diagnostic_20261029_memretry/monitor.jsonl`
- `tmp/pb12_diagnostic_20261029_memretry/training.log`
- `tmp/pb12_diagnostic_20261029_memretry/training.log.diagnostics.17932886830030567.jsonl`
- `tmp/pb12_diagnostic_20261029_memretry/report.json`
- `tmp/pb12_diagnostic_20261029_memretry/policy.mwfb`

### Validation

- An earlier post-fix Debug build attempt failed in `test_multiway_phase4`'s `vcpkg z-applocal` step (exit 32); the subsequent user-reported Debug build passed. The trainer hash is `6520FB95D63B013CDF54527EA902DCB09D29ECD83E6800B82CF716F9E2262A61`.
- User-reported compact CTest passed all 128 tests (`tmp/ctest_debug.log`). `LastTest.log` confirms all 61 storage cases passed, including first-record admission, aggregate-journal rollback recovery, and commit-peak rejection.
- User-reported focused PowerShell monitor checks passed.
- `verifiedprep` stopped when available physical memory reached 3,000,545,280 bytes, below the 3 GiB monitor floor. Its training log had reached batch 3/3, but the process ended with exit -1 and no final report; this attempt remains incomplete.
- The fresh `memretry` run completed with exit 0: 3/3 batches, 3,000 accepted trajectories, zero discarded, `failed=false`, and successful policy export. The monitor recorded 3 batches and checkpoints, no stop reason, and a minimum sampled available-memory value of 21.82 GiB.
- The monitor made 178 disk scans; the largest reported amplification was 1.6458x, with no disk stop. Each scan spans sequential filesystem reads, so its live/store/temporary component values are not an instantaneous breakdown. Compaction read/write amplification was 1.20128x/1.17755x.
- The journal recorded catalog run counts 3, 4, then 2; row run counts 2, 3, then 4; and page run counts 1, 2, then 3. This three-batch observation does not prove run-count growth is bounded.
- The fresh preparation evidence matches the trainer binary and Stage 9.4 bucket size/timestamp; `bucket_hash_measured=false`. The earlier `postfix`, `aggregatewalfix`, and `verifiedprep` artifacts remain preserved.
- `git diff --check` and PowerShell syntax parsing passed. No build or test was run by the assistant.

### Limitations

- The bucket content hash remains unmeasured. The P99 gate is inconclusive because three batches provide only two checkpoint cycles, short of the required three two-cycle cohorts. This diagnostic does not qualify Stage 9.4 or Stage 9.5.

## Stage 9.4 post-stop: Bound PB-12 external-store publication amplification

**Status:** Complete
**Completed:** 2026-10-29

- Added an opt-in fixed 3x bound for the PB-12 diagnostic. Append publications include WAL, segment/index temporaries, component-manifest publication, the aggregate-manifest temporary reserve, and all existing store files.
- Added exact index-only output counting and pre-publication/post-publication amplification checks for compaction. Batch suffix runs are consolidated within configured fan-in; rollback inputs remain until aggregate commit and cleanup.
- Closed compaction source streams before deleting intermediate suffix runs. Bounded cleanup errors now fail closed instead of silently leaving old run files in the disk-accounting root. The aggregate transaction still retains its rollback inputs.
- Kept the monitor's hard stop and generic external-storage defaults unchanged. Added regression cases for cap-driven chunking, rollback recovery, run-count limits, and reopen integrity.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `examples/multiway_workflow_main.cpp`
- `scripts/run/run_pb12_diagnostic.ps1`
- `tests/test_multiway_external_storage.cpp`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- User-reported Debug build passed after the source and test edits.
- User-reported compact CTest passed all 128 tests (`tmp/ctest_debug.log`); `LastTest.log` records the staged-byte recovery regression as passed.
- User reported the focused monitor checks passed. Prepared `tmp/pb12_diagnostic_20261029_postfix`; its evidence SHA-256 matches the rebuilt trainer binary and the preserved bucket size/timestamp baseline.
- No solver diagnostic job was started.

### Limitations

- The bucket content hash was not measured. The first bounded run later stopped before a completed batch; its scans do not establish workload memory or disk amplification. The exact compaction preflight adds an index-read pass; any diagnostic launch needs separate authorization.

## Stage 9.4 post-stop: Correct diagnostic startup guard

**Status:** Complete, implementation only
**Completed:** 2026-10-29

- Corrected the diagnostic monitor to allow an empty store directory during bucket-artifact loading, while retaining fail-closed checks for storage files without a training manifest.
- Preserved the stopped diagnostic and its monitor evidence.

### Files

- `scripts/run/pb12_diagnostic_process.ps1`
- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Inspected the stopped run's training and monitor logs; they show bucket loading when the guard fired.
- PowerShell syntax parsing and `git diff --check` passed. Tests were not run.

### Limitations

- The revised guard requires the focused monitor checks and integration validation.

## Stage 9.4 post-stop: Refresh diagnostic binary evidence

**Status:** Complete
**Completed:** 2026-10-29

- Preserved the original prepared diagnostic after its binary hash changed and prepared a separate directory with the current binary hash.
- Made the launcher identify a changed binary separately from a configuration mismatch.

### Files

- `scripts/run/run_pb12_diagnostic.ps1`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `tmp/pb12_diagnostic_20261029_rebuilt/training.conf`
- `tmp/pb12_diagnostic_20261029_rebuilt/plan.json`
- `tmp/pb12_diagnostic_20261029_rebuilt/preparation_evidence.json`
- `docs/PLURIBUS_LOG.md`

### Validation

- Checked the new configuration and binary hashes, launcher syntax, and `git diff --check`.
- No build, test, benchmark, or solver job was run.

### Limitations

- The complete monitor and trainer integration remains unverified.

## Stage 9.4 post-stop: Classify diagnostic headers

**Status:** Complete
**Completed:** 2026-10-29

- Added the observer and journal headers to the public CMake header list so the classification check can find them.

### Files

- `CMakeLists.txt`
- `docs/PLURIBUS_LOG.md`

### Validation

- A read-only classification scan found every `include/*.hpp` in exactly one header list.
- CMake configuration, builds, and tests were not run; the user requested separate authorization.

### Limitations

- CMake configuration and linking still require verification.

## Stage 9.4 post-stop: Bounded page publication and diagnostics

**Status:** Complete, implementation only
**Completed:** 2026-10-29

- Grouped dirty page updates within cache, WAL, segment, and publication-buffer bounds; aggregate transactions remain the commit boundary.
- Added an exclusive, synchronously flushed diagnostic journal for component publications, clean batches, and checkpoints, with timings, counters, and memory samples.
- Documented the compaction and pilot-validation limits. The Stage 9.4 hard stops are unchanged.

### Files

- `CMakeLists.txt`
- `docs/pb12_50m_memory_scaling_plan.md`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/engine/multiway_external_observer.hpp`
- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/workflow/multiway_diagnostic_journal.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/workflow/multiway_diagnostic_journal.cpp`
- `tests/test_multiway_diagnostic_journal.cpp`
- `tests/test_multiway_external_storage.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed during implementation.
- Builds, tests, benchmarks, and solver jobs were not run, as requested.

### Limitations

- The patch and journal overhead require authorized build, test, and bounded-run validation.
- A compaction call removes at most fan-in minus one active runs; run growth and pilot gates are not proven bounded.
- Smaller page-cache configurations can now be rejected because publication buffers are included in the cache budget.

## Stage 9.1 and 9.2: Pre-pilot validation gates

**Status:** Complete
**Completed:** 2026-10-29

- Completed Stage 9.1 focused validation for estimator arithmetic, bucket schema, external catalog/storage, WAL recovery, checkpoint manifests, sharded export, and workflow contracts.
- The prescribed Stage 9.2 full-build workflow passed, as confirmed by the user. Its CTest output reports 116/116 tests passing.
- Stage 9.3 was not rerun. Stage 9.4 remains pending.

### Files

- `tmp/pb12_stage91_20261029T062952Z/bucket_artifact.log`
- `tmp/pb12_stage91_20261029T062952Z/checkpoint_equivalence.log`
- `tmp/pb12_stage91_20261029T062952Z/external_storage.log`
- `tmp/pb12_stage91_20261029T062952Z/focused_build.log`
- `tmp/pb12_stage91_20261029T062952Z/memory.log`
- `tmp/pb12_stage91_20261029T062952Z/qualification_reports.log`
- `tmp/pb12_stage91_20261029T062952Z/sharded_export.log`
- `tmp/pb12_stage91_20261029T062952Z/sharded_workflow_lookup.log`
- `tmp/pb12_stage91_20261029T062952Z/sizing_report.log`
- `tmp/pb12_stage91_20261029T062952Z/solver.log`
- `tmp/pb12_stage91_20261029T062952Z/workflow_config.log`
- `tmp/build_debug.log`
- `tmp/ctest_debug.log`
- `docs/PLURIBUS_LOG.md`

### Validation

- Stage 9.1 focused suites passed; outputs are recorded in the listed run logs.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py` passed, as confirmed by the user; CTest reports 116/116 passed.

### Limitations

- Stage 9.4 and larger workloads remain pending.

## Stage 9.3: 1K deterministic differential pilot

**Status:** Complete
**Completed:** 2026-10-29

- Enabled PB-12 targeted coverage for `CompactInt32` and verified the same eight exact state/menu/seat/bucket rows before training.
- Ran one batch of 1,000 trajectories on both backends with seed 1, one fixed-scheduler worker, and the same `sizing/f1_sizing_v1` model settings. Both accepted all trajectories with zero discards.
- Public-state records, sparse rows, logical action values, per-street row counts, merge fingerprint, and policy-content hash matched. Compact checkpoint-only resume with zero added batches produced a byte-identical policy.
- External and Compact training identity hashes differ because storage backend is bound into identity; their bucket-model hashes and policy-content hashes match.

### Files

- `examples/multiway_workflow_main.cpp`
- `src/solver/multiway/workflow/multiway_pb12_coverage.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tmp/pb12_compact_reference_1k_20261029T081219/training.conf`
- `tmp/pb12_compact_reference_1k_20261029T081219/policy.mwfb`
- `tmp/pb12_compact_reference_1k_20261029T081219/report.json`
- `tmp/pb12_compact_reference_1k_20261029T081219/checkpoints/latest.bin`
- `tmp/pb12_compact_reference_1k_20261029T081219/policy_resume_verified.mwfb`
- `tmp/pb12_compact_reference_1k_20261029T081219/resume_report_verified.json`
- `tmp/pb12_compact_reference_1k_20261029T081219/comparison.md`
- `tmp/pb12_compact_reference_1k_20261029T081219/protected_before.json`
- `tmp/pb12_compact_reference_1k_20261029T081219/protected_after.json`

### Validation

- `cmake --build build-pb12 --target test_multiway_blueprint_training test_multiway_qualification_reports texas_multiway_train --parallel 2` passed.
- Focused Compact targeted-admission, External targeted-admission, and checkpoint-report tests each passed (1 test each).
- The Compact training command in `tmp/pb12_compact_reference_1k_20261029T081219/command.txt` exited 0; report has 1,000 accepted and zero discarded trajectories.
- The checkpoint-only resume command in `tmp/pb12_compact_reference_1k_20261029T081219/resume_command_verified.txt` exited 0 with zero added batches. The original and resumed Compact policies and checkpoints have identical SHA-256 hashes.
- The External manifest and Compact report show 69,204 public states, 31,078 rows, and 6,405,846 logical action values. Merge fingerprint `9806774443936985566` and policy-content hash `2488139010106883890` match.
- Pre/post SHA-256 and size snapshots matched for all 16,664 protected files, including the bucket artifact and the existing External run. `git diff --check --ignore-submodules=all` passed.

### Limitations

- The External report's `peak_public_states` is one below its committed manifest count, and its `peak_sparse_values` is unavailable as zero. The comparison uses the committed manifest for External counts and Compact `export_action_count` for logical-value parity. Backend-bound model identity and artifact payload hashes therefore are not equal; policy-content hashes are equal.
- Stage 9.4 and larger workloads were not run.

## PB-12 targeted 1K training pilot

**Status:** Complete
**Completed:** 2026-10-29

- Ran the planned one batch of 1,000 trajectories with `ExternalCompactInt32` and `--pb12-targeted-coverage`.
- Regular targeted training now runs the existing PB-12 exported-policy verifier after export. The run admitted all eight exact store keys, verified them in the policy, and replayed the captured histories.
- Training completed with 1,000 accepted trajectories, zero discards, nonzero rows on every street, and `failed=false`.

### Files

- `examples/multiway_workflow_main.cpp`
- `tmp/pb12_targeted_training_1k_capacity100m/training.conf`
- `tmp/pb12_targeted_training_1k_capacity100m/store/`
- `tmp/pb12_targeted_training_1k_capacity100m/policy.mwfb`
- `tmp/pb12_targeted_training_1k_capacity100m/report.json`
- `tmp/pb12_targeted_training_1k_capacity100m/checkpoints/latest.bin`

### Validation

- `cmake --build build-pb12 --target test_multiway_blueprint_training texas_multiway_train --parallel 2` passed.
- The focused `multiway_pb12_captured_preflop_misses_are_admitted_exported_and_replayed` test passed (1 test).
- The one-batch training command completed with exit code 0 and policy-content hash `2488139010106883890`.
- `git diff --check --ignore-submodules=all` passed.
- The bucket artifact was used as input only and remained 15,369,522,466 bytes with its existing 2026-08-31 timestamp.

### Limitations

- This is the planned 1K pilot, not a 50M qualification run.

## PB-12 targeted training admission and replay

**Status:** Complete
**Completed:** 2026-10-28

- Recovered the captured public histories from the PB-12 lookup report backup; no histories were inferred from IDs.
- Added a targeted admission API that rebuilds both captured branches, uses the normal coordinator path to add eight exact rows, and verifies each public state, seat, bucket range, and action-menu ID in External storage before export.
- Added `--pb12-targeted-coverage` for targeted rows before regular training and `--pb12-coverage-only` for zero-batch admission, export, and replay verification. Coverage-only requires a fresh External store.
- The exported policy is replay-checked against both captured branches. All eight exact provider lookups must hit and return normalized strategies.

### Files

- `CMakeLists.txt`
- `configs/multiway/pb12_targeted_coverage.conf`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/workflow/multiway_pb12_coverage.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_pb12_coverage.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Configure: `cmake -S . -B build-pb12 -G Ninja -DCMAKE_MAKE_PROGRAM="C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_TOOLCHAIN_FILE= -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON -DTEXASSOLVER_ENABLE_LOGGING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` passed.
- `cmake --build build-pb12 --target test_multiway_blueprint_training texas_multiway_train --parallel 2` passed.
- With `TEMP` and `TMP` set to `build-pb12/test-temp`, `test_multiway_blueprint_training.exe multiway_pb12_captured_preflop_misses_are_admitted_exported_and_replayed` passed (1 test).
- `texas_multiway_train.exe --config configs/multiway/pb12_targeted_coverage.conf --pb12-coverage-only` passed against the preserved bucket artifact. It reported `exact_keys=8`, `batches=0`, and produced a policy with 1,352 rows (89,416 bytes); the report records zero trajectories and `failed=false`.
- `git diff --check --ignore-submodules=all` passed.
- `artifacts/f1_sizing_v1/buckets/buckets.bin` remains present at 15,369,522,466 bytes and was read-only during the run.

### Limitations

- The default Visual Studio build in `build` was blocked by compiler error C1902; focused targets were built and verified in `build-pb12` with Clang/Ninja.

## PB-12 missing-key admission provenance

**Status:** Complete
**Completed:** 2026-10-28

- Compared all eight corrected replay keys with the external training row index, public catalog, and exported policy rows.
- All eight states are absent from both training stores and the policy. The evidence indicates a training coverage gap, not export filtering or identity loss.
- Confirmed the attempt policy and preserved blueprint have the same SHA-256; the parsed policy row count and payload hash match the export report.

### Files

- `scripts/analyze/compare_pb12_policy_admissions.py`
- `docs/PLURIBUS_LOG.md`

### Validation

- The comparison tool returned `training_coverage_gap` for all eight keys.
- The snapshot audit passed for the source and disposable copy: 16,669 files, no missing, extra, or mismatched files.
- C++ builds and tests were not run because no C++ sources changed.

### Limitations

- None identified.

## PB-12 External policy lookup replay diagnostics

**Status:** Complete
**Completed:** 2026-10-28

- Corrected lookup qualification to query the verified provider on first local visits when auditing. That result is diagnostic only; the existing uniform first-visit strategy remains unchanged.
- Added opt-in samples selected by deterministic hash priority across distinct missing policy keys, capped at 16 entries and 64 public history actions per entry. Samples include the exact policy key and the public inputs used to build its state ID, plus a reconstructed ID. Private cards are excluded.
- Training row admission and policy export use the same public-state/seat/bucket/action-menu key. The earlier 25,149 `Missing` statuses included first local visits where provider lookup was skipped, so they do not establish policy misses.
- Added `--diagnostic-limit`; zero keeps the existing report shape. Lookup fingerprints now include actual provider results on first local visits, so their values may differ from the old audit while remaining deterministic.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_policy_provider.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/workflow/multiway_lookup_qualification.hpp`
- `src/solver/multiway/workflow/multiway_lookup_qualification.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/application_and_script_io.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Read the preserved `tmp/pb12_lookup_replay_20261027/lookup_report.json` and confirmed its aggregate counts, policy hash, and equal replay fingerprints match the logged results. The old miss count is not an accurate provider-coverage measurement.
- Reviewed the training admission, export, and provider lookup key paths and the changed C++ diff.
- C++ review found no actionable issues. `git diff --check --ignore-submodules=all` passed.
- Targeted Debug build succeeded; both focused CTest targets passed (2/2). The repository `full_build.py` workflow also passed the full Debug build and all 116 CTest tests.
- Corrected replay against the preserved policy reported 39,742 hits, 8 actual missing infosets, zero missing buckets, and zero action-menu mismatches. Policy hash remained `11500758613148677101`; replay fingerprints both matched at `9162681264995464848`. The report captured all 8 distinct missing keys from trajectory 529 (lookup ordinals 21,492–21,499); every captured state ID reconstructed successfully and no private cards were included. Qualification correctly failed the zero-miss gate.
- The original adjacent `compact.cfg` was rejected because its discovery capacity exceeds the current 1,000,000-entry resource limit. Replay used the matching model-profile config at `tmp/pb12_phase9_3_1k_7e662d3c0c6f40029b3ee4cc9ec13ac0/compact.cfg`, whose capacity is valid.
- Final report: `tmp/pb12_lookup_diagnostics_20261028/lookup_report.json` (identical to `lookup_report_final.json`).

### Limitations

- The preserved replay package contains the policy and bucket artifacts but no original evaluator config or training checkpoint. The replay used a separate compact 1,000-trajectory config with the same model profile. No training checkpoint or admission catalog was found beside it, so true misses cannot be classified as unadmitted branches versus training-to-export identity problems.
