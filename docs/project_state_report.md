# TexasSolver Current Project State

## 1. Scope and snapshot

This report describes the code that exists in the repository and the public
paths that expose it. It is a requirements and implementation inventory, not
a code review and not a bug search.

Inspection snapshot:

- Date: 2026-08-09
- Branch: `vibe/pt1`
- HEAD: `af03a82` (`All test are fixed`)
- Working tree: no pre-existing changes at inspection time; this report is the
  new untracked documentation file
- Language: C++17
- Namespace: `texas::`
- Test directories were intentionally excluded from inspection and from this
  report.

The report is based on `README.md`, `AGENTS.md`,
`docs/multiway_release_runbook.md`, `docs/multiway_release_config.json`, the
public headers under `include/`, implementations under `src/`, build files,
and examples.

Status terms used below:

- **Implemented**: public API and corresponding library implementation exist.
- **Integrated**: available through a normal library entry point or composed
  subsystem.
- **Boundary**: implemented as a callback, artifact, or host-owned contract;
  the library does not provide the external model or service itself.
- **Limited**: intentionally restricted by the current implementation or
  release contract.

## 2. What the project is

TexasSolver is a reusable CMake C++17 library for poker subgame solving and
strategy analysis. It is a port of the Rust
`amaster97/poker_solver` implementation. The main target is
`TexasSolver::texas`.

The project contains two related solver families:

1. Small and heads-up game solvers based on recursive or flat-tree DCFR and
   MCCFR-style traversal.
2. A newer sampled/lazy infrastructure for structured heads-up ranges and
   multiway six-max postflop solving.

The multiway release design is library-only. The JSON release profile is not
parsed by the library. A host application maps that profile to C++ objects,
loads verified artifacts, supplies protected request data, and owns transport,
logging, and deployment policy.

## 3. Build, package, and public API surface

### 3.1 CMake library

`CMakeLists.txt` defines a static `texas` target and the alias
`TexasSolver::texas`. It lists the production headers and C++ sources
explicitly, publishes the include directory, and requires C++17.

The build supports:

- CMake 3.20 or newer.
- `TEXASSOLVER_BUILD_TESTS` option.
- `TEXASSOLVER_BUILD_EXAMPLES` option.
- `Threads::Threads`.

The vendored `external/pokerHandEvaluator/cpp` project is always added as the
private 5-card, 6-card, and 7-card evaluator dependency of `texas`.

### 3.2 Installation and consumption

The project installs:

- All headers under `include/`.
- The library archive/import library.
- Exported CMake targets under the `TexasSolver::` namespace.
- `TexasSolverConfig.cmake` and a same-major-version package version file.

The documented consumption modes are `add_subdirectory(...)` and
`find_package(TexasSolver CONFIG REQUIRED)`. `include/core/lib.hpp` provides a
convenience facade with aliases and inline entry points for the main solver,
HUNL, sampled HUNL, range, and multiway types.

## 4. Core data model and generic solver layer

### 4.1 Shared types

`include/core/types.hpp` defines the common scalar vocabulary:

- `PlayerId`, `ActionId`, `InfosetId`, and `InfosetKey`.
- `Probability` and `Value`, both represented as `double`.
- `ChanceOutcome` for chance action/probability pairs.
- `SolveOutput`, `SolveProfile`, and `WorkerProfile` for generic results and
  timing counters.

`include/core/game.hpp` defines the polymorphic game interface used by the
  generic recursive solver. `include/core/arena.hpp` provides reusable arena
  allocation for solver-owned storage.

### 4.2 DCFR

`include/solver/dcfr.hpp` contains the generic DCFR implementation and shared
accumulator structures. It provides:

- Configurable positive-regret, negative-regret, and strategy-sum discounting.
- Regret matching with uniform fallback when positive regret mass is zero.
- Infoset registration and flat regret/strategy arenas.
- Recursive CFR traversal over decision and chance nodes.
- Average-strategy export.
- Locked strategy support for selected infosets.

The same layer computes expected profile value, best-response value, and mean
unilateral improvement for two-player games.

