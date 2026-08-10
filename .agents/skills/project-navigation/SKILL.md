---
name: project-navigation
description: Rapidly locate related TexasSolver code, tests, examples, and placement for new C++ modules. Use for repository orientation, symbol tracing, feature discovery, or deciding where to add or modify files in this project.
---

# TexasSolver Navigation

Use `rg` first. Search `include`, `src`, `tests`, and `examples` together; open only matching files.

```powershell
rg -n "ExactTypeOrTerm" include src tests examples
rg -l "ExactTypeOrTerm" include src tests examples
rg --files include src tests examples | rg "hunl|multiway|range"
```

## Map

| Area | Purpose | Files |
|---|---|---|
| `include/`, `src/` | Public C++17 API and matching implementation | Mirror paths: `include/<area>/x.hpp` -> `src/<area>/x.cpp` |
| `core` | IDs, game interface, arena, public facade | `core/types.hpp`, `game.hpp`, `lib.hpp` |
| `games` | Poker state, rules, tree/graph construction | `kuhn`, `leduc`, `hunl_*`, `multiway_*` |
| `solver` | CFR/DCFR, HUNL, sampled HUNL, multiway solving | `dcfr*`, `hunl_*`, `multiway_*` |
| `ranges` | Range input, propagation, caching, bucketing | `range`, `source`, `cache`, `propagation` |
| `preflop` | Equity, class-169 RVR, preflop solver | `preflop*` |
| `util` | Abstraction, layouts, infosets, profiling, SIMD, suit iso | `util/*.hpp` |
| `tests` | One executable per `test_*.cpp`; shared harness | `test_main.cpp`, `test_<area>_<feature>.cpp` |
| `examples` | Executable entry points and benchmarks | `solve_kuhn.cpp`, `hunl_mccfr_postflop_main.cpp`, `benchmarks/` |
| `docs` | Roadmap and authoritative state/release notes | `project_state_report.md`, `implementation_roadmap.md` |
| `external/pokerHandEvaluator` | Vendored hand evaluator | CMake subproject |
| `cfr_core` | Rust reference/legacy source tree | Do not modify unless explicitly requested |

`CMakeLists.txt` globs `include/**/*.hpp` and `src/**/*.cpp`; a normal paired module needs no CMake source-list change. It builds static `TexasSolver::texas_core`.

## Architecture routes

- Generic small games: `games/kuhn`, `games/leduc` -> `solver/dcfr*`, `solver/exploit`, `solver/solver`.
- Exact HUNL: `games/hunl*` -> `games/hunl_tree` or `hunl_flat_*` -> `solver/hunl_flat_*`.
- Structured sampled HUNL: `solver/hunl_sampled_config` -> `builder` / `range` -> `scheduler` / `traversal` -> `storage`, `terminal`, `export`, `profile`.
- Multiway six-max: `games/multiway_rules`, `state`, `private`, `fixed`, `terminal`, `replay` -> `solver/multiway_*`. Start from the public entry type named in the request, then trace collaborator types with `rg`.
- Ranges/preflop: `ranges/*` owns solver-neutral range behavior; `preflop/*` owns equity and RVR logic.

Read `docs/project_state_report.md` only for a broad subsystem inventory, current contracts, or release boundaries.

## Find and place code

1. Search the public type/function first in `include/`; read its matching `src/` implementation next.
2. Search the exact basename in `tests` and `examples` for usage and contracts.
3. For a cross-cutting change, search the type name, not a broad concept. Use `rg -n "TypeName|function_name" include src tests examples`.
4. Add a new public module as a mirrored header/source pair in the owning area. Add `tests/test_<module>.cpp` for behavior. Add an example only for an end-user workflow or benchmark.
5. Update `include/core/lib.hpp` only when the new API should be part of the convenience facade. Keep internal helpers out of it.

Keep game rules/state in `games`, solving/training/traversal in `solver`, solver-independent ranges in `ranges`, and shared low-level helpers in `util` or `core`. Match nearby naming and namespace `core::`.

## Constraints that affect placement

- Keep legacy exact `HUNLFlatDCFR` behavior intact unless explicitly changing it.
- Put future sampled HUNL work in `solver/hunl_sampled_*`, not the full flat graph path.
- For sampled/multiway hot paths, use preallocated flat data and integer IDs; avoid allocation, strings, hash lookup, virtual dispatch, and logging in inner loops.
- Do not run builds or tests unless the user explicitly asks.