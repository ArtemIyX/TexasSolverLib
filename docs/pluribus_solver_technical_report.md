# Pluribus Poker Solver: Technical Architecture, Open Implementations, and a 64 GB “Pluribus-Lite” Design

**Research date:** 2026-08-27  
**Scope:** poker solving / decision engine only. UI, screen scraping, casino automation, player payments, and the human-match experiment are intentionally out of scope.  
**Target reader:** C++/Rust/Python systems programmer or an LLM coding agent implementing a solver.

---

## 0. Evidence labels used in this document

This report deliberately separates facts from implementation claims and from proposed engineering choices.

- **[PLURIBUS-ORIGINAL]** — directly stated in Brown & Sandholm's 2019 *Science* paper or its supplementary material.
- **[PAPER]** — directly supported by another research paper cited by Pluribus or closely related work.
- **[REIMPLEMENTATION]** — behavior or benchmark claimed by an open-source reimplementation; useful evidence, but not equivalent to the original Pluribus implementation and not independently peer reviewed.
- **[PROPOSED-LITE]** — an engineering proposal in this report for a practical 64 GB implementation. It is not a claim about the original Pluribus.
- **[ESTIMATE]** — back-of-the-envelope capacity/performance reasoning. Measure it in the target implementation before relying on it.

This distinction is important because the original Pluribus source code was **not released**. Brown & Sandholm published substantial pseudocode and technical detail, but public GitHub repositories are independent recreations.

---

## 1. Executive summary

Pluribus is best understood as a **two-level tabular game-theoretic solver**, not a neural-network poker bot.

Its core decision architecture is:

```text
                        OFFLINE
                           |
                           v
                +---------------------+
                | Abstract 6-max NLHE |
                +---------------------+
                           |
                           v
            External-sampling Linear MCCFR
            + negative-regret pruning
                           |
                           v
                 Blueprint strategy
                           |
                           | loaded at play time
                           v
                        ONLINE
                           |
                current public history
                           |
              Bayes-updated hand ranges
                           |
                           v
        Depth-limited imperfect-information search
        - starts at current betting-round root
        - current street uses lossless card states
        - future streets use lossy buckets
        - off-tree actions are inserted and re-solved
        - leaf values come from a portfolio of
          blueprint continuation strategies
                           |
                           v
                  sample mixed action
```

The original system did **not** use a learned policy/value neural network. It did **not** train on human hand histories. It did **not** adapt to individual opponents during the published experiment. It learned by self-play using counterfactual regret minimization and then improved decisions with real-time re-solving.

The most important Pluribus idea for practical six-player solving is not merely MCCFR. It is the combination of:

1. a coarse but global **blueprint**;
2. aggressive action/card abstraction;
3. **depth-limited online solving**;
4. a range belief over all possible private hands;
5. strategically diverse **continuation policies at search leaves**;
6. nested re-solving when opponents choose actions outside the abstraction.

Brown & Sandholm explicitly describe depth-limited search as the most important improvement for making six-player poker tractable, estimating that it reduced compute and memory requirements by at least five orders of magnitude compared with naively solving large multiway subgames to terminal states.

The original hardware is also instructive:

- blueprint training: one 64-core shared-memory node for about 8 days / about 12,400 CPU-core-hours, using **less than 0.5 TB RAM**;
- live play: one 28-core machine with **128 GB RAM**;
- **no GPU** was used.

A 64 GB implementation is therefore not achieved by simply compiling Pluribus on a smaller computer. You must reduce the blueprint and online search state. Fortunately, modern reimplementations show that this is quite plausible. In particular, `conorarmstrong/pluribus` reports a 200M-iteration six-max blueprint with 12 EMD buckets per postflop street, 128.5M infosets and a 4.3 GB exported strategy. That is not proof of superhuman strength, but it demonstrates that a Pluribus-style architecture can be compressed dramatically.

### Recommended practical direction

For a serious 64 GB project, this report recommends:

- **Rust or C++** for the hot solver core;
- exact/lossless 169-class preflop representation;
- start with **12–24 postflop blueprint buckets**, then experimentally scale toward 32–48 if memory permits;
- richer **128–256 bucket future-street abstraction inside online search**;
- lossless current-street information in online search, matching the Pluribus principle;
- a compact action abstraction, with rich preflop choices and much smaller turn/river menus;
- lazy infoset allocation;
- `int32` regrets with saturation/flooring;
- no in-memory postflop running average strategy; use periodic current-strategy snapshots, as Pluribus did;
- packed/quantized immutable blueprint storage for runtime;
- a 1326-combo range tracker per active player;
- depth-limited re-solving using the four Pluribus continuation policies;
- exact/vector river and preferably turn solving when only two players remain;
- out-of-core LMDB/mmap storage only if training state exceeds the RAM target.

Do **not** treat “better than humans” as an architectural property. A 64 GB solver may be extremely strong, but strength must be established with statistically meaningful evaluation against strong agents and, ideally, skilled humans. Reducing abstraction precision can introduce exploitable errors.

---

## 2. What Pluribus actually solves

### 2.1 Game

**[PLURIBUS-ORIGINAL]** Pluribus targeted six-player no-limit Texas Hold'em with:

- 6 seats;
- 100 big-blind effective stacks;
- blinds $50/$100 in the paper's experimental game;
- standard 52-card deck;
- four betting rounds: preflop, flop, turn, river;
- continuous legal bet/raise amounts in the real game, but a discrete action abstraction inside the solver.

A hand is an imperfect-information extensive-form game. A concrete internal game state can be represented as:

```text
State {
    street
    button / seat order
    active_players_bitset
    all_in_players_bitset
    folded_players_bitset
    stacks[6]
    committed_total[6]
    committed_street[6]
    pot / side pots
    min_raise_to
    last_full_raise_size
    acting_seat
    public_board[0..5]
    private_cards[6][2]       // only present in a sampled traversal state
    public_action_history
}
```

For the game-theoretic solver, however, the key object is not a full state but an **information set**: all concrete histories a player cannot distinguish because they share the same observations available to that player.

### 2.2 Public state versus information set

A useful implementation distinction:

```text
PublicStateKey = information visible to everybody
               = board + betting/action history + stack/pot state + active seats

InfoSetKey(player) = PublicStateKey
                   + player's private-card representation
                   + abstraction identifiers
```

The online Pluribus subgame root is not one hidden state. It is a **probability distribution over concrete hidden states inside one public state**.

This single fact explains why a chess-style “search from the current node” architecture is wrong for poker.

---

## 3. What Pluribus is NOT

For implementation planning, several misconceptions should be eliminated immediately.

### 3.1 Not a neural policy

**[PLURIBUS-ORIGINAL]** There is no neural policy network selecting actions. The blueprint is a tabular strategy over abstract information sets.

### 3.2 Not a value-network system

The published Pluribus depth-limited search does **not** require a DeepStack/ReBeL-style neural value function. It gets leaf robustness by giving each remaining player a choice among several complete blueprint-derived continuation strategies.

Modern reimplementations sometimes add value networks. That can be a good optimization, but it is a deviation from the original architecture.

### 3.3 Not imitation learning

**[PLURIBUS-ORIGINAL]** The solver does not need human poker data. The global strategy comes from self-play.

### 3.4 Not an opponent-specific exploiter

The published system did not build a persistent model of a particular opponent and intentionally deviate toward exploitative play. It assumes opponent actions can be interpreted using Pluribus's own strategy when constructing the online root distribution. This assumption is part of what the paper calls **unsafe search**.

### 3.5 Not “just CFR over full Hold'em”

Full six-max NLHE is far too large. Pluribus depends critically on abstraction, sampling, pruning, sparse allocation, and limited online subgames.

---

## 4. Core CFR concepts required to implement Pluribus

### 4.1 Counterfactual regret

For information set `I` belonging to player `i`, and legal action `a`, define instantaneous counterfactual regret:

```math
r_t(I,a) = v_i(σ^t_{I→a}, I) - v_i(σ^t, I)
```

where:

- `σ^t` is the current strategy profile;
- `σ^t_{I→a}` is the same strategy except the player deterministically chooses `a` at `I`;
- `v_i` is counterfactual value, weighted by the reach probabilities of chance and the other players but not by player `i`'s own probability of reaching `I`.

Cumulative regret:

```math
R_T(I,a) = Σ_{t=1..T} r_t(I,a)
```

Regret matching converts positive cumulative regrets into the next strategy:

```math
R⁺(I,a) = max(R(I,a), 0)
```

```math
σ(I,a) = R⁺(I,a) / Σ_b R⁺(I,b)
```

If all positive regrets are zero, use a uniform strategy over legal actions.

### 4.2 Why counterfactual reach is important

A player must learn what it *would* have gained by choosing another action even when its current policy rarely reaches the information set. Counterfactual reach weighting lets CFR decompose global regret into per-information-set regret.

### 4.3 External-sampling MCCFR

**[PLURIBUS-ORIGINAL]** The blueprint is trained using **external-sampling Monte Carlo CFR**.

For the traversing player:

- enumerate / evaluate every legal traverser action at each traverser infoset;
- sample chance outcomes;
- sample other players' actions from their current strategies.

Conceptual traversal:

```text
traverse(state, traverser):
    if terminal:
        return utility(traverser)

    if chance:
        outcome <- sample_chance()
        return traverse(next(state, outcome), traverser)

    p <- acting_player(state)
    I <- infoset(state, p)
    sigma <- regret_match(regret[I])

    if p != traverser:
        a <- sample(sigma)
        return traverse(next(state, a), traverser)

    for every legal action a:
        value[a] <- traverse(next(state, a), traverser)

    node_value <- sum_a sigma[a] * value[a]

    for every a:
        regret[I,a] += value[a] - node_value

    return node_value
```

Production code must include reach/importance weighting and strategy accumulation correctly; the pseudocode above is intentionally conceptual.

### 4.4 Why MCCFR is appropriate

A full traversal of the abstract six-player game is still enormous. Sampling chance and non-traverser actions reduces an iteration to a tractable stochastic estimate while retaining the regret-minimization framework.

---

## 5. Linear CFR in Pluribus