`include/solver/parallel_dcfr.hpp` adds a worker-plan based solver. Workers use
local infoset accumulation and the coordinator merges worker state. Parallel
selection is exposed through `parallel_dcfr_enabled()` and
`parallel_dcfr_worker_count()` and is also used by the generic solver wrapper
when the requested worker count and estimated root branching justify it.

### 4.3 Vector DCFR

`include/solver/dcfr_vector.hpp` and `src/solver/dcfr_vector.cpp` provide a
per-private-hand/vectorized DCFR variant. Each infoset stores vector-valued
regrets and strategy sums over hand entries. It supports:

- Per-hand regret matching.
- Vector reach propagation.
- Vector terminal evaluation callbacks.
- Optional skip masks.
- DCFR discounting over vector rows.
- Average-strategy export.

`dcfr_vector_parallel.hpp` contains chance-range partitioning helpers for
parallel vector traversal.

### 4.4 Exploitability and value analysis

`include/solver/exploit.hpp` provides heads-up betting-tree construction,
terminal utility evaluation, hole-card-pair enumeration, expected value, and
exploitability calculation. It exposes both per-combo and vector best-response
walk modes and a restricted-game value helper.

## 5. Kuhn and Leduc poker

### 5.1 Kuhn

`KuhnState` implements the complete two-player Kuhn game:

- Chance dealing.
- Pass and bet actions.
- Terminal fold and showdown utility.
- Current-player and legal-action queries.
- Infoset-key generation from private card and public action history.
- Polymorphic cloning and action application through `texas::Game`.

`texas::solve_kuhn(...)` runs generic or parallel DCFR and returns average
strategy, game value, exploitability, and solve metadata.

### 5.2 Leduc

`LeducState` implements the two-round Leduc game:

- Private-card dealing and one public-card chance phase.
- Separate round histories.
- Fold, call, and raise actions.
- Raise and call counters.
- Round completion and next-player progression.
- Showdown comparison with paired-card handling.
- Infoset keys containing the private card, public card where available, and
  betting histories.

`texas::solve_leduc(...)` uses the same generic DCFR interface.

## 6. Heads-up no-limit Hold'em engine

### 6.1 Card, action, and configuration model

`include/games/hunl.hpp` defines the heads-up no-limit Hold'em model.

Implemented pieces include:

- Four streets: preflop, flop, turn, river, and showdown terminal state.
- Integer card encoding as `rank * 4 + suit`.
- Card validation, distinct-card validation, rank/suit extraction, and string
  formatting.
- Fixed action identifiers for fold, check, call, five bet sizes, five raise
  sizes, and all-in.
- Configurable starting stack, blinds, ante, starting street, board,
  contributions, hole cards, raise caps, bet fractions, raise multipliers,
  all-in rules, minimum bet, and optional rake fields.
- Per-street betting menus and custom flop/turn/river bet-fraction menus.
- Stack-to-pot-ratio and amount calculation helpers.
- Canonical infoset encoding containing private cards, board, street segments,
  and bounded action-history codes.

`HUNLState` is an immutable-style value state. `apply(...)` and
`next_state(...)` return successor states. It handles:

- Player actions.
- Board chance cards.
- Street transitions.
- Fold and showdown terminals.
- All-in flags and contribution tracking.
- Legal action enumeration and current-player queries.

The configuration validator rejects malformed card, board, stack, action-menu,
raise-cap, range, and abstraction combinations before solving.

### 6.2 Recursive exact HUNL path

`HUNLTree` in `include/games/hunl_tree.hpp` builds a memoized recursive game
tree from `HUNLState`. Nodes retain:

- Decision, chance, fold, showdown, and depth-limited terminal information.
- Legal actions and child ids.
- Board, street, contribution, and chance metadata.
- Optional infoset keys.

The generic DCFR layer and the HUNL solve entry points can solve explicit
fixed-hole-card subgames recursively. This is the small-game
correctness/reference path used by the project architecture.

The public `solve_hunl_postflop(...)` entry point is deliberately narrower:

- It requires a postflop starting street.
- It requires explicit initial hole cards.
- It rejects non-zero rake.
- It rejects the legacy range/bucket contract.
- It rejects depth limits unless the newer typed leaf-evaluator path is used.

