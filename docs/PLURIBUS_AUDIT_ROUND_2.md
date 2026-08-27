# Pluribus Architecture Audit: Round 2

Findings: 2.

[R1] P1 — Same-street descendant decisions remain bucketed.
Location: `src/solver/multiway_traversal.cpp:306`
Evidence: Exact private-hand indexing is selected only when `state.id == root_->public_state.id`. If action returns to the hero later on the current street, the descendant row uses `table.lookup(...)` and `table.bucket_count()` at lines 308 and 319. Distinct live-street hands can therefore share regrets after the root action.
Impact: The rolling-precision boundary remains lossy within the current round, so blockers, draws, and hand-specific responses can collapse after a raise or re-raise.
Fix: Use canonical private-hand IDs for every decision on the root street. Switch to future buckets only after a street transition.

[R3] P1 — Continuation policies are not learned or mixed by search.
Location: `src/solver/multiway_continuation_selector.cpp:19`
Evidence: `select` returns the single largest positive regret. Traversal passes that one policy directly to the leaf evaluator (`src/solver/multiway_traversal.cpp:251`) and performs no sampling or mixing. `set_regrets` is called only by selector tests, so runtime search never updates these rows.
Impact: Production search still evaluates one externally fixed continuation per information set and cannot solve the four-policy continuation game.
Fix: Own continuation regrets in solver state, update them during traversal, and sample or mix from the regret-matched distribution with deterministic trajectory seeding.

R1: remaining — exact identity covers only the initial root node.
R2: fixed — multithreaded rollout evaluation with shared mutable context is rejected.
R3: remaining — selector rows are externally injected and reduced to argmax.

Builds and tests were not run.
