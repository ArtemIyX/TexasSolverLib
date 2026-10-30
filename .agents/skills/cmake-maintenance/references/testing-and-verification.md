# Testing and verification

## Registration and selection

Include CTest at the top level. It defines BUILD_TESTING and conditionally enables testing. Preserve existing project switches: TexasSolver currently requires both TEXASSOLVER_BUILD_TESTS and BUILD_TESTING. Inspect the current root before changing that contract.

Keep heavyweight, benchmark, research, dependency, and diagnostic targets out of ordinary test builds unless explicitly selected. Use descriptive test names; add LABELS, TIMEOUT, fixtures, or resource locks when actual scheduling needs justify them. Avoid timing-sensitive tests and tests requiring a live network for ordinary unit coverage.

Minimal pattern for this project's two switches:

```cmake
include(CTest)
option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" OFF)
if(BUILD_TESTING AND TEXASSOLVER_BUILD_TESTS)
    add_subdirectory(tests)
endif()
# tests/CMakeLists.txt, only if current discovery does not already add this test:
# add_executable(test_example test_example.cpp test_main.cpp)
# target_link_libraries(test_example PRIVATE TexasSolver::texas)
# add_test(NAME test_example COMMAND test_example)
```

A target name in add_test lets CMake resolve its executable; avoid hardcoded paths or suffixes. Match test framework/main ownership; do not link two test mains. For GoogleTest, gtest_discover_tests enumerates the built executable and handles parameterized tests; cross-compilation may require an emulator or another registration strategy. DISCOVERY_MODE PRE_TEST delays execution, but does not make a foreign binary executable.

## Execution boundary

Static review is allowed for an edit-only task. Do not infer authorization to run project builds/tests from this skill's presence. Do not run CMake configuration merely as a syntax checker: it can execute commands, compile probes, and populate dependencies.

When execution is explicitly requested, select checks based on the changed contract:

| Change | Relevant checks |
|---|---|
| Source registration | Affected target, source classification/boundary checks |
| Test registration | Tests OFF/ON configuration, selected test visibility and execution |
| Optional dependency | Feature OFF build without package; ON build with package |
| Install/export | Separate installed-package consumer; relocation if promised |
| OS/compiler handling | Matching OS/compiler CI jobs; inspect unsupported combinations |
| Presets | Schema compatibility, preset listing, selected configure/build/test workflow |
| Generated code | Clean generation and incremental regeneration after input changes |

Run Python through `scripts/codex_powershell.ps1`, using the available Python interpreter. Read `--help` or script arguments before assuming switches.

```powershell
# Read-only mapping:
& ./scripts/codex_powershell.ps1 python scripts/python/cmake_summary.py --target texas
# Only when full build/test execution is explicitly requested:
& ./scripts/codex_powershell.ps1 python scripts/python/full_build.py
```

At skill creation, full_build.py runs compact_build.py then compact_ctest.py and error scanners; do not assume it performs a fresh configure or tests all option combinations. Inspect current scripts/build cache. Use compact_build.py, compact_ctest.py, scan_build_errors.py, and scan_ctest_errors.py as appropriate. Do not run installs, benchmarks, or solver jobs merely because a build was authorized.

Review the diff and check whitespace. State skipped checks and actual results. Windows-only verification does not establish Linux/macOS success. Preserve existing boundary tests and add meaningful contract coverage when behavior changes warrant it; do not add tests that just mirror formatting or command spelling.

References: [CTest](https://cmake.org/cmake/help/latest/module/CTest.html), [add_test](https://cmake.org/cmake/help/latest/command/add_test.html), [test properties](https://cmake.org/cmake/help/latest/manual/cmake-properties.7.html#properties-on-tests), [GoogleTest](https://cmake.org/cmake/help/latest/module/GoogleTest.html).