### 6.3 Flat HUNL graph and DCFR

`HUNLFlatSolveGraph` and `HUNLFlatInfosetTable` provide a compact flat graph
representation with indexed nodes and contiguous infoset value storage.

The flat layer supports:

- Fold, showdown, chance, and decision node tags.
- Packed board/state metadata.
- Infoset rows with configurable hand/bucket and action layout.
- `Float64` and `Float32` storage modes at the flat-table API. Unsupported
  compressed modes are not part of the public contract.
- Cache-line-aligned worker scratch buffers.
- Memory estimation for graph, infosets, solver buffers, worker scratch,
  parallel plan, and auxiliary allocations.
- Parallel work plans based on infoset, value, node, and backward-cost ranges.

`HUNLFlatDCFR` implements the flat full-tree solver. It supports dense and
bucket-aware tables, optional sparse rows, parallel worker plans, discounting,
average-strategy export, and stage-level profiling.

`HUNLFlatPipeline` exposes an explicit iteration schedule:

1. Forward strategy/reach profiling.
2. Aggregate reach.
3. Opponent reach.
4. Showdown equity.
5. Optional depth-limited evaluation.
6. Backward counterfactual value propagation.
7. Regret update.

`HUNLFlatExpectedValue` evaluates a flat graph against an average strategy and
can use precomputed terminal-value tables.

Backend selection is controlled explicitly by `HUNLBackendSelection` in the
solve request. Profiling environment variables remain observability-only.

### 6.4 Flat MCCFR and sampled kernels

`HUNLFlatMCCFR` is retained as test-only research support. Its exposed
sampling modes are:

- Exact.
- Public-chance sampling.
- External sampling.
- Average-strategy traversal.

The research configuration retains these baseline modes:

- No baseline.
- Moving-average baseline.
- Depth-limited value-table and terminal-board-cache modes are not shipped.

It also exposes batch size, traversals per iteration, strategy-sampling
parameters, optional DCFR discounting, dense validation retention, iterative
external traversal, sparse storage, worker execution, and detailed sampled
profile counters.

The research flat path has both dense and sparse storage paths. It is not part
of the stable aggregate target.

### 6.5 HUNL bucket abstraction

`util/abstraction.hpp` loads versioned abstraction data from a packaged archive
format and exposes:

- Abstraction metadata and version.
- Flop, turn, and river bucket assignments.
- Board and hand index maps.
- Suit-canonical board/hand lookup.
- Bucket lookup by board, hole cards, and street.

`HUNLFlatBucketMap` binds abstraction tables to graph infosets. It stores
canonical boards, dense bucket ids, bucket hand counts, and optional bucket
weights. It can apply initial range inputs to bucket weights.

`HUNLBucketTerminalTable` precomputes per-board bucket showdown matrices. Each
matrix stores valid pair counts, net win counts, tie counts, and hand counts.
It can produce expected showdown value for supplied bucket weights and provides
a heuristic depth-limited value helper for the legacy flat path.

### 6.6 Structured range ownership

Structured sampled HUNL and multiway own their range representations directly.
The detached `ranges/` experiment and range-cache benchmark are not part of
the stable target or installed headers.

### 6.7 Preflop equity and preflop solving

The preflop module contains:

- 169-class hand indexing and decoding.
- Three suit/blocker variants for class-pair equity.
- Exact pair-equity enumeration.
- Monte Carlo pair-equity estimation.
- Flat and parallel 169 x 169 x 3 equity-table construction.
- Binary and CSV equity-table persistence.
- A preflop HUNL solver entry point.

`preflop_rvr.hpp` adds a class-169 range-vs-range implementation with:

- Class-combo enumeration.
- Blocker-mass tables.
- A preflop betting tree.
- Equity terminal-cache construction.
- Vector DCFR over class combo reach.
- Class-169 strategy export.

### 6.8 Structured sampled HUNL range solver

The structured range solver is separate from the legacy exact fixed-hand entry
point. Its main public types are in `solver/hunl_sampled_*.hpp` and
`games/hunl_solver.hpp`.

The implemented pipeline is:

