# Multiway 6-Max Blueprint Upgrade Plan

## Goal

Build an offline, coarse 2-6 player NLHE blueprint trainer. It must train and
export a generic 6-max policy without changing `HUNLFlatDCFR` behavior.

The production target is not an exact six-player Nash solution. It is a
memory-bounded, reproducible, multiway-aware strategy that supplies ranges,
fallback actions, and continuation policies to a later street-root resolver.

## Current starting point

The repository already contains the important low-level multiway contracts:

- `MultiwayState` supports 2-6 seats, betting progression, all-ins, min-raise
  reopening rules, and street transitions.
- `multiway_terminal` builds side pots and settles rake, split pots, and odd
  chips.
- `MultiwayCompiledPrivateRanges` samples compatible private deals.
- `MultiwayTerminalAdapter` connects a coordinator-owned public state to board
  chance and terminal settlement.
- `multiway_cfr` defines full-tree and external-sampling update math.
- `MultiwaySolverCoordinator` owns public-state admission, sparse rows, and
  deterministic worker-delta merging.

The missing layer is a complete multiway blueprint pipeline: action menus,
bucket mapping, lazy public-tree expansion, sampled traversal, checkpointing,
and a compact policy export. Do not adapt the HUNL sampled solver into a
multiway solver. Reuse its design lessons only.

## Freeze before implementation

The following inputs define a different game and must be versioned before
training begins:

```yaml
rules:
  seats: 6
  stack_bb: 100
  small_blind_bb: 0.5
  big_blind_bb: 1.0
  ante_bb: 0.0
  rake_percent: 0.0
  rake_cap_bb: 0.0
  no_flop_no_drop: true

blueprint:
  stack_bands: [20-35, 35-60, 60-90, 90-130, 130-200, 200+]
  postflop_buckets: { flop: 96, turn: 128, river: 192 }
  sampling: external_sampling_mccfr
  public_chance_sampling: true
  storage: sparse_action_major
```

Also freeze:

- button/seat order and odd-chip rule;
- preflop and postflop action templates;
- exact chip rounding rule for templates;
- bucket-model version and feature schema;
- deterministic seed derivation;
- model, rules, action, and bucket hashes.

Do not train a rake-free blueprint for a raked target game.

## Architecture boundary

```text
Exact multiway engine
  -> canonical public-state builder
  -> lazy sparse public tree
  -> external-sampling MCCFR traversal
  -> worker-local deltas
  -> fixed-order coordinator merge
  -> checkpoint / compact blueprint export
  -> later resolver and range service
```

The exact engine remains lossless for stacks, contributions, legal actions,
side pots, and public cards. Abstraction applies only to deliberately chosen
features: action templates, stack bands, public-board canonicalization, and
private-hand buckets.

## Phase 0: establish contracts and inventory

### Work

1. Add `docs/multiway_model_contract.md` with the frozen rules and all hashes.
2. Create a `MultiwayBlueprintConfig` separate from `MultiwayCFRConfig`.
3. Add a top-level `MultiwayModelIdentity` containing rules, action, bucket,
   terminal, and code-schema versions.
4. State explicitly that values are chips inside the trainer. Convert only at
   user-facing boundaries.
5. Audit every existing multiway boundary for a hidden heads-up assumption.

### Suggested files

- `include/solver/multiway_blueprint_config.hpp`
- `src/solver/multiway_blueprint_config.cpp`
- `include/solver/multiway_model_identity.hpp`
- `src/solver/multiway_model_identity.cpp`

### Acceptance criteria

- Invalid or mismatched identities are rejected before traversal.
- Every checkpoint can identify the exact rules and abstractions that produced
  it.
- `HUNLFlatDCFR` and `HUNLSampledSolver` public APIs remain unchanged.

## Phase 1: canonical public-state construction

### Work

Build a single coordinator-owned component that creates valid
`MultiwayPublicStateDescriptor` objects from the exact engine state.

1. Convert each exact legal action into a `MultiwayActionDescriptor`.
2. Assign stable `action_menu_id` values from the action abstraction version
   and context.
3. Assign deterministic public-state and history identities. The same
   canonical state must receive the same identity within a session.
4. Preserve the complete exact betting snapshot in each public state.
5. Canonicalize only suit-equivalent public boards. Keep an inverse mapping for
   private-hole and action lookup.
6. Build public child descriptors for betting actions, board chance, and street
   transitions. Admit them only through `MultiwaySolverCoordinator`.

### Suggested files

- `include/solver/multiway_public_builder.hpp`
- `src/solver/multiway_public_builder.cpp`
- `include/solver/multiway_canonical.hpp`
- `src/solver/multiway_canonical.cpp`

### Hot-path rules

- Use integer IDs, fixed arrays, and preallocated vectors.
- Do not use strings, hash-map lookup, formatting, or allocations per action.
- A map may be used by the coordinator during state admission, but not inside
  an action/bucket inner loop.

