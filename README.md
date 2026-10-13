# TexasSolver: Pluribus-Style Poker Solver

TexasSolver is a C++17 research library for imperfect-information poker solving. Its primary direction is a six-player no-limit Texas hold'em solver inspired by the public Pluribus architecture:

```text
offline sampled blueprint training
    -> verified strategy artifacts
    -> public-state range tracking
    -> bounded nested postflop search
    -> continuation evaluation
    -> mixed policy export for the current hand
```

The repository also contains deterministic small-game solvers, exact heads-up no-limit reference paths, sampled HUNL infrastructure, preflop tools, abstractions, artifact formats, evaluation adapters, and extensive contract tests.

This is a reconstruction based on public research. It is not the original Pluribus source code and does not claim algorithmic identity or proven superhuman playing strength.

## Project goal

The main goal is to build a reproducible, bounded, CPU-oriented Pluribus-style system that can:

- train a broad six-player blueprint through self-play;
- represent private-card beliefs over all 1,326 two-card combinations;
- refine the blueprint with range-aware public-state search;
- handle actions outside the blueprint abstraction;
- evaluate depth-limited leaves with continuation policies;
- preserve deterministic replay, artifact identity, and memory limits;
- export a mixed action policy for the actual private hand;
- support statistically sound comparison of blueprint-only and searched play.

The project deliberately does not pursue a complete exact solution of six-player no-limit hold'em. That game is too large to enumerate and repeatedly traverse as a full exact tree. The scalable path uses sampling, sparse storage, abstraction, and local search. Existing exact HUNL code remains a deterministic reference and compatibility path.

## Current status

The main software architecture is implemented.

Current maturity is **implementation-complete, strategy-quality unproven**.

Implemented:

- six-player rules, replay, chance, settlement, rake, side pots, and terminal utilities;
- canonical private-hand IDs and fixed-size range beliefs;
- sampled CFR blueprint training, sparse rows, checkpoints, and artifacts;
- action abstraction, pseudo-harmonic translation, and local off-tree expansion;
- exact current-street private-hand rows and future-street bucket artifacts;
- verified blueprint priors and model-bound artifact loading;
- bounded root external-sampling search with deterministic worker merging;
- street and action rerooting with posterior range transfer;
- normal, fold-heavy, call-heavy, and raise-heavy continuation policies;
- staged memory admission, deadlines, cancellation, and fallback behavior;
- differential evaluation adapters and AIVAT-compatible records.

Not yet established by repository evidence:

- a production-scale six-player blueprint;
- calibrated production abstractions and continuation settings;
- target-hardware latency and memory budgets;
- end-to-end playing strength, convergence, or low exploitability;
- statistically significant cross-play results against independent strong agents.

The latest implementation validation on 2026-08-31 passed the Debug build,
research workflow targets, and all 107 registered tests. This does not qualify
the production F1 artifacts or replace the required human execution evidence.

See [project state](docs/project_state.md) for the current evidence-based status and [the progress log](docs/PLURIBUS_LOG.md) for implementation history.

## How the Pluribus path works

### 1. Offline blueprint

The blueprint trainer performs sampled self-play over an abstracted multiway game. It uses external-sampling CFR, regret matching, linear strategy averaging, sparse row admission, deterministic seeds, checkpointing, and versioned model identities. The resulting artifact supplies broad strategy coverage for runtime search.

### 2. Public-state beliefs

At runtime, each seat has a fixed 1,326-entry range over canonical two-card combinations. Public cards remove blocked hands. Observed actions update the remaining probabilities with Bayes-style policy weighting. A search root therefore represents a public state plus beliefs over hidden hands, not only the bot's actual hand.

### 3. Nested search

The resolver constructs a request-local postflop search session. Search keeps exact canonical-hand decision rows on the current street and uses abstract future buckets below that boundary. Non-traversing players follow the verified blueprint prior. Worker-local deltas are merged in a fixed order for deterministic results.

### 4. Off-tree actions

Small deviations can be mapped with pseudo-harmonic action translation. Larger or strategically important deviations can be added to the local subgame. This prevents the offline action abstraction from becoming a rigid runtime action set.

### 5. Continuation values

Depth-limited leaves use typed continuation policies instead of solving the remainder of the game exactly. Normal, fold-heavy, call-heavy, and raise-heavy transforms are available, with model-bound caching and calibration contracts.