1. Validate a `HUNLStructuredRootRequest`.
2. Normalize two explicit hand-weight ranges into blocker-compatible joint
   deals.
3. Select an explicit hero hand and bucket for the exported acting-root
   strategy. The current structured contract uses bucket zero.
4. Sample private deals and public chance outcomes per deterministic trajectory.
5. Sample opponent actions and enumerate traverser actions using external
   sampling.
6. Apply regret and average-strategy deltas to worker-local streams.
7. Merge deltas in deterministic worker/trajectory order.
8. Export only the selected root strategy, with optional range-wide diagnostic
   export.

`HUNLSampledBuilder` lazily creates public nodes, edges, cached states, and
infoset ids. `HUNLSampledStorage` stores action-major bucket rows and provides
current-strategy calculation from regret rows.

`HUNLSampledScheduler` partitions trajectory ids deterministically. The
traversal API exposes both a direct merge path and a worker-safe unmerged path.
Worker-local deltas include trajectory ids so merge order is reproducible.

`HUNLSampledSolver` supports:

- Fixed-private roots through the explicit sampled graph path.
- Structured range roots through `HUNLSampledRangeSession`.
- Fresh bounded batch runs.
- Resumable structured sessions.
- Deadline-based whole-batch solving.
- Latest-clean-root export.
- Profile counters for traversal, merge, terminal, export, storage, and
  observed memory.

Memory preflight estimates graph/cache, infoset rows, sparse values, terminal
cache, worker deltas, exports, and structured range-session storage. Default
thresholds are 48 GiB warning and 60 GiB rejection. Adaptive fallback can
reduce minibatch, bucket, and depth hints before rejecting a request.

Depth-limited structured traversal uses the typed `HUNLLeafEvaluator` callback.
The callback receives a public state, exact sampled private deal, bucket reach,
value units, deadline, abstraction version, and model version. It must return a
populated same-unit result or the solve stops at the last clean batch. The
library does not provide a neural/value model itself.

### 6.9 HUNL SIMD and low-level performance support

`util/simd.hpp` implements scalar, SSE2, and AVX2 variants for common solver
row operations:

- Regret and strategy discounting.
- Positive-regret extraction and normalization.
- Regret and strategy updates.
- Strided action/bucket updates.
- Dot products and weighted reductions.
- Strategy-row construction.
- Float32 kernels.

Runtime backend detection selects the available implementation. The sampled
HUNL layer additionally exposes action-major Float32/Float64 kernels for
regret matching, average-strategy accumulation, regret-delta addition, SAXPY,
and mixed-precision reductions.

## 7. Multiway six-max stack

The multiway code is a distinct subsystem under `games/multiway_*` and
`solver/multiway_*`. It is designed around bounded six-max postflop solving,
lazy public-state expansion, sparse rows, deterministic worker merges, and
root-only deployment artifacts.

### 7.1 Rules and betting state

`MultiwayGameRules` is a versioned rules profile. The release profile is:

- Six seats.
- 10,000-chip stacks.
- 50/100 blinds.
- No ante.
- No straddle.
- No rebuy.
- Explicit zero rake.

`MultiwayState` supports two through six seats through a dynamic boundary and
tracks:

- Total and current-street contributions.
- Remaining stacks.
- Folded and all-in flags.
- Pending action and whether a seat may raise.
- Amount faced when a seat last acted.
- Current player, last aggressor, current bet, and last full raise size.
- Street and big blind.

The betting transition code handles check, call, fold, bet, raise, all-in,
full-raise reopening, short raises, pending-action rotation, street completion,
and the distinction between betting closure, board runout, street transition,
fold terminal, and showdown terminal.

`MultiwayBettingSnapshot` is a complete validated live-state handoff. It lets a
caller start a bounded subgame without replaying hidden prior action history.

`MultiwayFixedState` is the hot-path equivalent with fixed arrays for six seats
and fixed-size action menus. It avoids heap allocation during state transition
and is paired with `MultiwayFixedTerminalScratch` for reusable settlement
scratch.

### 7.2 Replay and rake

`MultiwayHandHistory` and `MultiwayReplayEvent` provide deterministic public
reconstruction:

