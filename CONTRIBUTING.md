# Contributing to TexasSolver

Thanks for helping improve TexasSolver. This project is a C++17 port of the Rust implementation from [amaster97/poker_solver](https://github.com/amaster97/poker_solver), so contributors are especially valuable when they help preserve behavioral parity, clarify naming, or add tests that make the port easier to trust.

## What This Repository Is

TexasSolver is a reusable CMake library for poker solving and analysis. The current codebase is organized around a few top-level module groups:

- `include/core` and `src/core` for shared types and library-facing glue
- `include/games` and `src/games` for Kuhn, Leduc, and HUNL game logic
- `include/solver` and `src/solver` for DCFR and exploitability code
- `include/preflop` and `src/preflop` for preflop-specific logic
- `include/util` and `src/util` for support code such as abstraction, layout, SIMD, PCS, and suit isomorphism
- `external/pokerHandEvaluator` for the vendored fixed-size hand evaluator submodule used by HUNL evaluation
- `tests` for local regression tests
- `examples` for usage samples


## Before You Start

Please read:

- [README.md](README.md)
- [LICENSE](LICENSE)

If you are changing solver behavior, it is also worth comparing against the Rust source in [cfr_core](https://github.com/amaster97/poker_solver/tree/main/crates/cfr_core) to understand the intended semantics.

## Contributor Goals

Good contributions usually do one of these:

- improve correctness
- reduce ambiguity in names, comments, or public API
- make the C++ port easier to compare with Rust
- add or strengthen tests
- improve build/install/docs ergonomics

Good contributions usually do not:

- introduce a new abstraction unless it genuinely simplifies the code
- rename public symbols without a strong reason
- change solver semantics without tests or a clear parity note

## Working With the Code

### Naming and structure

Try to keep names close to the Rust implementation when possible. That makes it easier to compare behavior across the two codebases.

If you need to add a new module, prefer placing it in the existing grouped structure instead of creating a new one-off folder per file.

### Public API

Public headers under `include/` define the library surface. If you add a new function or type that external users should call, place it in the relevant public header and keep the implementation in `src/`.

For HUNL hand ranking, the code now prefers the vendored `pokerHandEvaluator` submodule for 5-card, 6-card, and 7-card hands. The generic `evaluate_n` fallback still exists for larger or unusual inputs, but new tests should make it clear which path they are exercising.

### Include paths

Use the grouped include style already in the repository, such as:

```cpp
#include "solver/solver.hpp"
#include "games/hunl.hpp"
#include "preflop/preflop.hpp"
```

Avoid reintroducing the old nested `include/core/...` or `src/core/...` style.

## Build Instructions

From the repository root:

```bash
cmake -S . -B build
cmake --build build --config Release
```

On multi-config generators, `Release` is the default verification configuration used in this repository.

The top-level CMake project adds `external/pokerHandEvaluator/cpp` as a subdirectory and links `texas_core` to the `pheval` target. If you touch build files, keep that submodule wiring in mind so the fixed-size evaluator continues to build cleanly.

## Testing

Run the test suite with:

```bash
ctest --test-dir build -C Release --output-on-failure
```

If you change game logic, solver logic, or any low-level helper used by those paths, you should run the full suite.

If you add a new test file under `tests/test_*.cpp`, the CMake glob should pick it up automatically.

## What To Test

Try to add or update tests for:

- game state transitions
- chance-node behavior
- infoset key generation
- utility calculations
- solver convergence or output structure
- exploitability and game-value calculations
- abstraction/load/save helpers
- evaluator ordering and fixed-size hand ranking paths, especially `evaluate_5`, `evaluate_6`, and `evaluate_7`

For tricky behavior, it helps to add at least one test that confirms a known edge case or a parity detail from the Rust implementation.

## Documentation Expectations

This repository uses Doxygen-style comments in public headers. If you add or change a public symbol, please document it with a short `@brief` description and add parameter/return notes when useful.

Be concise. The goal is for headers to explain themselves without becoming a wall of prose.

## Port Parity With Rust

Because this project is a port of `amaster97/poker_solver`, contributors should be careful when changing:

- solver iteration logic
- action ordering
- infoset formatting
- chance outcome handling
- payoff calculations
- abstraction bucket lookups

If your change intentionally diverges from Rust, call that out clearly in the commit or PR description and add tests that explain the new behavior.

## Suggested Workflow

1. Create a branch for your work.
2. Make the smallest change that solves the problem.
3. Add or update tests.
4. Build the project.
5. Run the test suite.
6. Update docs if the user-facing API changed.

## Adding New Code

When adding a new class or function:

- keep the declaration in the most relevant public header
- keep the implementation in the matching `src/` module
- follow the existing naming style
- prefer `core::` namespace usage unless there is a strong reason to do otherwise
- add Doxygen comments for any public-facing symbol

When adding a new solver path, try to keep the structure close to the existing `solve_kuhn` and `solve_leduc` flow so the code stays easy to reason about.

## Examples

If you add a new example, place it under `examples/` and keep it short and runnable. Examples should show how to consume the library, not become mini-applications.

## Commits and Pull Requests

Please keep commits focused. A good pull request usually includes:

- a short summary of what changed
- why the change is needed
- how it was tested
- whether it preserves parity with the Rust reference

If the change affects performance, include a brief note about what got faster or slower.

## Reporting Problems

If you find a bug, it helps to include:

- the command you ran
- the build configuration
- expected behavior
- actual behavior
- a small reproduction if possible

For solver issues, include the game type and any relevant configuration values.

## License

Contributions to this repository are made under the [MIT](LICENSE) License, the same license used by the project.
