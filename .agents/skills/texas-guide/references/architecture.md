# Architecture and navigation

Use this map as a starting point, not a substitute for inspecting the current header and implementation. Search exact types with `rg -n 'TypeName' include src tests examples`; use `rg --files` to locate candidate files. Open the smallest relevant sections, then expand for ownership, concurrency, artifact, and public API contracts.

| Area | Role and starting points |
|---|---|
| `include/core`, `src/core` | Shared types, game interfaces, canonical combinations, checked arithmetic, portable binary, atomic publication, and `core/lib.hpp` facade. |
| `include/games`, `src/games` | Kuhn, Leduc, HUNL, and multiway game state, transitions, chance, settlement, and replay. Start with `multiway_state.hpp`, `multiway_rules.hpp`, or `multiway_terminal.hpp`. |
| `solver/generic` | Small-game solving and evaluation; start at `include/solver/generic/solver.hpp`. |
| `solver/hunl/flat` | Exact heads-up reference and compatibility implementation. Preserve its existing contract. |
| `solver/hunl/sampled` | Structured, sampled, lazy heads-up builder, range, scheduler, traversal, and storage. |
| `solver/multiway/blueprint` | Offline training, blueprint policy/store, checkpoints, and export. |
| `solver/multiway/session` | Range beliefs and updates, search and decision sessions. |
| `solver/multiway/resolver` | Runtime resolver, budgets, policy, and evaluation. |
| `solver/multiway/engine` | Scheduling, traversal and storage; `abstraction`, `continuation`, `evaluation`, and `workflow` own their named concerns. |
| `preflop`, `util` | Preflop equity and range-versus-range; shared utilities such as layouts, SIMD, profiling and suit isomorphism. |
| `tests`, `examples` | Behavior and usage evidence; `tests/cmake` tests build and API boundaries. |

For runtime search, begin with `include/solver/multiway/resolver/multiway_resolver.hpp`, then trace sessions, beliefs, policies, budget, and matching tests. Six-max beliefs use 1,326 canonical private combinations per seat; check blockers, normalization and observed-action updates in the implementation. The documented release mode can fall back when full prerequisites are absent, so check status and diagnostics rather than assuming a search ran.

Blueprint and runtime are distinct: offline sampled training produces model-bound artifacts; runtime uses verified priors and bounded request-local search. Changes affecting sampling must account for seeds, worker partitioning, worker-local deltas, deterministic merge, and value units. Changes to checkpoints or export must respect bounded memory, artifact identity, compatibility, and failure-safe publication. Compare scalar and optimized implementations where applicable.

The older `project-navigation` skill may mention `include/ranges`, `cfr_core`, or roadmap/state files absent from the current checkout. Do not create those paths based on that map. Search actual files; multiway beliefs are under `solver/multiway/session`, while sampled HUNL has its own range module. Existing source and tests take precedence over stale examples.