- Decision events record exact actor, action, target street contribution, and
  decision seed.
- Street-transition events record the next street and first player.
- Replay validates actor identity and legal action against the reconstructed
  state.
- The engine reconstructs public betting state from the initial config and
  ordered events.

`MultiwayRakePolicy` supports explicit zero rake and percentage rake with a
  basis-point rate, chip cap, and no-flop-no-drop control. Rake is applied once
  to the combined contested-pot total and is never taken from uncalled refunds.

### 7.3 Side-pot and showdown settlement

`build_multiway_pot_layout(...)` constructs ordered side pots from contribution
levels, identifies eligible live players, and returns single-contributor
layers as refunds.

`settle_multiway_terminal(...)` then:

- Applies folded-player eligibility rules.
- Applies the configured rake policy once.
- Selects winners from supplied `Strength` values.
- Splits tied pots.
- Distributes odd chips from an explicit cyclic starting seat.
- Returns pots, refunds, payouts, utilities, rake taken, and explicit utility
  units.

The fixed-array settlement kernel provides the same semantics without dynamic
allocation and is intended for worker-local hot paths.

`evaluate_multiway_showdown(...)` evaluates surviving seven-card hands through
the hand evaluator and delegates final accounting to the terminal layer.

### 7.4 Multiway private range sampling

`MultiwayPrivateConfig` accepts a board and a weighted range for each seat.
`MultiwayCompiledPrivateRanges` canonicalizes and merges duplicate/reversed
hole-card entries, builds cumulative weights, and supports allocation-free
worker sampling.

The sampling contract is one independent per-seat proposal per trajectory:

- Compatible proposals are accepted with their product proposal probability.
- Colliding deals are discarded.
- No global compatible-deal renormalization is performed.
- Per-sample reach fields expose chance, conditional, proposal, and inclusion
  reach values.

Coordinator preflight performs a bounded feasibility search before workers
start. It reports feasible, infeasible, or search-budget-exhausted status.

### 7.5 Multiway action abstraction

`MultiwayActionAbstraction` builds stable legal action menus containing exact
action ids, action indices, target street contributions, and action-menu ids.

It supports:

- Unopened, facing-single-open, facing-open-and-callers, and facing-three-bet
  preflop situations.
- Unknown, in-position, and out-of-position context.
- Compatibility sizing and contextual sizing modes.
- Configured first-bet and raise basis-point templates.
- Exact insertion of an observed off-tree legal action into a reconstructed
  menu.

`MultiwayPublicBuilder` creates coordinator-admitted root, action-child,
board-chance-child, sampled-board-child, and street-transition descriptors.
Public state ids and history ids are deterministic fingerprints of public
state, history, and legal menu data.

### 7.6 Bucket model and artifacts

`MultiwayBucketTable` stores 1326 fixed unordered hole-card assignments for one
canonical postflop board. Board-blocked hands use
`MULTIWAY_INVALID_BUCKET`. `MultiwayBucketRegistry` sorts tables and uses
binary-search lookup by street and canonical board.

The current baseline bucket artifact is deterministic feature hashing, not a
learned clustering model. Features include:

- Board rank mask and suit mask.
- Board pair count and size.
- Hole-card high and low ranks.
- Suitedness.
- Hole ranks paired with the board.
- Hole suits matching the board.

The standard release bucket profile is 96 flop buckets, 128 turn buckets, and
192 river buckets. Bucket registries support identity binding, coverage
validation, binary serialization, and deserialization.

### 7.7 Multiway CFR primitives

`multiway_cfr.hpp` implements the mathematical update boundary for two through
six seats:

- Full-tree CFR update.
- External-sampling MCCFR update.
- Explicit sampling reach and traverser reach.
- Multi-seat counterfactual reach as chance reach multiplied by every
  non-traverser seat reach.
- Regret matching.
- Regret and average-strategy delta application.
- NashConv calculation as the sum of unilateral best-response improvements.
- Exact and sampled quality-diagnostic metadata.

The update API keeps sampled action values separate from full-tree action
values and applies the sampling importance ratio explicitly.

### 7.8 Lazy public graph and deterministic root traversal

