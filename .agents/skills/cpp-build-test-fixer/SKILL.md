---
name: cpp-build-test-fixer
description: Build, diagnose, repair, and retest TexasSolver C++17 code. Use when the user asks to fix current compilation errors, failing CTest cases, or both. Run the mandated silent Debug build first, use cpp-worker before editing production C++ code, run compact CTest only after a successful build, keep tests intact while fixing product defects, and report every verified fix.
---

# TexasSolver Build and Test Fixer

Use this skill only when the user asks to build, test, diagnose, or repair failures. Invocation authorizes the commands below.

## Repair loop

1. Inspect the worktree and preserve unrelated user changes.
2. Run the required silent build from the repository root:

   ```powershell
   cmake --build build --config Debug --parallel -- /nologo /v:q "/clp:ErrorsOnly;NoSummary"
   ```

3. If compilation fails, read the diagnostics and affected declarations/callers. Apply `$cpp-worker` before editing production C++. Fix the root cause, then rerun the same build command.
4. Do not run CTest until the build succeeds.
5. Run the compact CTest runner:

   ```powershell
   python .agents/skills/cpp-build-test-fixer/scripts/ctest_compact.py --build-dir build --configuration Debug
   ```

6. If a test fails, inspect its assertion, protected contract, and production code. Apply `$cpp-worker` before changing production code. Fix production code, rerun the build, then rerun compact CTest.
7. When a production bug is fixed and no focused test protects it, apply `$cpp-unit-tests` to add a regression test. Do not rewrite a failing test to conceal a product defect.
8. Finish only after the build and compact CTest runner succeed, or stop with a concrete blocker.

## Command and scope rules

- Use the exact build command above. It emits compiler errors without normal build noise.
- Do not configure, delete, regenerate, or modify `build` unless the user explicitly requests it. Report a missing or unusable build directory as a prerequisite.
- Do not run benchmarks, installs, examples, or solver jobs.
- Rerun compilation after each logical code fix. Rerun compact CTest only after successful compilation.
- Do not change compiler flags, suppress diagnostics, skip tests, alter CTest registration, or lower assertions to obtain a green result.
- Never revert unrelated user work. Retain a change that fixes the cited root cause; add a new change only for a separate demonstrated failure.
- If the same diagnostic remains after three evidence-based attempts, stop and report the diagnostic, attempted fixes, and required decision.

## Failure triage

### Build failure

- Group duplicate diagnostics by root cause. Fix the earliest causal error first.
- Read the relevant header, implementation, and immediate callers before patching.
- Respect C++17, ownership, solver contracts, hot-path rules, and project style through `$cpp-worker`.
- Keep test compilation failures distinct from product compilation failures. Do not change test source unless it is independently proven invalid and the user authorizes that correction.

### Test failure

- Treat the existing assertion as the expected behavior unless the user explicitly changes the contract.
- Use compact output to identify the CTest target, GoogleTest or harness case, source location, and assertion/exception.
- Reproduce the failure through the smallest relevant code path. Fix production code rather than adjusting expected values, tolerances, timeouts, seeds, or test selection.
- For a new production defect found during repair, add a minimal deterministic regression test through `$cpp-unit-tests` when coverage is absent.
- Keep `HUNLFlatDCFR` behavior intact unless the failure is explicitly in scope. Preserve deterministic worker and scalar/SIMD contracts.

## Compact CTest runner

`scripts/ctest_compact.py` captures `ctest --output-on-failure` and prints only failed cases. It extracts CTest targets, GoogleTest cases, the repository's custom `[FAIL]` cases, source locations when present, and concise assertion text. It does not require JUnit support.

Use `--input-file <path>` only to parse an existing captured CTest transcript without invoking CTest. Normal repair work must use default run mode.

The runner prints `ALL TESTS PASSED` on success. A nonzero exit code means CTest itself failed or a test failed.

## Completion report

Report each root cause fixed, affected files, any regression test added, the exact passing build and CTest commands, and any unresolved blocker. Do not claim success unless both commands passed in the current repair loop.
