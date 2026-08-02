# Engineering Playbook for a 6-Max No-Limit Hold'em Cash Poker Agent

**Target event:** mixed field of humans and autonomous/RTA-style agents  
**Preparation window:** 30 days  
**Maximum machine:** AMD Ryzen 9 9950X3D, 64 GB RAM, NVIDIA RTX 5080 16 GB  
**Decision deadline:** 20 seconds  
**Constraint:** no persistent or personalized opponent modeling  
**Game:** 6-max no-limit Texas hold'em cash; exact stack, rake, blind, ante, straddle, and rebuy rules must be supplied by the event organizer

---

## 1. Executive recommendation

Build a **compressed equilibrium-oriented blueprint plus an anytime real-time resolver**. The closest proven architecture is Pluribus, but it must be redesigned for the 64 GB RAM ceiling.

The recommended system is:

1. A correct, fast 6-player NLHE engine with exact stack, side-pot, rake, and min-raise handling.
2. A coarse 6-max blueprint trained with **linear external-sampling Monte Carlo CFR**, negative-regret pruning, lazy allocation, and compact integer storage.
3. A fixed, anonymous within-hand public-belief tracker. It uses only the current hand's public actions and a generic reference strategy. It never stores player identities or cross-hand statistics.
4. A **street-root nested resolver** that inserts the opponent's exact observed bet size, represents the current street losslessly, and uses abstraction only for future streets.
5. A robust depth-limit evaluator based on several fixed continuation strategies rather than a neural value network.
6. Strict timing, memory, and fallback controls so the bot always returns a legal action before 20 seconds.
7. A gauntlet and duplicate-deal evaluation process designed to detect abstraction attacks, implementation bugs, and strategy collapse.

This is not a literal Pluribus reproduction. Published Pluribus figures include roughly 12,400 CPU core-hours for its blueprint, less than 512 GB during training, and up to 128 GB for the compressed live blueprint. Your 30-day budget on 16 physical cores is 11,520 physical-core-hours, which is in the same numerical range, but the RAM ceiling is much lower and the exact CPU generations are not comparable. The practical response is to use substantially fewer buckets, fewer blueprint actions, more compact storage, and stronger online refinement.

The central strategic idea is:

> Spend the month producing a broad, difficult-to-exploit generic strategy. Spend the 20-second clock adapting that strategy to the exact public state and bet sizes, not to a named opponent.

There is no credible guarantee of winning. Six-player cash poker is a general-sum imperfect-information game, and standard CFR convergence guarantees do not extend cleanly from two-player zero-sum games. The goal is therefore empirical robustness, low exploitability in reduced games, and strong cross-play—not a mathematically certified 6-player Nash equilibrium.

### Implementation progress: bounded lazy multi-street traversal

Completed production substeps:

- Added allocation-free `blueprint`, `fold_biased`, `call_biased`, and `raise_biased` continuation-policy kernels with the planned 5x weighting and normalization.
- Added allocation-free fixed-policy leaf expectation evaluation over caller-owned action, probability, and value buffers.
- Extended multiway external sampling from one root ply to configurable same-street decision depth.
- Added lazy public-state and sparse-row admission only along visited trajectories.
- Added deterministic opponent action sampling, traverser action enumeration, exact terminal settlement, and trajectory-local delta rollback on capacity exhaustion.
- Connected each fixed continuation policy to the production typed leaf callback through caller-owned, allocation-free provider views; invalid provider data is rejected as a non-finite leaf value.
- Rotated traversers deterministically across the stable root seat order so two-through-six-seat sparse rows can train without changing root-only export.
- Bounded configured same-street recursion to 1-64 decision plies.
- Preserved root-only strategy export and fixed-order worker merge behavior.
- Removed per-node strategy, action-value, reach, request, and CFR-delta heap allocations from recursive traversal by using caller-owned fixed buffers and direct bounded delta emission.
- Reused one terminal adapter per traversal and one reconstructed betting state per visited node while preserving trajectory seeding and fixed-order merge semantics.
- Added dedicated continuation-policy and recursive same-street traversal test files covering all four profiles, validation and normalization, caller-owned continuation-leaf views and provider failures, depth bounds and cutoff, valid opponent traversers, deterministic batch seat rotation, lazy admission, opponent sampling, terminal and leaf paths, and capacity rollback. Test code was written and statically inspected; tests were not executed.
- Added dedicated direct public-chance sampler and cross-street traversal test files covering deterministic paths, distinct seeds, collision exclusion, canonical flop combinations and exact probabilities, single-card turn/river sampling, bounded chance depth and action-menu width, oversized root-menu rejection before row admission, lazy chance/transition admission, next-street bucket traversal and reach cancellation, all-in runouts to exact river terminals, and cross-street rollback. Test code was written and statically inspected; tests were not executed.
- Added deterministic direct public-chance sampling without enumerating the legal chance menu, using fixed-size scratch, collision exclusion, unbiased bounded selection, sorted three-card flop combinations, and exact `1 / C(n,k)` probability metadata.
- Extended lazy traversal across action, sampled chance, and street-transition children with next-street action abstraction and bucket lookup.
- Propagated sampled public chance into both chance reach and proposal reach exactly once, with branch-local restoration before sibling traversal.
- Added configurable zero-through-three public chance boundaries while keeping the constructor source-compatible; zero retains typed-leaf behavior at strategic street boundaries.
- Made all-in board runouts bypass strategic cutoffs, sample through the river, and settle with the exact terminal adapter.
- Kept the sampled chance hot path allocation-free, bypassed redundant coordinator lookup and full root/private validation for already-admitted traversal states, removed a duplicate chance-state reconstruction, and replaced recursive overflow vectors with fixed eight-entry strategy/value scratch.

Completed review substep:

- Statically reviewed direct bounded RNG, canonical flop unranking, exact chance probabilities, collision filtering, chance/proposal reach cancellation, action/chance depth boundaries, typed admission order, regular transitions, all-in runouts, future bucket lookup, rollback, arbitrary-seat traversal, admitted-state fast paths, inline action scratch, API compatibility, and dedicated test assumptions. No build, compile, or test command was run.
- Confirmed the direct preflop sampler fixture is valid: six fixed private cards leave 46 available cards, giving `C(46,3) = 15180` canonical flop outcomes; turn and river fixtures correctly leave 43 and 42 cards.
- Remediated the recursive scratch P2: the production abstraction needs at most five actions and one exact off-tree insertion needs at most six, so traversal now enforces an explicit eight-action compact limit at the root and before admitting lazily generated menus.
- Final static re-review confirmed oversized root menus fail before sparse-row admission or sampling, generated menus fail before child admission, fixed strategy/value recursion scratch performs no heap allocation, and chance/traversal reach math is unchanged. Persistent vector-backed menu and public-descriptor construction remains the separately documented allocation target. No build, compile, or test command was run.
- Statically re-reviewed external-sampling reach math, nested reach restoration, arbitrary-seat traversal, deterministic batch rotation, recursion bounds, continuation leaf data flow, API compatibility, and dedicated test compile plausibility. No build, compile, or test command was run.
- Confirmed sampled opponent reach and proposal reach are retained until a nested traverser update, traverser reach is restored after action enumeration, sampled-branch reach is restored on return, and rejected trajectories rewind all emitted deltas.
- Fixed the continuation evaluator factory to accept an explicit caller-owned context pointer, preventing a temporary context from binding to a returned evaluator and documenting the lifetime of the context and synchronous provider views.
- Confirmed the P1 integration gates are remediated: fixed continuation policies reach the production leaf callback, and deterministic trajectory rotation permits every root seat to act as traverser when reachable within the configured depth.

Remaining limitations for the next milestone:

- Regular street-transition recursion is opt-in through `max_public_chance_depth`; its default remains zero for compatibility.
- Future-street traversal requires the bucket registry to contain the sampled canonical board tables it may visit.
- Custom root or generated action menus above eight entries are rejected by the compact traversal boundary.
- The fixed policy leaf adapter consumes blueprint action probabilities and continuation action values supplied by the caller; CPU terminal rollouts across future streets are not connected yet.
- Traversal remains logically worker-partitioned and deterministic but is not dispatched concurrently.
- Continuation-policy profile selection across all active seats and common-random-number rollout batches remain to be implemented.
- Profile persistent public-descriptor construction/admission before replacing its vector-backed board, history, and edge payloads with arena-backed compact records.
- Profile public-state admission, child descriptor construction, bucket lookup, terminal settlement, and fixed-order merge before changing their storage or scheduling; these remain the likely allocation and lookup costs.

---

## 2. Decisions that must be frozen in the first 24 hours

The following parameters change the game enough that they cannot be treated as small implementation details:

```yaml
rules:
  seats: 6
  small_blind_bb: 0.5
  big_blind_bb: 1.0
  ante_bb: 0.0
  button_ante_bb: 0.0
  straddles_allowed: false
  initial_stack_bb: 100
  stack_policy: fixed        # fixed | variable | capped
  minimum_buyin_bb: 100
  maximum_buyin_bb: 100
  rake_percent: 0.0
  rake_cap_bb: 0.0
  no_flop_no_drop: true
  run_it_twice: false
  rebuy_policy: none
  time_bank_ms: 20000
```

### 2.1 Why stack and rake must be exact

A 40 BB game, 100 BB game, and 250 BB game are strategically different. The action tree, relevant bet sizes, all-in thresholds, and value of suited hands all change with stack-to-pot ratio. Rake can alter marginal preflop calls and postflop bluff-catching enough that training a rake-free policy for a raked game is unsafe.

If stacks can vary, discretize effective stack into a small number of bands and solve the exact stack online. A reasonable first set is:

- 20–35 BB
- 35–60 BB
- 60–90 BB
- 90–130 BB
- 130–200 BB
- 200+ BB

Do not silently round stack sizes inside the engine. The abstraction may map them, but the legal-action generator and payoff calculation must remain exact.

### 2.2 Clarify what “opponent modeling is forbidden” means

There are two materially different interpretations.

**Interpretation A: no personalized or persistent modeling.** This normally forbids player IDs, cross-hand statistics, adaptive nodelocks, exploitative profiles, and human-specific timing or sizing analysis. It still permits estimating anonymous ranges from actions within the current hand using a fixed strategy model. This is the mode assumed by the recommended design.

**Interpretation B: no inference about opponents' private holdings from their actions.** This would prohibit even ordinary Bayesian range updates. That restriction would remove a fundamental input to rational poker decisions and should be confirmed explicitly.

Implement both modes behind a configuration flag:

```yaml
opponent_inference:
  mode: anonymous_within_hand   # anonymous_within_hand | blockers_only
  persistent_player_ids: false
  cross_hand_statistics: false
  timing_features: false
  chat_or_name_features: false
  nodelocking: false
```

In `anonymous_within_hand` mode, ranges are updated using the fixed generic blueprint or previous anonymous search result. In `blockers_only` mode, ranges are changed only by card removal and impossible-hand filtering. The second mode will be significantly weaker.

---

## 3. Why several tempting approaches are poor fits

### 3.1 A fixed randomized policy such as “30% fold, 70% all-in”

This creates obvious, stable leaks. Humans and bots do not need identity-specific learning to exploit a globally over-aggressive strategy. They can respond immediately with tighter calling ranges and favorable seat selection. Randomness is not the same as strategic balance.

### 3.2 A pure lookup table with nearest-size translation

A blueprint alone is vulnerable at abstraction boundaries. Humans can choose unusual bet sizes specifically to land between the bot's abstract actions. A nearest-size mapping may produce discontinuous reactions: a tiny change in an opponent bet can cause a large strategy change. Exact off-tree action insertion and re-solving are therefore core requirements, not optional polish.

### 3.3 Reproducing full Pluribus

The architecture is relevant; the resource profile is not directly reproducible. Pluribus' published live strategy could use up to 128 GB, double the available RAM. A literal copy also depends on proprietary engineering details and a very large game abstraction. The correct lesson is to copy the structure—coarse blueprint, nested search, continuation policies—not its scale.

### 3.4 DeepStack, ReBeL, Deep CFR, or an end-to-end neural self-play system

These methods are important research, but they are risky foundations for a 30-day 6-max build.

- DeepStack's continual re-solving is a major inspiration, but its learned counterfactual value network was developed for heads-up poker and required a substantial training pipeline.
- ReBeL's main theoretical setting is two-player zero-sum games.
- Deep CFR replaces tables with neural approximators, introducing replay-buffer, stability, calibration, and architecture risk.
- Student of Games and related systems combine search and learned self-play at a scale and complexity inappropriate for a one-month engineering sprint.
- Neural policies may generalize smoothly, but a wrong value model at the resolver leaf can systematically corrupt every decision.

A small neural model may be added later as an auxiliary estimator, but it should not be on the critical path.

### 3.5 Full-game GPU CFR

Recent work shows dramatic GPU parallelism for certain CFR formulations, but the newest 2026 results are preprints and do not provide a mature, directly reusable 6-max NLHE implementation. Large poker trees have irregular branching, sparse information-set access, and large state tables, which do not map as cleanly to a 16 GB GPU as dense benchmark games do. GPU use should focus on work that is naturally batched: equity evaluation, rollout leaves, feature generation, and possibly small dense subgames.

### 3.6 Only using a heads-up postflop solver

A heads-up solver is valuable as a regression oracle for two-player subgames. It does not solve preflop multiway interactions, squeeze dynamics, side pots, changing player counts, or the general-sum nature of six-player cash poker.

---

## 4. System architecture

```text
Tournament protocol
        |
        v
+-----------------------+
| Exact game engine     |
| legal actions/payoffs |
+-----------------------+
        |
        +--------------------+
        |                    |
        v                    v
+------------------+   +-----------------------+
| Blueprint store  |   | Anonymous hand ranges |
| compact strategy |   | 1326 combos/player    |
+------------------+   +-----------------------+
        |                    |
        +---------+----------+
                  v
        +-----------------------+
        | Street-root resolver  |
        | exact observed action |
        | current street exact  |
        | future buckets        |
        +-----------------------+
                  |
                  v
        +-----------------------+
        | Robust leaf evaluator |
        | continuation policies |
        | batched GPU rollouts  |
        +-----------------------+
                  |
                  v
        +-----------------------+
        | Strategy sampler      |
        | deadline watchdog     |
        +-----------------------+
                  |
                  v
             Legal action
```

The system has two operating phases.

### Offline phase

- Generate card/equity features.
- Create hand buckets.
- Train the coarse blueprint.
- Compress strategy data.
- Run cross-play, local best response, and duplicate-deal tests.

### Online phase

- Parse the exact public state.
- Update anonymous ranges.
- Create or reuse a current-street search tree.
- Insert the exact observed off-tree action.
- Solve until the hard internal deadline.
- Sample from the latest stable strategy.
- Return a legal action before the external deadline.

---

## 5. Exact game engine: the highest-priority component

A strategy trained on a subtly incorrect engine is worse than no solver because it will confidently optimize the wrong game.

### 5.1 Recommended implementation language

Use C++20 or Rust for the production engine and resolver. Python may be used for experiment orchestration, analysis, and cluster scripts, but it should not own the live game loop.

### 5.2 State representation