### 5.1 Principle

Later iterations are generally based on better strategies. Linear CFR reduces the influence of poor early iterations by weighting later iterations more strongly.

**[PLURIBUS-ORIGINAL]** Pluribus used linear weighting for regrets and average strategies during the first 400 minutes of training. Instead of multiplying every entry on every iteration, it periodically applied a discount every 10 minutes.

If `T` is elapsed training time in minutes, the supplement gives the discount factor:

```math
(T/10) / (T/10 + 1)
```

applied every 10 minutes during the first 400 minutes.

The authors report that experiments in two-player NLHE subgames showed about a **3× convergence speedup** from this modification.

### 5.2 Implementation lesson

Do not implement “linear weighting” in the most mathematically literal but bandwidth-heavy way if it requires touching hundreds of millions of rows every iteration.

Use a schedule or lazy/global scaling representation.

Possible implementation approaches:

```text
A. Pluribus-style periodic bulk discount
B. Per-table global scale exponent + lazy row materialization
C. Epoch-local weighting with periodic normalization
```

For a first faithful implementation, A is simplest to validate.

---

## 6. Negative-regret pruning

### 6.1 Exact published behavior

**[PLURIBUS-ORIGINAL]** After the first 200 minutes:

- on **95%** of iterations, traverser actions whose accumulated regret was below `-300,000,000` were skipped;
- on the remaining **5%**, all traverser actions were explored;
- actions on the **river/final betting round were never pruned** by this rule;
- actions that immediately lead to a **terminal state were never pruned**;
- the prune/no-prune choice was made for the whole iteration rather than separately for every action.

Regrets were stored as **4-byte integers** and floored at:

```text
-310,000,000
```

This avoided integer overflow and allowed a previously bad action to recover rather than drifting to an arbitrarily negative value.

The authors estimated approximately another **2× speedup** from the pruning changes.

### 6.2 Why pruning is unusually useful in six-max

Strong six-max play folds a large percentage of starting hands. Huge regions of the theoretical game tree therefore become essentially irrelevant under a good strategy.

Pruning does two things:

1. fewer recursive branches per traversal;
2. indirectly makes a coarse card abstraction behave more selectively because strategically irrelevant underlying states contribute much less often to a shared bucket.

### 6.3 Lite implementation rule

**[PROPOSED-LITE]** Copy the Pluribus exemptions exactly before experimenting with more aggressive pruning. “Prune every negative action everywhere” is not equivalent.

Recommended constants for the first faithful version:

```text
PRUNE_WARMUP       = measured by elapsed work; start with paper-equivalent schedule
PRUNE_PROBABILITY  = 0.95
REGRET_PRUNE_LIMIT = -300_000_000
REGRET_FLOOR       = -310_000_000
REGRET_STORAGE     = i32
```

Later, replace wall-clock warmups with iteration/work-unit thresholds for reproducibility across machines.

---

## 7. Blueprint abstraction

The blueprint is not a solution to full NLHE. It is a solution to an abstract game.

Two independent abstractions are used:

```text
real game
  |
  +-- information/card abstraction
  |
  +-- action/bet-size abstraction
  |
  v
abstract extensive-form game
```

---

## 8. Information/card abstraction

### 8.1 Lossless state counts

**[PLURIBUS-ORIGINAL]** If only strategically identical card situations are merged, the supplement reports approximately:

| Street | Strategically unique information situations |
|---|---:|
| Preflop | 169 |
| Flop | 1,286,792 |
| Turn | 55,190,538 |
| River | 2,428,287,420 |

That is already enough to show why full lossless postflop storage is infeasible for a global six-max blueprint.

### 8.2 Blueprint abstraction

**[PLURIBUS-ORIGINAL]**:

- preflop: lossless 169 canonical starting-hand classes;
- flop: 200 lossy buckets;
- turn: 200 lossy buckets;
- river: 200 lossy buckets.

The postflop situations were clustered using domain-specific features.

### 8.3 Real-time-search abstraction

This is more precise than the blueprint:

**[PLURIBUS-ORIGINAL]**:

- current betting round: **lossless information abstraction**;
- future rounds inside the subgame: **500 buckets per round**.

The future-round buckets were built separately for each flop using a potential-aware algorithm with Earth Mover's Distance ideas.

This asymmetry is fundamental:

> The blueprint can be coarse because online search locally recovers precision exactly where the actual hand is being played.

### 8.4 Why equity-only buckets are insufficient

Two hands may have equal current equity while having very different future distributions.

Example:

```text
Hand A: stable made hand, equity stays around 55% on most runouts
Hand B: draw-heavy hand, often becomes 5% or 95% on future cards
```

Same mean equity does not imply same strategic behavior.

Potential-aware representations encode a distribution over future strength rather than only current hand strength.

### 8.5 Practical feature representation

**[PROPOSED-LITE]** A programmer-friendly first version can use an EMD-friendly quantile vector:

```text
CardFeature {
    equity_quantiles[8 or 16]
    optional_potential_features[]
    optional_equity_vs_opponent_strength_tiers[]
}
```

For 1-D distributions, EMD can be computed efficiently from cumulative histograms or quantile vectors.

For a stronger but more expensive abstraction, add “opponent cluster hand strength” style features, representing equity against several opponent-range strength groups.

### 8.6 Suit isomorphism

**[PROPOSED-LITE]** Canonicalize strategically equivalent suit permutations before feature calculation and lookup. This reduces duplicated work and cache size substantially.

Canonical key concept:

```text
canonicalize_suits(hole_cards, board)
    -> lexicographically minimal representation under 24 suit permutations
```

Do not blindly enumerate all 24 permutations in the hottest runtime path; cache or use a direct canonicalization method after correctness is established.

---

## 9. Action abstraction

### 9.1 Original high-level rule

**[PLURIBUS-ORIGINAL]** The blueprint allowed between **1 and 14 raise sizes**, depending on decision point. Raise sizes were fractions of the pot.

Fold and call/check were included whenever legal.

The candidate sizes were manually selected based partly on raise sizes that earlier versions of the bot used with meaningful probability.

### 9.2 Street-dependent precision

The blueprint is deliberately richer preflop, because normal preflop decisions usually do not receive real-time search.

**[PLURIBUS-ORIGINAL]** On turn and river:

- first raise in the round: at most
  - `0.5 pot`
  - `1.0 pot`
  - `all-in`
- subsequent raises: at most
  - `1.0 pot`
  - `all-in`

During real-time search, the number of available raise sizes varied between **1 and 6**.

### 9.3 Scale of the abstract betting tree

**[PLURIBUS-ORIGINAL]** The blueprint action abstraction contained:

```text
664,845,654 total action sequences
```

but only:

```text
413,507,309 action sequences
```

were ever encountered in training.

Regret memory for an action sequence was allocated only when first encountered. This sparse/lazy allocation reduced memory by more than a factor of two according to the supplement.

Online subgames were tiny by comparison: roughly **100–2,000 player-action sequences**.

### 9.4 Why action abstraction is a dominant memory lever

If average legal actions grow from 4 to 7, recursive history count can grow exponentially with betting depth. Card-bucket count is approximately a linear multiplier; action branching can be much worse.

For a low-memory solver, reducing an unnecessary bet size may save more RAM than aggressively reducing hand buckets.

---

## 10. Blueprint strategy accumulation

A subtle but extremely useful memory optimization from the original system is often omitted in reimplementations.

### 10.1 Preflop

**[PLURIBUS-ORIGINAL]** After the first 800 minutes, Pluribus stored the **average strategy** for the first betting round and used it as the preflop blueprint.

### 10.2 Postflop

For later streets, Pluribus did **not** keep a full running average strategy in memory.

Instead:

1. after the initial 800 minutes;
2. take a snapshot of the **current** strategy every 200 minutes;
3. save snapshots to disk;
4. build the postflop blueprint by averaging those snapshots afterward.

The paper reports that this reduced memory usage by nearly half and reduced iteration cost.

### 10.3 Lite rule

**[PROPOSED-LITE]** This should be a first-class design requirement:

```text
Training hot state:
    regrets: resident
    preflop strategy accumulator: resident
    postflop running strategy accumulator: NOT resident

Postflop:
    periodic strategy snapshots -> disk
    final export = average(snapshot_1 ... snapshot_n)
```

Do not spend half your 64 GB budget on a postflop average matrix just because a generic CFR implementation does so.

---

## 11. Offline blueprint training pipeline

A robust implementation pipeline should look like this:

```text
[1] Rules/game-engine validation
       |
[2] Card abstraction generation
       |
[3] Action abstraction/tree rules
       |
[4] Infoset key encoding
       |
[5] Sparse CFR table backend
       |
[6] Linear external-sampling MCCFR
       |
[7] Negative-regret pruning
       |
[8] Checkpoint + metrics
       |
[9] Periodic strategy snapshots
       |
[10] Final blueprint compiler/quantizer
```

### 11.1 Training loop pseudocode

```text
initialize regret tables
initialize preflop strategy accumulator

for epoch / traversal t:
    if discount_schedule_triggered(t):
        discount_regrets()
        discount_preflop_strategy_sum()

    full_iteration = random() < 0.05 || before_prune_warmup

    for traverser in players:
        deal sampled hidden cards + chance trajectory as required
        external_sampling_traverse(root, traverser, full_iteration)

    periodically:
        update / accumulate preflop average strategy

    periodically after snapshot warmup:
        export current postflop strategy snapshot

    periodically:
        checkpoint regrets, RNG/progress state, abstraction hash
```

### 11.2 Reproducibility metadata

Every blueprint artifact should contain:

```yaml
format_version: ...
game:
  players: 6
  stack_bb: 100
  blinds: [0.5, 1.0]
training:
  traversals: ...
  random_seed_family: ...
  linear_discount_schedule: ...
  prune_threshold: ...
  prune_probability: ...
abstraction:
  card_bucket_counts: [169, ...]
  card_abstraction_hash: ...
  action_menu_version: ...
  action_menu_hash: ...
build:
  git_commit: ...
  compiler: ...
  endian: little
metrics:
  infosets_per_street: ...
  allocated_bytes: ...
```

