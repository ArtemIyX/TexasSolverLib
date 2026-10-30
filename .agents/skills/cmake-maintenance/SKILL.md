---
name: cmake-maintenance
description: Create, modify, troubleshoot, or review TexasSolver CMakeLists.txt files, CMake modules and package templates, toolchain files, and CMake presets. Use before every CMakeLists.txt edit, including source registration, test registration, dependency linking, platform conditions, and install/export changes.
---

# CMake Maintenance

Use before editing any repository CMakeLists.txt. Also use for related `.cmake`, `.cmake.in`, and CMake preset changes. Apply to requested build-system reviews without making edits. Merely reading C++ code does not require this skill.

## Essential rules

- Preserve the requested scope, existing target names, install interfaces, default options, and C++17 compatibility unless the requested behavior requires a change.
- Read the current `cmake_minimum_required()` and check feature/preset compatibility against that baseline. Do not raise it silently or copy features from latest documentation without checking introduction versions.
- Model requirements on targets. Prefer `target_sources`, `target_compile_features`, `target_include_directories`, `target_compile_definitions`, and `target_link_libraries` over global flags and directory-wide settings.
- Choose PUBLIC, PRIVATE, and INTERFACE from the actual consumer contract. Do not fix missing transitive dependencies by exposing everything publicly.
- Keep production, tests, examples, research, benchmarks, and diagnostics independently selectable. Do not enable expensive or optional workloads as a side effect of registering a source.
- Do not edit vendored CMake files or update submodules unless necessary for the user's task. Adapt integration in the parent where practical.
- Do not configure, build, run tests, install, fetch dependencies, or execute solver jobs unless the user explicitly requests the relevant execution. Configuration can itself download dependencies or execute programs. Static inspection is the default for an edit-only request.
- This skill grants no permission to commit, push, publish, or change unrelated configuration.

## Work procedure

1. Inspect repository instructions, the affected target and its consumers, relevant options, package templates, and existing CMake boundary tests. Use targeted `rg` searches. The read-only `scripts/python/cmake_summary.py` can help, but its regex summary is not an evaluator and may miss dynamic target names and generator expressions.
2. Read only the supporting guidance needed for this change:
   - [Targets and modules](references/targets-and-modules.md): sources, visibility, library boundaries, options, generated code.
   - [Dependencies and packages](references/dependencies-and-packages.md): external linking, vendoring, downloads, static dependencies, install/export contracts.
   - [Testing and verification](references/testing-and-verification.md): test registration, heavy tests, scripts, execution authorization, validation matrix.
   - [Platforms and presets](references/platforms-and-presets.md): OS/compiler distinctions, configurations, cross-compilation, preset examples.
   - [Sources and large projects](references/sources-and-projects.md): authoritative documentation and upstream examples; open the particular source when exact behavior is uncertain.
3. Make the smallest coherent change. Splitting CMake files is justified by ownership or readability, not by a mandatory file-size limit. Preserve dependency order and directory scope when moving declarations.
4. Review target reachability, transitive usage requirements, optional configurations, Windows/Linux/macOS implications, install paths, and minimum-version compatibility. Examine existing contract tests before changing public target/package behavior.
5. Run authorized checks only. For Python scripts use `scripts/codex_powershell.ps1`. When full build/test verification is explicitly requested, use `scripts/python/full_build.py`; inspect its actual behavior rather than assuming it configures a fresh tree. Use narrow checks where appropriate.
6. Report what changed and why, exactly what verification ran, and any remaining platform/package uncertainty. Use the project `update-project-log` skill after completed changes. Do not claim execution success from static inspection.

## Project facts to recheck before use

At skill creation, the project requires CMake 3.20, exports `TexasSolver::texas`, uses C++17, links Threads and a vendored evaluator, and gates ordinary tests with both `TEXASSOLVER_BUILD_TESTS` and `BUILD_TESTING`. Heavy tests, research, examples, logging, and Tracy have separate options. These are navigation hints, not frozen design requirements; inspect current files before acting.

## Invocation examples

- "Add a new solver source to CMakeLists.txt": load this skill, inspect the existing source classification and target, register it without broad restructuring.
- "Add a test executable": load this skill, preserve test opt-in and platform gating, inspect how tests are currently discovered before duplicating registration.
- "Fix the installed package on Linux": load this skill, trace exported dependencies and relocation, then propose or run only authorized consumer verification.
- "Review this CMake diff": load this skill, inspect contracts, report actionable findings, and leave files unchanged.