```cpp
struct GameRules {
    uint8_t max_players;
    int32_t small_blind_chips;
    int32_t big_blind_chips;
    int32_t ante_chips;
    int32_t button_ante_chips;
    int32_t initial_stack_chips;
    int32_t min_buyin_chips;
    int32_t max_buyin_chips;
    uint32_t rake_basis_points;
    int32_t rake_cap_chips;
    bool no_flop_no_drop;
    bool straddles_allowed;
};

struct PlayerState {
    int32_t stack;
    int32_t committed_street;
    int32_t committed_hand;
    bool folded;
    bool all_in;
    bool has_acted_since_last_full_raise;
};

struct PublicState {
    Street street;
    std::array<Card, 5> board;
    uint8_t board_count;
    uint8_t button_seat;
    uint8_t acting_seat;
    int32_t pot_before_rake;
    int32_t current_bet;
    int32_t last_full_raise_size;
    std::array<PlayerState, 6> players;
    ActionHistory history;
};
```

Use integer chips, never floating-point chip amounts. Convert to pot fractions only for abstraction selection.

### 5.3 Engine invariants

Every transition must verify:

- Total chips are conserved before rake.
- Folded players never act again.
- All-in players never act again.
- Calling amount is never negative.
- A raise is legal only when it satisfies the minimum full-raise rule, except for a short all-in.
- A short all-in does not incorrectly reopen betting.
- Side pots contain exactly the players eligible to win them.
- Rake is applied according to the event's exact rule and cap.
- Showdown distributes odd chips using the documented seat rule.
- Public cards and private cards never overlap.

### 5.4 Testing

Create exhaustive or property-based tests for:

- Heads-up through six-way preflop sequences.
- Multiple short all-ins.
- A short raise that does not reopen action.
- Three or more side pots.
- Folded contributors to side pots.
- Split pots and odd chips.
- Maximum legal overbet and exact all-in behavior.
- Every possible number of remaining players at each street.

The engine must support deterministic replay from a serialized hand history.

---

## 6. Card representation, equity, and canonicalization

### 6.1 Hand indexing

There are 1,326 unordered two-card combinations. Maintain a fixed canonical index for each combo. A range is therefore a vector of 1,326 nonnegative weights.

```cpp
using Range = std::array<float, 1326>;
```

For hot loops, use aligned arrays and vectorize blocker filtering. A compact `uint16_t` or half-precision representation may be used for stored ranges, but active search ranges should generally remain `float` to avoid normalization error.

### 6.2 Suit isomorphism

Canonicalize strategically equivalent suit permutations. This can reduce public-board and private-hand duplication substantially. The canonicalizer must be tested as a pure function:

```text
canonical(board, hole_cards, history_suit_labels)
    -> canonical_board
    -> canonical_hole_cards
    -> inverse_suit_map
```

Do not use suit-isomorphism shortcuts that merge strategically distinct blocker relations.

### 6.3 Equity engine

Use an exact hand evaluator and two evaluation modes:

1. Exact enumeration when the remaining card space is small.
2. Batched Monte Carlo for early streets or multiway feature generation.

The RTX 5080 is well suited to batched evaluation because thousands of board completions and hand matchups can be processed in parallel. Keep CPU and GPU evaluators cross-validated against one another.

---

## 7. Information abstraction

The blueprint must compress private information. The runtime resolver should avoid abstraction on the current street whenever possible.

### 7.1 Preflop

Use all 169 standard equivalence classes as an initial feature, but preserve exact combo blockers where the runtime search needs them. Position, action history, stack band, and number of active players remain separate information-set dimensions.

### 7.2 Postflop blueprint buckets

A realistic 64 GB starting configuration is:

```yaml
blueprint_buckets:
  flop: 96
  turn: 128
  river: 192
```

A more aggressive configuration, allowed only after memory measurement, is:

```yaml
blueprint_buckets:
  flop: 128
  turn: 160
  river: 224
```

Pluribus used 200 buckets per postflop street in its blueprint. The lower counts above are deliberate RAM concessions.

### 7.3 Runtime search abstraction

```yaml
resolver_private_state:
  current_street: lossless
  next_street_buckets: 256
  later_street_buckets: 256
```

“Lossless current street” means the resolver tracks every legal private two-card combination separately after blocker filtering. Future public cards and later private-state evaluations may be bucketed.

If memory or runtime is too high, use 192 future buckets before reducing current-street resolution.

### 7.4 Bucket features

Do not cluster only by scalar hand strength. Use a feature vector that includes future potential and multiway behavior.

Recommended features:

- Current equity versus one generic opponent.
- Current equity versus two through five generic opponents.
- Equity quantiles rather than only mean equity.
- Histograms of future equity after one-card and two-card runouts.
- Probability of improving to pair, two pair, trips, straight, flush, full house, quads, or straight flush.
- Nut potential and reverse-implied-odds indicators.
- Blocker features for nut flushes, straights, and full houses.
- Board texture: pairedness, monotonicity, connectedness, high-card structure.
- Draw type and redraw potential.
- Expected showdown rank conditional on reaching later streets.

Potential-aware abstraction literature supports comparing distributions of future hand strength rather than only expectations. An approximate Earth Mover's Distance or Wasserstein distance between equity histograms is appropriate.

### 7.5 Clustering process

1. Enumerate or sample canonical public boards.
2. For every compatible private hand, generate the feature vector.
3. Normalize feature dimensions by robust scale.
4. Use k-means as the fast baseline.
5. For the final model, test k-medoids or hierarchical refinement with approximate Earth Mover's Distance on equity histograms.
6. Split buckets with the highest internal strategic variance.
7. Merge buckets that remain behaviorally indistinguishable in small exact solves.

### 7.6 Validate the abstraction

For a sample of states, solve a higher-resolution local game and compare action distributions inside each proposed bucket. A bucket is suspicious when hands assigned to it choose materially different actions or have materially different counterfactual values.

Useful diagnostics:

```text
within_bucket_action_JS_divergence
within_bucket_value_stddev
bucket_population
bucket_board_coverage
bucket_position_coverage
```

Prioritize splitting buckets with both high population and high action divergence.

---

## 8. Action abstraction

The blueprint needs a compact action grammar. The resolver must be able to add exact observed actions.

### 8.1 Principles

- Always retain fold, check, call, and all-in when legal.
- Use fewer sizes in multiway pots than heads-up pots.
- Use fewer sizes at low stack-to-pot ratio.
- Include small, medium, large, and all-in actions where strategically relevant.
- Never translate an exact opponent action without preserving the exact pot and stack consequences.
- Treat preflop and postflop separately.

### 8.2 Recommended preflop blueprint grammar

All sizes below are templates, not blindly legal actions. They must be rounded to legal chips and clipped by stack.

```yaml
preflop_actions:
  unopened:
    - fold
    - call
    - raise_to_2.25bb
    - raise_to_3.0bb
    - raise_to_4.5bb
    - all_in

  facing_single_open:
    - fold
    - call
    - raise_to_3.0x_in_position
    - raise_to_3.5x_out_of_position
    - all_in

  facing_open_and_callers:
    - fold
    - call
    - raise_to_open_plus_1.0x_per_caller
    - pot_raise
    - all_in

  facing_three_bet_or_more:
    - fold
    - call
    - raise_to_2.2x
    - all_in
```

Context pruning is mandatory. For example, do not retain six near-duplicate raise sizes when the effective stack is 18 BB.

### 8.3 Recommended postflop blueprint grammar

For the first bet on a street:

```yaml
postflop_first_bet:
  active_players_4_to_6:
    - 0.33_pot
    - 0.75_pot
    - all_in

  active_players_3:
    - 0.33_pot
    - 0.75_pot
    - 1.25_pot
    - all_in

  active_players_2:
    - 0.25_pot
    - 0.50_pot
    - 1.00_pot
    - 1.50_pot
    - all_in
```

For raises:

```yaml
postflop_raise:
  - minimum_raise
  - 0.75_pot_after_call
  - 1.25_pot_after_call
  - all_in
```