If the abstraction hash does not match at runtime, reject the blueprint rather than silently playing nonsense.

---

## 12. Online real-time search: the part that makes Pluribus special

### 12.1 Search is always used postflop

**[PLURIBUS-ORIGINAL]** Real-time search is always used on flop, turn and river.

On preflop it is normally avoided because the blueprint is more detailed. A special search is triggered if an opponent's raise is more than $100 away from any blueprint raise and no more than four players remain. Otherwise an off-tree preflop raise can be translated into the blueprint abstraction using pseudo-harmonic action translation.

### 12.2 The subgame root is a distribution, not a node

Suppose public state `G` contains many possible concrete hidden-card states `h`.

The root chance node assigns probability proportional to reach under strategy profile `σ`:

```math
P(h | G, σ) ∝ π^σ(h)
```

normalized over all `h ∈ G`.

In programmer terms:

```text
RootState = {
    public_state,
    range[seat0][1326],
    range[seat1][1326],
    ...
}
```

with blocker constraints applied when concrete hidden deals are sampled.

### 12.3 Range tracker

**[PLURIBUS-ORIGINAL]** Pluribus maintains a probability distribution over the 1326 possible two-card combinations for each player from an outside observer's perspective.

Initially:

```math
P(hand) = 1 / 1326
```

before conditioning on known cards.

At a betting-round transition it updates ranges using Bayes' rule according to the assumed strategy profile:

```math
P(h | observed action a, history)
    ∝ P(a | h, history, σ) * P(h | history)
```

Then normalize and zero hands incompatible with known board / hero cards.

If no prior online search has occurred, `σ` is the blueprint. Otherwise, the previously solved strategy is used.

### 12.4 Unsafe search assumption

Pluribus does not know opponents' true policies. It therefore asks:

> “What would my own strategy have done with each possible opponent hand?”

and assumes the observed opponent actions were generated by that strategy when constructing beliefs.

This is called **unsafe search** because the theoretical safety guarantees of two-player safe subgame solving do not hold.

The supplement states two practical reasons for the choice:

- careful unsafe search performed better empirically than safe alternatives in their setting;
- zero-probability hands need not be solved, providing about a **4× speedup** in practice.

Do not confuse “unsafe” with “buggy.” It is a deliberate performance/empirical-strength tradeoff.

---

## 13. Nested search and the betting-round root

A naive solver might re-solve only from the exact current decision. Pluribus intentionally does something different.

**[PLURIBUS-ORIGINAL]** When re-solving within a betting round:

- search begins from the **start of the current betting round**;
- Pluribus's own already-chosen actions for its actual hand are frozen so a later resolve remains consistent with what it already did;
- opponents' earlier actions in the current round are **not frozen**, allowing the resolve to consider that they may have shifted strategy earlier in the round;
- when a new betting round begins, the root advances to that new public state.

Conceptual state:

```text
SearchContext {
    street_root_public_state
    street_root_ranges[6]
    strategy_profile_from_last_resolve
    frozen_hero_infosets_for_actual_hand[]
    inserted_offtree_actions[]
}
```

This architecture is more robust than solving only below the current node while keeping much of the practical speed benefit of unsafe search.

---

## 14. Exact depth limits used by Pluribus

This is one of the most useful pieces of the supplementary material.

**[PLURIBUS-ORIGINAL]**:

### Preflop search

If a preflop search is triggered:

```text
search until end of preflop
leaf = chance node before flop
```

### Flop search when >2 players started the flop

Stop at whichever occurs first:

```text
A. chance node at the start of turn
B. immediately after the second raise action on the flop
```

### All other cases

```text
search to the end of the game
```

Thus turn/river or smaller subgames can often be solved to terminal states while the large multiway flop is depth-limited.

### Why this is better than “always search N plies”

The limit follows poker structure and computational explosion, not an arbitrary action depth.

For an implementation, define a semantic leaf predicate:

```text
bool is_search_leaf(state, root_context) {
    if root_street == PREFLOP:
        return state.street == FLOP;

    if root_street == FLOP && players_at_root > 2:
        return state.street == TURN || flop_raise_count >= 2;

    return state.is_terminal();
}
```

---

## 15. Two online CFR regimes

**[PLURIBUS-ORIGINAL]** Pluribus chose between two solvers depending on the subgame:

1. **Monte Carlo Linear CFR** for relatively large / early subgames;
2. an optimized **vector-based Linear CFR** for smaller/later subgames, sampling one public board per thread.

This gives a useful engineering split:

```text
large multiway tree:
    sampling traversal

small / late / often heads-up tree:
    range-vectorized solver
```

### 15.1 Final-iteration versus average strategy

A subtle online detail:

- Pluribus **played the final-iteration strategy** from the online solve;
- the weighted average strategy was still used to update `σ` / beliefs.

Reason given: average strategies may retain small probabilities on poor actions that the latest strategy has effectively eliminated.

For a reimplementation, store both:

```text
SolveResult {
    final_strategy_for_action_selection
    averaged_strategy_for_range_updates
}
```

Do not silently use one for both purposes.

---

## 16. Online action abstraction and off-tree handling

### 16.1 Current search menu

**[PLURIBUS-ORIGINAL]** Online search usually uses no more than about five raise actions at a decision, with 1–6 possible raise sizes depending on state.

### 16.2 Opponent chooses a size outside the search tree

Pluribus does **not** simply round the bet and continue forever.

**[PLURIBUS-ORIGINAL]**:

1. add the exact opponent action as a legal action to all appropriate nodes in the current public state;
2. re-run search from the betting-round root;
3. keep the root fixed until the street changes;
4. keep already-played hero decisions for the hero's actual hand frozen.

This is a major strength improvement over pure action translation.

### 16.3 Blueprint leaf is off-tree

If a search leaf corresponds to an action history not present in the blueprint, the original system maps it to a nearby blueprint node using deterministic **pseudo-harmonic action translation**.

---

## 17. Depth-limit leaf values: continuation strategy portfolio

This is the most distinctive part of the Pluribus online solver.

A perfect-information engine can assign one scalar evaluation to a leaf. In poker that can be exploitable because future strategy depends on hidden information and players can change behavior after the leaf.

### 17.1 Four continuation policies

**[PLURIBUS-ORIGINAL]** At every search leaf, each remaining player chooses among four blueprint-derived continuation strategies:

```text
C0: original blueprint
C1: fold-biased blueprint
C2: call-biased blueprint
C3: raise-biased blueprint
```

Biasing is exactly described as:

```text
fold-biased:
    fold probability *= 5
    renormalize

call-biased:
    call probability *= 5
    renormalize

raise-biased:
    every raise probability *= 5
    renormalize
```

Each player's continuation-policy choice is treated as another strategic action inside the search game and must be consistent across leaf states that are indistinguishable to that player.

This prevents the search from assuming a single brittle blueprint continuation.

### 17.2 Why the portfolio works conceptually

Suppose the blueprint underestimates the value of bluff catching in a leaf region. A scalar blueprint rollout would bake that mistake into every leaf value. The continuation-choice game allows opponents to select a call-heavier future strategy, forcing the searching agent to be robust to that plausible strategic deviation.

It is a compact way to approximate strategic uncertainty beyond the search horizon.

### 17.3 Continuation compression

**[PLURIBUS-ORIGINAL]** To reduce memory, each continuation strategy was compressed by sampling **one action per abstract infoset** from its probability distribution and storing only that action using the minimum necessary bits.

The authors argue repeated visits to the same abstract infoset in one continuation are unlikely, so this introduces little practical bias.

This is an excellent low-memory trick for a 64 GB implementation.

---

## 18. End-to-end online decision pseudocode

```text
function observe_hand_start(public_state):
    root_public_state = public_state
    for seat in players:
        range[seat] = legal_uniform_1326(board, known_hero_cards_if_applicable)
    sigma = blueprint
    frozen_hero_decisions.clear()

function observe_opponent_action(action):
    if action not in current_subgame_action_abstraction:
        add_exact_action_to_public_state(action)
        solve_result = solve_from_street_root(
            root_public_state,
            root_ranges,
            frozen_hero_decisions
        )
        sigma = solve_result.average_strategy_for_beliefs

    advance_public_history(action)

function hero_action():
    if should_use_blueprint_only_preflop():
        strategy = blueprint.lookup(hero_infoset)
    else:
        solve_result = solve_from_street_root(...)
        strategy = solve_result.final_strategy.lookup(hero_actual_infoset)
        sigma = solve_result.average_strategy_for_beliefs

    action = sample(strategy)
    frozen_hero_decisions.push(hero_actual_infoset, action constraints)
    advance_public_history(action)
    return action

function on_new_street(new_public_state):
    for seat in players:
        range[seat] = bayes_update(
            range[seat],
            observed_street_actions,
            sigma,
            board_blockers
        )

    root_public_state = new_public_state
    root_ranges = range
    frozen_hero_decisions.clear()

    solve_result = solve_from_street_root(...)
    sigma = solve_result.average_strategy_for_beliefs
```

The actual Bayes updates must account for impossible joint private-card assignments / blockers when sampling concrete states. Independent marginal ranges are a convenient representation, not a statement that hands are statistically independent after card removal.

---

## 19. Hardware and computational facts from the original system

**[PLURIBUS-ORIGINAL]**:

### Blueprint

- Pittsburgh Supercomputing Center Bridges machine;
- one large shared-memory node;
- 4 × 16-core Intel Xeon E5-8860 v3 = 64 cores;
- less than **0.5 TB** memory used;
- approximately 8 days / roughly 12,400 core-hours as reported in the main work.

### Live search

- one shared-memory node;
- 2 × 14-core Intel Haswell E5-2695 v3 = 28 cores;
- **128 GB RAM**;
- **no GPUs**.

### Implication

A 64 GB target is primarily a **memory-layout and abstraction problem**, not necessarily a GPU problem.

---

## 20. Open-source implementation survey

None of the following is the official Pluribus source.

### 20.1 `conorarmstrong/pluribus`

Repository: <https://github.com/conorarmstrong/pluribus>