`MultiwayPublicStateDescriptor` contains only public state data:

- Stable id and parent id.
- Typed incoming edge.
- Canonical public history.
- Validated betting snapshot.
- Board and board-runout state.
- Legal action menu.

It contains no private cards, opponent ranges, or mutable policy data.

`MultiwaySolverCoordinator` owns public-state admission, sparse-row admission,
row mutation, worker-delta merging, root-policy export, and solve diagnostics.

`MultiwaySparseRowStorage` stores action-major `[action][bucket]` rows in
Float64. Rows are admitted only through configured maximum row and value
limits. Workers never mutate coordinator rows directly.

`MultiwayRootExternalSamplingTraversal` implements bounded lazy traversal:

- Samples a private deal through an opaque coordinator-bound token.
- Samples opponent decisions and public chance.
- Enumerates traverser decisions.
- Admits only visited public states and infoset rows.
- Resolves exact fold/showdown terminals through the terminal adapter.
- Stops at a typed `MultiwayLeafEvaluator` boundary when configured.

`MultiwayRootBatchRunner` partitions trajectories deterministically, runs worker
streams, joins workers before merge, and merges streams through the coordinator
in fixed order. Its output retains trajectory, acceptance, discard, and merged
delta counts.

### 7.9 Terminal adapter and public chance

`MultiwayTerminalAdapter` is the cold integrated boundary for private sampling,
public chance, street transitions, and terminal settlement. It provides:

- Opaque private-deal tokens bound to one coordinator.
- Access to sampled hole cards only for bound traversal code.
- Private sampling reach values.
- External-sampling request construction.
- Complete canonical chance-edge enumeration.
- Direct sampled public chance without materializing all edges.
- Canonical flop combinations and one-card turn/river outcomes.
- Root-owned street transitions.
- Fold/showdown settlement using the established terminal layers.

### 7.10 Continuation leaves and bounded rollouts

`MultiwayFixedContinuationPolicy` provides allocation-free continuation policies:

- Blueprint policy.
- Fold-biased policy.
- Call-biased policy.
- Raise-biased policy.

It can transform a blueprint menu and evaluate action values under the selected
continuation policy.

`multiway_rollout_leaf.hpp` provides a fixed-state, caller-scratch rollout
boundary. It supports:

- Bounded betting-action traversal.
- Exact all-in runout enumeration when the runout space is within the cap.
- Common-random-number seeded sampling for larger runout spaces.
- Reusable fixed terminal scratch.
- Evaluation of all four continuation policies under one seed batch.
- Complete, capped-fallback, and invalid-context status.

This is a library callback surface. The caller supplies the action provider,
input provider, seeds, and per-concurrent-caller scratch.

### 7.11 Blueprint training and root export

`MultiwayBlueprintTrainingSession` composes rules, blueprint config, bucket
registry, action abstraction, CFR config, solver limits, root snapshot, and an
optional leaf evaluator.

`MultiwayBlueprintTrainer` supports:

- Deterministic trajectory batches.
- Linear iteration weighting.
- Optional regret discounting.
- Optional negative-regret pruning with warmup and interval controls.
- Late-window average accumulation.
- Current, weighted-average, and late-window root policy export.
- Resume validation against model identity, schedule hash, and deterministic
  seed.

`MultiwayBlueprintSnapshot` is intentionally root-only. It stores quantized
root action probabilities, model identity, public state/infoset/bucket ids,
trajectory count, policy kind, and training metadata. It does not export dense
global rows, reach tables, or worker scratch.

### 7.12 Checkpoints, manifests, and audit records

`MultiwayRootPolicyArtifact` provides atomic snapshot save/load and resume-identity
validation.

`MultiwayBlueprintArtifacts` adds the release artifact boundary:

- Atomic checkpoint write.
- `.manifest` sidecar with schema, model identity, and snapshot hash.
- Verified load against expected identity and recomputed snapshot hash.
- Primary plus known-good fallback loading.
- Artifact source tracking.

`MultiwayPublicDecisionLog` is a public audit record containing model identity,
public state id, actor, sampled public action, resolver status, fallback state,
and quantized policy. It deliberately excludes cards, ranges, raw seeds, and
worker state.

