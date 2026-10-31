# TexasSolver project guide and working practices

Snapshot: 2026-10-29, This is a static inspection, not a fresh build, test, performance measurement, or assessment of all source files. Local files are authoritative for this snapshot; the public GitHub overview can describe an older state. Recent PR pages were not accessible during this inspection, so the history section uses local commit subjects rather than claiming to summarize PR discussions.

## 1. Purpose and maturity

TexasSolver is a C++17 research library for imperfect-information poker. Its primary direction is a CPU-oriented, bounded six-player no-limit hold'em system inspired by public descriptions of Pluribus. It is not the original Pluribus implementation. The README describes the architecture as implementation-complete but strategy quality as unproven. The existence of training, search, and evaluation components does not establish production-scale coverage, calibrated abstractions, convergence, playing strength, or statistically significant wins against strong independent opponents.

The intended pipeline is:

1. Train an abstract multiway blueprint with sampled self-play.
2. Publish versioned, verified strategy artifacts and supporting bucket data.
3. Maintain public state and private-hand beliefs for every seat.
4. Build a bounded local postflop search around the observed state.
5. Evaluate depth-limited leaves with typed continuation policies.
6. Export a mixed action policy for the current private hand, with a legal fallback if admission fails.
7. Evaluate changes through controlled, reproducible comparisons.

See `README.md` for the stated implementation status and `docs/pluribus_solver_technical_report.md` for research context. Treat the latter's evidence labels and historical recommendations separately from guarantees of the current implementation.

## 2. Repository map

| Area | Responsibility | Start here |
|---|---|---|
| `include/core`, `src/core` | Common types, IDs, game interface, arenas, checked arithmetic, fingerprints, portable formats, publication, facade | `include/core/lib.hpp`, `types.hpp`, `checked_arithmetic.hpp`, `atomic_publish.hpp` |
| `include/games`, `src/games` | Rules, state, legal transitions, replay, hands, settlement | `kuhn.hpp`, `leduc.hpp`, `hunl*.hpp`, `multiway_state.hpp`, `multiway_rules.hpp`, `multiway_terminal.hpp` |
| `include/solver/generic`, `src/solver/generic` | Small-game solver APIs, DCFR and evaluation | `solver.hpp` and neighboring headers |
| `include/solver/hunl/flat` | Exact heads-up reference and compatibility path | `hunl_flat_*` |
| `include/solver/hunl/sampled` | Structured sampled heads-up construction, traversal, scheduler, storage, ranges | `hunl_sampled_*` |
| `include/solver/multiway/blueprint` | Training, policy storage, checkpoint, artifact and export | `multiway_blueprint_trainer.hpp`, `multiway_checkpoint.hpp`, `multiway_export.hpp` |
| `include/solver/multiway/session` | Beliefs, action updates, decision/search sessions | `multiway_range_belief.hpp`, `multiway_range_update.hpp`, `multiway_search_session.hpp` |
| `include/solver/multiway/resolver` | Runtime entry, budgets, policies and evaluation | `multiway_resolver.hpp`, `multiway_resolver_budget.hpp` |
| `include/solver/multiway/engine` | Traversal, scheduling and storage | `multiway_scheduler.hpp`, `multiway_external_storage.hpp` |
| `include/solver/multiway/abstraction`, `continuation`, `evaluation`, `workflow` | Action/card abstraction, leaf policies, evaluation, workflow contracts | Search the relevant public type first |
| `include/preflop`, `src/preflop` | Preflop equity and range-versus-range work | Search `preflop` or the relevant public type |
| `include/util`, `src/util` | Layout, abstraction, SIMD, profiling, suit isomorphism and supporting algorithms | Owning header and matching source |
| `tests` | C++ test executables and CMake contract tests | `tests/test_main.cpp`, matching `test_*.cpp`, `tests/cmake` |
| `examples` | Public API example and opt-in research workflows/benchmarks | `solve_kuhn.cpp`, `multiway_workflow_main.cpp` |
| `scripts` | Inspection, compact builds, run orchestration and analysis | `scripts/python`, `scripts/run`, `scripts/analyze` |
| `docs` | Technical background, logs, operational procedures and measurement reports | `PLURIBUS_LOG.md`, `application_and_script_io.md` |
| `external/pokerHandEvaluator` | Pinned third-party hand evaluator submodule | `.gitmodules`, parent CMake integration |
| `config`, `configs/multiway` | Example logging and multiway experiment inputs | `logging.example.toml`, relevant `.cfg` or `.conf` |