Prune by stack-to-pot ratio:

```yaml
spr_pruning:
  below_1_5:
    first_bet: [0.33_pot, all_in]
    raise: [minimum_raise, all_in]
  from_1_5_to_4:
    first_bet: [0.33_pot, 0.75_pot, all_in]
    raise: [minimum_raise, 0.75_pot_after_call, all_in]
  above_4:
    use_full_context_set: true
```

### 8.4 Exact off-tree action insertion

When an opponent uses an unrepresented size `x`:

1. Reconstruct the search from the start of the current street.
2. Add the exact action `x` at the observed decision node.
3. Also retain the nearest lower and upper blueprint sizes if legal.
4. Retain all-in.
5. Re-solve the current-street subgame.

This prevents humans from exploiting a static mapping boundary.

### 8.5 Pseudo-harmonic translation fallback

If the resolver cannot run, map an unrepresented pot-normalized size `x` between abstract sizes `A` and `B` probabilistically:

```text
P(map x to A) = ((B - x) * (1 + A)) / ((B - A) * (1 + x))
P(map x to B) = 1 - P(map x to A)
```

Here `A`, `x`, and `B` are measured consistently as fractions of the pot before the bet.

Use this only as a fallback. It is better than deterministic nearest-size translation, but exact insertion plus nested re-solving is preferred.

---

## 9. Blueprint training algorithm

### 9.1 Recommended baseline

Use **linear external-sampling MCCFR** with alternating traversers.

Why this variant:

- Full CFR is too expensive for the entire game.
- Outcome sampling has high variance.
- External sampling samples chance and non-traverser actions while enumerating the traverser's actions, giving a practical balance between cost and variance.
- Linear weighting gives later, stronger iterations more influence.
- It is compatible with negative-regret pruning and lazy storage.

### 9.2 Core regret-matching rule

For information set `I` and action `a`, let cumulative regret be `R(I,a)`.

```text
R_plus(I,a) = max(R(I,a), 0)

sigma(I,a) = R_plus(I,a) / sum_b R_plus(I,b)
```

If all positive regrets are zero, use a uniform strategy over legal abstract actions.

For linear CFR, iteration `t` receives weight proportional to `t` when accumulating the average strategy or updating weighted quantities.

### 9.3 External-sampling pseudocode

```text
function TRAIN_BLUEPRINT(num_iterations):
    initialize compact regret store R
    initialize compact strategy accumulator S

    for t in 1 .. num_iterations:
        traverser = t mod num_players
        deal chance seed
        EXTERNAL_TRAVERSE(root, traverser, t, reach_traverser=1)

        if discount_checkpoint(t):
            discount_early_regrets_and_strategy(R, S, t)

        if snapshot_checkpoint(t):
            write_atomic_snapshot(R, S, metadata)


function EXTERNAL_TRAVERSE(state, traverser, t, reach_traverser):
    if terminal(state):
        return payoff_to_traverser(state)

    if chance_node(state):
        outcome = sample_chance(state)
        return EXTERNAL_TRAVERSE(next(state, outcome), traverser, t,
                                 reach_traverser)

    player = acting_player(state)
    I = abstract_information_set(state, player)
    actions = abstract_legal_actions(state)
    strategy = regret_matching(R[I], actions)

    if player != traverser:
        a = sample(strategy)
        return EXTERNAL_TRAVERSE(next(state, a), traverser, t,
                                 reach_traverser)

    values = array(len(actions))
    node_value = 0

    for a in actions:
        values[a] = EXTERNAL_TRAVERSE(next(state, a), traverser, t,
                                      reach_traverser * strategy[a])
        node_value += strategy[a] * values[a]

    for a in actions:
        instantaneous_regret = values[a] - node_value
        R[I,a] += linear_weight(t) * instantaneous_regret
        S[I,a] += linear_weight(t) * reach_traverser * strategy[a]

    return node_value
```

Production code must include sampled reach and importance corrections appropriate to the exact MCCFR formulation. The pseudocode is conceptual and should be checked against the original external-sampling MCCFR derivation before implementation.

### 9.4 Negative-regret pruning

Actions with sufficiently negative cumulative regret can be skipped most of the time. Periodically run an unpruned traversal so an action can recover.

Recommended initial policy:

```yaml
negative_regret_pruning:
  enabled: true
  warmup_iterations: 500000
  prune_threshold_scaled: -300000000
  pruned_fraction_target: 0.90
  full_traversal_every: 20
```

The exact threshold depends on the regret scaling and payoff units. The listed value follows the scale reported in the Pluribus supplement and must not be copied without matching its integer scaling.

### 9.5 Lazy allocation

Do not allocate a full record for every theoretically possible abstract information set. Allocate an information-set record only when a traversal reaches it.

A compact record may be:

```cpp
struct InfoSetRecord {
    uint32_t action_offset;
    uint32_t strategy_offset;
    uint16_t action_mask;
    uint8_t action_count;
    uint8_t flags;
};
```

Regrets can be stored in flat `int32_t` arrays. Average strategy can be written to snapshots and omitted from the hot training set when necessary.

Avoid `std::unordered_map` per information set. Its pointer and allocator overhead can consume more memory than the strategy data. Prefer a compact custom hash table, sorted key/value blocks, or a minimal perfect hash after a discovery pass.

### 9.6 Regret scaling and overflow

Use fixed-point integer regret when possible:

```text
scaled_regret = round(real_regret * REGRET_SCALE)
```

Choose `REGRET_SCALE` through measurement. Monitor saturation and overflow. Use 64-bit temporaries for updates even if stored values are 32-bit. Periodically rescale all values if needed.

### 9.7 Parallelization

The Ryzen 9 9950X3D has 16 physical cores and 32 threads. Start with physical-core workers; add SMT only after benchmarking.

Two safe implementation choices are:

**Worker-local mini-batches with periodic reduction**

- Each worker traverses using a local delta table.
- Every fixed number of traversals, deltas are merged into the global table.
- Easier to reason about, but local tables consume memory.

**Sharded global store**

- Hash information sets into many shards.
- Each shard has a compact lock or carefully designed atomic update path.
- Lower duplicate memory, but synchronization can dominate.

Do not begin with unrestricted Hogwild writes. First make single-threaded training deterministic on toy games, then validate the parallel result by exploitability or cross-play in small games.

Suggested training allocation:

```yaml
training_threads:
  physical_workers: 16
  smt_workers_initially: 0
  io_thread: 1
  gpu_feed_thread: 1
  merge_interval_traversals: 4096
```

The I/O and GPU feed threads may share SMT contexts after profiling.

### 9.8 Average strategy versus current strategy

In two-player zero-sum CFR, the weighted average strategy is the standard output. In multiplayer general-sum games, average-profile convergence is not guaranteed in the same way. Therefore save and evaluate:

- The weighted average strategy.
- The current regret-matched strategy.
- Exponentially weighted late-iteration averages.
- Several late snapshots.

Choose the deployed blueprint by a cross-play and robustness matrix, not by theory alone.

### 9.9 Optional variance reduction

Variance-reduced MCCFR with learned or tabular baselines can improve sample efficiency greatly. Add it only after the baseline trainer is correct and producing snapshots. A broken baseline estimator can bias or destabilize training.

A safe progression is:

1. State-value moving-average baseline.
2. Information-set/action baseline.
3. Neural baseline only if there is spare time and strong validation.

---

## 10. Anonymous within-hand range tracking

### 10.1 What is stored

For each active seat, maintain a distribution over compatible two-card combinations:

```cpp
struct AnonymousRanges {
    std::array<Range, 6> seat_range;
};
```

Initialize each opponent to a generic legal range at the start of every hand. Reset all data at hand end. Never index the model by player name, account, device, or prior hand behavior.

### 10.2 Bayesian action update