`MultiwayProtectedReplayRecord` binds public hand history and decision seeds to
model identity and an integrity hash. It is intended for protected storage,
not public logging.

### 7.13 Multiway candidate evaluation

`evaluate_multiway_candidates(...)` is a callback-driven evaluation harness.
It runs duplicate deals under cyclic seat rotations and can produce:

- Cross-play cells.
- Reduced-game NashConv.
- Local best-response reports.
- Off-tree action-policy gauntlet reports.
- Timing and resident-memory metrics.
- Normalization, leaf-variance, pruning, and worker-imbalance metrics.
- Stable failure fixtures for callback rejection and invalid policy/output
  cases.

The evaluator owns scheduling and statistical aggregation. Candidate policy
objects and actual match/best-response/off-tree logic remain callback-owned.

## 8. Current multiway resolver behavior

`MultiwayResolver` is implemented as an in-process, deadline-safe request
boundary with validation and fallback handling. It currently does the
following:

1. Validates public state, board, hero cards, actor, legal state, ranges, and
   identity metadata.
2. Reconstructs the legal action menu from the public betting snapshot.
3. Inserts an exact observed action if the supplied menu contains one that is
   legal but outside the configured abstraction menu.
4. Looks up the hero bucket when a matching bucket registry is available.
5. Uses fallback priority of compatible latest stable root, compatible blueprint
   policy, and static legal policy.
6. Normalizes the result and samples an action from the normalized policy.
7. Uses an optional host-owned stable-root cache for later fallback; request
   cards and ranges are not retained by the resolver.
8. Enforces the configured deadline reserve and reports status and diagnostics.

`ReleaseDefault` delivers a live root external-sampling search only when the
request is eligible and the resolver has verified root and full-blueprint
artifacts, matching buckets, a valid leaf evaluator, and bounded search
limits. It otherwise uses the stable-root, blueprint, and static-legal
fallback chain. `FallbackOnly` uses that fallback chain without search; the
former perturbation loop has been removed.

## 9. Utility and performance infrastructure

The `util/` module contains:

- Abstraction archive loading and canonical bucket lookup.
- Infoset registries and flat infoset row layout.
- Checked numeric arithmetic and bounded iteration helpers.
- Cache-line aligned allocators.
- PCS deterministic pseudo-random sampling, weighted sampling, and Bernoulli
  helpers.
- SIMD kernels and runtime backend detection.
- Suit-isomorphism grouping, public chance canonicalization, hand-index
  permutation, and flat-node collapse caches.
- Thread join guards for exception-safe worker cleanup.
- Environment-controlled profiling with scoped timers, aggregate counters,
  console output, and report-file output.

Suit-isomorphism support exists for flat graph/chance utilities. The sampled
HUNL configuration does not expose a public-chance-isomorphism compatibility
flag. The stable structured range path retains complete chance expansion.

## 10. Examples and executable surfaces

The CMake example set includes:

- `examples/solve_kuhn.cpp`: minimal Kuhn solve.
- `examples/benchmarks/main.cpp`: general benchmark entry point.
- `examples/benchmarks/flat_scheduler_main.cpp`: flat scheduler benchmark.
- `examples/benchmarks/hunl_backend_compare_main.cpp`: recursive/flat backend
  comparison.

These examples demonstrate library consumption and benchmarking. They are not
the deployment host or a poker client integration.

## 11. Release profile mapping

`docs/multiway_release_config.json` defines the deployment profile and maps it
to:

- `MultiwayGameRules`.
- `MultiwayBlueprintConfig`.
- `MultiwayMemoryBudget`.
- `MultiwayResolverConfig`.

The runbook requires the host to enforce:

- 48 GiB memory warning.
- 56 GiB resident operating cap.
- 60 GiB host hard cap.
- 20,000 ms external deadline.
- 15,000 ms internal resolver deadline.
- 100 ms resolver deadline reserve.
- 32 trajectories per resolver batch and 64 maximum batches.
- Verified primary/known-good checkpoint pairs.
- Matching bucket registry and model identity.
- Static legal-policy fallback for valid requests.
- No sampled action for invalid requests.
- Public decision logs without hero cards or opponent ranges.
- Protected replay records for integrity-bound seeds and public history.

