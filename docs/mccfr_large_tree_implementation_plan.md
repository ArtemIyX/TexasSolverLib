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
| Sampled builder/storage/traversal | experimental heads-up engine | Runtime public-state admission and worker-local delta/ordered coordinator merge exist; single-thread validation only until immutable snapshots and coordinator-owned expansion/row admission are complete. |
| Multiway state, terminal, private, CFR | standalone validated helpers | Betting snapshots/reopening, bound pot settlement, compiled private ranges, and explicit CFR contracts exist; no integrated multiway solve API yet. |
| Value-network leaf integration | contract only | Public depth-limited solves fail closed until every backend consumes the same evaluator. |

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

## Implementation status (2026-07-22)

Implemented foundations: blocker-aware joint HUNL range normalization; typed
root actions; fail-closed public depth limits plus a leaf-evaluation contract;
runtime sampled cache admission; worker-local deltas with ordered coordinator
merge; multiway betting snapshots, validated pots/odd-chip order, compiled
private ranges, external-sampling row math, and estimator diagnostics.

Still blocking production: public range solving must carry joint private reach
through traversal; leaf requests must be evaluated by every backend; sampled
workers need immutable snapshots and coordinator-only expansion/row admission;
and the multiway helpers must be connected into an integrated traversal,
storage, export, and policy-evaluation pipeline.