Given a fixed reference strategy `sigma_ref`, observed action `a`, public state `s`, and opponent hand `h`:

```text
r_new(h) proportional_to r_old(h) * sigma_ref(a | I(s,h))
```

Then:

1. Set blocked or impossible hands to zero.
2. Normalize the range.
3. Apply a probability floor before normalization to avoid accidental collapse from abstraction zeros.

```text
likelihood = max(sigma_ref(a | I(s,h)), epsilon)
r_new(h) = r_old(h) * likelihood
r_new = normalize(r_new)
```

A practical floor is `1e-5` to `1e-4`, tuned by tests. Do not use the floor to hide a broken strategy store.

### 10.3 Reference strategy selection

Use only anonymous, fixed sources:

- The deployed blueprint for preflop and unsolved streets.
- The previous resolver strategy for actions generated inside the same current-street solve.
- A pseudo-harmonic mixture when the exact action was not represented and no search result exists.

Do not update this reference from a human's historical behavior.

### 10.4 Strict blockers-only mode

When the event forbids any action-conditioned range inference:

```text
r_new(h) = 0 if h conflicts with public or known private cards
r_new(h) = r_old(h) otherwise
normalize
```

The resolver should then place more weight on robust continuation-policy mixtures and less on fine-grained exploitative-looking responses.

---

## 11. Real-time nested street-root resolving

### 11.1 Why solve from the start of the street

Re-solving only from the current decision can create inconsistent beliefs and allow earlier off-tree actions to be mishandled. The recommended method reconstructs the subgame from the beginning of the current street, including all actions already observed on that street.

For the bot's actual private hand, actions it already took are constrained to the realized action. Opponent actions are not treated as proof that they would always take those actions with every hand; their ranges are updated through the fixed reference strategy.

### 11.2 Search state

The resolver input is:

```cpp
struct ResolveRequest {
    PublicState street_root;
    ActionHistory observed_current_street;
    std::array<Range, 6> anonymous_ranges;
    Combo our_actual_hand;
    uint8_t our_seat;
    int64_t hard_deadline_ns;
    ResolverConfig config;
};
```

### 11.3 Search depth policy

Use an adaptive policy based on street, active players, and tree size.

```yaml
search_depth:
  preflop:
    stop_at: flop_chance_node

  flop_active_players_4_to_6:
    stop_at_first_of:
      - turn_chance_node
      - second_raise_on_flop

  flop_active_players_3:
    stop_at_first_of:
      - turn_chance_node
      - third_aggressive_action

  flop_heads_up:
    prefer: terminal
    fallback: turn_or_depth_limit

  turn:
    prefer: river_chance_or_terminal

  river:
    prefer: terminal
```

The policy is a starting point. Runtime measurement should determine the exact cutoff.

### 11.4 Continuation-policy leaf evaluation

At every depth-limited leaf, do not assign a single scalar value from one arbitrary continuation. Give each remaining player a small menu of fixed continuation policies and let the solving process account for the policy choice.

Use four policies initially:

1. `blueprint`: unmodified blueprint continuation.
2. `fold_biased`: multiply fold probability by 5 and renormalize.
3. `call_biased`: multiply call/check-call probability by 5 and renormalize.
4. `raise_biased`: multiply bet/raise probability by 5 and renormalize.

```text
function biased_policy(base, target_action_class, factor=5):
    p = base
    for action in actions:
        if class(action) == target_action_class:
            p[action] *= factor
    return normalize(p)
```

This follows the successful continuation-strategy concept used in Pluribus. It is cheaper and safer in a one-month build than training a high-quality 6-player counterfactual value network.

Optional after validation: add `small_bet_biased` and `large_bet_biased`, creating six policies. Do not increase `K` until runtime and leaf variance are measured because the joint continuation-policy space grows rapidly with the number of active players.

### 11.5 Leaf rollout evaluation

Use common random numbers across candidate continuation choices to reduce comparison noise.

```text
function EVALUATE_LEAF(leaf, ranges, continuation_profile, seed_batch):
    total = zero_vector(num_players)

    for seed in seed_batch:
        sample compatible private hands from ranges
        sample remaining board cards
        roll out fixed continuation policies to terminal
        total += exact_terminal_payoffs

    return total / len(seed_batch)
```

Batch thousands of rollouts on the GPU. Keep a CPU reference implementation for correctness tests.

### 11.6 Solver selection

Choose based on estimated subgame size:

- Large tree or early street: linear external-sampling MCCFR.
- Small river/turn tree: vectorized linear CFR or discounted CFR over all private combos.
- Tiny heads-up regression case: exact or high-precision solve.

The resolver is an anytime algorithm. It must publish a stable strategy snapshot periodically so the watchdog can stop it at any moment.

### 11.7 Decision pseudocode

```text
function DECIDE(observation, deadline_20s):
    internal_deadline = deadline_20s - 1500ms

    state = parse_and_validate(observation)
    legal = generate_exact_legal_actions(state)

    if len(legal) == 1:
        return legal[0]

    update_anonymous_ranges(state.latest_public_action)

    fallback = blueprint_or_safe_translated_strategy(state, legal)
    publish_candidate(fallback)

    request = build_street_root_resolve_request(state, internal_deadline)
    tree = build_or_reuse_search_tree(request)
    insert_exact_observed_actions(tree, state.current_street_history)

    while now() < internal_deadline:
        run_resolver_batch(tree)
        if strategy_is_numerically_valid(tree.root_strategy):
            publish_candidate(tree.root_strategy)

    strategy = latest_published_candidate()
    strategy = project_to_exact_legal_actions(strategy, legal)
    action = sample_with_deterministic_auditable_rng(strategy)

    assert action in legal
    log_decision(state, strategy, action, timing, model_hash)
    return action
```

### 11.8 Time budget

Use a hard internal deadline. A recommended allocation is:

```yaml
online_timing_ms:
  parse_validate: 300
  range_update: 300
  tree_build_or_reuse: 1200
  solve_and_rollout: 15000
  final_projection_and_sampling: 400
  transport_and_watchdog_reserve: 2800
  hard_internal_deadline: 17200
```

The exact numbers depend on the tournament protocol. A conservative internal cutoff between 17.0 and 18.5 seconds is preferable to any timeout risk.

### 11.9 Fallback hierarchy

At every decision, a legal candidate must already exist before expensive search begins.

1. Current decision's latest stable resolver strategy.
2. Earlier stable strategy from the same current-street search.
3. Exact blueprint action if represented.
4. Pseudo-harmonic translation of the blueprint.
5. Minimal safe legal policy.

The minimal safe policy should not be a simplistic all-in default. A reasonable emergency order is:

- Check when legal.
- Call only when an exact pot-odds and terminal-equity sanity rule permits it.
- Otherwise fold.
- Use a raise only when it is already present in a validated cached strategy.

This emergency policy should almost never run; its purpose is fault containment.

### 11.10 Sampling, not argmax

Do not always choose the highest-probability action. Deterministic argmax play makes the policy easier to exploit and changes the strategic object produced by CFR.

Use a reproducible, auditable seed:

```text
seed = HMAC_SHA256(
    secret_run_key,
    tournament_id || hand_id || seat || decision_index || model_hash
)
```

Do not derive randomness from wall-clock time alone.

---

## 12. Memory design

The 64 GB ceiling must include the operating system, allocator fragmentation, file cache, protocol process, and crash headroom.

### 12.1 Training budget

```text
Operating system and safety headroom        8–10 GB
Regret records and action metadata         38–44 GB
Bucket tables and card/equity caches         3–5 GB
Thread-local buffers and merge deltas        3–5 GB
Checkpoint staging                           1–2 GB
--------------------------------------------------
Maximum target resident set                 56–60 GB
```

Set a process memory limit below the physical maximum and fail early when exceeded.

