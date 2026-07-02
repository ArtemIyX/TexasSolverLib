# AGENTS.md

Guidelines for LLM coding agents working in this repository.

## Project

TexasSolver is a C++17 poker-solving library. It contains Kuhn, Leduc, Hold'em/HUNL game logic, DCFR-style solvers, exploitability/value tools, abstraction utilities, suit-isomorphism helpers, SIMD-related utilities, and a vendored `pokerHandEvaluator` submodule.

Main CMake target:

- `TexasSolver::texas_core`

The current strategic direction is documented in:

- `docs/mccfr_large_tree_implementation_plan.md`

Read that document before making major HUNL solver changes.

## Hard Rules

- Do not run build, test, benchmark, install, or long-running solver commands unless the user explicitly asks.
- Do not add poker client automation, screen scraping, clicking, stealth, evasion, account/session code.
- Do not revert user changes or unrelated dirty files.
- Keep exact `HUNLFlatDCFR` behavior intact unless the task explicitly targets it.
- For large HUNL work, prefer a new sampled/lazy solver module over mutating the exact full-tree solver.

## Architecture Direction

The main future solver task is a new sampled/lazy sparse HUNL solver:

- external-sampling MCCFR first;
- public-chance sampling;
- deterministic multithreaded trajectory batches;
- worker-local deltas with fixed-order merge;
- sparse regret and average-strategy rows;
- lazy public-state expansion;
- root-only export for timed solving;
- 64 GB RAM guardrails;
- 10-15 second RTA-style decision budget;
- SIMD only after profiling proves row math is hot.

Use the current exact solver as:

- small-game correctness oracle;
- benchmark baseline;
- action/range/bucket semantics reference;
- terminal utility convention reference.

Do not make sampled production mode build the full flop graph or dense full strategy/reach/value tables.

## Hot-Path Performance Rules

In traversal, row update, terminal inner loops, and merge inner loops and hot-pathes:

- no `std::string`;
- no formatting/logging;
- no heap allocation;
- no `new` / `delete`;
- no `std::shared_ptr`;
- no `std::function`;
- no virtual dispatch;
- no exceptions as normal control flow;
- no hash-map lookup inside per-action or per-bucket loops;
- no unreserved `std::vector::push_back`.

Preferred hot-path style:

- flat arrays;
- compact integer ids;
- raw pointer or `std::span` non-owning views;
- preallocated worker scratch;
- stack/fixed-size arrays for small action menus;
- action-major contiguous rows: `row[action][bucket]`;
- scalar reference kernels first, optional SIMD kernels later.

Ownership rule:

- own memory with RAII containers at subsystem boundaries;
- pass raw pointer/span views into hot kernels;
- raw pointers must not own memory;
- avoid shared ownership in solver internals.

## Memory Rules

The sampled solver must be designed for a 64 GB desktop:

- warn around 48 GB;
- stay below about 56 GB resident memory;
- reject unsafe configs before about 60 GB estimated memory;
- avoid paging;
- do not dense-export the whole strategy during a timed decision.

Avoid:

- full production flop graph;
- dense current strategy;
- dense full reach arrays;
- dense node/action values;
- per-worker graph-sized scratch;
- per-terminal-node dense showdown matrices;
- debug strings in stored nodes/rows.

Every large allocation should appear in memory preflight and runtime profiling.

## Multithreading Rules

For sampled solving:

- parallelize by trajectory batches, not exact-solver depth stages;
- seed per trajectory, not only per worker;
- keep worker-local regret/strategy deltas;
- merge in deterministic fixed order first;
- avoid floating-point atomics;
- profile merge time and worker imbalance.

Add dynamic scheduling only after deterministic static scheduling is tested.

## Code Organization

Prefer new sampled-solver files for the next-generation HUNL engine:

- `include/solver/hunl_sampled_solver.hpp`
- `src/solver/hunl_sampled_solver.cpp`
- `include/solver/hunl_sampled_config.hpp`
- `include/solver/hunl_sampled_storage.hpp`
- `include/solver/hunl_sampled_traversal.hpp`
- `include/solver/hunl_sampled_scheduler.hpp`
- `include/solver/hunl_sampled_builder.hpp`
- `include/solver/hunl_sampled_terminal.hpp`
- `include/solver/hunl_sampled_export.hpp`
- `include/solver/hunl_sampled_simd.hpp`
- `include/solver/hunl_sampled_profile.hpp`

Keep subsystem interfaces narrow. Traversal should receive views/references, not own global mutable state.

## Style

- Use C++17.
- Match existing namespace/style patterns, especially `core::`.
- Keep comments short and useful.
- Prefer clear structured APIs over ad hoc strings.
- Use integer ids and explicit metadata for states, rows, actions, buckets, and trajectories.
- Keep scalar validation paths even when adding SIMD.
- Keep exact-mode behavior deterministic.

## Testing And Verification

Do not run build or test commands unless the user explicitly asks.

When asked to test, typical commands from `README.md` are:

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For docs-only changes, do not run tests.

## Solver Quality Principles

The product goal is strong bounded postflop solving, not a perfect full-game Nash equilibrium.

Prioritize:

- range/blocker correctness;
- stable root action mix;
- small action abstractions;
- bucket abstractions;
- depth-limited leaves;
- memory-safe sparse storage;
- root-only timed export;
- reproducible profiles and seeds.

For RTA-style/offline resolving, keep the interface at:

```text
structured game state in -> strategy/diagnostics out
```

Do not expand the repository toward poker-client automation.