**Assessment:** strongest starting point found for a modern practical Pluribus-style six-player implementation.

**[REIMPLEMENTATION]** Current repository describes:

- Rust;
- 2–6 player NLHE;
- parallel external-sampling Linear MCCFR;
- Pluribus-style negative-regret pruning;
- EMD/k-medians-style postflop abstraction;
- Bayesian 1326-combo range tracking;
- depth-limited online re-solving;
- exact/vector later-street solving paths;
- test/evaluation tooling including local best response and real hand replay.

Reported July 2026 blueprint result:

```text
iterations:              200,000,000
postflop buckets/street: 12
infosets:                128.5 million
exported strategies:     ~101 million
blueprint file:          4.3 GB
training wall time:      79 minutes on 16 cores
```

The repository additionally reports agreement/evaluation experiments and online-search gains. Treat these as project benchmarks, not peer-reviewed proof of superhuman six-max strength.

Useful quick-start commands documented by the repo:

```bash
cargo build --release
./target/release/pluribus train --iters 200000000 --out blueprint.bin
./target/release/pluribus inspect --blueprint blueprint.bin
./target/release/pluribus eval --blueprint blueprint.bin --hands 200000 --baseline random
./target/release/pluribus lbr --blueprint blueprint.bin --hands 20000
```

**Why it matters for 64 GB:** the 4.3 GB exported 12-bucket blueprint is direct evidence that very coarse Pluribus-style blueprints can be compact. Training working-set size is larger than the exported file, so measure RSS before extrapolating.

### 20.2 `c-heidt/pluribus-opponent-exploitation`

Repository: <https://github.com/c-heidt/pluribus-opponent-exploitation>

**Assessment:** particularly valuable as an engineering reference for **out-of-core / beyond-RAM** training.

**[REIMPLEMENTATION]** It describes:

- full-deck six-player Pluribus-style training;
- Linear MCCFR + regret pruning;
- LMDB-backed sparse table indexes;
- shared-memory / mmap integer chunks;
- resumable card abstraction generation;
- `uint16` memmapped river bucket lookup rather than huge Python dictionaries;
- Bayesian range tracking;
- depth-limited subgame search;
- Cython hot loop.

This architecture is highly relevant if a 64 GB machine cannot retain the complete training state. NVMe-backed LMDB/mmap will be slower than RAM but allows the project to fail gracefully instead of OOMing.

The repository also explores opponent exploitation, which is outside the original Pluribus equilibrium-focused scope. Keep those layers separate if fidelity matters.

### 20.3 `fedden/poker_ai`

Repository: <https://github.com/fedden/poker_ai>

**Assessment:** historically important educational implementation, not a current production six-max Pluribus base.

**[REIMPLEMENTATION]**:

- Python;
- clustering + MCCFR architecture;
- repository archived July 16, 2024;
- README explicitly says it currently supports only a **20-card deck without modification**;
- blueprint and real-time-search work remained incomplete in the published project state.

Use it for understanding components and test structure, not as the fastest route to full 52-card six-max.

### 20.4 `whatsdis/pluribus`

Repository: <https://github.com/whatsdis/pluribus>

**Assessment:** proof-of-concept / incomplete port.

**[REIMPLEMENTATION]** README labels the solver WIP and says its Python port using Deuces had not been tested. It also includes browser/casino integration goals, which are unrelated to the solver research task.

Do not use this as a correctness oracle.

### 20.5 `mfine15/pluribus`

Repository: <https://github.com/mfine15/pluribus>

**Assessment:** compact educational/from-scratch implementation with CFR, abstraction and search components; useful for reading, less useful as a performance foundation.

It claims Linear CFR, external sampling, regret pruning, card/action abstraction and depth-limited search, with toy-game validation. Its own README is appropriately cautious about search validation and superhuman claims.

### 20.6 `cRITlamb/pluribus2`

Repository: <https://github.com/cRITlamb/pluribus2>

**Assessment:** fork/evolution of the educational implementation. Good for conceptual test cases and module decomposition, but not evidence of full-scale Pluribus strength.

### 20.7 `amaster97/poker_solver`

Repository: <https://github.com/amaster97/poker_solver>

**Assessment:** useful solver-engine reference, but it solves a different problem.

**[REIMPLEMENTATION]**:

- heads-up no-limit, not six-max;
- Python reference + Rust performance core;
- tabular Discounted CFR with Brown/Sandholm parameters;
- range-vs-range postflop solver;
- suit isomorphism;
- vectorized terminal evaluation / chance parallelism.

Excellent source for optimized late-street CFR engineering, testing and Python↔Rust differential validation. It is not a drop-in Pluribus blueprint trainer.

### 20.8 `bupticybee/TexasSolver`

Repository: <https://github.com/bupticybee/TexasSolver>

**Assessment:** useful high-performance postflop solver reference, not a Pluribus implementation.

**[REIMPLEMENTATION]**:

- C++;
- Texas Hold'em / short-deck local GTO solving;
- efficient flop subgame solving;
- old README benchmark reports 1.6 GB memory and 172 s for a particular small action-tree comparison;
- principally heads-up/postflop-style solving, not a 6-player global blueprint + nested search architecture.

The newer GPU product repository does not publish the internal GPU solver source, so it should not be treated as an open implementation reference.

---

## 21. Recommended implementation starting point

If the goal is **“get a Pluribus-like system working first, then improve it”**:

### Option A — fastest practical route

Fork / study `conorarmstrong/pluribus`.

Advantages:

- Rust performance core already exists;
- 2–6 player engine;
- current full-deck path;
- blueprint + search architecture;
- measurable artifact sizes;
- strong test/evaluation surface.

Then progressively replace deviations with paper-faithful components where they matter.

### Option B — architecture-first / custom engine

Build a new Rust/C++ solver using:

- Brown/Sandholm supplement as behavioral spec;
- `conorarmstrong/pluribus` as a practical implementation reference;
- `c-heidt/...` for disk/mmap storage design;
- `amaster97/poker_solver` and TexasSolver for optimized heads-up late-street/vector solver ideas.

This is better if long-term performance and ownership matter more than fastest initial demo.

---

# Part II — Pluribus-Lite for 64 GB RAM

## 22. Design objective

**[PROPOSED-LITE]** Build a six-max 100bb full-deck NLHE agent that retains the essential Pluribus architecture while fitting comfortably on a 64 GB workstation.

Target constraints:

```yaml
players: 6
stack: 100bb
ram_physical: 64 GiB
training_target_rss: <= 52 GiB
runtime_target_rss: <= 48 GiB
os_and_page_cache_headroom: >= 8-12 GiB
full_deck: true
neural_network_required: false
primary_language: Rust or C++
```

### Critical honesty condition

The objective is **not** “guaranteed superhuman on 64 GB.”

The objective is:

> preserve the mechanisms most responsible for Pluribus's strength, aggressively reduce global precision, and use online search to recover local precision; then prove strength empirically.

---

## 23. Memory strategy: where to spend precision

A naive strategy is to reduce every component by 2×. A better strategy is to spend RAM where it affects the actual decision most.

Recommended priority:

```text
highest precision
    |
    |  current online street: lossless
    |  current active ranges: full 1326 combos
    |  late-street exact/vector solve when feasible
    |
    |  online future streets: medium/high bucket count
    |
    |  preflop blueprint: relatively rich
    |
    |  global postflop blueprint: coarse
    v
lowest precision
```

This follows the original design philosophy: coarse global solution + accurate local repair.

---

## 24. Three 64 GB profiles

### 24.1 Profile DEV-12

Purpose: prove end-to-end architecture quickly.

```yaml
blueprint:
  preflop: 169 lossless classes
  flop_buckets: 12
  turn_buckets: 12
  river_buckets: 12
  action_menu: coarse
  target_iterations: 50M-200M
online:
  current_street: lossless
  future_buckets: 64-128
  continuation_policies: 4
```

This resembles the granularity demonstrated by the modern Rust reimplementation. It should be considered a baseline, not a final strength target.

### 24.2 Profile BALANCED-24/32 — recommended first serious target

```yaml
blueprint:
  preflop: 169 lossless
  flop_buckets: 24
  turn_buckets: 24
  river_buckets: 32
  postflop_average_strategy: snapshots_on_disk
  regrets: int32
  lazy_infosets: true
online:
  current_street: lossless
  flop_future_buckets: 128
  turn_future_buckets: 192
  river_future_buckets: 256  # only where a future abstraction is actually needed
  raise_sizes_per_node: typically <= 5
```

Why this profile:

- compared with the original 200-bucket blueprint it reduces the largest card multiplier by ~6–8×;
- compared with a 12-bucket proof-of-concept it spends 2–3× more blueprint precision;
- online search recovers much finer current-state precision.

### 24.3 Profile AMBITIOUS-32/48

Attempt only after profiling the previous level.

```yaml
blueprint:
  flop_buckets: 32-48
  turn_buckets: 32-48
  river_buckets: 48-64
runtime:
  mmap_blueprint: true
  quantized_strategy: true
online:
  future_buckets: 192-256+
```

This profile may fit in 64 GB with a compact implementation, but training RSS depends heavily on infoset occupancy, action menus and hash-table overhead. It must be gated by memory telemetry rather than guessed from bucket count alone.

---

## 25. Back-of-the-envelope scaling from a public reimplementation

**[ESTIMATE]** The Rust reimplementation reports roughly:

```text
101M exported strategies -> 4.3 GB blueprint
```

Very rough exported bytes per strategy row:

```text
4.3e9 / 101e6 ≈ 42.6 bytes/row
```

If infoset count scaled linearly with postflop bucket count — it will not be perfectly linear — going from 12 buckets to 32 could imply roughly:

```text
4.3 GB * (32 / 12) ≈ 11.5 GB exported strategy
```

That is comfortably below 64 GB for runtime.

However, **training state is much larger** because it includes:

- regret vectors;
- mutable table/index capacity;
- hash-table overhead;
- temporary traversal state;
- card-abstraction data;
- thread-local buffers;
- checkpoints/snapshot buffers.

Therefore the exported-file extrapolation must not be used as a training-RAM prediction.

Practical rule:

```text
never choose next bucket count from file size;
choose it from peak RSS at the previous bucket count.
```

---

## 26. Proposed 64 GB memory budget

Runtime target example:

| Component | Budget |
|---|---:|
| OS, allocator slack, stacks, libraries | 6 GiB |
| Blueprint mapped/decoded working set | 12–18 GiB |
| Card abstraction / lookup / evaluator caches | 4–6 GiB |
| Online subgame tables + arenas | 12–18 GiB |
| Range/search worker buffers | 4 GiB |
| Page cache / emergency headroom | 12+ GiB |

Training target example:

| Component | Budget |
|---|---:|
| Mutable CFR tables | 28–36 GiB |
| Infoset index / hash capacity | 6–10 GiB |
| Abstraction tables + evaluator caches | 3–5 GiB |
| Per-thread traversal memory | 2–4 GiB |
| snapshot/checkpoint staging | 2–4 GiB |
| safety margin | 6–10 GiB |

Exact values must come from telemetry.

---

## 27. Compact infoset representation

### 27.1 Never use strings in the hot path

Bad:

```text
"P2|FLOP|board_bucket=17|history=call,raise_50,..."
```

Use an interned numeric public-history ID and compact card bucket.

Example:

```rust
struct InfoKey(u64);

// conceptual packing, exact bit allocation depends on your tree
// [ public_history_id | card_bucket | street | player metadata ]
```

Even better: if each public history owns a contiguous array of card buckets, no hash key is needed for every `(history,bucket)` pair.

```text
PublicHistoryNode {
    bucket_row_base: u32/u64
    bucket_count: u16
    legal_action_mask: u16
    action_count: u8
}
```

Then:

```text
row_id = bucket_row_base + card_bucket
```

### 27.2 Separate tree topology from learning arrays

```text
Immutable topology:
    public nodes
    legal actions
    child node IDs
    terminal metadata

Mutable training arrays:
    regret rows
    preflop strategy sums
```

This improves locality and allows the final runtime blueprint to discard training-only metadata entirely.

---

## 28. Regret row layout

Action count is small and varies by node.

Simple packed representation:

```text
NodeMeta {
    regret_offset: u32/u64
    action_count: u8
}

regrets: contiguous i32[]
```

No `Vec`/heap allocation per infoset.

If an infoset has 4 actions:

```text
4 * int32 = 16 bytes
```

versus a language-level hash-map value/vector which can cost many times more.

### Optional advanced layout

Group infosets by action count:

```text
RegretTable2: array<[i32;2]>
RegretTable3: array<[i32;3]>
RegretTable4: array<[i32;4]>
...
```

This enables SIMD-friendly regret matching and removes an offset indirection.

---

## 29. Blueprint runtime format

Training representation and runtime representation should be different artifacts.

### 29.1 Runtime strategy can be quantized

For mixed action probabilities, `f64` is unnecessary.

Options:

```text
u8 probabilities summing to 255
u16 probabilities summing to 65535
```

Recommended first choice:

```text
u16 per action
```

because it gives excellent probability resolution while halving `float32` storage.

For sampling, convert only the selected row:

```text
r = random_u16()
cumulative += prob_u16[a]
```

No float conversion is even required.

### 29.2 Pack deterministic/pure rows specially

Many trained states will be near-pure.

Possible encoding:

```text
header bit:
    0 -> mixed row follows
    1 -> deterministic action index in remaining bits
```

This can cut file size further and improve cache locality.

### 29.3 mmap the immutable blueprint

Do not necessarily `read()` the entire file into a duplicate heap buffer.

```text
mmap read-only blueprint
OS page cache loads hot regions
```

Use explicit prefetching for likely next public histories if profiling shows page faults matter.

---

## 30. Card abstraction storage under 64 GB

Avoid huge object/hash lookups keyed by card tuples.

### 30.1 Compact bucket ID

For <= 65,535 buckets:

```text
bucket_id: uint16
```

### 30.2 Combinadic / perfect indexing

Map canonical `(hole, board)` combinations to dense numeric positions and store bucket IDs in a memory-mapped array.

This pattern is demonstrated by the `c-heidt` implementation for very large river tables.

### 30.3 Compute-on-demand where cheaper

Some exact river quantities can be computed faster than loading a gigantic precomputed dictionary if you have a highly optimized evaluator and vectorized range evaluation.

Benchmark:

```text
lookup bandwidth + random page faults
vs
vector exact evaluation
```

Do not precompute merely because old solvers did.

---

## 31. Action abstraction for Pluribus-Lite

A reasonable initial menu:

### Preflop

Keep richer choices because search usually does not repair ordinary preflop spots.

Example, state-dependent subset:

```text
open / raise-to candidates:
    2.0bb
    2.25bb
    2.5bb
    3.0bb
    4.0bb
    selected larger squeeze sizes
    all-in where strategically relevant
```

Do not blindly expose all values in every state. Build context-specific menus.

### Flop

```text
first bet/raise candidates:
    0.33 pot
    0.50 pot
    0.75 pot
    1.00 pot
    all-in
```

Use fewer choices after raises.

### Turn/river

Begin close to the published Pluribus menu:

```text
first raise:
    0.5 pot
    1.0 pot
    all-in

subsequent raise:
    1.0 pot
    all-in
```

Add overbets only after ablation demonstrates value worth the memory cost.

### Search-only exact opponent action

When the opponent bets an arbitrary size:

```text
insert exact size into current public state
re-solve from street root
```

This lets the global blueprint stay small without rounding every unusual live action.

---

## 32. 64 GB online search design

### 32.1 Router

```text
if preflop:
    if severe off-tree raise && active_players <= threshold:
        run depth-limited search
    else:
        blueprint / translation

else if river:
    exact/vector solve if tractable

else if turn:
    vector solve if active_players == 2 and range size manageable
    otherwise MCCFR

else if flop:
    if large multiway:
        depth-limited MCCFR to published structural boundary
    else:
        solve deeper / terminal if budget allows
```

### 32.2 Arena allocation

Every online solve should allocate from a reusable arena:

```text
SearchArena {
    node_metadata[]
    regret_arrays[]
    strategy_arrays[]
    sampled_state_stack[]
    worker_scratch[]
}
```

Reset offsets between decisions rather than destructing millions of small allocations.

### 32.3 Range pruning

Unsafe search's practical advantage partly comes from not solving zero-probability hands.

Use a very small epsilon only after normalization:

```text
if range_weight[combo] == 0:
    omit
```

Be cautious about pruning merely “small” probabilities: repeated Bayes updates can make an incorrectly deleted combo impossible to recover.

---

## 33. Multiway range sampling

Independent per-seat range sampling can generate card collisions. A correct sampler must condition on blockers.

Conceptual sequential sampler:

```text
used_cards = public_board + hero_known_cards

for opponent in randomized_or_fixed_order:
    candidates = combos not intersecting used_cards
    probability ∝ marginal_range[opponent][combo]
    combo = sample(candidates)
    used_cards += combo
```

This produces a conditional joint sample from the product of marginals subject to card exclusion. It is not a complete model of correlation induced by prior betting, but it is a practical root sampler.

For high-precision two-player vector solving, enumerate both ranges with exact blocker masks rather than sampling.

---

## 34. Continuation policies for the Lite solver

Implement the original four first:

```text
NORMAL
FOLD_X5
CALL_X5
RAISE_X5
```

Do not prematurely add dozens of personas. More continuation choices increase leaf branching and search complexity.

Compile continuation artifacts separately from the full blueprint:

```text
continuation/<policy>/<infoset> -> sampled action bits
```

For action counts <= 8, 3 bits are sufficient per sampled continuation action.

Four policies can therefore be extremely compact compared with full probability vectors.

---

## 35. Training with only 64 GB: in-RAM path

Start in RAM because it is much faster than out-of-core storage.

### Step 1

Train 12-bucket full-deck blueprint and record:

```text
peak RSS
allocated infosets per street
action-count histogram
bytes per infoset
hash load factor
traversals/s
regret-table bytes
index bytes
```

### Step 2

Estimate the next bucket count using **measured infoset growth**, not a fixed multiplier.

### Step 3

Try 24 buckets.

### Step 4

If peak RSS remains below ~42–45 GiB after table growth stabilizes, try 32.

Why leave so much spare memory? Long-running hash tables, checkpoints, OS page cache and transient snapshots can create late-run peaks.

### Step 5

If 32 is close to the limit, improve layout before reducing algorithm quality:

- remove strings;
- compact keys;
- lower hash-table overhead;
- contiguous action arrays;
- eliminate postflop strategy sums;
- quantize non-hot immutable structures;
- ensure duplicate per-thread tables are not present.

---

## 36. Out-of-core training fallback

If the desired abstraction still exceeds RAM, use a storage hierarchy:

```text
hot recent CFR chunks -> RAM/shared memory
cold CFR chunks       -> memory-mapped NVMe
infoset key->row index -> LMDB / compact persistent hash
checkpoints            -> NVMe
```

This follows ideas visible in `c-heidt/pluribus-opponent-exploitation`.

### Requirements

- local NVMe strongly preferred;
- fixed-size chunks;
- dirty chunk tracking;
- asynchronous/checkpoint-safe flushing;
- crash-consistent metadata;
- no per-update fsync;
- avoid random small writes to a general-purpose database in the CFR hot loop.

A good design maps chunk files and lets the OS page cache manage residency.

---

## 37. Parallelism

### 37.1 Blueprint

Useful coarse unit:

```text
independent MCCFR traversal / traverser
```

Potential implementations:

1. shared concurrent regret table, many worker traversals;
2. local delta buffers merged periodically;
3. independent replicas merged at larger intervals.

The best choice depends on collision rate and table bandwidth.

### 37.2 Avoid lock-per-infoset

Use:

- sharded maps;
- striped locks;
- atomic integer adds where practical;
- thread-local deltas for hot rows;
- deterministic batch merges for debug mode.

### 37.3 Search

Parallelize independent samples/replicas and merge once per iteration block rather than synchronizing at every node.

### 37.4 NUMA

The original server was a multi-socket shared-memory system. A modern high-core-count workstation may still show NUMA/cache locality issues.