### 12.2 Live-play budget

```text
Compressed blueprint                       18–24 GB
Current resolver tree and regrets           16–22 GB
Ranges, card tables, bucket tables           3–5 GB
Leaf rollout buffers and pinned staging      2–4 GB
Protocol, logs, allocator reserve            2–3 GB
OS and emergency headroom                    8–10 GB
--------------------------------------------------
Maximum target resident set                 54–60 GB
```

### 12.3 Blueprint compression

Recommended live format:

- Flat arrays, not pointer graphs.
- Delta-coded or succinct information-set keys.
- `uint8_t` action probabilities summing to 255 for deployed strategy.
- Action masks to omit impossible entries.
- Block compression for cold strategy segments.
- Memory-map cold postflop blocks only if worst-case page-fault latency is acceptable.
- Keep preflop and common flop nodes resident.

Quantization rule:

```text
q_i = round(255 * p_i)
adjust largest q_i so sum(q_i) = 255
p_i_runtime = q_i / 255
```

Measure exploitability or cross-play degradation after quantization. Use `uint16_t` probabilities for sensitive nodes if 8-bit quantization changes actions materially.

### 12.4 Resolver allocation

Use a monotonic arena per search or per street. Reset the arena in constant time after the hand or street. Avoid general-purpose heap allocation in traversal loops.

---

## 13. GPU role

The RTX 5080 has 16 GB VRAM. It should be treated as a high-throughput accelerator, not a storage device for the whole game.

Best uses:

- Batched hand evaluation.
- Multiway equity feature generation.
- Future runout histograms for abstraction.
- Batched leaf rollouts with common random seeds.
- Small dense vectorized subgames.
- Optional inference for a small auxiliary value model.

Poor uses under this schedule:

- Full sparse 6-max regret table.
- Irregular pointer-heavy traversal.
- A new custom GPU CFR framework built from scratch during the final week.

Design the GPU path so a CPU fallback exists. A driver reset or GPU error must not cause a timeout.

---

## 14. Thirty-day execution plan

The schedule assumes one machine running continuously and disciplined scope control.

### Days 1–2: freeze rules and legal interpretation

Deliverables:

- Signed-off game-rule configuration.
- Written interpretation of the opponent-model restriction.
- Protocol simulator.
- Exact decision-time semantics.
- Initial memory and latency budgets.

Go/no-go gate:

- No strategy training begins until stack, rake, blinds, and legal raise rules are known.

### Days 1–4: engine and test harness

Deliverables:

- Exact legal-action generator.
- Side-pot and rake calculator.
- Deterministic hand replay.
- Fast evaluator.
- Property tests and randomized differential tests.

Gate:

- At least one million random hands without chip-conservation or legal-action failure.

### Days 3–7: abstraction pipeline

Deliverables:

- Canonical card indexing.
- GPU equity generator.
- Feature dataset.
- First postflop buckets.
- Action grammar and SPR pruning.

Gate:

- Bucket assignment is deterministic and fully covered for every legal state sampled.

### Days 5–9: MCCFR trainer on toy games

Deliverables:

- Kuhn poker and Leduc poker correctness.
- Single-thread external-sampling MCCFR.
- Linear weighting.
- Snapshot format.
- Parallel update prototype.

Gate:

- Convergence trend on exact small games.
- Reproducible snapshot checksums.
- No integer overflow or memory leak.

### Days 8–10: first reduced 6-max training run

Use deliberately small buckets and action sets. The objective is an end-to-end artifact, not strength.

Deliverables:

- Complete coarse strategy.
- Compression and reload.
- Bot can play full hands from the artifact.

Critical pivot gate on Day 10:

- If the blueprint exceeds memory, reduce buckets and action sizes immediately.
- Do not pivot to a new neural architecture.
- Preserve current-street online search quality; sacrifice blueprint resolution first.

### Days 10–17: blueprint v1 trains continuously

While training runs, implement:

- Anonymous range tracker.
- Street-root search reconstruction.
- Exact off-tree action insertion.
- Runtime tree arena.
- Stable-strategy publication.
- Deadline watchdog.

Save daily snapshots and evaluate them against older snapshots and scripted policies.

### Days 15–20: robust leaf evaluator

Deliverables:

- Four continuation policies.
- GPU rollout batches.
- Common-random-number evaluation.
- CPU/GPU parity tests.
- Resolver depth policy.

Gate:

- Leaf value variance is measured.
- A resolver batch produces a valid strategy under the memory cap.

### Days 18–22: full integration

Deliverables:

- End-to-end 20-second decision path.
- Exact off-tree re-solving.
- Cache and tree reuse within a hand.
- Strict fallback chain.
- Full logging and replay.

Gate:

- Zero timeouts in a 100,000-decision synthetic soak test.
- p99 decision completion before the internal deadline.

### Days 21–25: robustness gauntlet and blueprint v2

Run against:

- Always-fold.
- Always-call.
- Push/fold jam policy.
- Minimum-bet spam.
- Pot-size-only policy.
- Extreme overbet policy.
- Uniform random legal actions.
- Scripted tight-aggressive and loose-aggressive policies.
- Previous blueprint snapshots.
- Resolver disabled versus enabled.
- Deliberately chosen off-tree sizes near abstraction boundaries.

Use results to change abstraction and engineering bugs, not to personalize against a named player.

### Days 24–27: human and adversarial tests

Ask strong human testers to search for stable leaks:

- Weird preflop sizes.
- Tiny block bets.
- Repeated geometric overbets.
- Multiway squeeze sequences.
- Side-pot edge cases.
- Timing and timeout attacks.
- Bet sizes exactly around translation boundaries.

Record public states and failures, not player profiles.

### Day 28: strategy freeze

Freeze:

- Blueprint hash.
- Bucket tables.
- Action configuration.
- Resolver configuration.
- RNG protocol.

After this point, only correctness fixes are allowed.

### Day 29: packaging and deterministic replay

Deliverables:

- Single production executable or supervised process bundle.
- Immutable model files.
- SHA-256 manifest.
- Startup self-test.
- Memory-limit self-check.
- Protocol reconnect behavior.
- Exact replay of logged decisions.

### Day 30: bug fixes only

No last-day strategic experiments. Run tournament-length soak tests, restart tests, GPU-failure tests, corrupted-model tests, and clock-skew tests.

---

## 15. Evaluation plan

### 15.1 Do not trust raw winnings from ordinary hands

Poker payoff variance is enormous. Use duplicate deals and seat rotation:

- Reuse the same shuffled deck across strategy matchups.
- Rotate each policy through all six seats.
- Pair results by deck seed.
- Report confidence intervals by block bootstrap.

### 15.2 Cross-play matrix

Evaluate every serious snapshot against:

- Earlier snapshots.
- Current strategy and average strategy.
- Resolver on and off.
- Different continuation-policy sets.
- All scripted gauntlet agents.

A model that wins only against its immediate training partner is not acceptable.

### 15.3 Reduced-game exploitability tests

Full 6-player exploitability is not tractable. Use:

- Exact best response in tiny poker games.
- Local Best Response on heads-up and reduced abstractions.
- Heads-up postflop solver comparisons on sampled subgames.
- Three-player reduced games where feasible.
- Abstraction-boundary adversaries.

Local Best Response provides a lower bound on exploitability rather than a proof of safety, but it is useful for finding severe leaks.

### 15.4 AIVAT and control variates

AIVAT can reduce variance substantially when suitable value estimates and known action probabilities are available. Implement it only after duplicate deals and paired-seat rotation are correct. An incorrect variance-reduction estimator can be worse than a wide but honest interval.

### 15.5 Required metrics