The navigation skill mentions `include/ranges` and some root-level solver paths that do not exist in this checkout. Range functionality is distributed among sampled HUNL and multiway session modules. Likewise, the skill references project-state/roadmap files that are absent here. Search the actual checkout before relying on an older path. `CONTRIBUTING.md` also has old include examples; check the current header name and namespace.

## 3. Architecture and contracts

### Solver families

- **Kuhn and Leduc:** small deterministic games suitable for checking solver behavior, public APIs, values and exploitability.
- **Exact HUNL:** deterministic reference and compatibility path. Preserve `HUNLFlatDCFR` behavior unless explicitly asked to change it. Do not silently move its semantics into the sampled system.
- **Sampled HUNL:** lazy, structured research path intended to avoid materializing an infeasible full graph. Use it for new scalable heads-up work when appropriate.
- **Multiway blueprint:** offline external-sampling CFR, sparse admission, regret and average-strategy storage, checkpoints and model-bound artifacts.
- **Multiway runtime:** state and belief tracking, bounded search sessions, action handling, continuation evaluation, policy export and fallback.
- **Preflop:** separate equity and range-versus-range logic.

### Beliefs and live search

The documented representation uses 1,326 canonical two-card combinations per seat. Public-card blockers eliminate incompatible hands; observed actions update beliefs using policy weights. Current-street decision rows use exact private-hand identities, while future streets can use abstract buckets. The blueprint is a read-only prior; live mutable data belongs to the bounded request/session. See the relevant session headers before changing probability normalization, blocker behavior, units or ownership.

The resolver's documented `ReleaseDefault` mode admits root external-sampling search only when its release profile, verified artifacts and required dependencies are present. Otherwise it returns a documented legal fallback. The deployment host must supply verified full and compact blueprint artifacts, a matching bucket registry, a typed terminal leaf, solver limits and memory budget. The library does not itself provide a production poker-client automation host or parse the JSON release profile. Inspect resolver status and diagnostics contracts rather than assuming every legal action came from a successful search.

Off-tree actions can be translated or admitted into the local subgame. Depth-limited evaluation uses normal, fold-heavy, call-heavy and raise-heavy continuation transformations. Changes to these choices require calibration and evaluation, not only compilation.

### Determinism, concurrency and memory

- Specify seed derivation, trajectory partitioning, worker count, update order and merge order when claiming reproducibility. A fixed seed alone is insufficient.
- Prefer worker-local scratch and deltas, then a defined deterministic merge. Avoid floating-point atomics as a substitute for an ordered reduction. Different worker counts are not automatically bitwise equivalent.
- Bound data before admission: account for rows, graph/cache, scratch, worker deltas, terminal data, export and checkpoint transients. Separate process budget and system-wide reserve; one does not enforce the other.
- Keep sparse admission and artifact publication out of traversal kernels. Validate identity, lengths, arithmetic, model provenance, paths and failure behavior at boundaries.
- Preserve the scalar reference when introducing SIMD. Compare outputs under appropriate numerical tolerances and measure before claiming a speedup.
- Treat checkpoint/export formats, checksums, immutable leases and atomic publication as compatibility contracts. Do not assume a partially written artifact is usable.

The PB-9 report records one 50,000-trajectory qualification on its specified hardware and workload: 8 workers were fastest among 2, 4, 8 and 16, and the wrapper observed about 25.3 GB peak working set during the checkpoint period. The reported payload hashes differed across worker counts. This is historical evidence for that setup, not a universal worker recommendation or memory bound; later changes may alter it.

## 4. C++ implementation conventions

Use C++17. Place game rules and state in `games`, training/traversal in the owning `solver` module, and general utilities in `core` or `util`. A suitable public module normally has a mirrored `include/.../name.hpp` and `src/.../name.cpp`, plus a focused test. Add declarations to the facade only if they belong in its public convenience API. Use actual owning namespaces from adjacent headers.

Prefer structured APIs, compact integer identifiers, explicit metadata, fixed-width storage where needed, checked arithmetic, and clear ownership. RAII containers own resources at subsystem boundaries; pointers or lightweight non-owning views pass buffers to kernels. Raw pointers must not own memory. `std::span` is a C++20 standard type, so in this C++17 project use an existing compatible view or pointer-and-size interface rather than introducing an unguarded C++20 dependency.

