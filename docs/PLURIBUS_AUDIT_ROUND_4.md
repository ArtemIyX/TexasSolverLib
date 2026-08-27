# Pluribus Architecture Audit: Round 4

Findings: 4.

[R5] P1 — Continuation regrets use the wrong utility and weight.
Location: `src/solver/multiway_traversal.cpp:294`
Evidence: Every traverser appends raw leaf values to the row keyed by `continuation_actor`. Merge applies `values - node_value` without requiring `traverser == actor` or applying counterfactual/sampling reach.
Impact: Opponent utilities and unequally sampled paths corrupt continuation-policy regrets, producing an invalid policy mixture.
Fix: Update only the continuation actor's traversal and apply the external-sampling importance weight used by normal infoset updates.

[R6] P1 — One private-card collision aborts the entire search batch.
Location: `src/solver/multiway_terminal_adapter.cpp:140`
Evidence: `try_sample_into` returns false for a normal one-shot range collision, but `sample_private_deal` throws. The worker catches it as fatal and `MultiwayRootBatchRunner::run` rethrows at `src/solver/multiway_traversal.cpp:723`.
Impact: Broad multi-player ranges almost inevitably force runtime search to `ResourceExhausted` fallback before a clean batch completes.
Fix: Treat proposal collisions as discarded trajectories, not worker errors.

[R7] P1 — Decision-session preflight omits exact-row storage.
Location: `src/solver/multiway_resolver.cpp:756`
Evidence: `begin_decision_session` preflights unexpanded `max_sparse_values`, then `MultiwaySolverCoordinator` adds up to `min(max_sparse_rows, max_public_states) * 1326 * 8` values at `src/solver/multiway_solver.cpp:337`.
Impact: A session admitted under the memory cap can later allocate far beyond it or fail allocation.
Fix: Preflight the same immutable exact-row capacity passed to the coordinator.

[R8] P1 — Continuation delta buffers bypass memory preflight.
Location: `src/solver/multiway_memory.cpp:111`
Evidence: Preflight counts one `MultiwayWorkerDelta` buffer per worker. Each worker also reserves `max_worker_delta_entries` `MultiwayContinuationDelta` objects at `include/solver/multiway_traversal.hpp:145`, plus merge pointers, but none are counted.
Impact: Admitted search memory can exceed the operating or hard cap and fail during runner construction or merging.
Fix: Add continuation streams and merge scratch to mandatory worker-delta admission.

Builds and tests were not run.
