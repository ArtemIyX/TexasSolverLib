# Tools, commands, and operational boundaries

These are examples, not automatic permission to execute them. Project skills prohibit builds, tests, benchmarks, installs, and solver jobs without the user's explicit request. `cmake-maintenance` also prohibits configuration without explicit authorization. For Python in the Windows agent environment, use `scripts/codex_powershell.ps1`.

## Read-only investigation

```powershell
rg -n 'MultiwayResolver' include src tests examples
rg --files include src tests examples | rg 'multiway.*resolver'
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/cmake_summary.py --tests
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/cpp_file_summary.py include/solver/multiway/resolver/multiway_resolver.hpp
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/extract_includes.py --reverse multiway_resolver.hpp
```

Other inspection helpers include `repo_summary.py`, `extract_symbols.py`, `find_large_files.py`, and `list_recent_changes.py`. `cmake_summary.py` is a heuristic, not a CMake evaluator. Inspect its relevant inputs when dynamic targets or generator expressions matter. Search before dumping large files; exclude build, generated, vendor and artifact directories unless the task concerns them.

## Authorized build and test examples

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/full_build.py
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/compact_build.py --target texas --parallel 4
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts/python/compact_ctest.py --parallel 4
ctest --test-dir build -C Debug -R '^test_cmake_structure$' --output-on-failure
```

`full_build.py` invokes a Debug compact build followed by Debug CTest and writes logs under `tmp`; it does not configure a fresh build directory. Compact helpers default to 16 jobs. `compact_build.py` has MSBuild-oriented command arguments; inspect it before using another generator or operating system. CMake's `--config` selects a multi-config generator's build configuration; for single-config generators set `CMAKE_BUILD_TYPE` during configuration. Use `scan_build_errors.py` and `scan_ctest_errors.py` on saved logs, noting that they write reports.

`format_cpp.py file`, `folder`, and `project` modify selected files unless `--check` is given; project mode skips vendored code. CI uses clang-format 23. Run scripts under `scripts/run` and specialized build/Tracy scripts only with explicit authorization; they may launch workloads or write large outputs. `scripts/analyze` has analysis and audit tools with their own inputs and output side effects. See `docs/application_and_script_io.md` and each script's arguments before use.

For logging, `docs/texas_logging.md` explains `LoggingSession`, explicit TOML configuration, precedence, sinks, `--quiet`, and `--no-log`. Disabled logging macros do not evaluate their arguments. Keep logging and formatting out of hot paths. Historical measurements in `docs/pb9_multiway_training_scaling_findings.md` describe a particular workload and machine; do not generalize their worker count or memory peak to a different setup.
