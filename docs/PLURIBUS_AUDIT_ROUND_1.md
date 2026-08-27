# Pluribus Architecture Audit: Round 1

Findings: 3.

[R1] P1 — Runtime current-round search is bucketed, not lossless.
Location: `src/solver/multiway_traversal.cpp:313`
Evidence: Every decision, including the live root, gets its row bucket from `MultiwayBucketRegistry`; `make_search_root` stores the hero's bucket and `export_root_policy` exports only that bucket (`src/solver/multiway_resolver.cpp:363`, `src/solver/multiway_solver.cpp:792`). Two distinct current-street hands assigned to one bucket therefore share regrets and strategy.
Impact: Violates the documented Pluribus rolling-precision boundary. Current-street blockers, draws, and hand-specific action mixes cannot be resolved losslessly.
Fix: Use an exact private-hand ID for current-round row indexing. Reserve future-bucket lookup for crossed search horizons/continuation states.

[R2] P1 — Parallel rollout leaves share mutable scratch/cache state.
Location: `src/solver/multiway_resolver.cpp:487`
Evidence: `run_search` shallow-copies `MultiwayRolloutLeafContext`, retaining its `scratch`, `cache`, and `diagnostics` pointers, then starts `worker_count` concurrent traversal workers (`src/solver/multiway_resolver.cpp:503`; `src/solver/multiway_traversal.cpp:585`). The rollout context contract requires one scratch/cache/diagnostics set per concurrent caller (`include/solver/multiway_rollout_leaf.hpp:164`).
Impact: With `worker_count > 1`, workers race while mutating rollout scratch, cache vectors, and diagnostics. Leaf values and exported policy become nondeterministic; this is undefined behavior.
Fix: Construct worker-local rollout contexts, scratch, cache, and diagnostics, then merge diagnostics after the batch. Otherwise reject multithreaded rollout evaluation.

[R3] P1 — Continuation policy is globally fixed instead of strategically selected.
Location: `src/solver/multiway_continuation_selector.cpp:13`
Evidence: `MultiwayFixedContinuationSelector::select` ignores its information-set key and always returns `policy_`. The report requires the continuation mode to be a strategic choice at abstract continuation information sets (`docs/pluribus_technical_report.md:545`).
Impact: The runtime cannot select or mix the four continuation policies by state. Search leaf values model one globally forced future, not Pluribus's continuation-policy game.
Fix: Store and regret-match a continuation-policy row keyed by the documented continuation information set, and use its sampled/mixed choice during leaf evaluation.