Pin worker groups and shard hot tables if profiling shows remote-memory traffic.

---

## 38. SIMD/vector optimization targets

High-value loops:

```text
regret matching over fixed small action count
range normalization
Bayes action-likelihood multiplication
showdown value across 1326 combos
blocker masks
terminal fold/call value accumulation
strategy quantization / sampling preparation
```

For late-street range-vs-range solving, organize data as structure-of-arrays:

```text
reach_p0[combo]
reach_p1[combo]
value[combo]
regret[action][combo]
strategy[action][combo]
```

rather than an object per hand.

---

## 39. GPU: optional, not foundational

Original Pluribus used no GPU.

A GPU can help with:

- mass hand evaluation;
- abstraction feature generation;
- large vector CFR kernels;
- clustering feature workloads.

But moving an irregular six-player traversal with dynamic betting trees to GPU can cost more engineering time than it saves.

For a first 64 GB system:

```text
CPU solver correctness first
GPU only for proven vector/batch bottlenecks
```

---

## 40. Expected strength: what “better than humans” would require

There is no safe conversion such as:

```text
32 buckets + 200M iterations = superhuman
```

Important reasons:

1. Six-player CFR does not have the same simple equilibrium-convergence guarantees as two-player zero-sum CFR.
2. Abstraction can create systematic strategic errors.
3. Off-tree play can expose action-abstraction weaknesses.
4. Online search can be worse than a strong blueprint if it is underconverged or has inaccurate leaf values.
5. Poker variance makes small samples nearly meaningless.

A strong 64 GB system is plausible because:

- the original global blueprint itself was abstract;
- current-round online search is much more precise;
- large human mistakes are often far larger than small abstraction errors;
- modern data structures can store much more solver state per GB than generic 2010s-era structures.

But it still requires measurement.

---

## 41. Evaluation ladder

Do not jump directly to human matches.

### Level 0 — mathematical toy games

Required:

```text
Kuhn poker
Leduc poker
small-deck multiway poker
```

Check known values / OpenSpiel equivalents where available.

### Level 1 — engine invariants

- every hand terminates;
- chip utility sums to zero;
- legal min-raise behavior;
- short all-in reopening rules;
- side pots;
- fold terminal values;
- board runouts have no duplicates;
- make/undo is byte-equivalent to copy/apply reference engine.

### Level 2 — CFR differential tests

Maintain a slow reference solver and fast production solver for tiny games.

Same seed + same sampled traversal should produce matching regret deltas.

### Level 3 — abstraction tests

- suit-isomorphic hands map identically;
- bucket assignment deterministic;
- obviously different strategic hands separate;
- action menus are legal at every stack/pot state;
- translation is monotonic and bounded.

### Level 4 — self-play / baseline sanity

Versus:

- random;
- call-only;
- simple tight/aggressive heuristic;
- previous blueprint versions.

This catches catastrophic bugs but does not establish strength.

### Level 5 — exploitability proxies

Full six-max best response is generally intractable. Use:

- Local Best Response (LBR);
- exact/tighter best response on restricted late-street subgames;
- heads-up reductions where exact exploitability is measurable;
- cross-play between independently trained blueprints.

### Level 6 — external strong bot

For heads-up components, play a genuinely external system rather than only shared-code baselines.

For six-max, use independently implemented agents if available.

### Level 7 — human evaluation

Only after the solver is stable.

Use:

- many thousands of hands;
- duplicate deals if possible;
- AIVAT/control variates where valid;
- confidence intervals;
- pre-registered decision on success threshold.

---

## 42. Search-ablation experiments that matter most

Run paired-deal comparisons so card luck cancels as much as possible.

Ablate one component at a time:

```text
A. blueprint only
B. blueprint + search
C. B without lossless current-street abstraction
D. B with 64 vs 128 vs 256 future buckets
E. B with only normal continuation policy
F. B with 4 continuation policies
G. B with action translation only
H. B with exact off-tree action insertion + re-solve
I. unsafe street-root nested search vs current-node-only resolve
J. final-iteration play vs average-strategy play
```

These experiments tell you where to spend RAM/CPU.

---

## 43. Programmer-facing module architecture

Recommended repository layout:

```text
/pluribus-lite
  /crates or /src
    /cards
      card
      deck
      evaluator
      suit_canonical
      combinadic

    /game
      state
      legal_actions
      sidepots
      make_undo
      history
      public_state

    /abstraction
      card_features
      emd
      clustering
      bucket_lookup
      action_menu
      action_translation

    /cfr
      regret_matching
      external_sampling
      linear_discount
      pruning
      traversal
      snapshots

    /blueprint
      trainer
      sparse_tables
      checkpoint
      compiler
      reader
      quantization

    /range
      combo_index
      range1326
      blockers
      bayes
      joint_sampler

    /search
      router
      root_builder
      nested_context
      mccfr_solver
      vector_solver
      continuation
      offtree
      arena

    /eval
      match_runner
      duplicate
      aivat
      lbr
      restricted_br
      metrics

  /tools
    build_abstraction
    inspect_blueprint
    replay_hand
    memory_profile

  /tests
    toy_games
    game_invariants
    cfr_diff
    abstraction
    search
    serialization

  /docs
    ARCHITECTURE.md
    ALGORITHM_SPEC.md
    BLUEPRINT_FORMAT.md
    SEARCH_SPEC.md
    EVALUATION.md
```

---

## 44. Interfaces an LLM coding agent should implement against

Keep boundaries explicit.

### Game engine

```rust
trait GameState {
    fn is_terminal(&self) -> bool;
    fn acting_player(&self) -> PlayerId;
    fn legal_actions(&self, out: &mut SmallVec<Action>);
    fn apply(&mut self, action: Action) -> UndoToken;
    fn undo(&mut self, token: UndoToken);
    fn utility(&self, player: PlayerId) -> Chips;
    fn public_key(&self) -> PublicStateId;
}
```

### Abstraction

```rust
trait CardAbstraction {
    fn blueprint_bucket(&self, cards: HoleCards, board: Board) -> BucketId;
    fn search_bucket(&self, cards: HoleCards, board: Board, root_street: Street) -> BucketId;
}

trait ActionAbstraction {
    fn actions(&self, state: &State, mode: SolveMode, out: &mut SmallVec<Action>);
}
```

### Blueprint

```rust
trait BlueprintPolicy {
    fn strategy(&self, public: PublicStateId, bucket: BucketId) -> StrategyView;
}
```

### Range tracker

```rust
struct Range1326 {
    weight: [f32; 1326]
}

impl Range1326 {
    fn remove_blocked(&mut self, cards: CardMask);
    fn bayes_update(&mut self, likelihood: &[f32; 1326]);
    fn normalize(&mut self);
}
```

### Search

```rust
struct SearchInput<'a> {
    root_public_state: &'a State,
    ranges: &'a [Range1326; 6],
    frozen_hero_decisions: &'a [FrozenDecision],
    blueprint: &'a dyn BlueprintPolicy,
    limits: SearchLimits,
}

struct SearchOutput {
    final_policy: LocalPolicy,
    average_policy: LocalPolicy,
    iterations: u64,
    nodes: u64,
}
```

These interfaces allow the game engine, abstraction and CFR implementation to be tested independently.

---

## 45. Determinism/debug mode

High-performance stochastic solvers are difficult to debug if every run differs.

Add a slow deterministic mode:

```yaml
threads: 1
rng: fixed counter-based generator
hash iteration: deterministic
floating point: fixed path
sampling logs: enabled
```

Log a traversal as:

```text
seed
traverser
chance samples
opponent action samples
infoset IDs
strategies before update
child values
regret deltas
```

A single bad regret update can otherwise contaminate millions of later states.

---

## 46. Checkpoint design

A blueprint run may take hours/days. Checkpointing is not optional.

Checkpoint must include:

```text
regret tables
preflop average accumulator
training progress / traversal count
linear-discount epoch
pruning state/schedule
RNG stream positions or reproducible seed counters
card abstraction artifact hash
public/action tree format version
training configuration hash
```

Use atomic directory generation:

```text
checkpoint.tmp/
  metadata
  tables...
fsync required files
rename checkpoint.tmp -> checkpoint.N
```

Never write a mutable “latest.bin” in place and hope the process does not crash.

---

## 47. Metrics to collect during training

Every 1–5 minutes record:

```text
iterations / traversals
traversals per second
active infosets total/per street
new infosets per million traversals
RAM RSS
allocated table bytes
hash load factor
average actions per infoset
pruned action percentage
percentage of full (5%) iterations
regret saturation/floor hits
strategy entropy by street
checkpoint duration
```

The most important low-memory metric is:

```text
new_infosets / unit_work
```

If it has not plateaued, current RSS is not a safe prediction of final RSS.

---

## 48. Blueprint compiler

Do not play directly from the mutable training checkpoint.

Compiler pipeline:

```text
training checkpoints + snapshots
        |
        v
validate abstraction hash
        |
        v
construct preflop average strategy
        |
        v
average postflop current-strategy snapshots
        |
        v
remove unreachable / zero rows if safe
        |
        v
quantize probabilities
        |
        v
build deterministic-row shortcuts
        |
        v
build continuation-policy sampled artifacts
        |
        v
write mmap-friendly blueprint file + index
```

The compiler can spend CPU/disk because it runs once; runtime format should optimize reads.

---

## 49. Suggested binary blueprint layout

One possible format:

```text
Header
  magic[8]
  version u32
  game_hash[32]
  abstraction_hash[32]
  action_tree_hash[32]
  player_count u8
  stack_bb u16
  street_directory_offset u64

StreetDirectory[4]
  history_count u32
  history_table_offset u64
  strategy_blob_offset u64

HistoryEntry
  public_history_id u32
  bucket_count u16
  action_count u8
  encoding_flags u8
  strategy_offset u64

Strategy blob
  packed deterministic rows OR
  u16 probability vectors
```

Use little-endian fixed-width fields and explicit versioning. Avoid language-native serialization formats for long-lived solver artifacts.

---

## 50. Recommended implementation roadmap