Traversal, row updates, terminal inner loops and merges are hot paths. Avoid strings, logging, formatting, allocations, `new`/`delete`, shared ownership, `std::function`, virtual dispatch, normal-flow exceptions, hash lookups inside per-action/per-bucket loops, and unreserved vector growth. Prefer preallocated scratch, fixed arrays for small menus, compact IDs and contiguous action-major rows `row[action][bucket]`. These are not a reason to obscure cold setup code.

For sampled traversal, preserve the distinction between sampled chance/non-traverser actions and enumerated traverser actions. Check regret, reach, average strategy and utility units together. Pay attention to legal action sets, card blockers, zero-probability ranges, terminal settlement, ties, side pots and rake. Follow nearby contracts rather than assuming all solver families use identical semantics.

## 5. CMake structure

The minimum is CMake 3.20 and the project version is 0.1.0. The root `CMakeLists.txt` owns project setup, options, explicitly classified headers and sources, and the classification validator. `cmake/TexasSolverTargets.cmake` defines targets, dependency integration and installation; `examples/CMakeLists.txt` and `tests/CMakeLists.txt` own their respective declarations; `cmake/TexasSolverConfig.cmake.in` supplies the installed package config. No root `CMakePresets.json` was found in this snapshot.

`texas` is a static library with build-tree alias `TexasSolver::texas`. It requires C++17, links Threads and `pheval`, and links `Cabinet` on Windows. Windows builds use the real abstraction source and non-Windows builds select its stub. The target carries binary provenance tokens for source commit, dirty state, compiler and configuration. Treat provenance as a diagnostic and confirm its acquisition succeeded when it matters.

`TexasSolver::types` is an installable C++17 interface target for the curated, solver-independent value headers. It has no solver, evaluator, thread, logging, or platform-library link requirements. Existing operational headers include their extracted `*_types.hpp` files, retaining their declaration namespaces and include paths. Installed `find_package(TexasSolver CONFIG REQUIRED)` still resolves Threads because the package imports the shared solver export set; use `TexasSolver::types` to compile these types without linking `TexasSolver::texas`.

The extracted headers also include `core/street_types.hpp`, `games/hunl_flat_graph_types.hpp`, `games/hunl_terminal_types.hpp`, `games/multiway_*_types.hpp`, `solver/multiway/blueprint/multiway_blueprint_config_types.hpp`, and `solver/multiway/abstraction/multiway_model_identity_types.hpp`. The value declarations and inline comparisons are available from `TexasSolver::types`; validation, graph construction and other out-of-line operations remain implemented by `TexasSolver::texas` and require that library when called.

### Option matrix

| Option | Default | Effect and constraints |
|---|---:|---|
| `TEXASSOLVER_BUILD_TESTS` | OFF | Enables test subtree only together with `BUILD_TESTING=ON` |
| `TEXASSOLVER_BUILD_HEAVY_TESTS` | OFF | Includes designated heavy solver and boundary tests; still needs test gates |
| `TEXASSOLVER_BUILD_EXAMPLES` | OFF | Builds the small example and enables the example subtree |
| `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES` | OFF | Adds multiway workflows and benchmarks; requires examples ON |
| `TEXASSOLVER_BUILD_LEGACY_RESEARCH` | OFF | Adds non-installed `TexasSolver::legacy_vector` |
| `TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH` | OFF | Adds `TexasSolver::hunl_fixed_research` and permits its gated test |
| `TEXASSOLVER_ENABLE_LOGGING` | OFF | Enables internal application logging and requires `spdlog` and `tomlplusplus`; OFF builds disabled implementation |
| `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS` | OFF | Adds separate non-installed instrumented targets; requires `external/tracy/CMakeLists.txt` |
| `TEXASSOLVER_TRACY_ON_DEMAND` | ON | Collector-connected instrumentation mode |
| `TEXASSOLVER_TRACY_SAMPLING`, `..._CALLSTACKS`, `..._LOCKS`, `..._MEMORY` | OFF | Optional diagnostic detail; verify actual target and macro wiring before assuming coverage |

The flat MCCFR research library is also constructed when tests or research examples are enabled. This does not make it part of the installed stable package. Logging is a separate internal target even in disabled mode; the installed main library does not require logging. The vendored evaluator's generic option names are saved and restored around its subdirectory; required card evaluators are enabled and unused PLO components, dependency tests and examples are disabled. Preserve parent-project option isolation when changing integration.

