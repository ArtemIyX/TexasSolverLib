# CMake, package, and test boundaries

Before changing a `CMakeLists.txt`, `.cmake`, `.cmake.in`, toolchain, or preset, read `.agents/skills/cmake-maintenance/SKILL.md`. CMake configuration itself requires explicit authorization under that skill; it may execute programs or obtain dependencies. Inspect current files and options before acting.

## Structure

- Root `CMakeLists.txt`: CMake 3.20 baseline, C++ project, options, explicit production/header category lists and classification checks.
- `cmake/TexasSolverTargets.cmake`: library targets, evaluator integration, optional research/diagnostics, and install/export rules.
- `cmake/TexasSolverConfig.cmake.in`: installed package configuration.
- `tests/CMakeLists.txt` and `examples/CMakeLists.txt`: independently gated test and example targets.

`TexasSolver::texas` aliases the static `texas` target and is the installed consumer API. The evaluator submodule, Threads, platform selection, include interfaces, C++17, and exported dependencies matter. Preserve `PUBLIC`, `PRIVATE`, and `INTERFACE` contracts and avoid accidental inclusion of research, application, compatibility, or diagnostic files in the installed library. Prefer parent-side integration to unnecessary vendor changes.

Each `src/*.cpp` and `include/*.hpp` must be in exactly one root classification list. A source glob checks classification, not automatic production registration. The `cpp-worker` skill's automatic source-discovery statement is stale in this checkout. Tests differ: `tests/test_*.cpp` use `CONFIGURE_DEPENDS`, with feature and platform gates and shared `test_main.cpp`.

## Option routing

Recheck definitions in root CMake. Defaults are OFF except Tracy on-demand, which is ON.

| Options | Meaning |
|---|---|
| `TEXASSOLVER_BUILD_TESTS`, `BUILD_TESTING` | Both must be ON for test registration. |
| `TEXASSOLVER_BUILD_HEAVY_TESTS` | Includes gated heavy tests when test registration is enabled. |
| `TEXASSOLVER_BUILD_EXAMPLES`, `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES` | Research examples require examples ON. |
| `TEXASSOLVER_BUILD_LEGACY_RESEARCH`, `TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH` | Enable separate research libraries; fixed HUNL also gates its associated test. |
| `TEXASSOLVER_ENABLE_LOGGING` | Internal logging implementation; enabled mode needs `spdlog` and `tomlplusplus`. |
| `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS` | Separate non-installed diagnostic targets; requires `external/tracy`. |
| `TEXASSOLVER_TRACY_ON_DEMAND`, `TEXASSOLVER_TRACY_SAMPLING`, `TEXASSOLVER_TRACY_CALLSTACKS`, `TEXASSOLVER_TRACY_LOCKS`, `TEXASSOLVER_TRACY_MEMORY` | Diagnostic options. Inspect actual target and macro wiring before assuming coverage. |

For package edits inspect `tests/cmake` boundary tests, install and transitive dependency behavior, and relocation. For test edits inspect existing gates, support sources and target links before adding special cases. CTest normally runs only the configured tests; inspect registration rather than assuming every optional test is present.

CI currently checks formatting, Linux Debug build and CTest, Linux Release build, and Windows Debug/Release builds. Inspect `.github/workflows/ci.yml` for its current sequence and exact coverage; a build job is not evidence its tests ran.
