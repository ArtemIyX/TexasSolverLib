# Rust to C++ Port Status

This document compares the Rust reference crate in `cfr_core` with the current C++ port in `src/core` and `include/core`.

The goal is to identify what is already ported, what is only partially ported, and what is still missing from the Rust surface area.

## Summary

- The core solver stack has been ported in C++.
- The HUNL, preflop, SIMD, layout, PCS, and abstraction modules also exist in C++.
- The remaining gaps are mostly around Rust-only tooling: benches, examples, crate-level entrypoints, and a few known follow-up features.
- There is also one known Rust-side partial implementation in the vector solver path: suit-iso reduction is still explicitly unfinished there.

## Rust files and C++ status

### Already ported

These Rust modules have a clear C++ counterpart in `src/core` and `include/core`:

- `abstraction.rs` -> `src/core/abstraction.cpp`, `include/core/abstraction.hpp`
- `arena.rs` -> `src/core/arena.cpp`, `include/core/arena.hpp`
- `dcfr.rs` -> `src/core/dcfr.cpp`, `include/core/dcfr.hpp`
- `dcfr_vector.rs` -> `src/core/dcfr_vector.cpp`, `include/core/dcfr_vector.hpp`
- `dcfr_vector_parallel.rs` -> `src/core/dcfr_vector_parallel.cpp`, `include/core/dcfr_vector_parallel.hpp`
- `exploit.rs` -> `src/core/exploit.cpp`, `include/core/exploit.hpp`
- `game.rs` -> `include/core/game.hpp`
- `hunl.rs` -> `src/core/hunl.cpp`, `include/core/hunl.hpp`
- `hunl_eval.rs` -> `src/core/hunl_eval.cpp`, `include/core/hunl_eval.hpp`
- `hunl_solver.rs` -> `src/core/hunl_solver.cpp`, `include/core/hunl_solver.hpp`
- `hunl_tree.rs` -> `src/core/hunl_tree.cpp`, `include/core/hunl_tree.hpp`
- `kuhn.rs` -> `src/core/kuhn.cpp`, `include/core/kuhn.hpp`
- `layout.rs` -> `src/core/layout.cpp`, `include/core/layout.hpp`
- `leduc.rs` -> `src/core/leduc.cpp`, `include/core/leduc.hpp`
- `pcs.rs` -> `src/core/pcs.cpp`, `include/core/pcs.hpp`
- `preflop.rs` -> `src/core/preflop.cpp`, `include/core/preflop.hpp`
- `preflop_equity.rs` -> `src/core/preflop_equity.cpp`, `include/core/preflop_equity.hpp`
- `preflop_rvr.rs` -> `src/core/preflop_rvr.cpp`, `include/core/preflop_rvr.hpp`
- `simd.rs` -> `src/core/simd.cpp`, `include/core/simd.hpp`
- `solver.rs` -> `src/core/solver.cpp`, `include/core/solver.hpp`
- `suit_iso.rs` -> `src/core/suit_iso.cpp`, `include/core/suit_iso.hpp`

### Partially ported

These Rust modules exist in C++, but the Rust side still contains behavior or follow-up work that is not fully mirrored as a complete product boundary:

- `dcfr_vector.rs`
  - The Rust file still contains an explicit `unimplemented!()` for suit-isomorphism reduction.
  - The C++ port has the module, but this area should be treated as a known follow-up rather than a fully closed parity milestone.

### Still missing

These Rust files do not yet have clear C++ equivalents as standalone tools or top-level surfaces:

- `lib.rs`
  - The Rust crate entrypoint and PyO3 bridge have no direct C++ equivalent yet.
  - The C++ side has `include/core/core.hpp` and `src/core/core.cpp`, but not a full top-level facade matching the Rust crate API shape.
- Benchmarks:
  - `benches/dcfr_bench.rs`
  - `benches/rvr_profile.rs`
  - `benches/preflop_rvr_profile.rs`
  - `benches/flop_subgame_perf.rs`
- Examples:
  - `examples/build_preflop_equity.rs`
  - `examples/rvr_bench.rs`
- Rust-specific dev/test workflow artifacts that have not been recreated as first-class C++ tooling:
  - `benches/baseline.json`
  - Python/PyO3-facing test entrypoints

## Rust test coverage and C++ coverage

The Rust crate contains integration tests for:

- HUNL state
- preflop initial contributions
- preflop RvR smoke
- DCFR parameter guards
- HUNL parity
- SIMD dispatch and smoke coverage

The C++ tree currently has translated tests for:

- arena
- abstraction
- dcfr
- dcfr vector
- hunl eval
- hunl state
- hunl tree
- kuhn
- layout
- leduc
- pcs
- simd

That means the C++ port is strongest on the core module translation, but still lighter on end-to-end workflow coverage.

## What this means for the port

The C++ rewrite is past the “basic module skeleton” stage.

The remaining Rust work to port is mainly:

- benchmark harnesses
- generator/example utilities
- crate-level facade/API shape
- known follow-up behavior around suit-iso reduction in the vector solver path

## Recommended next implementation targets

If the goal is to keep moving the port forward by copying Rust functionality into C++, the next best targets are:

1. `lib.rs`-style top-level C++ facade
2. benchmark/example command-line tools
3. vector-solver follow-up work around suit-iso reduction
4. any remaining Rust workflow glue that produces artifacts or profiles

## Notes

- This document is based on the current repository tree and the Rust/C++ file names present in this workspace.
- It is intentionally conservative: if a Rust feature exists but only as internal behavior with no visible C++ surface yet, it is treated as still pending.