### Milestone 0 — rules engine

Exit criteria:

- complete 6-max no-limit betting rules;
- make/undo path;
- side pots;
- all-in fast-forward;
- 100k random-hand zero-sum fuzz test.

### Milestone 1 — CFR correctness

- Kuhn CFR;
- Leduc CFR;
- external sampling;
- regret matching;
- deterministic differential tests.

Exit: known game values / exploitability converge.

### Milestone 2 — Linear CFR + pruning

- periodic linear discount;
- negative-regret pruning;
- paper exemptions;
- `i32` floor.

Exit: toy-game fixed point is not destroyed by pruning.

### Milestone 3 — Hold'em abstraction

- 169 preflop classes;
- suit canonicalization;
- feature generation;
- EMD clustering;
- compact bucket lookup;
- abstract action menus.

Exit: deterministic bucket/action tests.

### Milestone 4 — 6-max coarse blueprint

Start with 12 buckets.

Exit:

- 50M+ traversals;
- checkpoint/resume;
- artifact compiler;
- no memory growth surprises;
- beats trivial agents strongly.

### Milestone 5 — range tracker

- 1326 combos;
- blockers;
- Bayesian updates;
- conditional joint sampler.

Exit: synthetic-policy Bayes tests recover known hidden-range shifts.

### Milestone 6 — nested online MCCFR

- betting-round root;
- hero action freezing;
- current-street lossless abstraction;
- future buckets;
- published depth rules.

Exit: search strategy stable under increasing iterations on fixed subgames.

### Milestone 7 — continuation portfolio

- normal/fold/call/raise-biased blueprint;
- continuation choice at leaves;
- compact sampled continuation storage.

Exit: leaf-policy ablation shows expected robustness improvement.

### Milestone 8 — off-tree exact action re-solving

- insert arbitrary bet size;
- re-solve from street root;
- blueprint leaf pseudo-harmonic fallback.

### Milestone 9 — vector late-street solver

- range-vs-range river;
- exact blockers;
- SIMD/vector terminal evaluation;
- turn extension if affordable.

### Milestone 10 — scale blueprint

Run bucket sweep:

```text
12 -> 24 -> 32 -> 48
```

Only advance if both:

```text
peak RSS target satisfied
strength / exploitability proxy improves materially
```

### Milestone 11 — serious evaluation

- LBR;
- restricted exact BR;
- independent blueprint cross-play;
- paired duplicate evaluation;
- external agent tests.

Only then discuss “superhuman.”

---

## 51. What an LLM agent should NOT do

To prevent common implementation failures, put these rules in the project `AGENTS.md` / `SKILL.md`:

1. **Do not replace the poker game engine with simplified betting rules** to make tests pass.
2. **Do not use hero's real private cards when computing opponent strategy** except through legitimate blocker conditioning.
3. **Do not key hot CFR tables with strings.**
4. **Do not store postflop running average strategy in RAM** unless a benchmark proves the memory is acceptable.
5. **Do not prune river or immediate-terminal actions** in the Pluribus negative-regret pruning mode.
6. **Do not round every off-tree opponent bet and continue without re-solving.**
7. **Do not treat an online subgame root as a single hidden state.**
8. **Do not use one scalar blueprint rollout as the only depth-limit leaf value** in the Pluribus-style search mode.
9. **Do not claim exploitability from win rate versus random/calling bots.**
10. **Do not claim superhuman strength without statistically meaningful external evaluation.**
11. **Do not silently change card/action abstraction between training and runtime.**
12. **Do not use the final online strategy for Bayes updates if the implementation spec calls for the weighted average strategy.**
13. **Do not allocate heap vectors/objects per infoset in the production table.**
14. **Do not optimize stochastic parallel code before a deterministic one-thread reference passes differential tests.**
15. **Do not add a value network merely because modern poker papers use one.** First implement the original continuation-policy leaf mechanism correctly; compare later.

---

## 52. Highest-risk technical areas

### 52.1 No-limit betting correctness

Min-raise, short-all-in reopening and side-pot bugs silently corrupt the game tree.

### 52.2 Information-set leakage

If a key contains inaccessible cards or sampled opponent state, the solver will learn a cheating perfect-information strategy.

### 52.3 Incorrect reach weights in MCCFR

A solver can “look reasonable” while converging to the wrong strategy.

### 52.4 Abstraction mismatch

A bucket ID computed differently at runtime than training is catastrophic but hard to notice.

### 52.5 Action history identity

Pot-fraction actions are context dependent. The same label can correspond to different absolute amounts and legal constraints.

### 52.6 Multiway online beliefs

Marginal ranges plus blockers are an approximation to the full joint hidden-state distribution. Sampling and weighting need careful tests.

### 52.7 Underconverged online solves

Real-time search is not automatically an improvement. An underconverged local solver can overwrite a stronger global blueprint with noise.

Add confidence / convergence checks and evaluate search gain directly.

---

## 53. Recommended “first experiment” on a 64 GB workstation

If using the Rust `conorarmstrong/pluribus` code as the initial executable baseline:

```bash
cargo build --release

# First verify the documented 12-bucket regime.
./target/release/pluribus train \
  --players 6 \
  --stack 10000 \
  --buckets 12 \
  --iters 200000000 \
  --out blueprint_12.bin

./target/release/pluribus inspect --blueprint blueprint_12.bin

# Basic sanity only, not a strength proof.
./target/release/pluribus eval \
  --blueprint blueprint_12.bin \
  --hands 200000 \
  --baseline random

./target/release/pluribus lbr \
  --blueprint blueprint_12.bin \
  --hands 20000
```

Record peak RSS externally during training.

Then run matched experiments with:

```text
12 buckets
24 buckets
32 buckets
```

at a smaller fixed iteration budget first, e.g. enough to estimate table-growth behavior, before committing to the largest long run.

Compare:

```text
peak RSS
infosets
bytes/infoset
iterations/sec
LBR result
cross-play
search gain
```

Do not immediately maximize iterations; first find the best abstraction that fits the machine.

---

## 54. Why a 64 GB version can still be strong

The strongest argument is architectural, not numerical.

Original Pluribus deliberately separates:

```text
GLOBAL GENERALIZATION      LOCAL PRECISION
coarse blueprint      +    lossless/current-state search
```

A Lite solver can make the global half much coarser while preserving the local half.

A human opponent only visits one microscopic trajectory through the global game tree. It is wasteful to store original-grade precision for every unreachable history if the solver can regenerate precision near the actual trajectory in real time.

The important condition is that the coarse blueprint must remain good enough to provide:

- reasonable range priors;
- strategically sensible leaf continuations;
- a safe-ish starting policy for online optimization;
- strong preflop behavior where search is limited.

That is why **12 buckets may be adequate for a development blueprint but not necessarily a final blueprint**. A practical tuning loop should increase blueprint precision until the marginal strength gain per GB is worse than spending the same resources on online search.

---

## 55. Optional improvements after a faithful Lite baseline

These are **not original Pluribus** and should be introduced only by ablation.

### 55.1 Safe/gadget resolving in two-player late streets

When only two players remain, theoretically safer subgame-solving gadgets can reduce errors from incorrect opponent ranges.

### 55.2 Value network for flop leaves

A ReBeL/DeepStack-like value network can permit much shorter flop search while representing a richer continuation than four fixed policies.

Tradeoff:

- additional training pipeline;
- belief-state input complexity;
- approximation risk;
- can greatly improve fixed-time search if trained well.

### 55.3 Strategic/action-aware card abstraction

Instead of clustering only equity/potential features, cluster states according to strategy behavior from a previous blueprint. This can use scarce buckets more efficiently.

### 55.4 Restricted Nash response / opponent exploitation

Useful for exploiting a known weak fixed population, but it changes the objective and can increase exploitability. Keep it as a separate policy layer.

### 55.5 Learned blueprint compression

A neural policy could compress a huge table, but then inference error, calibration, mixed-strategy fidelity and off-distribution behavior become new problems. Table quantization is much easier to validate first.

---

## 56. Papers directly relevant to understanding Pluribus

### P0 — Main Pluribus paper

Noam Brown, Tuomas Sandholm. **“Superhuman AI for multiplayer poker.”** *Science*, 365(6456), 885–890, 2019. DOI: 10.1126/science.aay2400.

- Primary system description.
- Six-player result.
- Blueprint + real-time search architecture.

<https://www.science.org/doi/10.1126/science.aay2400>

### P1 — Pluribus supplementary material — highest-value implementation reference

Brown & Sandholm. **Supplementary Materials for “Superhuman AI for multiplayer poker.”**

This is the most useful source for a programmer. It includes:

- exact hardware;
- action/card abstraction counts;
- MCCFR/pruning constants;
- integer regret storage;
- snapshot strategy method;
- nested search details;
- range beliefs;
- depth limits;
- four continuation strategies;
- pseudocode for blueprint MCCFR and nested search.

<https://noambrown.com/papers/19-Science-Superhuman_Supp.pdf>

Alternate author mirror:
<https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf>

### P2 — Original CFR

Martin Zinkevich, Michael Johanson, Michael Bowling, Carmelo Piccione. **“Regret Minimization in Games with Incomplete Information.”** NeurIPS 2007.

<https://proceedings.neurips.cc/paper/2007/hash/08d98638c6fcd194a4b1e6992063e944-Abstract.html>

### P3 — Monte Carlo CFR

Marc Lanctot, Kevin Waugh, Martin Zinkevich, Michael Bowling. **“Monte Carlo Sampling for Regret Minimization in Extensive Games.”** NeurIPS 2009.

This is the foundation for sampling CFR traversals rather than walking the complete game tree on every iteration.

### P4 — Discounted/Linear CFR

Noam Brown, Tuomas Sandholm. **“Solving Imperfect-Information Games via Discounted Regret Minimization.”** AAAI 2019.

DOI: 10.1609/aaai.v33i01.33011829  
<https://ojs.aaai.org/index.php/AAAI/article/view/4007>  
<https://arxiv.org/abs/1809.04040>

Pluribus cites this for Linear CFR / discounting behavior.

### P5 — Depth-limited imperfect-information solving

