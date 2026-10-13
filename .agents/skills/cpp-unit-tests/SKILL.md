---
name: cpp-unit-tests
description: Create or update focused C++17 unit, contract, regression, deterministic, and differential tests for TexasSolver. Use when adding a C++ module, changing behavior, fixing a reported bug, freezing existing behavior before refactoring, or requesting test coverage in this repository. Create a separately runnable `tests/test_<module>.cpp` file for a new module when appropriate. Never run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks.
---

# TexasSolver C++ Unit Tests

Freeze observable behavior with small, deterministic tests. Add coverage for new behavior and every confirmed bug so that its failure cannot silently return.

## Workflow

1. Identify the changed public API or confirmed bug behavior. Read its header, implementation, nearest tests, and callers needed to define the contract.
2. Find the closest test by subsystem and follow its fixture, naming, tolerance, and exception style.
3. For a new module, add `tests/test_<module>.cpp`. For an existing module, extend its focused test file unless a distinct contract deserves its own file.
4. Cover the intended behavior, boundaries, invalid input, and regression path that matter for the change. Use the smallest deterministic fixture that proves each contract.
5. Do not run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks. State that tests were added but not run.

## Test layout and harness

- Place every normal C++ test at the top level as `tests/test_<module>.cpp`. The CMake glob is non-recursive: a nested path such as `tests/core/test_range.cpp` is not discovered and is forbidden unless the build configuration is deliberately changed.
- Each top-level `tests/test_*.cpp` source becomes its own executable and CTest entry through the CMake glob. Do not add a normal test source to `CMakeLists.txt`.
- Include the narrow production headers first and `"test_harness.hpp"`. Avoid `core/lib.hpp` unless the test explicitly covers the facade.
- Define cases with `TEST_CASE(descriptive_snake_case_name)`.
- Use `EXPECT_TRUE`, `EXPECT_EQ`, `EXPECT_NEAR`, and `EXPECT_THROW(expression, exception_type)` from `tests/test_harness.hpp`.
- Use `EXPECT_NEAR` for floating-point values and make tolerances explicit. Use exact comparison only when the contract guarantees it.
- A test executable accepts an optional test-name substring through `test_main.cpp`; keep test names specific enough to select independently.
- Keep local fixture helpers in an anonymous namespace. Do not add shared helpers until at least two test files genuinely need the same stable fixture.

## What to test

For every changed behavior, select the relevant checks:

- valid nominal result and public output shape
- lower/upper boundaries, empty or singleton inputs, and legal maximums
- invalid, duplicate, blocked, non-finite, negative, overflow, and malformed inputs where the API accepts such data
- state preservation after a rejected operation when that is a contract
- ownership, move/lifetime, serialization, and artifact identity boundaries
- deterministic repeatability with fixed configuration and seeds
- one-worker versus fixed multi-worker equivalence with an explicit exact or tolerance rule
- scalar versus SIMD or direct versus traversal differential behavior
- memory-preflight warning/rejection and no-partial-publication behavior for bounded solver paths

Do not add assertions unrelated to the contract. More coverage means more independently meaningful behavior checks, not a larger number of redundant assertions.

## Regression tests from bug reports

For each confirmed bug report:

1. Reduce the report to the smallest input, state transition, or configuration that reproduces the bad behavior.
2. Name the case after the protected behavior, not the person, ticket, or implementation detail.
3. Assert the required result or rejection, including unchanged prior state when relevant.
4. Add the regression beside the owning module or contract, for example `test_<module>.cpp` or `test_<area>_<contract>.cpp`.
5. Keep the fixture deterministic and self-contained. Do not require network, wall-clock timing, environment state, random sampling, a large solve, or test ordering.

Do not freeze an unclear or accidental behavior. If the report does not establish the intended contract, ask for that contract before writing a regression test.

## Solver-specific rules

- Preserve `HUNLFlatDCFR` behavior unless the change explicitly targets it. Add a narrow regression test before modifying a legacy solver contract.
- Use tiny games, fixed ranges, fixed seeds, limited trajectories, and bounded action menus. Do not make a unit test depend on a long solve or benchmark threshold.
- Test canonical card IDs, blocker handling, range normalization, legal actions, terminal utilities, value units, and policy normalization directly where possible.
- Test sampled paths through deterministic trajectory IDs and clean-batch boundaries. Verify worker-local merge results against a direct or one-worker reference when relevant.
- Test SIMD through the scalar reference across tails, special numeric values, and randomized-but-deterministically-generated rows.
- Treat performance and allocation tests as contract tests only when instrumentation already exists or the user asks. Never infer speed from a flaky wall-clock assertion.

## Test quality rules

- Test public behavior rather than private implementation details, except when a low-level helper is itself the intended API/contract.
- Make each test fail for one primary reason. Split unrelated scenarios into separate `TEST_CASE`s.
- Assert exact observable consequences, not merely that code does not throw.
- Avoid copying the implementation algorithm into the expected-value calculation. Prefer a hand-computed oracle, a known fixture, or an existing trusted scalar/direct path.
- Use loops only to cover a compact family of equivalent edge cases. Include the loop index or input in assertions when the harness permits a clear failure diagnosis.
- Avoid global mutable state. If a test must change an environment variable or static setting, use a local RAII guard that restores it on every exit.
- Preserve existing test names and contracts unless the requested change deliberately updates the behavior they freeze.

## Verification policy

It is forbidden to run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks.

If verification is explicitly requested, use the repository commands:

```powershell
cmake -S . -B build
cmake --build build --config Debug -- /nologo /v:q "/clp:ErrorsOnly;NoSummary"
ctest --test-dir build -C Debug --output-on-failure
```

Run only the requested scope when it is clear how to do so. Report the exact commands and results. Do not silently expand a targeted verification into a full benchmark or solver run.

## Completion report

Report:

- test files created or updated
- behaviors and regressions covered
- whether validation was not run, or the exact user-requested commands and results

Do not claim a test passes unless it was actually run.
