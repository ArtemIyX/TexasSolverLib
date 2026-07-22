# Large-tree MCCFR implementation plan

## Status and authority

This is the authoritative implementation plan for blueprint generation and
bounded online sub-solving.  It supersedes any implied production status in
older review notes.  `AGENTS.md` and this document are the required design
contract before major HUNL or multiway solver work.

The product contract is:

```text
structured public state + weighted private ranges + abstraction/version
    -> bounded solve -> typed root actions + policy/value/quality diagnostics
```

The library does not automate poker clients.  It produces offline blueprints,
training data, and structured online sub-solve results only.

## Module maturity

| Module | Maturity | Permitted use |
| --- | --- | --- |
| Recursive DCFR / `HUNLFlatDCFR` | small-game oracle | Correctness comparison and tiny-tree tests; not the production large-tree engine. |
| `HUNLFlatMCCFR` | full-graph prototype/oracle | MCCFR semantics and deterministic baseline only; never production RTA until it no longer needs a full graph or graph-sized worker scratch. |
| Sampled builder/storage/traversal | experimental heads-up engine | Single-thread validation only until snapshot traversal, bounded deltas, deterministic merge, and runtime memory admission are complete. |
| Multiway state, terminal, private, CFR | standalone validated helpers | May not be exposed as a multiway solve API until connected through the production traversal/storage contract. |
| Value-network leaf integration | unimplemented contract | No backend may invent heuristic or zero cutoff values in production. |

## Production architecture

The production solver is a separate sampled/lazy engine, not a widened
full-graph solver:

1. Validate a structured root request including public state, stacks and
   contributions, active seats, jointly compatible weighted ranges, abstraction
   and model identifiers, and value units.
2. Preflight every retained allocation.  Warn around 48 GiB, target less than
   56 GiB resident use, and reject before a conservative 60 GiB bound.  Runtime
   cache admission repeats this check before growth.
3. Assign deterministic global trajectory identifiers.  Each trajectory seed
   derives from the global seed, iteration, traverser, and trajectory id.
4. Traverse immutable batch snapshots only.  Workers own preallocated scratch
   and emit bounded sparse deltas; they never mutate the builder or central
   storage.
5. Intern/expand public states and create rows at a coordinator boundary,
   then merge worker deltas in fixed worker/row/action/bucket order.
6. Apply DCFR discounting at defined batch boundaries, expose root-only typed
   action exports, and retain the last clean snapshot on cancellation/failure.
7. Evaluate every depth cutoff through the same typed, batched leaf-evaluator
   contract.  Backends that cannot do so reject depth limiting.

## Phased delivery gates

### Phase A — shared contracts

- Structured root/range request and range normalization.
- Typed action descriptors and stable abstraction/menu identity.
- Leaf evaluator with per-seat values, units, conservation convention, model
  version, deterministic request ordering, and failure policy.
- Explicit named quality metrics and diagnostic metadata.

### Phase B — safe sampled heads-up solve

- Immutable traversal snapshot and coordinator-only expansion/storage growth.
- Bounded sparse worker delta arena and deterministic merge.
- Enforced cache/memory admission and accurate retained-capacity accounting.
- Resumable deadline cursor with no replayed trajectories.
- Exact tiny-game and injected-leaf equivalence tests.

### Phase C — multiway integration

- Snapshot-capable betting state with cumulative short-raise reopening.
- Validated side-pot layouts and positional/fractional odd-chip rule.
- Compiled, allocation-free compatible private-range sampler.
- External-sampling update math with explicit sampling/inclusion reach.
- Integrated 3–6 player traversal, typed export, policy evaluation, and
  NashConv estimator diagnostics.

## Regression gate matrix

No phase advances merely because output is finite or normalized.  Required
tests must show:

- range/blocker changes alter posterior reach and strategy as expected;
- exact injected continuation values reproduce untruncated tiny trees;
- repeated runs and worker counts obey the documented deterministic merge
  contract;
- runtime retained capacity remains under the preflight bound;
- action descriptors match the legal root edge menu;
- multiway toy games match exhaustively enumerated expectations.

