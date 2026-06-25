# `cfr_core` to C++ Port Plan

This document turns the Rust crate in `cfr_core` into a file-by-file C++ rewrite plan.

The goal is not a line-by-line translation of Rust syntax. The goal is to recreate the same behavior, data flow, and test surface in idiomatic C++ while preserving the current solver semantics:

- DCFR/Kuhn/Leduc core behavior
- HUNL postflop and preflop solvers
- exploitability and game-value walks
- SIMD/layout/chance-sampling helpers
- benchmark and test coverage

## 1. What `Cargo.toml` tells us

The manifest shows one crate, `cfr_core`, with these important traits:

- Library crate with both `cdylib` and `rlib` outputs
- PyO3 binding layer exposed from `src/lib.rs`
- Core dependencies:
  - `pyo3`
  - `ndarray`
  - `ndarray-npy`
  - `serde`
  - `serde_json`
  - `arrayvec`
  - `rayon`
- Dev-only PyO3 dependency for tests
- Bench targets:
  - `dcfr_bench`
  - `rvr_profile`
  - `preflop_rvr_profile`
  - `flop_subgame_perf`

For the C++ version, this implies:

- Replace PyO3 with a native C++ API. Python compatibility is not required for this rewrite.
- Replace `serde_json` with a C++ JSON library or a minimal parser for the existing config shape.
- Replace `ndarray` / `ndarray-npy` with a C++ tensor/vector representation and NPZ reader.
- Replace `rayon` with `std::thread`, a thread pool, or a task system.
- Replace `arrayvec` with `std::array` or `std::vector` with reserved capacity.

## 2. Recommended C++ project layout

Use the existing `src/` and `include/` roots, and place this module under dedicated subfolders:

```text
include/
  core/
    arena.hpp
    abstraction.hpp
    dcfr.hpp
    dcfr_vector.hpp
    dcfr_vector_parallel.hpp
    exploit.hpp
    game.hpp
    hunl.hpp
    hunl_eval.hpp
    hunl_solver.hpp
    hunl_tree.hpp
    kuhn.hpp
    layout.hpp
    leduc.hpp
    pcs.hpp
    preflop.hpp
    preflop_equity.hpp
    preflop_rvr.hpp
    simd.hpp
    solver.hpp
    suit_iso.hpp
src/
  core/
    arena.cpp
    abstraction.cpp
    dcfr.cpp
    dcfr_vector.cpp
    dcfr_vector_parallel.cpp
    exploit.cpp
    game.cpp
    hunl.cpp
    hunl_eval.cpp
    hunl_solver.cpp
    hunl_tree.cpp
    kuhn.cpp
    layout.cpp
    leduc.cpp
    preflop.cpp
    preflop_equity.cpp
    preflop_rvr.cpp
    pcs.cpp
    simd.cpp
    solver.cpp
    suit_iso.cpp
tests/
benches/
examples/
assets/
```

## 3. Porting order

Port in dependency order so each layer has something stable to build on:

1. Core utilities: `game`, `arena`, `simd`, `layout`, `pcs`
2. Small toy games: `kuhn`, `leduc`
3. Generic solver orchestration: `dcfr`, `solver`, `exploit`
4. HUNL model: `abstraction`, `hunl`, `hunl_eval`, `hunl_tree`, `hunl_solver`
5. Postflop/vector performance work: `dcfr_vector`, `dcfr_vector_parallel`
6. Preflop tooling: `preflop_equity`, `preflop`, `preflop_rvr`
7. Chance isomorphism helpers: `suit_iso`
8. Examples, benches, tests

## 4. File-by-file recreation guide

### `src/lib.rs`

This is the public Rust entrypoint and PyO3 module.

Recreate it in C++ as:

- a top-level facade header like `include/core/core.hpp`
- exported functions for:
  - `solve_kuhn`
  - `solve_leduc`
  - `solve_hunl_postflop`
  - `solve_hunl_preflop`
  - `compute_exploitability`
  - `compute_restricted_game_value`
  - `solve_range_vs_range_rust`
  - `solve_hunl_preflop_rvr`
  - `solve_hunl_preflop_rvr_class169`

Implementation notes:

- Keep a single result struct analogous to `SolveOutput`.
- Preserve output field names if you want to keep the current result shape stable for future adapters.
- Keep error conversion logic out of the core solver.

### `src/game.rs`

Define the foundational game interface.

Port to C++ as an abstract base class or concept:

- `initial()`
- `is_terminal()`
- `current_player()`
- `legal_actions()`
- `chance_outcomes()`
- `apply(action)`
- `infoset_key(player)`
- `utility()`