```yaml
quality_metrics:
  paired_mbb_per_hand: true
  bb_per_100: true
  bootstrap_95_percent_ci: true
  crossplay_minimum_score: true
  lbr_lower_bound: true
  off_tree_boundary_score: true

systems_metrics:
  timeout_rate_target: 0
  invalid_action_rate_target: 0
  p99_decision_ms_target: 17000
  peak_rss_gb_target: 60
  gpu_error_fallback_success: 1.0
  deterministic_replay_success: 1.0
```

Do not select the final model using only mean win rate. Include worst-case gauntlet performance and confidence bounds.

---

## 16. Failure modes and mitigations

### 16.1 Range collapse

**Symptom:** after an unusual action, nearly all opponent combos receive zero probability.

**Mitigation:** likelihood floors, off-tree action mixtures, normalization assertions, and entropy monitoring.

### 16.2 Abstraction boundary attack

**Symptom:** strategy changes sharply for bets just below and above an abstract size.

**Mitigation:** exact action insertion, neighboring sizes in the local search, pseudo-harmonic fallback, and adversarial boundary tests.

### 16.3 Blueprint memory explosion

**Symptom:** lazy information-set discovery grows beyond the 60 GB target.

**Mitigation:** reduce action branches first, then postflop bucket counts; compress keys; remove cold average-strategy data from RAM; retain strong online search.

### 16.4 Resolver tree explosion

**Symptom:** early-street multiway searches consume the entire clock or RAM.

**Mitigation:** adaptive depth, fewer future buckets, chance sampling, action pruning, and leaf continuation evaluation.

### 16.5 Multiplayer CFR oscillation

**Symptom:** current snapshots cycle and cross-play results are unstable.

**Mitigation:** save multiple output forms, use population cross-play, consider late-iteration mixtures, and avoid assuming average-strategy convergence.

### 16.6 Self-play overfitting

**Symptom:** later snapshots beat their parent but lose to simple scripted agents or humans.

**Mitigation:** diverse fixed gauntlet, snapshot population, seat rotation, and worst-case model selection.

### 16.7 Deterministic action leakage

**Symptom:** identical public states always produce the same action.

**Mitigation:** sample the complete mixed strategy with a secret reproducible seed.

### 16.8 Protocol timeout

**Symptom:** a strong search result is lost because the process submits late.

**Mitigation:** internal deadline, pre-published fallback, independent watchdog, bounded logging, and transport reserve.

### 16.9 Side-pot or raise-rule bug

**Symptom:** invalid action, wrong terminal payoff, or training on illegal trees.

**Mitigation:** exact rule tests, differential simulation, and rejection of any hand whose replay does not conserve chips.

### 16.10 GPU dependency failure

**Symptom:** driver error stalls a leaf rollout and misses the deadline.

**Mitigation:** asynchronous GPU jobs, timeout cancellation, CPU fallback, and no blocking synchronization near the decision deadline.

### 16.11 Quantization damage

**Symptom:** compressed blueprint performs materially worse than the full snapshot.

**Mitigation:** node-level divergence checks, 16-bit storage for sensitive nodes, and cross-play before deployment.

### 16.12 Human exploitation of fixed timing

**Symptom:** decision latency reveals whether the state is common, difficult, or off-tree.

**Mitigation:** never use timing as an opponent feature; optionally add a bounded submission delay policy only if tournament rules allow and only after guaranteeing the deadline. Strategy correctness is more important than timing camouflage.

---


---

## 19. Suggested runtime configuration

```yaml
resolver:
  hard_internal_deadline_ms: 17200
  publish_every_iterations: 64
  exact_current_street_hands: true
  future_buckets: 256
  continuation_policies:
    - blueprint
    - fold_biased_5x
    - call_biased_5x
    - raise_biased_5x
  action_insertion:
    include_exact_observed: true
    include_neighbor_below: true
    include_neighbor_above: true
    always_include_all_in: true
  solver_selection:
    small_tree: vector_linear_cfr
    large_tree: external_sampling_linear_mccfr
  gpu_rollout:
    enabled: true
    batch_size: 8192
    common_random_numbers: true
    cpu_fallback: true
  memory:
    max_resident_gb: 60
    search_arena_gb: 20
  safety:
    require_precomputed_fallback: true
    reject_nan_strategy: true
    reject_negative_probability: true
    legal_projection: true
```

---

## 20. Strategy-store key design

A blueprint information-set key should contain only strategically relevant public and private abstractions, never opponent identity.

```text
InfoSetKey = hash(
    ruleset_id,
    stack_band,
    seat_relative_to_button,
    active_player_mask,
    street,
    canonical_board_id,
    private_bucket,
    abstract_action_history,
    committed_stack_bands,
    side_pot_signature,
    acting_seat
)
```

Be careful with imperfect recall. Merging histories that require different strategic responses may save memory but can create severe leaks. Preserve at least:

- Street-by-street aggressive-action count.
- Position of each aggressor.
- Pot and effective-stack band.
- Whether the pot was limped, single-raised, three-bet, or four-bet.
- Number of callers.
- All-in and side-pot structure.
- Current-street action sequence in enough detail to reconstruct legal raises.

The exact engine history remains lossless even when the blueprint key is abstract.

---

## 21. Training and deployment observability

Every snapshot should include:

```json
{
  "model_version": "2026-xx-xx-v17",
  "rules_hash": "...",
  "bucket_hash": "...",
  "action_config_hash": "...",
  "trainer_commit": "...",
  "iterations": 123456789,
  "traversals": 987654321,
  "wall_clock_seconds": 123456,
  "peak_rss_bytes": 59000000000,
  "regret_scale": 100000,
  "allocated_infosets": 123456789,
  "pruned_action_fraction": 0.89,
  "rng_seed_manifest": "..."
}
```

Each live decision log should contain public information only, plus the bot's own hand in a protected replay log if permitted:

```json
{
  "hand_id": "...",
  "decision_index": 7,
  "public_state_hash": "...",
  "legal_actions": ["fold", "call", "raise:3400"],
  "final_strategy": [0.18, 0.54, 0.28],
  "sampled_action": "call",
  "blueprint_ms": 4,
  "range_update_ms": 12,
  "tree_build_ms": 340,
  "solve_ms": 15500,
  "iterations": 4812,
  "peak_search_bytes": 17123456789,
  "fallback_level": 0,
  "model_hash": "..."
}
```

Do not log or infer names, timing profiles, chat, or historical tendencies of opponents.

---

## 22. Practical model-selection rule

At the end of Day 27, create a score for each candidate snapshot:

```text
score =
    0.35 * lower_confidence_bound_crossplay
  + 0.25 * lower_confidence_bound_gauntlet
  + 0.15 * off_tree_boundary_robustness
  + 0.10 * reduced_game_LBR_score
  + 0.10 * latency_reliability_score
  + 0.05 * memory_safety_score
```

The exact weights may change, but the principle is important: select for robust lower-bound performance and systems reliability, not the highest noisy mean.

Reject any candidate with:

- Any invalid action.
- Any tournament-clock timeout.
- Peak RSS above the safety target.
- Unexplained strategy NaNs or zero-sum normalization failures.
- Catastrophic loss to a simple gauntlet policy.
- Strong discontinuity around an action-abstraction boundary.

---

## 23. Minimal viable fallback architecture

A full 6-max blueprint may not be ready by Day 10. The fallback plan is still solver-based and should be prepared from the beginning.

### Tier 1: preferred

- Coarse 6-max blueprint.
- Anonymous range filtering.
- Current-street nested resolver.
- Four continuation policies.

### Tier 2: reduced blueprint

- Much smaller preflop and postflop bucket counts.
- Same exact engine and action insertion.
- More runtime effort in postflop resolving.

### Tier 3: rule-based preflop plus resolver-heavy postflop

- Position/stack/action-conditioned preflop charts generated from small solves or coarse CFR.
- Exact current-state postflop resolver using generic ranges.
- This is weaker multiway but still more defensible than random or static all-in play.

