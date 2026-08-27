# Pluribus Architecture Audit: Round 5

Findings: 7.

[R9] P1 - Range export aliases every exact hand to the actual hand.
Location: `src/solver/multiway_search_session.cpp:192`
Evidence: `export_hero_policy` initializes every combo lookup with `root.root_bucket` and changes it only for bucketed roots. Resolver roots set `root_uses_exact_private_hand`, so all exported rows read the actual hand's exact bucket.
Impact: Range-wide strategy and later Bayes updates use one hand's policy for every possible hero hand.
Fix: In exact mode, set the storage bucket to `hole_index(combo)` for each exported row.

[R10] P1 - Exact row ids are reused as blueprint bucket ids.
Location: `src/solver/multiway_traversal.cpp:343`
Evidence: Same-street traversal sets `bucket` to the canonical 0..1325 hand id, then passes that same value to `blueprint_policy_->strategy_into` at line 367. Blueprint rows are keyed by abstraction buckets. An exact id that numerically exists as a blueprint bucket returns the strategy for a different hand class; other ids miss.
Impact: Non-traverser actions and sampling reach use unrelated blueprint strategies, corrupting regret updates and the solved policy.
Fix: Keep separate exact storage and abstract blueprint bucket ids; map the sampled hole through `table.lookup` for blueprint access.

[R11] P1 - Root-only blueprint fallback ignores its root key.
Location: `src/solver/multiway_resolver.cpp:158`
Evidence: `apply_blueprint_policy` checks only model identity and matching actions. It never compares the snapshot's `public_state`, `infoset`, or `bucket`, although `MultiwayBlueprintSnapshot` stores all three.
Impact: Any request sharing the model and part of the action menu can receive a policy trained for another public state, seat, or private bucket.
Fix: Require the complete snapshot root key, or query `MultiwayBlueprintStore` with the reconstructed infoset and bucket.

[R12] P1 - Stable fallback cache aliases all private hands.
Location: `src/solver/multiway_resolver.cpp:657`
Evidence: The cache key is only model identity, public-state id, and action menu. Runtime search stores an actual-hand policy at line 933. A later fallback for another hand at the same public state hits that entry.
Impact: The resolver can play one private hand using another hand's strategy.
Fix: Include hero seat and canonical exact hand or bucket in cache lookup and storage keys.

[R13] P1 - Any folded seat disables runtime search.
Location: `src/solver/multiway_resolver.cpp:331`
Evidence: Eligibility returns `FoldedSeat` for every folded non-hero slot, and `make_search_root` rejects it again at line 356. The request contract explicitly keeps seat ids valid after players fold.
Impact: Normal postflop six-max states after any fold always fall back and never run nested search.
Fix: Retain folded seats in public/terminal accounting, but require ranges and traversal participation only for live or all-in seats as appropriate.

[R14] P1 - Resolver fabricates positional and odd-chip metadata.
Location: `src/solver/multiway_resolver.cpp:369`
Evidence: `make_search_root` always creates seat order `0..N-1` and sets both `next_street_first_seat` and `odd_chip_first_seat` to 0. `MultiwayRootSnapshot` supports an arbitrary cyclic first seat, but the resolver request carries no values to preserve it.
Impact: Non-seat-0 positional layouts start later streets from the wrong player and award tied-pot odd chips from the wrong seat, changing utilities and regrets.
Fix: Add validated seat order, next-street first seat, and odd-chip first seat to the resolver boundary and copy them into the root.

[R15] P1 - Decision deadline is not enforced inside a batch.
Location: `src/solver/multiway_resolver.cpp:546`
Evidence: The budget is checked only before `runner.run`. Workers receive no deadline and test `cancelled_` only for worker errors, so an expired deadline cannot stop the current trajectory batch.
Impact: A large or slow first batch can exceed the decision deadline by its full remaining runtime, violating bounded live-search latency.
Fix: Pass a cooperative deadline/cancellation token into workers, check it between trajectories, and merge only a deterministic clean completed unit.