This file should become the generic contract used by the rest of the solver.

### `src/dcfr.rs`

This is the core DCFR solver.

Recreate:

- regret accumulation
- strategy derivation
- discounting rules
- optional locked strategies
- alpha/beta/gamma validation

Suggested C++ shape:

- `template <class G> class DCFRSolver`
- `solve(iterations)` returning average strategy
- helper functions for regret update and normalization

### `src/solver.rs`

This is orchestration around DCFR plus exploitability and game value.

Port this almost directly into C++:

- `SolveOutput`
- `solve_kuhn`
- `solve_leduc`
- generic solve wrapper
- expected value recursion
- exploitability computation
- best-response traversal

This file is a good early milestone because it validates the game interface and recursion model.

### `src/kuhn.rs`

Kuhn poker state implementation.

Port as a small deterministic state machine:

- state encoding
- legal actions
- chance dealing
- infoset key generation
- terminal utilities

This is one of the best first test beds for the C++ solver.

### `src/leduc.rs`

Leduc state implementation.

Port the same way as Kuhn, but with:

- two rounds
- public card reveal
- more complex terminal utility logic
- deeper best-response recursion requirements

Use the existing Rust behavior as the reference for exact action/legal-state transitions.

### `src/exploit.rs`

Exploitability and game-value walk utilities for HUNL.

Port this as:

- recursive best-response evaluation
- on-policy EV evaluation
- strategy lookup keyed by infoset
- support for restricted hole-card sets

This file is central to verifying parity after the solver port.

### `src/abstraction.rs`

HUNL abstraction table loading and lookup.

Port as:

- abstraction-table loader
- data container for abstraction metadata
- lookup helpers used by solver/tree code

If Rust currently reads `.npz` or similar artifacts, keep the same format for the C++ version.

### `src/hunl.rs`

HUNL configuration and game-state model.

Port as:

- configuration struct
- serialization/deserialization support
- state and parameter handling
- utilities shared by the HUNL solver stack

This file should define the data model that all HUNL components agree on.

### `src/hunl_eval.rs`

Hand evaluation logic for HUNL.

Port carefully and test heavily:

- board and hole-card evaluation
- showdown comparisons
- terminal payoff helpers

This is a correctness-sensitive file, so it deserves its own parity tests before anything performance-related.

### `src/hunl_tree.rs`

Tree construction for HUNL.

Port as:

- node definitions
- tree traversal helpers
- chance/action expansion
- history indexing

This file likely becomes the structural backbone for HUNL solve passes.

### `src/hunl_solver.rs`

Full HUNL DCFR solver driver.

Port this after `hunl`, `hunl_tree`, `hunl_eval`, and `dcfr` are already stable.

Responsibilities to preserve:

- solve entrypoints
- iteration loop
- strategy accumulation
- optional locked strategies
- runtime statistics
- wall-clock measurements

### `src/layout.rs`

Cache-blocked infoset layout.

Port as a low-level memory-layout utility:

- index packing/unpacking
- contiguous storage helpers
- cache-friendly ordering

This should stay in a separate module because it affects performance-critical data structures.

### `src/pcs.rs`

Public chance sampling helpers.

Port as the bridge between chance nodes and packed representations:

- chance-child indexing
- sample/expand logic
- precomputed mapping helpers

### `src/simd.rs`

SIMD kernel layer.

Port to C++ with explicit backend selection:

- scalar fallback
- SSE/AVX/NEON variants if desired
- compile-time or runtime dispatch

Keep the interface backend-neutral so higher-level code does not care which implementation runs.

### `src/dcfr_vector.rs`

Vector-form DCFR for range-vs-range solving.

Port as a separate solver family rather than mixing it into scalar DCFR:

- vector regret storage
- vector strategy updates
- hand-pair traversal
- memory-profile reporting

This module is likely the most performance-sensitive part of the rewrite.

### `src/dcfr_vector_parallel.rs`

Parallel wrapper for the vector solver.

Port with:

- explicit parallel chance subtree handling
- per-thread buffers
- deterministic merge order
- serial fallback path

If reproducibility matters, make the merge order deterministic from day one.

### `src/preflop_equity.rs`

Precomputed equity table handling.

Port as:

- NPZ/table loader
- lookup API for 169x169 or related class tables
- helper methods for class-vs-class equity access

Keep the runtime format compatible with the existing artifact if possible.

### `src/preflop.rs`

Preflop subgame solver.

Port as:

- preflop-specific tree traversal
- equity-leaf substitution
- solve driver for subgame mode

This layer should sit on top of `hunl` and `preflop_equity`.

### `src/preflop_rvr.rs`

Full preflop range-vs-range solver.

Port as a separate engine from `preflop.rs`:

- full-tree solve
- class or hand resolution
- configurable open sizes and reraises
- optional restricted root reach vectors

This module depends on the preflop equity table and the preflop tree model.

### `src/suit_iso.rs`

Suit isomorphism helpers.

Port as pure utility code:

- board/chance grouping
- permutation helpers
- equivalence-class detection

Because the Rust code marks this as stage-1 / not fully wired, preserve the same rollout strategy in C++.

## 5. Benchmarks to recreate

### `benches/dcfr_bench.rs`

Create a C++ benchmark that measures the scalar DCFR loop on Kuhn/Leduc or the equivalent smoke fixture.

### `benches/rvr_profile.rs`

Create a profiling benchmark for vector-form RvR with phase breakdowns.

### `benches/preflop_rvr_profile.rs`

Create a preflop RvR profile benchmark with the same instrumentation style.

### `benches/flop_subgame_perf.rs`

Create a flop-subgame performance benchmark around the HUNL solver hot path.

### `benches/baseline.json`

Keep a benchmark baseline artifact in a comparable JSON format so you can diff timings after each port step.

## 6. Example programs to recreate

### `examples/build_preflop_equity.rs`

Recreate as a C++ utility or generator tool that:

- computes the preflop equity table
- writes the table to the same artifact format
- validates shape and checksum after generation

### `examples/rvr_bench.rs`

Recreate as a simple command-line runner for the range-vs-range solver.

## 7. Tests to recreate

Port tests file by file and keep them close to the Rust intent.

### `tests/hunl_state_unit.rs`

Validate:

- state transitions
- legal actions
- terminal payoffs
- infoset key stability

### `tests/preflop_initial_contributions.rs`

Validate initial contribution accounting for preflop paths.

### `tests/preflop_rvr_smoke.rs`

Smoke-test the full preflop RvR solver.

### `tests/test_dcfr_alpha_guard.rs`

Validate alpha/beta/gamma parameter guards and input checks.

### `tests/test_hunl_rust.rs`

Use this as the Rust reference for HUNL parity. In C++, recreate the same scenario coverage and compare outputs.

### `tests/test_simd_cross_platform_smoke.rs`

Create smoke tests for scalar and SIMD paths across platforms.

### `tests/test_simd_dispatch.rs`

Validate backend dispatch selection.

### `tests/test_simd_phase3.rs`

Validate phase-3 SIMD behavior and output parity.

### `tests/test_simd_phase4.rs`

Validate phase-4 SIMD behavior and output parity.

## 8. Suggested implementation phases

### Phase 1: Build the core contracts

Implement:

- `game`
- `kuhn`
- `leduc`
- `dcfr`
- `solver`

Goal: get the smallest solver stack working in C++.

### Phase 2: Add HUNL structure

Implement:

- `hunl`
- `hunl_eval`
- `hunl_tree`
- `exploit`
- `hunl_solver`

Goal: reproduce the existing Rust HUNL functionality.

### Phase 3: Add performance and scale features

Implement:

- `arena`
- `layout`
- `pcs`
- `simd`
- `dcfr_vector`
- `dcfr_vector_parallel`

Goal: reach the vectorized and parallel path.

### Phase 4: Add preflop support

Implement:

- `preflop_equity`
- `preflop`
- `preflop_rvr`

Goal: support the preflop full-tree and subgame workflows.

### Phase 5: Add CLI tools

Implement:

- example runners
- benchmark harnesses

Goal: make the C++ rewrite usable from the same workflows as Rust.

## 9. Practical migration rules

- Keep the public API stable where possible.
- Port one test fixture at a time.
- Prefer small adapter layers over giant translation units.
- Keep serialization formats unchanged until the core rewrite is proven.
- Add parity tests before optimizing.
- Treat `dcfr`, `solver`, `exploit`, and `hunl_eval` as correctness-critical.
- Treat `simd`, `layout`, `pcs`, and `dcfr_vector_parallel` as performance-critical.

## 10. Minimal acceptance checklist

The C++ rewrite can be considered healthy when it can:

- solve Kuhn and Leduc with matching outputs
- reproduce HUNL postflop and preflop results within acceptable float tolerance
- load the same config and equity artifacts
- pass the translated unit tests
- run the translated benchmarks
- preserve the current module boundaries so future maintenance stays manageable