### Acceptance criteria

- Replaying an action path reproduces the exact child snapshot.
- Public-state canonicalization never merges different blocker structures.
- A child cannot be admitted with a different exact pot, stack, board, or
  legal-action result than its parent transition implies.

## Phase 2: multiway action abstraction

### Work

Implement a compact action-menu generator. It selects abstract sizes but
always executes exact chip amounts.

1. Retain fold, check, call, and all-in whenever legal.
2. Generate size templates by street, active-player count, pot, effective
   stack, and stack-to-pot ratio.
3. Use fewer sizes with 3-6 players than in heads-up pots.
4. Deduplicate templates after legal chip rounding and stack clipping.
5. Include action-context metadata in the menu identity: active-player mask,
   street, facing bet, pot/stack band, and position.
6. Record exact target street contribution in every descriptor.

### Initial grammar

```yaml
postflop_first_bet:
  4_to_6_players: [0.33_pot, 0.75_pot, all_in]
  3_players: [0.33_pot, 0.75_pot, 1.25_pot, all_in]
  2_players: [0.25_pot, 0.50_pot, 1.00_pot, 1.50_pot, all_in]

postflop_raise:
  - minimum_raise
  - 0.75_pot_after_call
  - 1.25_pot_after_call
  - all_in
```

Preflop requires separate templates for unopened pots, single opens,
open-plus-callers, and 3-bet-or-larger pots.

### Suggested files

- `include/solver/multiway_action_abstraction.hpp`
- `src/solver/multiway_action_abstraction.cpp`

### Acceptance criteria

- Menus contain no duplicate resulting states.
- Every produced descriptor can be applied by `MultiwayState`.
- All-in and short-raise behavior remains exact.

## Phase 3: private-hand abstraction

### Work

Implement a versioned bucket model for each street. A multiway bucket must not
be based only on heads-up equity.

1. Preserve exact 1,326-combo ranges at the boundary.
2. Produce a bucket ID for every compatible combo at a canonical public board.
3. Start with 96 flop, 128 turn, and 192 river buckets.
4. Feature vectors must include equity against one through five generic
   opponents, equity distributions over future runouts, draw/nut potential,
   blocker features, board texture, and showdown-rank features.
5. Store immutable bucket tables indexed by model identity and canonical board.
6. Make the current training blueprint bucketed. Reserve lossless current
   street private hands for the later online resolver.

### Suggested files

- `include/solver/multiway_bucket_model.hpp`
- `src/solver/multiway_bucket_model.cpp`
- `include/solver/multiway_bucket_features.hpp`
- `src/solver/multiway_bucket_features.cpp`
- `tools/build_multiway_buckets_main.cpp`

### Acceptance criteria

- Bucket lookup rejects incompatible board/hole combinations.
- Suit-isomorphic states map consistently.
- Diagnostics identify bucket population, value variance, and action
  divergence.

## Phase 4: lazy public-tree storage

### Work

Extend the coordinator so traversal can request children without materializing
the full 6-max game tree.

1. Preallocate bounded state, edge, row, and delta arenas from the memory
   preflight.
2. Expand only visited public states.
3. Store action edges contiguously by state.
4. Keep sparse rows action-major: `row[action][bucket]`.
5. Store only regret and average-strategy accumulators. Do not add dense reach,
   value, or global strategy tables.
6. Reject a batch before mutation if its worst-case allocation exceeds the
   configured hard cap.

### Suggested files

- `include/solver/multiway_storage.hpp`
- `src/solver/multiway_storage.cpp`
- `include/solver/multiway_memory.hpp`
- `src/solver/multiway_memory.cpp`

### Memory policy

- Warn at 48 GB resident estimate.
- Reduce action branches before bucket counts when possible.
- Reject configurations above a 60 GB estimate.
- Do not give each worker graph-sized scratch memory.

### Acceptance criteria

- No full flop graph is built for production training.
- Allocations are visible in preflight and profile output.
- Capacity exhaustion fails predictably without corrupting an existing
  checkpoint.

## Phase 5: external-sampling MCCFR traversal

### Work

Implement one traversal that uses the existing multiway private sampling,
terminal adapter, CFR math, and coordinator contracts.

1. Sample a joint private deal once per trajectory.
2. Sample public chance outcomes, including a three-card flop as one chance
   event when appropriate.
3. At a traverser decision, enumerate legal abstract actions and calculate
   counterfactual regret deltas.
4. At non-traverser decisions, sample from regret-matched strategy.
5. Update every seat according to a fixed traverser schedule.
6. Use `make_multiway_external_sampling_request` so sampling reach comes from
   the compiled private-deal contract, never reconstructed ad hoc.