### 6. Policy export and fallback

The resolver exports a range-wide policy and selects the row for the actual private hand. Search is admitted only when artifact identities, ranges, root state, dependencies, memory, and time limits are valid. Otherwise it uses stable-root, blueprint, or static-legal fallback behavior.

`MultiwayResolverSearchMode::ReleaseDefault` is the normal release mode. Legacy and shadow modes remain for compatibility and evaluation.

## Solver families

| Family | Purpose | Main location |
|---|---|---|
| Kuhn and Leduc | Small deterministic games for API examples, CFR behavior, and exploitability checks | `include/solver/generic`, `src/solver/generic`, `include/games` |
| Exact HUNL | Deterministic heads-up postflop reference and compatibility path | `include/solver/hunl/flat`, `src/solver/hunl/flat` |
| Sampled HUNL | Structured, lazy, memory-bounded heads-up research path | `include/solver/hunl/sampled`, `src/solver/hunl/sampled` |
| Multiway blueprint | Offline six-player sampled CFR training and artifact production | `include/solver/multiway/blueprint`, `src/solver/multiway/blueprint` |
| Multiway runtime | Range tracking, traversal, scheduling, resolver lifecycle, and policy export | `include/solver/multiway`, `src/solver/multiway` |
| Preflop | Equity tables and range-versus-range solving | `include/preflop`, `src/preflop` |

Keep exact `HUNLFlatDCFR` behavior intact unless a task explicitly targets it. New scalable HUNL work should normally use the sampled or lazy modules.

## Repository layout

| Path | Purpose |
|---|---|
| `include/core`, `src/core` | Shared IDs, game interfaces, arenas, fingerprints, serialization, and public facade |
| `include/games`, `src/games` | Kuhn, Leduc, HUNL, and multiway rules and state |
| `include/solver`, `src/solver` | Generic, exact HUNL, sampled HUNL, and multiway solving systems |
| `include/preflop`, `src/preflop` | Preflop equity and range-versus-range tools |
| `include/util`, `src/util` | Abstraction, layouts, profiling, SIMD, suit isomorphism, and low-level helpers |
| `tests` | Unit, regression, deterministic, differential, package, and architecture-boundary tests |
| `examples` | Small API examples, multiway workflow entry points, and research benchmarks |
| `scripts` | Compact repository, build, test, Git, and source-inspection helpers |
| `docs` | Project state, Pluribus technical background, roadmap history, and migration notes |
| `external` | Vendored third-party dependencies with their own licenses and upstream history |
| `.agents/skills` | Repository-specific instructions for LLM coding agents |
| `artifacts` | Local generated solver artifacts and experimental outputs; not part of the installed API |

Public headers and source files use mirrored ownership paths. The stable installed library is `TexasSolver::texas`. Research and compatibility targets are opt-in and are not silently added to the installed public surface.

## Requirements

- CMake 3.20 or newer
- a C++17 compiler
- thread support
- Git submodule support
- Python 3 for repository helper scripts

No vcpkg packages are currently required. The fixed-size poker hand evaluator is included as a Git submodule.

## Clone

```bash
git clone --recursive <repository-url>
cd TexasSolver
```

For an existing clone:

```bash
git submodule update --init --recursive
```

## Build

Minimal library build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Build examples:

```bash
cmake -S . -B build -DTEXASSOLVER_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

Build tests:

```bash
cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

On this repository's Windows agent environment, run commands through the PATH-normalizing wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 cmake --build build --config Debug
```

### CMake options

| Option | Default | Purpose |
|---|---:|---|
| `TEXASSOLVER_BUILD_TESTS` | `OFF` | Build and register the normal test suite |
| `TEXASSOLVER_BUILD_HEAVY_TESTS` | `OFF` | Register heavyweight boundary and solver tests |
| `TEXASSOLVER_BUILD_EXAMPLES` | `OFF` | Build the small public example |
| `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES` | `OFF` | Build multiway workflows and benchmark executables; requires examples |
| `TEXASSOLVER_BUILD_LEGACY_RESEARCH` | `OFF` | Build the legacy vector DCFR reference library |
| `TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH` | `OFF` | Build fixed-private sampled HUNL reference kernels |

## Use as a CMake package

Install the library:

```bash
cmake --install build --config Release --prefix <install-prefix>
```

Consume it from another CMake project:

```cmake
find_package(TexasSolver CONFIG REQUIRED)
target_link_libraries(my_solver PRIVATE TexasSolver::texas)
```

The convenience facade is `include/core/lib.hpp`. Internal solver, storage, scheduler, artifact, and resolver types should be included from their owning subsystem headers.

Minimal Kuhn example:

```cpp
#include "solver/generic/solver.hpp"

