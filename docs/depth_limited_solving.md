# Depth-Limited Solving

This document describes the planned optimization for the multithreaded CPU poker solver:

- what depth-limited solving is
- how to estimate EV at the depth limit
- how to expose the feature in the solver API
- what performance to expect
- how to implement it step by step
- what tests to add

## 1. What Depth-Limited Solving Is

Depth-limited solving means we do not solve the full game tree all the way to terminal showdown nodes.
Instead, we stop expanding the tree at a chosen depth and replace everything below that point with an
approximate continuation value.

In practice:

- above the cutoff depth, the solver behaves normally
- at the cutoff depth, the solver stops branching
- the remaining future value is estimated with a heuristic

This is useful when the full tree is too large or too slow for CPU solving.

## 2. What Happens At The Depth Limit

At the depth limit, the solver must answer:

- "If the game continued from this node, what is the expected value?"

That value is used like a terminal payoff for that node.

So the depth-limit node becomes a pseudo-terminal node:

- actions below it are not expanded
- the subtree below it is not solved explicitly
- EV is approximated from the current state

## 3. Best Way To Estimate Depth-Limited EV

The best heuristic EV depends on the game street and how much information we have.
For a CPU poker solver, the strongest practical approach is usually:

### 3.1 Range-based continuation EV

Estimate value from:

- current pot size
- board texture
- player ranges
- position / action history
- remaining streets

The simplest version is:

- evaluate hand strength or equity versus opponent range
- convert equity into chip EV using pot and future betting assumptions

### 3.2 Rollout / Monte Carlo heuristic

If exact range math is too expensive at the cutoff, run a small number of sampled rollouts:

- complete the remaining board cards
- sample opponent holdings from the current range
- simulate terminal outcomes

This is usually more accurate than a pure static heuristic, but more expensive.

### 3.4 Recommended choice for this solver

For the first implementation, use a simple deterministic heuristic:

- exact or approximate hand equity vs range
- discounted by remaining betting uncertainty
- optionally improved by board texture and street depth

This is easier to validate than rollout-based or learned approaches.

## 4. API Behavior

The solver should accept a depth-limit parameter.

Recommended behavior:

- `depth_limit_plies = 0` means no depth limit
- `depth_limit_plies = 0` solves until terminal nodes
- any positive value `N` means stop after `N` plies from the current node

Important:

- the meaning of "depth" must be defined consistently across the tree builder and solver
- the heuristic EV function must receive enough state to compute a stable continuation estimate

## 5. Performance Expectations

Depth-limited solving should improve performance because:

- fewer nodes are expanded
- fewer terminal evaluations are needed
- the subgame tree is smaller
- multithreading scales better when each subtree is lighter

Expected outcome:

- much faster solves for large postflop trees
- lower memory use
- some loss in accuracy compared to full-depth solving

Tradeoff:

- shallower depth limits increase speed
- deeper limits increase accuracy

The main goal is to find a cutoff where accuracy loss is acceptable for the speed gain.

## 6. Step-By-Step Implementation Plan

### Step 1: Define the depth-limit meaning

- choose whether depth is measured in plies, actions, or streets
- document the rule in code and README
- make `0` map to "solve to terminal"

### Step 2: Add solver configuration

- add a depth-limit field to the relevant solver config
- thread it through tree building and solving code
- validate inputs early

### Step 3: Mark cutoff nodes in tree construction

- stop expanding branches after the configured depth
- flag those nodes as depth-limited leaves
- make sure terminal nodes and depth-limited nodes are distinguishable

### Step 4: Implement heuristic EV evaluation

- write a function that estimates continuation EV from a cutoff node
- start with simple range equity and pot-based projection
- keep it deterministic
- make it fast enough to use many times during solve

### Step 5: Integrate heuristic EV into the solver

- when a cutoff node is reached, call the heuristic EV function
- return that value as if the node were terminal
- ensure the solver still aggregates EV correctly across branches

### Step 6: Add multithreaded support safely

- make the heuristic evaluator thread-safe
- avoid shared mutable state
- keep any cached data read-only or properly synchronized

### Step 7: Measure accuracy and speed

- benchmark full-depth vs depth-limited solves
- compare game value and exploitability
- measure node count, runtime, and memory usage

### Step 8: Tune the heuristic

- adjust equity model assumptions
- add board texture weighting if needed
- add street-specific logic if the cutoff is too rough

### Step 9: Document the final behavior

- describe the cutoff rule
- describe the heuristic EV math
- document expected tradeoffs and examples

## 7. Tests To Add

### 7.1 Parameter tests

- `depth_limit = 0` solves to terminal
- positive depth limit stops at the expected cutoff
- invalid depth values are rejected

### 7.2 Tree structure tests

- cutoff nodes are marked correctly
- terminal nodes remain terminal
- no children are created below the depth limit

### 7.3 Heuristic EV tests

- heuristic EV is deterministic for the same node
- heuristic EV returns sensible sign and magnitude on simple toy spots
- heuristic EV changes when board/range inputs change

### 7.4 Solver integration tests

- full-depth solve still matches existing behavior
- depth-limited solve returns a valid game value
- depth-limited leaves are used instead of expanding the subtree

### 7.5 Regression tests

- compare a known small game against exact solve
- verify no crash on all street types
- verify multithreaded and single-threaded results match

### 7.6 Performance tests

- depth-limited solve is faster than full-depth on a large tree
- memory usage drops or stays bounded
- scaling improves when multiple threads are enabled

### 7.7 Accuracy tests

- compare depth-limited EV vs full solve on representative spots
- track error as depth changes
- confirm deeper limits reduce heuristic error

## 8. Suggested Acceptance Criteria

- `0` depth limit means terminal solve
- cutoff nodes return heuristic EV without expanding children
- the solver remains deterministic
- multithreaded results match single-threaded results
- speed improves on large trees
- accuracy loss is measured and documented