Noam Brown, Tuomas Sandholm, Brandon Amos. **“Depth-Limited Solving for Imperfect-Information Games.”** NeurIPS 2018.

<https://arxiv.org/abs/1805.08195>

This is essential background for understanding why leaf values in poker cannot be treated like chess evaluation scores and why continuation strategies are introduced.

### P6 — Nested / safe subgame solving

Noam Brown, Tuomas Sandholm. **“Safe and Nested Subgame Solving for Imperfect-Information Games.”** NeurIPS 2017.

<https://arxiv.org/abs/1705.02955>

Background for repeated re-solving, off-tree actions and the safe/unsafe design space.

### P7 — Potential-aware abstraction + Earth Mover's Distance

Sam Ganzfried, Tuomas Sandholm. **“Potential-Aware Imperfect-Recall Abstraction with Earth Mover's Distance in Imperfect-Information Games.”** AAAI 2014.

DOI: 10.1609/aaai.v28i1.8816  
<https://ojs.aaai.org/index.php/AAAI/article/view/8816>

Relevant to future-potential-aware hand clustering.

### P8 — Action translation / pseudo-harmonic mapping

Sam Ganzfried, Tuomas Sandholm. **“Action Translation in Extensive-Form Games with Large Action Spaces: Axioms, Paradoxes, and the Pseudo-Harmonic Mapping.”** IJCAI 2013.

<https://www.ijcai.org/Abstract/13/028>

Relevant when an opponent chooses a bet size that was not part of the blueprint abstraction.

### P9 — Lossless abstraction

Andrew Gilpin, Tuomas Sandholm. **“Lossless Abstraction of Imperfect Information Games.”** JACM 54(5), 2007.

DOI: 10.1145/1284320.1284324.

Relevant to suit-isomorphic / strategically identical card states.

---

## 57. Related poker-AI systems worth reading, but not required to clone Pluribus

### Libratus

Brown & Sandholm's heads-up system preceding Pluribus. Important for nested solving, abstraction and opponent off-tree handling.

### DeepStack

Moravčík et al., *Science* 2017. Important contrasting design: continual re-solving + learned counterfactual value networks.

### ReBeL

Later public-belief-state search combining self-play and value learning. Useful if eventually replacing Pluribus's hand-engineered continuation portfolio with a learned leaf evaluator.

Do not mix these architectures into the baseline until the original Pluribus-style system is measurable. Otherwise it becomes impossible to know which technique helped.

---

## 58. Source/repository index

### Sources supplied in the research request

- Wikipedia overview: <https://en.wikipedia.org/wiki/Pluribus_(poker_bot)>
- `fedden/poker_ai`: <https://github.com/fedden/poker_ai>
- `whatsdis/pluribus`: <https://github.com/whatsdis/pluribus>
- `conorarmstrong/pluribus`: <https://github.com/conorarmstrong/pluribus>
- `amaster97/poker_solver`: <https://github.com/amaster97/poker_solver>
- `bupticybee/TexasSolver`: <https://github.com/bupticybee/TexasSolver>
- Smithsonian overview: <https://www.smithsonianmag.com/smart-news/poker-playing-ai-knows-when-hold-em-when-fold-em-180972643/>
- Science: <https://www.science.org/doi/10.1126/science.aay2400>
- The Verge: <https://www.theverge.com/2019/7/11/20690078/ai-poker-pluribus-facebook-cmu-texas-hold-em-six-player-no-limit>
- Ars Technica: <https://arstechnica.com/science/2019/07/facebook-ai-pluribus-defeats-top-poker-professionals-in-6-player-texas-holdem/>

### Additional implementations found

- `mfine15/pluribus`: <https://github.com/mfine15/pluribus>
- `cRITlamb/pluribus2`: <https://github.com/cRITlamb/pluribus2>
- `c-heidt/pluribus-opponent-exploitation`: <https://github.com/c-heidt/pluribus-opponent-exploitation>

---

## 59. Final recommended architecture

If I were implementing the 64 GB system today, the target would be:

```yaml
name: Pluribus-Lite-64
language: Rust

poker:
  players: 2-6
  primary_mode: 6max
  stack_bb: 100
  deck: 52

blueprint:
  algorithm: external-sampling Linear MCCFR
  preflop_abstraction: lossless_169
  postflop_buckets_initial: [24, 24, 32]
  bucket_sweep: [12, 24, 32, 48]
  card_features: potential-aware equity distribution / EMD
  regrets: int32
  regret_floor: -310000000
  negative_regret_pruning: true
  prune_probability: 0.95
  lazy_allocation: true
  postflop_strategy_average: disk_snapshots
  runtime_strategy_encoding: uint16_or_packed
  runtime_storage: mmap

search:
  preflop: blueprint_mostly
  postflop: always_resolve
  root: beginning_of_current_betting_round
  ranges: full_1326_marginals_per_player
  current_street_card_abstraction: lossless
  future_buckets: 128_to_256
  large_subgames: monte_carlo_linear_cfr
  small_late_subgames: vector_cfr
  leaf_policies:
    - blueprint
    - fold_x5
    - call_x5
    - raise_x5
  opponent_offtree_action: insert_exact_and_resolve
  hero_previous_actions: freeze_actual_hand_only
  action_selection_policy: final_iteration
  belief_update_policy: weighted_average

storage:
  strings_in_hot_path: false
  topology: immutable_numeric
  mutable_actions: contiguous_i32
  card_bucket_ids: uint16
  checkpointing: atomic
  out_of_core_fallback: mmap_plus_lmdb_on_nvme

evaluation:
  toy_game_exactness: required
  differential_reference_solver: required
  lbr: required
  restricted_best_response: required
  duplicate_deals: recommended
  aivat: recommended
  human_superhuman_claim_without_external_stats: forbidden
```

### The main trade

The original Pluribus spent hundreds of GB during training to make a globally stronger blueprint. The Lite design deliberately spends much less on the global table and shifts importance to local online solving.

The first question to answer experimentally is therefore not:

> “How many iterations can we run?”

It is:

> “At fixed 64 GB, what allocation of RAM between blueprint precision and online-search precision produces the strongest measured policy?”

That should drive every later optimization.

---

## 60. Condensed implementation checklist

### Fidelity-critical

- [ ] Correct six-player no-limit game engine.
- [ ] No hidden-information leakage in infoset keys.
- [ ] External-sampling MCCFR verified on toy games.
- [ ] Linear weighting / discount schedule.
- [ ] Pluribus negative-regret pruning including exemptions.
- [ ] `i32` regret storage with floor.
- [ ] Lossless preflop + lossy global postflop abstraction.
- [ ] Lazy infoset/action-sequence allocation.
- [ ] Preflop average strategy + postflop snapshot averaging.
- [ ] Bayesian 1326-combo range tracker.
- [ ] Search root at beginning of current betting round.
- [ ] Hero actual-hand past decisions frozen; opponents not frozen within current round.
- [ ] Current search street lossless; future streets bucketed.
- [ ] Structural Pluribus depth limits.
- [ ] Four continuation strategies at leaves.
- [ ] Exact off-tree action insertion + re-solve.
- [ ] Final-iteration policy for action selection; averaged policy for belief update.

### 64 GB engineering

- [ ] Numeric infoset IDs; no strings.
- [ ] Packed contiguous regret rows.
- [ ] No postflop running strategy sum in RAM.
- [ ] Quantized runtime policy.
- [ ] mmap blueprint.
- [ ] Suit canonicalization.
- [ ] Compact `uint16` card bucket storage.
- [ ] Reusable online-search arena.
- [ ] Peak-RSS telemetry.
- [ ] Out-of-core NVMe fallback.

### Strength validation

- [ ] Kuhn/Leduc convergence.
- [ ] Small-deck multiplayer tests.
- [ ] Engine fuzz tests.
- [ ] Reference-vs-fast differential CFR tests.
- [ ] Search-vs-blueprint paired ablation.
- [ ] LBR / restricted BR.
- [ ] Cross-play independent seeds.
- [ ] External agent evaluation.
- [ ] Confidence intervals before any human-strength claim.

---

# References

1. Brown, N., & Sandholm, T. (2019). *Superhuman AI for multiplayer poker*. Science 365(6456), 885–890. <https://doi.org/10.1126/science.aay2400>
2. Brown, N., & Sandholm, T. (2019). *Supplementary Materials for Superhuman AI for multiplayer poker*. <https://noambrown.com/papers/19-Science-Superhuman_Supp.pdf>
3. Zinkevich, M., Johanson, M., Bowling, M., & Piccione, C. (2007). *Regret Minimization in Games with Incomplete Information*. NeurIPS.
4. Lanctot, M., Waugh, K., Zinkevich, M., & Bowling, M. (2009). *Monte Carlo Sampling for Regret Minimization in Extensive Games*. NeurIPS.
5. Brown, N., & Sandholm, T. (2019). *Solving Imperfect-Information Games via Discounted Regret Minimization*. AAAI. <https://doi.org/10.1609/aaai.v33i01.33011829>
6. Brown, N., Sandholm, T., & Amos, B. (2018). *Depth-Limited Solving for Imperfect-Information Games*. NeurIPS. <https://arxiv.org/abs/1805.08195>
7. Brown, N., & Sandholm, T. (2017). *Safe and Nested Subgame Solving for Imperfect-Information Games*. NeurIPS. <https://arxiv.org/abs/1705.02955>
8. Ganzfried, S., & Sandholm, T. (2014). *Potential-Aware Imperfect-Recall Abstraction with Earth Mover's Distance in Imperfect-Information Games*. AAAI. <https://doi.org/10.1609/aaai.v28i1.8816>
9. Ganzfried, S., & Sandholm, T. (2013). *Action Translation in Extensive-Form Games with Large Action Spaces: Axioms, Paradoxes, and the Pseudo-Harmonic Mapping*. IJCAI. <https://www.ijcai.org/Abstract/13/028>
10. Gilpin, A., & Sandholm, T. (2007). *Lossless Abstraction of Imperfect Information Games*. JACM 54(5). <https://doi.org/10.1145/1284320.1284324>

---

**End of report.**
