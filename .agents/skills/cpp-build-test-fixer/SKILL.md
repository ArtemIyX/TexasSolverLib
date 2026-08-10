---
name: cpp-build-test-fixer
description: Build, diagnose, repair, and retest TexasSolver C++17 code. Use when the user asks to fix current compilation errors, failing CTest cases, or both. Run the mandated silent Debug build first, use cpp-worker before editing production C++ code, run compact CTest only after a successful build, keep tests intact while fixing product defects, and report every verified fix.
---

# TexasSolver Build and Test Fixer

Use this skill only when the user asks to build, test, diagnose, or repair failures. Invocation authorizes the commands below.

## Repair loop

1. Inspect the current worktree and preserve unrelated user changes.
2. Run the required silent build from the repository root:

   ```powershell
   cmake --build build --config Debug --parallel -- /nologo /v:q "/clp:ErrorsOnly;NoSummary"
   ```

3. If compilation fails, read the compiler diagnostics and the affected declarations/callers. Apply `$cpp-worker` before editing C++ production code. Fix the root cause with the narrowest correct change, then rerun the same build command.
4. Do not run CTest until the build succeeds.
5. After a successful build, run the bundled compact runner:

   ```powershell
   python .agents/skills/cpp-build-test-fixer/scripts/ctest_compact.py --build-dir build --configuration Debug
   ```

6. If a test fails, inspect the reported assertion, the test's protected contract, and the production code. Apply `$cpp-worker` before changing production code. Fix product code first and rerun the build, then the compact CTest runner.
7. When a production bug is fixed and no focused test protects it, apply `$cpp-unit-tests` to add a regression test. Do not rewrite a failing test to conceal a product defect.
8. Finish only after the build and compact CTest runner both succeed, or stop with a concrete blocker.

## Command and scope rules

- Use the exact build command above. It emits compiler errors without normal build noise.
- Do not configure, delete, regenerate, or modify the build directory unless the user explicitly requests it. If `build` is absent or unusable, report that prerequisite.
- Do not run benchmarks, installs, examples, or solver jobs.
- Rerun compilation after each logical code fix. Rerun compact CTest only after a successful compilation.
- Do not change compiler flags, suppress diagnostics, skip tests, alter CTest registration, or lower assertions to obtain a green result.
- Never revert unrelated user work. Do not loop between competing fixes: retain a change when it fixes the cited root cause, and introduce a new change only for a separate demonstrated failure.
- If the same diagnostic remains after three evidence-based attempts, stop and report the code, diagnostic, attempted fixes, and required decision instead of continuing speculative edits.

## Failure triage

### Build failure

- Group duplicate diagnostics by root cause. Fix the earliest causal error before secondary errors.
- Read the relevant header, implementation, and immediate callers before patching.
- Respect C++17, ownership, solver contracts, hot-path rules, and existing namespace/style through `$cpp-worker`.
- Keep test compilation failures distinct from product compilation failures. Do not change test source unless it is independently proven invalid and the user authorizes that correction.

### Test failure

- Treat the existing assertion as the expected behavior unless the user explicitly changes the contract.
- Use the compact runner output to identify test executable, internal test case, source location, and assertion/reported exception.
- Reproduce the failure through the smallest relevant code path. Fix production code rather than adjusting expected values, tolerances, timeouts, seeds, or test selection.
- For a new production defect found during repair, add a minimal deterministic regression test through `$cpp-unit-tests` when coverage is absent.
- Keep `HUNLFlatDCFR` behavior intact unless the failure is explicitly in scope. Preserve deterministic worker and scalar/SIMD contracts.

## Compact CTest runner

`scripts/ctest_compact.py` runs CTest quietly with JUnit output and prints only failed cases with their test target, internal test name, source location when present, and the most relevant failure text.

Use `--junit-file <path>` only to parse an existing JUnit result without running CTest. Normal repair work must use the default run mode.

The runner prints `ALL TESTS PASSED` on success. A nonzero exit code means CTest itself failed, a test failed, or no JUnit result could be produced.

## Completion report

Report concisely:

- each root cause fixed and affected files
- any regression test added
- the exact build command and compact CTest command that passed
- any unresolved blocker, with its diagnostic and why it was not safely changed

Do not claim success unless both commands actually passed in the current repair loop.