The install exports `TexasSolver::texas`, `TexasSolver::types`, the evaluator target and selected public headers under a relocatable CMake package. A types-only consumer can use:

```cmake
find_package(TexasSolver CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE TexasSolver::types)
```

The package still discovers Threads before importing the shared export set. Keep build/install include interfaces, transitive dependencies, platform libraries and the installed header list coherent. Validate through a separate consumer after installation and relocation when such execution has been explicitly requested.

### Source and header registration

Production sources and installed headers are explicit lists. The root checks every `src/*.cpp` and `include/*.hpp` against exactly one relevant category. A new file must be classified correctly as stable, platform, application, test support, research, compatibility or another existing category. A glob is used to *validate classification*, not to automatically add arbitrary production sources. The `cpp-worker` skill's statement that normal pairs are automatically discovered is stale here. Test sources are different: `tests/test_*.cpp` are discovered with `CONFIGURE_DEPENDS` and then gated by platform and feature conditions.

Before any CMake, package template, toolchain or preset edit, read `.agents/skills/cmake-maintenance/SKILL.md`. Preserve the minimum version and opt-in boundaries; model requirements with target-level commands and accurate `PUBLIC`/`PRIVATE`/`INTERFACE` visibility. Review package tests, consumers, Windows/Linux behavior, optional configurations and vendor option isolation. Avoid editing vendored CMake where parent integration is sufficient. A CMake configure is an execution step and may trigger dependency activity; agents need explicit authorization before running it under the skill.

## 6. Commands and execution policy

The following are examples for a developer who intends to execute them. Repository skills explicitly prohibit an agent from running builds, tests, benchmarks, installs or solver jobs without the user explicitly asking; the CMake skill also requires explicit permission for configuration. `full_build.py` does not configure a fresh tree. Use the Windows wrapper for Python scripts in this agent environment. These commands were not run for this report.

### Setup and basic builds

```powershell
git submodule update --init --recursive
cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

For single-configuration generators set `-DCMAKE_BUILD_TYPE=Debug` or `Release` at configure time; Visual Studio is multi-configuration and `--config` selects the configuration at build/test time. Use separate build directories for conflicting options, compilers, sanitizers or diagnostic instrumentation. In the Windows agent environment the documented wrapper can normalize PATH:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/full_build.py
```