7. Resolve terminal utilities only through `MultiwayTerminalAdapter`.
8. Record discarded incompatible private deals separately from accepted
   trajectories.

### Suggested files

- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_traversal.cpp`
- `include/solver/multiway_scheduler.hpp`
- `src/solver/multiway_scheduler.cpp`

### Determinism

Derive RNG input from `(master_seed, batch_id, trajectory_id, traverser_id)`.
Partition trajectory IDs statically. Workers must emit deltas locally, sort by
the documented fixed order, and merge only through the coordinator.

### Acceptance criteria

- One worker and multiple workers produce bitwise-identical merged results for
  the same configuration.
- No worker mutates public-state storage or sparse rows directly.
- Traversal performs no heap allocation in per-action, per-bucket, terminal,
  or merge inner loops.

## Phase 6: training session, checkpoints, and export

### Work

1. Add a session object that runs bounded batches and publishes immutable
   checkpoints.
2. Make checkpoint writing atomic: write a temporary artifact, validate its
   manifest, then publish it.
3. Export compact average strategies only. Quantize deployed probabilities
   after comparing them with the in-memory policy.
4. Retain root-action and selected public-state query APIs. Do not export a
   dense full-tree policy for use during a timed decision.
5. Store diagnostics: traversals, discarded deals, state/row counts, memory,
   merge time, seed manifest, and all model identities.

### Suggested files

- `include/solver/multiway_blueprint_trainer.hpp`
- `src/solver/multiway_blueprint_trainer.cpp`
- `include/solver/multiway_checkpoint.hpp`
- `src/solver/multiway_checkpoint.cpp`
- `include/solver/multiway_export.hpp`
- `src/solver/multiway_export.cpp`

### Acceptance criteria

- Resume produces the same result as an uninterrupted run at a batch boundary.
- A corrupted or identity-mismatched checkpoint is rejected.
- Exported probabilities are legal, finite, non-negative, and normalized.

## Phase 7: resolver-facing interfaces

This phase does not implement the full real-time resolver. It provides the
interfaces it will require.

1. Query a blueprint policy from an exact public state plus a bucketed private
   hand.
2. Update anonymous within-hand ranges from the fixed blueprint only.
3. Expose exact observed action insertion as a resolver request, not as a
   mutation of the offline blueprint.
4. Support continuation-policy lookup at depth limits.
5. Keep all player identity and cross-hand data out of keys, artifacts, and
   logs.

### Suggested files

- `include/solver/multiway_blueprint_query.hpp`
- `src/solver/multiway_blueprint_query.cpp`
- `include/solver/multiway_range_update.hpp`
- `src/solver/multiway_range_update.cpp`

## Validation sequence

Add tests with each phase. Do not wait for the trainer to be complete.

1. Exact engine: 2-6 seats, folds, all-ins, short raises, side pots, odd chips,
   rake, and street transitions.
2. Public builder: parent/child replay, canonical board identity, exact action
   descriptors, and menu deduplication.
3. Buckets: compatible-card validation, deterministic lookup, suit mapping,
   and version mismatch rejection.
4. Traversal: tiny two-player and three-player games with hand-computed
   updates; terminal utility signs and chip conservation.
5. Sampling: proposal reach, collision discards, board chance probability, and
   seeded reproducibility.
6. Parallel batches: deterministic fixed-order merge and worker-count
   invariance.
7. Checkpoints: resume equivalence, corruption rejection, and export
   normalization.
8. Evaluation: duplicate deals, seat rotation, cross-play, simple policy
   gauntlet, and reduced-game NashConv or local-best-response diagnostics.

## Rollout order

1. Phase 0 and Phase 1.
2. Phase 2 with exact engine property tests.
3. Phase 3 bucket build and lookup only.
4. Phase 4 storage and memory preflight.
5. Phase 5 traversal in a tiny 2-player game, then 3-player, then 6-player.
6. Phase 6 offline checkpoint/export.
7. Phase 7 resolver interfaces.

Do not start full 6-max training until the 2-player and 3-player reduced
games have deterministic replay, correct terminal values, bounded memory, and
stable cross-play behavior.

## Explicit non-goals for this upgrade

- No poker-client automation, scraping, clicking, stealth, account, or session
  code.
- No persistent or player-specific opponent model.
- No neural value network on the critical path.
- No dense full-game tree, full reach table, or full policy export.
- No mutation of the exact HUNL solver to force multiway support.
- No SIMD work until profiles show row math is a material bottleneck.

## Definition of done

The upgrade is complete when a user can provide a versioned 2-6 player ruleset,
ranges, action abstraction, and bucket model; run a deterministic,
memory-preflighted external-sampling training session; resume from a validated
checkpoint; and query a compact, legal, normalized multiway blueprint policy
with diagnostics. The policy must preserve exact public game semantics while
remaining coarse enough to fit the 64 GB training limit.
