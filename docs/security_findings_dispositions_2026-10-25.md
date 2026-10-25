# Security Findings Dispositions

Completed 2026-10-25 against the local source tree and the 19-alert inventory in
`docs/TexasSolverLib_CodeQL_Findings_2026-10-25.md`.

## Dispositions

| Finding | Disposition | Evidence |
|---|---|---|
| CodeQL #15-19 | Fixed | `tests/test_multiway_memory.cpp` now releases each global allocation with its matching unsized delete family and alignment. The focused test passes normally and under MSVC AddressSanitizer. |
| CodeQL #3 | Fixed | `include/core/checked_arithmetic.hpp` and the HUNL benchmark widen operands before multiplication/addition, reject negative and overflowing values, and reject indices above `uint32_t`. Boundary tests pass. |
| Copilot #2 and #5 | Fixed | Workflow resource limits validate capacities, workers, batch counts, aggregate trajectories, and checked memory estimates before training logs, worker creation, or training. Focused configuration tests pass. |
| Copilot #1, #3, and #4 | Fixed for the supported workflow executable | `multiway_workflow_path_policy` authorizes canonical in-root paths, rejects traversal/device syntax, rejects symlink/reparse components, enforces input/output kinds and overwrite policy, and checks workflow artifact aliases before filesystem operations. Focused path and workflow-boundary tests pass. |
| CodeQL #1 | Fixed | `extract_symbols.py` uses a bounded line length and a linear balanced-template scan instead of the greedy template regex. Normal, malformed, and oversized-input tests pass. |
| CodeQL #4-14 | Locally patched | The vendored Tracy 0.14.1 arithmetic expressions are widened before multiplication. Tracy diagnostics targets compile successfully. No newer `v0.14.*` tag was returned by the upstream tag query; the local patch is therefore tracked rather than presented as an upstream upgrade. |
| CodeQL #2 | Fixed by default | Tracy MCP now binds IPv4 loopback by default. Non-loopback binding requires `TRACY_MCP_ALLOW_REMOTE=1` and a valid explicit IPv4 address. Invalid configuration fails closed and remote opt-in emits a warning. Bind-policy tests pass. |

## Build and reachability evidence

- Normal Tracy diagnostics are disabled by default in CMake. With
  `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=ON`, `texas_multiway_train_tracy` and
  `texas_solver_multiway_training_scaling_tracy` compiled successfully.
- CMake reports `TRACY_ONLY_LOCALHOST=ON`, `TRACY_ONLY_IPV4=ON` for the client
  diagnostics configuration.
- The MCP server is started by its manual `start_mcp.sh` entry point, not by the
  normal CMake build. Its default bind policy is covered separately.

## Verification

- Focused CTest: checked arithmetic, allocation ownership, workflow resource
  configuration, and path policy: 4/4 passed.
- Workflow boundary/configuration/path tests: 3/3 passed.
- MSVC AddressSanitizer build and ownership test passed with
  `ASAN_OPTIONS=allocator_may_return_null=1`. The test contains an intentional
  maximum-size allocation probe; without that option ASan aborts before the
  probe can return null.
- Tracy MCP Python tests: 4/4 passed.
- Symbol-parser Python tests: 3/3 passed, including pathological input.
- Tracy-enabled targets compiled successfully.
- `scripts/codex_powershell.ps1 scripts/python/full_build.py`: build passed and
  the full 113-test suite passed.
- Independent `cpp-code-review`: no remaining actionable findings.

## Limitations

- CodeQL was not rerun because the `codeql` executable is unavailable in this
  environment. The repository alert state is therefore not independently
  confirmed closed.
- The symlink regression test conditionally skips the symlink case when the OS
  denies symlink creation. The policy still rejects symlink/reparse components
  when the platform exposes them.
- Path authorization is a canonical preflight around existing path-based file
  APIs. It rejects unsafe existing links and traversal, but it does not provide
  an atomic no-follow file-handle operation against every possible concurrent
  filesystem race.