`full_build.py` invokes the compact Debug build and then Debug CTest in `build`, scanning errors on failure. It assumes the existing configured build has the desired options and writes logs under `tmp`. The compact build defaults to 16 jobs; compact CTest also defaults to 16. Resource-constrained machines should choose lower parallelism on individual helpers. `compact_build.py` includes MSBuild-style arguments and its default Windows branch discovers MSBuild; inspect the helper before using it with a different generator or operating system. Standard CMake commands are more portable.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/compact_build.py --target texas --parallel 4
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/compact_ctest.py --parallel 4
ctest --test-dir build -C Debug -R '^test_cmake_structure$' --output-on-failure
```

For research executables, configure both `-DTEXASSOLVER_BUILD_EXAMPLES=ON` and `-DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON`. For heavyweight tests add `-DTEXASSOLVER_BUILD_HEAVY_TESTS=ON`. Enable logging only with an appropriate vcpkg toolchain and dependencies. Tracy requires its separate external checkout and diagnostic configuration; do not assume its presence from the evaluator submodule. CMake cache options persist within a build directory.

### Package consumption

After an authorized configure/build/install:

```powershell
cmake --install build --config Release --prefix <install-prefix>
```

Consumer CMake:

```cmake
find_package(TexasSolver CONFIG REQUIRED)
target_link_libraries(my_solver PRIVATE TexasSolver::texas)
```

Point `CMAKE_PREFIX_PATH` at the install prefix or `TexasSolver_DIR` at its package directory. A successful in-tree build alone does not prove the installed package works or relocates.

### Investigation and formatting

```powershell
rg -n 'ExactTypeOrFunction' include src tests examples
rg --files include src tests examples | rg 'multiway.*resolver'
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/cmake_summary.py --tests
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/cpp_file_summary.py include/solver/multiway/resolver/multiway_resolver.hpp
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/extract_includes.py --reverse multiway_resolver.hpp
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/format_cpp.py file src/core/example.cpp --check
```

The example formatting path is illustrative and must be replaced with an existing file. `format_cpp.py file`, `folder`, and `project` modify files unless `--check` is passed; project mode excludes vendored code. CI uses clang-format 23. Inspect proposed formatting before applying it broadly.

## 7. Script catalog and operational side effects

| Script | Purpose and notable behavior |
|---|---|
| `scripts/codex_powershell.ps1` | Runs a command after normalizing process PATH; returns its exit code. |
| `python/repo_summary.py` | Compact repository overview. |
| `python/cmake_summary.py` | Heuristic CMake summary; dynamic targets and generator expressions require reading actual CMake. |
| `python/cpp_file_summary.py` | Includes, symbols and TODOs for one file, with `--members`, `--signatures`, `--top`. |
| `python/extract_symbols.py` | Filterable symbol index, including file, module, type and public filters. |
| `python/extract_includes.py` | Include relationships, reverse dependencies, modules and missing includes. |
| `python/find_large_files.py` | File-size analysis, with exclusions and optional tracked/generated modes. |
| `python/list_recent_changes.py` | Compact Git history/status; submodule access may affect its operation. |
| `python/compact_build.py` | Executes a build and writes its full log, default `tmp/build_debug.log`. |
| `python/compact_ctest.py` | Runs Debug CTest in `build`, writes `tmp/ctest_debug.log`. |
| `python/full_build.py` | Orchestrates the two preceding scripts; failure scanners produce reports. |
| `python/scan_build_errors.py`, `scan_ctest_errors.py` | Reduce existing logs to actionable reports; write output files. |
| `python/format_cpp.py` | Check or format one file, folder or project; without `--check` changes files. |
| `scripts/build_multiway_normal.ps1`, `build_multiway_tracy.ps1` | Specialized normal/instrumented build paths; inspect parameters and outputs before execution. |
| `scripts/run` | Focused tests, worker/memory matrices, captures, PB-12 diagnostics, training and measurement. These can launch processes and create large artifacts. |
| `scripts/analyze` | Telemetry, captured traces, snapshots, qualification comparisons and PB-12 audit/export analysis. Inspect inputs, output paths and side effects. |

For full arguments, stage relationships and output contracts, use `docs/application_and_script_io.md` and the script's own parameter declarations. PB-12-specific procedures live in `docs/pb12_poststop_diagnostic_procedure.md` and `docs/md/`; they are experiment-specific, not generic defaults. Run training, capture or measurement only with explicit authorization, a known workload and checked memory/disk constraints.

## 8. Tests, CI and review

Each eligible `test_*.cpp` gets an executable with the shared `test_main.cpp`. Platform, heavy-test, logging and research gates exclude some tests. Both `BUILD_TESTING` and `TEXASSOLVER_BUILD_TESTS` must be enabled. Use `ctest --test-dir build -C Debug -N` to inspect registered tests when execution is authorized; a successful build does not establish that tests were registered. CMake contract tests cover structure, source classification, package configuration, public headers, namespaces, evaluator options and subsystem boundaries; some are heavyweight-gated.

Choose tests that exercise observable behavior: legal transitions and terminal values, determinism and replay, malformed artifacts, overflow and allocation admission, concurrent ownership, reference-versus-optimized results, and failed-publication cleanup as relevant. Test boundary and failure cases, not just implementation details. For solver changes, distinguish exact correctness, sampling reproducibility, numerical equivalence and statistical strength. A short deterministic test cannot prove convergence or performance.

Current `.github/workflows/ci.yml` runs format first, Linux Debug build, Linux Debug CTest using the saved build, Linux Release build, then Windows Release and Windows Debug builds in parallel after Linux Release. The workflow as inspected runs CTest on Linux Debug only; it does not show Windows or Linux Release CTest jobs. CI checks out recursive submodules for build jobs and uses `--no-tests=error` for its CTest job. Preserve logs and examine the first root failure, not only secondary compiler cascades.

For C++ changes use the project `cpp-worker`, `cpp-unit-tests` and `cpp-code-review` skills where applicable. Review public contract, ownership/lifetimes, determinism, concurrency, numerical semantics, memory bounds and hot-path cost. The user must explicitly request a build/test run; cite only checks actually performed. For completed implementation or repository documentation, the project log skill requests a newest-first factual entry recording files, validation and limitations.

## 9. Logging and profiling

`TexasSolver::logging` is an internal application target. `LoggingSession` manages lifetime; categories include Workflow, Solver, Storage, Checkpoint and Validation. Configuration is explicit: defaults, TOML, environment, CLI, then the workflow training audit path. No TOML file is automatically discovered. `--no-log` disables general and audit sinks; `--quiet` disables console logs and progress while preserving configured file/audit output. When logging is compiled out, macro arguments are not evaluated, so arguments must not have required side effects. See `docs/texas_logging.md` for precedence, sink behavior and progress details.

Keep diagnostic formatting out of solver inner loops. Instrumentation can change timing and memory behavior. Compare normal and Tracy builds separately, record the exact options and binary provenance, and distinguish interval throughput from end-to-end throughput. Do not infer system safety from a metric that misses checkpoint/export transients.

## 10. Working workflow

1. Read `AGENTS.md` and `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`; load the applicable project skills. Establish whether a task authorizes execution, Git mutation or only inspection.
2. Search the exact type or behavior. Read the public header, implementation, corresponding tests and a real call site. Expand to ownership, concurrency or serialization boundaries as needed.
3. Identify public API, semantics, units, determinism, error and memory contracts before editing. Preserve unrelated user work and opt-in target boundaries.
4. Make the smallest coherent change. Register any new source/header explicitly and classify it. Add meaningful tests appropriate to behavior and risk.
5. Review the diff and architecture boundaries. If execution was expressly requested, start with the narrowest relevant test, then broaden according to risk and user scope. Record configurations, commands, outcomes and failures honestly.
6. After completed implementation/documentation, update `docs/PLURIBUS_LOG.md` using the project skill. Do not claim that unrun tests passed.
7. Commit only when asked. The git skill requests scoped staging and a branch-derived subject such as `pb-1 Multiway: Add ...`; never discard or stage unrelated changes.

The repository workflow prefers targeted searches, compact outputs and existing scripts. This saves time but does not justify skipping relevant correctness, ownership or public-package context.

## 11. Recent change context and source reliability

Local commit subjects include PR #30 on targeted coverage and bounded external storage; #29 on sequential checks and contribution templates; #28 on disk-backed multiway storage and bounded policy export; #27 on excluding vendored code from formatting; #26 on ownership, arithmetic, paths, regex and Tracy security remediation; #21 on deterministic multiway qualification and scaling; and #20 on cross-platform build checks and formatting. Subsequent local commits add the CMake maintenance skill, modularize target declarations and trim historical log entries. These are commit-subject summaries, not independent verification of full PR scope or merged state beyond local history.

The current `docs/PLURIBUS_LOG.md` records earlier Windows/MSVC configuration, full-build/test, installation and relocated-consumer checks for the CMake refactor, including a transient PDB mismatch and later successful runs. Those are prior reported results, not results from this report. Exact CMake 3.20, Linux and macOS were listed as limitations for that particular work.

The working tree contained pre-existing untracked `.pr-drafts/`. Ordinary Git status encountered a permission error when inspecting the evaluator submodule; status with submodule inspection disabled showed the untracked directory. This means the submodule's own worktree state was not assessed.

## 12. Primary references

- `README.md`: project goals, current status, requirements, build and public API.
- `AGENTS.md`, `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`: agent workflow and hot-path constraints.
- `.agents/skills/project-navigation/SKILL.md`, `cpp-worker/SKILL.md`, `cpp-unit-tests/SKILL.md`, `cpp-code-review/SKILL.md`, `cmake-maintenance/SKILL.md`, `git-commit-hygiene/SKILL.md`, `update-project-log/SKILL.md`: task-specific guidance. Verify historical path and discovery claims against current files.
- `CMakeLists.txt`, `cmake/TexasSolverTargets.cmake`, `tests/CMakeLists.txt`, `examples/CMakeLists.txt`, `cmake/TexasSolverConfig.cmake.in`: actual build and package contracts.
- `docs/application_and_script_io.md`, `docs/texas_logging.md`: operational input/output and logging reference.
- `docs/PLURIBUS_LOG.md`, `docs/pb9_multiway_training_scaling_findings.md`: historical changes and bounded evidence.
- `docs/pluribus_solver_technical_report.md`: broader research context and explicit evidence labels.
- `.github/workflows/ci.yml`, `.github/pull_request_template.md`, `.github/ISSUE_TEMPLATE`: present CI and contribution workflow.
- [Public repository](https://github.com/ArtemIyX/TexasSolverLib) and [closed pull requests](https://github.com/ArtemIyX/TexasSolverLib/pulls?q=is%3Apr+state%3Aclosed): external context; PR pages were unavailable during this inspection.
