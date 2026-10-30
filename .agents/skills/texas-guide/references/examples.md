# Task examples

These examples show how to choose evidence and skills. Adapt to the user's scope; do not automatically run commands that require explicit authorization.

## Trace runtime resolver behavior

Request: "Explain why the resolver returned a fallback."

1. Search `MultiwayResolver` and the status returned by the actual call; read `include/solver/multiway/resolver/multiway_resolver.hpp`, its implementation, and `tests/test_multiway_resolver.cpp`.
2. Trace the relevant budget, artifact, range, and leaf admission checks only as needed. Distinguish legal fallback from completed search and explain diagnostic evidence.
3. Use `project-navigation`; if asked to change behavior, also use `cpp-worker`, relevant test guidance, and C++ review. Do not claim a runtime result from static reading.

## Add a C++ module

Request: "Add a reusable public state helper."

1. Identify the owning area and inspect a nearby header/source pair and tests. Place rules/state in `games`, shared IDs in `core`, and solver logic in its owning solver subsystem.
2. Use `cpp-worker`, `cpp-unit-tests`, and `cmake-maintenance`. Add the appropriate header and source to explicit classification lists; decide intentionally whether the header belongs in the installed API or the facade.
3. Add focused observable-behavior tests under `tests/test_*.cpp`; inspect test gates and support dependencies. Review ownership and hot-path implications. Run builds/tests only when explicitly requested.

## Review deterministic solver changes

Request: "Review this multithreaded training diff."

1. Use `git-commit-hygiene` to inspect the relevant diff and `cpp-code-review` to review it; preserve unrelated user work.
2. Follow trajectory seed derivation, worker-local writes, merge order, memory admission, error handling, and lifetime rules. Verify the stated reproducibility scope, including worker count and partitioning.
3. Check matching deterministic and differential tests; report actionable findings with evidence. A fixed seed does not by itself establish cross-worker-count equivalence.

## Diagnose a build failure

Request: "Explain the failure in this existing build log."

1. Read the first meaningful compiler or CMake error and its source context. Use `scan_build_errors.py` on the supplied log only if writing its output is appropriate.
2. For missing sources, inspect root CMake classification and the owning target. For test registration inspect both test gates and `tests/CMakeLists.txt`; for a consumer failure inspect installed headers and exports.
3. Load `cmake-maintenance` before changing CMake. Do not assume `full_build.py` configures the tree, and do not rerun build or configure unless explicitly authorized.

## Change a hot kernel

Request: "Speed up a terminal evaluation loop."

1. Identify the actual kernel and measurement before optimizing. Keep allocation, strings, logging, virtual calls, and per-action hash lookup out of the loop.
2. Check utility units, hand blockers, ties, side pots, and existing scalar behavior. Preserve a scalar reference when adding SIMD and ensure scratch and memory admission remain bounded.
3. Use differential cases and measure only if requested. State workload, compiler, configuration, worker count, timing scope, and memory peak; do not present an old PB-9 result as a new benchmark.

## Update CMake packaging

Request: "Expose a new public header in the installed library."

1. Read `cmake-maintenance`, inspect header classification, `cmake/TexasSolverTargets.cmake`, package config and `tests/cmake` contracts.
2. Preserve install-relative includes, target usage requirements, vendor isolation, and CMake 3.20 compatibility. Avoid adding internal or research interfaces to the installed API accidentally.
3. When the user explicitly asks for execution, verify the relevant configuration and a separately installed and relocated consumer. Report what actually ran.