### Tier 4: safe tournament completion

- Validated static mixed strategy.
- Pseudo-harmonic translation.
- No resolver dependency.
- Guaranteed legal action and zero timeout.

Do not make a late pivot to an untested end-to-end neural agent.

---

## 24. What is likely to create the largest competitive advantage

In this particular event, the largest gains are unlikely to come from adding a sophisticated neural network. They are more likely to come from:

1. **Exact rule implementation.** Many experimental agents lose through side-pot, min-raise, stack, and protocol errors.
2. **Off-tree robustness.** Humans will deliberately choose sizes that stress action abstraction.
3. **Reliable real-time search.** A coarse blueprint becomes much stronger when the exact current street is resolved.
4. **Good leaf continuation diversity.** One blueprint rollout is too brittle; several biased continuations protect the depth limit.
5. **Memory-efficient data layout.** Saving 2× memory can be equivalent to doubling abstraction resolution.
6. **A zero-timeout watchdog.** A single timeout can cost far more than a marginal strategy improvement gains.
7. **Cross-play and adversarial evaluation.** Self-play score alone can hide large common-mode weaknesses.
8. **Correct randomized deployment.** Sampling the mixed strategy prevents deterministic exploitation.

---

## 25. Final recommendation

The strongest feasible route under the stated restrictions is a **Pluribus-inspired, non-neural, anonymous, abstraction-plus-search agent**:

- Train a coarse six-player blueprint with linear external-sampling MCCFR.
- Use aggressive memory engineering: lazy allocation, int32 regrets, quantized deployed probabilities, flat arrays, and compact keys.
- Keep current-street private hands lossless in the runtime resolver.
- Insert exact observed bet sizes and solve from the start of the street.
- Evaluate depth-limited leaves with four robust continuation strategies.
- Use the GPU for batched equity and rollouts, not as the home of the entire CFR tree.
- Enforce a hard internal deadline and always maintain a legal fallback strategy.
- Evaluate by duplicate deals, seat rotation, cross-play, reduced-game LBR, and explicit abstraction-boundary attacks.
- Never store player identity, cross-hand statistics, timing profiles, or nodelocks.

The hardest part is not inventing a new poker-AI algorithm. It is integrating known strong ideas into a correct, compact, deadline-safe system in 30 days. A modest abstraction with reliable nested search is more likely to survive and outperform humans and brittle agents than an ambitious neural system that is only partly trained or poorly validated.

---

## 26. Research references

### Core multiplayer poker systems

1. Noam Brown and Tuomas Sandholm, **“Superhuman AI for multiplayer poker”**, *Science*, 2019.  
   Main paper: https://noambrown.github.io/papers/19-Science-Superhuman.pdf  
   Supplement: https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf

2. Matej Moravčík et al., **“DeepStack: Expert-level artificial intelligence in heads-up no-limit poker”**, *Science*, 2017.  
   https://arxiv.org/abs/1701.01724

3. Noam Brown and Tuomas Sandholm, **“Safe and Nested Subgame Solving for Imperfect-Information Games”**, NeurIPS 2017.  
   https://arxiv.org/abs/1705.02955

4. Noam Brown, Adam Lerer, Sam Gross, and Tuomas Sandholm, **“Deep Counterfactual Regret Minimization”**, ICML 2019.  
   https://proceedings.mlr.press/v97/brown19b.html

5. Noam Brown, Anton Bakhtin, Adam Lerer, and Qucheng Gong, **“Combining Deep Reinforcement Learning and Search for Imperfect-Information Games”** (ReBeL), NeurIPS 2020.  
   https://arxiv.org/abs/2007.13544

6. Martin Schmid et al., **“Player of Games” / Student of Games**, 2021–2023.  
   https://arxiv.org/abs/2112.03178

### CFR and depth-limited solving

7. Marc Lanctot et al., **“Monte Carlo Sampling for Regret Minimization in Extensive Games”**, NeurIPS 2009.  
   https://proceedings.neurips.cc/paper/2009/hash/00411460f7c92d2124a67ea0f4cb5f85-Abstract.html

8. Noam Brown and Tuomas Sandholm, **“Solving Imperfect-Information Games via Discounted Regret Minimization”**, AAAI 2019.  
   https://arxiv.org/abs/1809.04040

9. Martin Schmid et al., **“Variance Reduction in Monte Carlo Counterfactual Regret Minimization”**, AAAI 2019.  
   https://ojs.aaai.org/index.php/AAAI/article/view/4048

10. Noam Brown, Tuomas Sandholm, and Brandon Amos, **“Depth-Limited Solving for Imperfect-Information Games”**, NeurIPS 2018.  
    https://arxiv.org/abs/1805.08195

11. Richard Gibson, **“Regret Minimization in Games with Incomplete Information”**, 2013.  
    https://arxiv.org/abs/1305.0034

### Abstraction and action translation

12. Sam Ganzfried and Tuomas Sandholm, **“Potential-Aware Imperfect-Recall Abstraction with Earth Mover's Distance in Imperfect-Information Games”**, AAAI 2014.  
    https://ojs.aaai.org/index.php/AAAI/article/view/8816

13. Sam Ganzfried and Tuomas Sandholm, **“Action Translation in Extensive-Form Games with Large Action Spaces: Axioms, Paradoxes, and the Pseudo-Harmonic Mapping”**, IJCAI 2013.  
    https://www.cs.cmu.edu/~sandholm/reverse%20mapping.ijcai13.pdf

### Evaluation

14. Neil Burch et al., **“AIVAT: A New Variance Reduction Technique for Agent Evaluation in Imperfect Information Games”**, AAAI 2018.  
    https://ojs.aaai.org/index.php/AAAI/article/view/11481

15. Johannes Heinrich and David Silver, **“Local Best Response: Effective Exploitability Evaluation in Large Imperfect-Information Games”**, 2016.  
    https://arxiv.org/abs/1612.07547

### Open-source implementation references

16. OpenSpiel, Google DeepMind.  
    https://github.com/google-deepmind/open_spiel

17. PokerRL.  
    https://github.com/EricSteinberger/PokerRL

18. TexasSolver.  
    https://github.com/bupticybee/TexasSolver

19. DecisionHoldem.  
    https://github.com/AI-Decision/DecisionHoldem

### Recent work to monitor, not use as the critical path

20. **“Parallelizing Counterfactual Regret Minimization”**, 2026 preprint.  
    https://arxiv.org/abs/2605.14277

21. **“Real-Time Parallel Counterfactual Regret Minimization”**, 2026 preprint.  
    https://arxiv.org/abs/2605.19928

### Hardware references

22. AMD Ryzen 9 9950X3D official product specifications.  
    https://www.amd.com/en/products/processors/desktops/ryzen/9000-series/amd-ryzen-9-9950x3d.html

23. NVIDIA GeForce RTX 5080 official specifications.  
    https://www.nvidia.com/en-us/geforce/graphics-cards/50-series/rtx-5080/

---

## 27. Important limitations

- Published poker systems do not provide a turnkey open-source 6-max NLHE cash agent at this strength level.
- Six-player cash poker is general-sum; equilibrium guarantees are weaker than in heads-up zero-sum poker.
- Exact performance depends heavily on the event's stack, rake, ante, and protocol rules.
- The 64 GB cap forces a coarser blueprint than published Pluribus.
- The 30-day schedule leaves little room for a new neural training pipeline.
- Continuation-policy leaf values are an approximation and must be stress-tested.
- “No opponent modeling” must be defined by the organizer; anonymous within-hand Bayesian range filtering may or may not be permitted under their wording.
- Human testers can still discover leaks not covered by scripted evaluation.

The plan above is therefore an engineering strategy for maximizing robustness and expected competitiveness under tight resource constraints, not a claim of solved six-player poker.