The host must package the known-good checkpoint, manifest, bucket registry,
release profile, and static legal policy together. Artifact promotion is a host
release operation after validation and evaluation.

## 12. Important implementation boundaries for future agents

1. **Legacy HUNL and sampled structured HUNL are separate contracts.** Do not
   infer that fields on `HUNLConfig` automatically enable range solving through
   `solve_hunl_postflop(...)`.
2. **Depth-limited solving is callback-owned.** A depth cutoff requires the
   typed leaf evaluator with explicit units and model provenance.
3. **Multiway JSON is host configuration.** The library exposes C++ mapping
   types but does not parse `multiway_release_config.json`.
4. **Multiway deployment exports are root-only.** The production artifact does
   not contain a dense global strategy or worker state.
5. **Model identity is part of every multiway artifact boundary.** Rules,
   action abstraction, bucket model, terminal model, resolver schema, and code
   schema all contribute to identity.
6. **The baseline multiway bucket model is deterministic feature hashing.** It
   is not a trained clustering or neural bucket model.
7. **The multiway resolver owns the release traversal path.** One-shot resolve
   requests use the external-sampling runner, while runtime sessions retain
   beliefs, reroot state, and the configured continuation selector.
8. **Public chance isomorphism is not active in sampled HUNL.** The existing
   suit-isomorphism utilities belong to the older flat/public graph support.
9. **The intended production interface remains structured game state in and
   strategy/diagnostics out.** No poker client automation, screen scraping, or
   account/session integration exists in the project architecture.

## 13. Primary source map

| Area | Main public files |
| --- | --- |
| Build and facade | `CMakeLists.txt`, `include/core/lib.hpp`, `cmake/TexasSolverConfig.cmake.in` |
| Generic games and DCFR | `include/core/game.hpp`, `include/games/kuhn.hpp`, `include/games/leduc.hpp`, `include/solver/dcfr.hpp`, `include/solver/parallel_dcfr.hpp` |
| HUNL state | `include/games/hunl.hpp`, `include/games/hunl_solver.hpp`, `src/games/hunl.cpp` |
| HUNL exact/flat | `include/games/hunl_tree.hpp`, `include/games/hunl_flat_graph.hpp`, `include/solver/hunl_flat_state.hpp`, `include/solver/hunl_flat_dcfr.hpp` |
| HUNL sampled | `include/solver/hunl_sampled_solver.hpp`, `include/solver/hunl_sampled_range.hpp`, `include/solver/hunl_sampled_traversal.hpp`, `src/solver/hunl_sampled_range.cpp` |
| HUNL abstraction | `include/util/abstraction.hpp`, `include/solver/hunl_bucket_map.hpp` |
| Preflop | `include/preflop/preflop.hpp`, `include/preflop/preflop_equity.hpp`, `include/preflop/preflop_rvr.hpp` |
| Multiway game | `include/games/multiway_rules.hpp`, `include/games/multiway_state.hpp`, `include/games/multiway_fixed.hpp`, `include/games/multiway_terminal.hpp` |
| Multiway private/showdown | `include/games/multiway_private.hpp` |
| Multiway traversal/storage | `include/solver/multiway_solver.hpp`, `include/solver/multiway_traversal.hpp`, `include/solver/multiway_terminal_adapter.hpp` |
| Multiway abstraction/buckets | `include/solver/multiway_action_abstraction.hpp`, `include/solver/multiway_bucket_model.hpp`, `include/solver/multiway_bucket_artifact.hpp` |
| Multiway training/artifacts | `include/solver/multiway_blueprint_trainer.hpp`, `include/solver/multiway_export.hpp`, `include/solver/multiway_checkpoint.hpp`, `include/solver/multiway_artifact.hpp` |
| Multiway resolver/evaluation | `include/solver/multiway_resolver.hpp`, `include/solver/multiway_evaluation.hpp` |
| Release contract | `docs/multiway_release_config.json`, `docs/multiway_release_runbook.md` |