#include <iostream>

int main() {
    const auto result = texas::solve_kuhn(10, 1.5, 0.0, 2.0);
    std::cout << result.game_value << '\n';
}
```

The full example is [examples/solve_kuhn.cpp](examples/solve_kuhn.cpp).

## Examples and workflow executables

With `TEXASSOLVER_BUILD_EXAMPLES=ON`, `texas_solver_example` demonstrates the small public API.

With `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON`, CMake also creates:

- `texas_multiway_train`
- `texas_multiway_buckets`
- `texas_multiway_inspect`
- `texas_multiway_evaluate`
- `texas_multiway_finalize`
- HUNL sampled, scaling, backend-comparison, and scheduler benchmarks

The multiway executables currently expose a bounded `--tiny` artifact-pipeline qualification path and validate workflow arguments. They are research entry points, not a complete production command-line deployment host. Production integration must own artifact paths, request construction, deadlines, protected evaluation records, and environment-specific game I/O.

## Why `external/` exists

`external/pokerHandEvaluator` is the [HenryRLee/PokerHandEvaluator](https://github.com/HenryRLee/PokerHandEvaluator) Git submodule. TexasSolver uses its C++ `pheval` target for fast fixed-size 5-card, 6-card, and 7-card hand ranking.

It is kept under `external/` because:

- it is third-party code, not TexasSolver-owned implementation;
- pinning a submodule revision makes evaluator behavior reproducible;
- the solver can build without requiring a separately installed evaluator package;
- its upstream history and Apache-2.0 license remain clearly separated from the repository's MIT-licensed code;
- CMake can disable the dependency's tests, examples, and unused PLO targets while linking only the required evaluator.

Do not casually edit vendored code. Prefer an upstream fix or a deliberate submodule revision update. Review both [the evaluator documentation](external/pokerHandEvaluator/README.md) and [its license](external/pokerHandEvaluator/LICENSE) when changing this boundary.

## Scripts

The `scripts/` directory exists to keep repository inspection and verification compact. These tools reduce large trees, source files, compiler output, and test logs into focused summaries suitable for both developers and LLM agents.

| Script | Use case |
|---|---|
| `codex_powershell.ps1` | Normalize duplicate or malformed Windows `PATH` variables before executing a command |
| `repo_summary.py` | Print a compact TexasSolver repository map, module counts, entry points, public types, and large files |
| `cmake_summary.py` | Summarize CMake targets, links, includes, options, and registered tests |
| `cpp_file_summary.py` | Inspect includes, namespaces, types, functions, members, and TODOs in one C++ file without dumping bodies |
| `extract_symbols.py` | Build a compact repository-wide C++ symbol index |
| `extract_includes.py` | Show source dependency and import relationships |
| `find_large_files.py` | Find unexpectedly large files while excluding build, vendor, artifact, and generated directories |
| `list_recent_changes.py` | Summarize recent commits and current working-tree changes |
| `compact_build.py` | Run the standard compact Debug build, save the full transcript, and print useful failures |
| `compact_ctest.py` | Run CTest, save the full transcript, and print failed tests and actionable lines |
| `scan_build_errors.py` | Reduce an existing build, test, or runtime log to unique actionable failures |
| `scan_ctest_errors.py` | Deduplicate the repository test harness's `[FAIL]` lines |
| `full_build.py` | Run the repository's full build and test workflow through the compact helpers |

Examples:

```bash
python scripts/repo_summary.py
python scripts/cmake_summary.py
python scripts/cpp_file_summary.py include/solver/multiway/resolver/multiway_resolver.hpp
python scripts/list_recent_changes.py
```

Build and test scripts execute real commands. Repository agents must not run builds, tests, benchmarks, installs, or long solver jobs unless the user explicitly requests them.

## Why `.agents/skills/` exists

`.agents/skills/` contains small, version-controlled instruction packages for LLM coding agents. Each skill has a `SKILL.md` with its trigger, workflow, architectural rules, and expected output. Keeping these instructions in the repository makes agent behavior repeatable and gives new contributors a direct way to request the right workflow.

| Skill | When to request it |
|---|---|
| `project-navigation` | Locate the correct subsystem, API, implementation, tests, and file placement |
| `cpp-worker` | Implement or optimize C++17 code while preserving solver and hot-path constraints |
| `cpp-unit-tests` | Add focused unit, contract, regression, deterministic, or differential tests |
| `cpp-code-review` | Review selected C++ changes for correctness, concurrency, performance, and contracts |
| `implementation-workflow` | Coordinate a complete C++ implementation from navigation through tests, review, logging, and commit |
| `git-commit-hygiene` | Inspect Git state, review diffs, stage files, or create a clean commit |
| `update-project-log` | Record completed work, actual verification, changed files, and limitations |

Agents that support repository-local skills can be prompted explicitly:

```text
Use $project-navigation to locate the multiway resolver admission path.
Use $cpp-worker and $cpp-unit-tests to add a deterministic range-update contract.
Use $cpp-code-review to review the current multiway traversal diff.
Use $implementation-workflow to implement this C++ change end to end.
Use $git-commit-hygiene to review and commit only the files from this task.
```

Start an agent session from the repository root so it can discover `AGENTS.md` and `.agents/skills/`. Explicitly naming a skill is preferred when a task should follow that workflow. Agents must still obey the user's scope and the repository hard rules.

## Development rules

Read [AGENTS.md](AGENTS.md) before agent-assisted changes and [CONTRIBUTING.md](CONTRIBUTING.md) before contributing code.

Important constraints:

- use C++17;
- preserve deterministic exact-mode behavior;
- keep `HUNLFlatDCFR` compatible unless explicitly targeted;
- use sampled or lazy modules for large new HUNL work;
- keep allocation, strings, logging, hash lookup, virtual dispatch, and shared ownership out of traversal and merge inner loops;
- own memory with RAII containers at subsystem boundaries and pass non-owning pointer or span views into hot kernels;
- keep scalar validation paths when adding SIMD;
- do not add poker-client automation, screen scraping, clicking, stealth, evasion, or account/session code;
- do not modify unrelated dirty files;
- do not claim strategic strength from unit-test success.

The token-efficient repository workflow is documented in [CODEX_TOKEN_EFFICIENT_WORKFLOW.md](CODEX_TOKEN_EFFICIENT_WORKFLOW.md). It favors search, focused file ranges, compact helper output, diffs, and narrow verification.

## Documentation

- [Current project state](docs/project_state.md): authoritative maturity, subsystem coverage, remaining gaps, and recommended next work.
- [Pluribus technical report](docs/pluribus_technical_report.md): public research background and reconstructed requirements.
- [Pluribus progress log](docs/PLURIBUS_LOG.md): newest-first implementation, audit, validation, and limitation history.
- [Solver layout migration plan](docs/solver_layout_migration_plan.md): ownership and path migration record.
- [Contributing guide](CONTRIBUTING.md): code organization, build, tests, and contribution expectations.

## Recommended next milestones

1. Freeze a versioned experimental configuration and artifact identity.
2. Generate and verify a small end-to-end blueprint.
3. Compare blueprint-only and searched self-play through the evaluation boundary.
4. Calibrate action abstraction, future buckets, continuation policies, and off-tree thresholds.
5. Scale training while tracking coverage, checkpoint equivalence, memory, and deterministic replay.
6. Profile `ReleaseDefault` on target CPU and memory limits.
7. Promote a configuration only after statistical policy-quality and rollback gates pass.

## Scope and safety

TexasSolver provides solver and research library code. It does not provide poker-client control, screen scraping, automated clicking, account management, session handling, stealth, or evasion. A deployment host may integrate the library with an authorized game or evaluation environment, but that host is outside this repository.

Passing tests establishes implementation contracts. It does not establish convergence, Nash equilibrium in six-player poker, profitability, or professional-level play.

## License

TexasSolver is licensed under the [MIT License](LICENSE). Third-party code under `external/` retains its own license.
