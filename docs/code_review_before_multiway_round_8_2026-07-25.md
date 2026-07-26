# Eighth code review before integrated multiway solving

**Date:** 2026-07-25  
**Reviewed commit:** `b784319`  
**Method:** primary static review plus an independent `code_reviewer` agent pass  
**Verification:** no build, tests, benchmarks, or solver runs were executed, per `AGENTS.md`

## Executive verdict

**NO-GO for integrated multiway traversal.**

Seven rounds were not enough. The current branch contains new P0 correctness
problems and several incomplete contracts. The most serious issues are:

1. structured sampled trajectories reuse one random draw for two supposedly
   independent chance selections;
2. terminal and leaf values can use incompatible units in one regret update;
3. depth-limited leaf requests discard the sampled private-hand identity;
4. compiled multiway range sampling performs an infeasible exact joint-mass
   enumeration;
5. timed structured solving can exceed its deadline;
6. the structured heads-up root still cannot represent a normal live
   facing-bet or off-tree state.

Do not begin multiway traversal by widening the current structured range path.
Fix the shared sampling, value, live-root, deadline, and batch-merge contracts
first.

Severity:

- **P0:** can bias solver updates, mix incomparable values, or block normal
  production inputs.
- **P1:** breaks deterministic, bounded, typed, or production-safe execution.
- **P2:** leaves ambiguous metadata, long-run stability, or missing regression
  gates.

## P0 findings

### P0-1: structured range sampling reuses the private-deal random draw

The deal selector creates `PcsRng deal_rng(seed)` and consumes its first draw.
Traversal then creates a second `PcsRng rng(seed)` from the same seed. Its first
sampled public-chance or opponent-action draw repeats the deal draw.

Evidence:

- private-deal draw: `src/solver/hunl_sampled_range.cpp:336-344`;
- traversal RNG restarted from the same seed:
  `src/solver/hunl_sampled_range.cpp:346-351`;
- reach math multiplies deal, chance, and opponent proposal probabilities as
  if those samples came from the stated product proposal.

Impact:

The actual joint proposal is correlated, but the importance denominator treats
it as independent. MCCFR estimators are biased even though each marginal draw
looks valid and deterministic.

Required fix:

- use one RNG stream and continue after the deal draw, or use explicitly
  domain-separated seeds;
- add a statistical/exhaustive toy regression whose private deal changes the
  next chance distribution;
- compare estimated values and regrets with exact enumeration.

Status: **Fixed.** A trajectory now selects its private deal and all later
chance/opponent actions from one continued `PcsRng` stream, so the recorded
proposal factors match the sampled joint proposal.

### P0-2: structured HUNL and multiway terminal values can use incompatible units

`HUNLState::utility()` always divides by `big_blind`, so terminal values are in
big blinds. `HUNLStructuredRootRequest` defaults to `Chips`. At a depth cutoff,
the traversal requests leaf values in `root.value_units` and compares them
directly with terminal values.

Evidence:

- terminal big-blind conversion: `src/games/hunl.cpp:813-855`;
- structured root defaults to chips:
  `include/games/hunl_solver.hpp:19-25`;
- terminal and leaf values meet in one traversal:
  `src/solver/hunl_sampled_range.cpp:147-171`.

The multiway boundary has the same gap:

- `MultiwayRootSnapshot` accepts four units:
  `include/solver/multiway_solver.hpp:133-146`;
- settlement emits raw chip utilities:
  `src/games/multiway_terminal.cpp:157-160`;
- `MultiwayTerminalAdapter::resolve_terminal()` performs no conversion:
  `src/solver/multiway_terminal_adapter.cpp:223-250`.

Impact:

An action can compare a fold terminal in big blinds with a depth-limited leaf
in chips. Regrets and reported root values then have no valid meaning.
Multiway results can also be labeled as big blinds or normalized fractions
while containing chips.

Required fix:

- define one internal value unit;
- convert terminal and leaf values at one checked boundary;
- carry units in terminal and solve results;
- reject unsupported units until conversion is implemented;
- test branches that mix terminal and cutoff children for every unit.

Status: **Fixed.** Structured HUNL converts its internal big-blind terminal
utilities to the requested chips or big-blinds boundary before comparison with
same-unit leaves. Multiway settlement declares chip utilities; the terminal
adapter converts them once to the root's chips or big-blinds unit and labels the
result. Unsupported normalizations are rejected at root validation.

### P0-3: depth-limited leaf requests lose private-hand identity [Fixed]

Fixed: leaf requests now carry the exact sampled two-player hole-card deal,
while `public_state` remains private-card free. The contract explicitly marks
the values as deal-conditional, and the injected leaf regression verifies the
private deal is valid and available.

At a cutoff, traversal clears `hole_cards` and supplies one scalar reach value
per seat. Every private infoset also uses bucket zero.

Evidence:

- hole cards are removed and scalar vectors are constructed:
  `src/solver/hunl_sampled_range.cpp:157-166`;
- private rows use `bucket_count = 1` and bucket zero:
  `src/solver/hunl_sampled_range.cpp:43-66`;
- current tests only require absent holes and one reach value:
  `tests/test_ranges_solver_integration.cpp:39-52`.

Impact:

A leaf backend cannot distinguish AA from 72o, reconstruct blockers, identify
the sampled bucket, or evaluate the sampled private deal. Deal-dependent leaf
values can collapse to the same request and bias every upstream regret.

Required fix:

- pass exact sampled private cards for trajectory evaluation, or pass stable
  bucket IDs plus complete bucket reach vectors;
- define whether leaf values are deal-conditional or range-conditional;
- compare a deal-sensitive leaf backend with an untruncated exact tree.

### P0-4: compiled multiway range sampling requires infeasible exact enumeration [Fixed]

Fixed: compiled sampling now makes exactly one independent per-seat proposal
per trajectory and discards collisions. Accepted samples retain their direct
product proposal probability; construction no longer computes a global joint
partition function by recursive enumeration.

The round-7 feasibility preflight was fixed, but construction then runs a
second recursive enumeration to compute total compatible joint mass. That
enumeration has another hard-coded one-million-node limit.

Evidence:

- exact compatible-mass recursion and fixed limit:
  `src/games/multiway_private.cpp:50-69`;
- constructor invokes it after the separate feasibility preflight:
  `src/games/multiway_private.cpp:160-205`.

Impact:

Broad two-player ranges already approach or exceed this search size. Three
through six broad ranges are vastly larger. Normal production ranges fail
before any worker trajectory can be sampled. The exposed feasibility result
does not describe this second failure.

Required fix:

- do not compute a global joint partition function by Cartesian recursion;
- use a sequential card-compatible proposal with directly computable proposal
  probability, or make one independent draw per trajectory and discard
  collisions;
- keep chance, proposal, and inclusion reach explicit;
- validate two-through-six-seat proposals against exact small-range
  enumeration.

### P0-5: timed structured solving can exceed its deadline

Status: **Fixed.** Structured range sessions now use deterministic one-trajectory
subbatches with a resumable intra-batch cursor. They check the deadline before
and after every trajectory, at traversal nodes, before and after leaf calls,
before merge, and during root export. Timed calls reserve one millisecond for
root export and return the last fully exported root strategy if time expires.
Leaf requests carry the cooperative deadline, and an expired leaf result is
discarded without merging or advancing its trajectory cursor. The regression
uses a deliberately slow deadline-aware leaf callback and verifies no batch is
published.

The deadline is checked only before a whole minibatch. It is not checked between
trajectories, around leaf calls, after the batch, or during root export.

Evidence:

- only pre-batch check: `src/solver/hunl_sampled_range.cpp:315-324`;
- complete minibatch runs without cancellation:
  `src/solver/hunl_sampled_range.cpp:326-361`;
- root export runs after the loop:
  `src/solver/hunl_sampled_range.cpp:363`;
- export scans every normalized joint deal:
  `src/solver/hunl_sampled_range.cpp:290-307`.

Impact:

A batch or export can finish arbitrarily far beyond the RTA deadline. A
bounded `resume_structured_batches()` call can also exhaust its requested batch
count after the deadline without another loop iteration that sets
`timed_out`. `solve_for()` normally detects the timeout on the next iteration,
but only after the overshooting work has completed.

Required fix:

- use bounded deterministic subbatches;
- check cancellation between trajectories and expensive leaf batches;
- reserve time for root export and fallback action selection;
- check the clock after work before publication;
- add a deliberately slow leaf callback regression.

### P0-6: the structured root cannot represent a live off-tree subgame

Status: **Fixed.** HUNLLiveRootSnapshot is an immutable-by-value structured
resolver boundary carrying the exact public HUNLState, typed legal action menu,
canonical public history, and state version. Admission validates board, stacks,
total contributions, pending call, raise rights, actor, action menu, and
history before rebinding the request configuration. Structured traversal now
attaches each sampled private deal to this validated state instead of
reconstructing the initial configuration. Legacy config-derived roots remain
compatible but still reject unequal contributions. The regression covers a
player-zero live facing-bet root with an inserted 200-chip off-tree bet.

`HUNLStructuredRootRequest` rejects unequal contributions. The range session
always reconstructs `HUNLState::initial(config)`.

Evidence:

- unequal contributions rejected:
  `src/games/hunl_solver.cpp:168-181`;
- state rebuilt from the initial configuration:
  `src/solver/hunl_sampled_range.cpp:234-237`.

Impact:

The production-facing sampled path cannot represent a normal observed bet,
arbitrary acting seat, reopening state, inserted off-tree size, or complete
prior public history. This is the shared resolver foundation that multiway
would otherwise copy.

Required fix:

Replace the config-only root with an immutable full live snapshot containing
exact stacks, total/street contributions, pending and raise rights, acting
seat, canonical public history, typed legal actions, board, ranges, versions,
and value units.

## P1 findings

### P1-1: coordinator admission again accepts untyped and disconnected states [Fixed]

Post-round-7 commit `b784319` weakened two previously fixed contracts:

1. a child with `incoming_edge.kind == None` is silently treated as a betting
   action;
2. any parentless `BoardRunout` state is admitted even when it is not the
   immutable root.

Evidence:

- `None` rewritten as `BettingAction`:
  `src/solver/multiway_solver.cpp:97-112`;
- parentless runout exception:
  `src/solver/multiway_solver.cpp:446-468`;
- tests depend on parentless runout admission:
  `tests/test_multiway_solver.cpp:355-361` and `:461-471`.

Impact:

Typed edge provenance is optional again. A disconnected runout need not
preserve root history, chips, board, or action lineage.

Required fix:

- only the configured root may have `parent_id == 0`;
- every non-root state must carry an explicit typed edge;
- update test fixtures instead of weakening validation to accept omitted edge
  metadata.

Status: **Fixed.** Child transitions now require an explicit typed edge,
including betting actions, and the coordinator admits a parentless state only
when it is its configured immutable root.

### P1-2: multiway worker merge is nontransactional and worker-count dependent [Fixed]

`merge_worker_streams()` validates one stream, then immediately applies its
deltas before inspecting later streams.

Evidence:

- direct validate-and-apply loop:
  `src/solver/multiway_solver.cpp:499-514`;
- row mutation occurs per individual delta:
  `src/solver/multiway_solver.cpp:388-411`.

Impact:

A bad later stream or overflow leaves earlier updates and diagnostics
committed. Applying all worker-zero values before worker-one values also makes
floating-point order depend on trajectory partitioning and worker count.

Required fix:

- globally k-way merge by cell and trajectory ID;
- aggregate and validate the complete batch first;
- validate all resulting central values;
- commit every cell once only after all validation succeeds;
- test identical trajectory sets under several worker partitions.

Status: **Fixed.** The coordinator now validates all bounded streams, performs
a global cell/trajectory ordering, computes and validates every pending cell,
then commits each cell once. Duplicate trajectory updates to the same cell are
rejected before any row or diagnostic mutation.

### P1-3: root construction does not establish joint private-range feasibility [Fixed]

`MultiwayRootSnapshot::validate()` performs only independent marginal
validation.

Evidence:

- root calls `private_ranges.validate()` but not feasibility or compilation:
  `src/solver/multiway_solver.cpp:243-290`;
- incompatible ranges fail later in `MultiwayCompiledPrivateRanges`.

Impact:

An immutable solve request can be published with no compatible joint private
deal. Failure moves from request admission into later solver preparation.

Required fix:

Bind a successful structured feasibility/proposal compilation result to solve
request construction. Preserve distinct infeasible, search-exhausted, and
proposal-normalization statuses.

Status: **Fixed.** `MultiwaySolveRequest` now retains the successful
feasibility result and immutable compiled private-range proposal. Infeasible
and search-exhausted feasibility statuses are rejected during request
construction before a coordinator can be created.

### P1-4: terminal adapter inputs are not proven descendants or sampler outputs [Fixed]

The adapter checks monotonic chips, folds, all-ins, street order, and board
prefix. It does not replay coordinator-admitted action/chance edges. It also
accepts any card-compatible public `MultiwayJointPrivateSample`; it does not
prove positive mass in the root ranges or successful sampler provenance.

Evidence:

- partial lineage checks:
  `src/solver/multiway_terminal_adapter.cpp:61-103`;
- private validation checks only card compatibility:
  `src/solver/multiway_terminal_adapter.cpp:41-59`.

Impact:

Fabricated monotonic betting states, abstraction-external bet sizes, direct
street jumps, and out-of-range private deals can reach settlement.

Required fix:

Resolve chance, transitions, and terminals from an admitted public-state ID
plus a sampler-issued immutable deal token. Do not accept unrelated raw
snapshots and publicly constructible samples at this boundary.

Status: **Fixed.** `MultiwayTerminalAdapter` is now bound to a coordinator.
Its public chance, transition, and terminal APIs accept only coordinator-admitted
public-state IDs and opaque deal tokens issued from the request's compiled
private-range sampler. Raw snapshots and publicly constructible samples are no
longer accepted at this boundary.

### P1-5: structured range execution ignores workers and mutates central storage during traversal

Status: **Fixed.** The coordinator now performs a deterministic admission pass
for each fixed-size trajectory subbatch using the same RNG stream as execution.
It creates rows before worker launch; recursive workers only use immutable
lookup and storage views, accumulate local delta streams, and merge once in
global trajectory order. The workers setting partitions the subbatch
deterministically. Leaf callbacks are serialized pending the dedicated batched
leaf boundary. The regression compares one and two workers over the same four
trajectory IDs and verifies identical sparse rows.

The structured positive path is serial. `config.workers` is unused.
`PrivateInfosetCoordinator::ensure()` performs hash lookup, allocation, row
admission, and central storage mutation from recursive traversal.

Evidence:

- central row admission: `src/solver/hunl_sampled_range.cpp:37-72`;
- traversal calls admission: `src/solver/hunl_sampled_range.cpp:188-191`;
- serial trajectory loop: `src/solver/hunl_sampled_range.cpp:315-358`.

Impact:

This path does not satisfy immutable batch snapshots, coordinator-only
admission, worker-local deltas, or deterministic multithreaded scheduling.

Required fix:

Prepare/admit rows at a coordinator boundary, freeze lookup views for a batch,
run worker-local trajectories, then globally merge in fixed trajectory order.

### P1-6: structured-session memory guardrails omit major allocations

Status: **Fixed.** Structured preflight now estimates joint-deal retention,
private-infoset lookup storage, session retention, bounded worker scratch, and
root export space. The live session tracks and checks its own retained and
transient budget before deal normalization, lookup growth, export, and worker
scratch allocation. Solver observed peak memory retains the structured
preflight peak for one-shot sessions. The regression verifies the new estimate
fields and rejects a one-byte direct-session cap before range construction.

The session retains a normalized joint-deal vector and an unbounded private
infoset hash map. These are absent from memory estimates. The normalized
heads-up range also reserves the complete 1,326 by 1,326 deal capacity.

Evidence:

- retained deals and infoset map:
  `src/solver/hunl_sampled_range.cpp:247-276`;
- unconditional joint-deal reserve: `src/games/hunl.cpp:435`;
- solver memory estimate omits session-owned containers:
  `src/solver/hunl_sampled_solver.cpp:548-590`.

Impact:

A request can pass the 60 GB guardrail while session-owned hashes, deal tables,
leaf objects, and scratch allocations exceed it.

Required fix:

Include every retained and peak allocation in preflight and observed memory.
Enforce the same byte budget before deal, infoset-map, row, and scratch growth.

### P1-7: the leaf callback is not batched or provenance-checked

Status: **Fixed.** The coordinator now dry-runs each deterministic trajectory
after admission, collects all cutoff requests for the bounded subbatch, and
calls the leaf backend once before immutable worker traversal consumes the
ordered results. Results must acknowledge every request and match units,
scope, blueprint/model versions, finite two-player shape, and the zero-sum
utility convention. Deadline checks bracket the batched backend call; a
deadline or callback failure publishes no partial batch. Regressions cover a
multi-request callback plus provenance and conservation rejection.

Traversal calls the batch callback with `count == 1` from recursion and
allocates/copies state and vectors for every cutoff. It checks units and
finiteness only.

Evidence:

- scalar callback call: `src/solver/hunl_sampled_range.cpp:153-170`;
- leaf contract includes model and abstraction metadata:
  `include/solver/hunl_leaf_evaluator.hpp`.

Impact:

The intended batched backend cannot amortize inference. A result with the wrong
model/abstraction provenance or conservation convention can enter regrets.

Required fix:

Collect deterministic cutoff requests across trajectories, call the backend in
real batches, and validate units, versions, result count, shape, and the chosen
utility-conservation convention.

### P1-8: rejection work is not bounded by the decision deadline [Fixed]

`max_rejection_attempts` accepts any nonzero `uint32_t`. One trajectory may
perform all attempts without cancellation.

Evidence:

- unbounded configuration domain:
  `include/games/multiway_private.hpp:31-35`;
- rejection loop: `src/games/multiway_private.cpp:218-264`.

Impact:

`UINT32_MAX` no longer wraps, but it can still block for billions of proposals.

Required fix:

Use an admitted work budget, cancellation checks, or one proposal attempt per
trajectory.

Status: **Fixed.** Compiled private-range sampling makes one independent
per-seat proposal per trajectory and discards collisions. It no longer retries
up to `max_rejection_attempts`.

### P1-9: the target cash-game contract has no rake semantics

Status: **Fixed.** An immutable typed rake policy now belongs to game, root,
showdown, and terminal inputs. The default is an explicit validated zero-rake
policy. Percentage rake is floor-rounded, capped, and applied exactly once to
the combined contested-pot total, never to uncalled-bet refunds; it honors
no-flop-no-drop and is removed from ordered pots deterministically before
payout splitting. Terminal results expose the rake, so chip conservation is
`payouts + refunds + rake == contributions` and utility conservation is
`sum(utilities) == -rake`. The root terminal-model identity includes the rake
policy. Regressions cover cap, no-drop, explicit zero, validation, identity,
and conservation.

The playbook requires exact rake before strategy training, but multiway game,
root, terminal, and result contracts contain no rake policy, cap, or
no-flop-no-drop metadata.

Evidence:

- requirement: `docs/6max_nlhe_poker_ai_30_day_playbook.md:6-18` and `:52-62`;
- state contract: `include/games/multiway_state.hpp:29-63`;
- terminal contract: `include/games/multiway_terminal.hpp:11-44`;
- root contract: `include/solver/multiway_solver.hpp:131-147`.

Impact:

A raked game is silently solved and settled as rake-free. Even a zero-rake
event is not explicitly frozen in the versioned root contract.

Required fix:

Add a typed rake policy and apply it exactly once in terminal settlement.
Require explicit zero rake when disabled. Include rake in game/model identity
and conservation tests.

### P1-10: current multiway state and terminal APIs are not production hot kernels

Status: **Fixed.** `multiway_fixed` now provides a separate six-seat bounded
state view, fixed action menu, cold vector-to-fixed conversions, reusable
worker terminal scratch, and an allocation-free side-pot settlement kernel.
It keeps the vector state/terminal APIs unchanged as correctness oracles and
does not yet alter traversal ownership. Fixed state action/application and
raked side-pot settlement regressions compare the kernel directly with those
oracles.

`MultiwayState` owns multiple vectors, copies them in `apply()`, and allocates a
new action vector at each decision. Terminal settlement allocates vectors for
levels, contributors, eligible seats, winners, pots, payouts, and utilities.

Evidence:

- allocating action API and copied state:
  `src/games/multiway_state.cpp:305-425`;
- terminal allocations:
  `src/games/multiway_terminal.cpp:30-160`.

Impact:

Using these APIs directly inside sampled traversal violates the repository's
hot-path rules and will make six-seat trajectory throughput allocation-bound.

Required fix:

Keep these components as correctness oracles. Add fixed-capacity
`std::array<..., 6>` state views, stack/fixed action menus, preallocated worker
scratch, and an allocation-free terminal kernel for production traversal.

### P1-11: float row storage silently loses updates and disagrees with the public numerical helper

Status: **Fixed.** Coordinator-owned sparse regrets and average-strategy sums
now use Float64 storage. The deterministic global merge order and direct row
mutation both reject any finite nonzero delta that does not change its Float64
accumulator, matching the public numerical helper instead of silently losing
mass. The precision-boundary regression exercises the actual worker-stream
merge at `2^53`, verifies transactional rejection of `+1`, and accepts the
next representable `+2` update.

`apply_multiway_cfr_update()` now throws when a nonzero double delta is absorbed
by a large double accumulator. The actual sparse storage uses floats and casts
each update without checking whether it changed the stored value.

Evidence:

- double no-op rejection:
  `src/solver/multiway_cfr.cpp:286-307`;
- float sparse mutation:
  `src/solver/multiway_solver.cpp:388-411`.

Impact:

Tests of the helper do not protect production storage. Large regrets or
strategy sums can silently stop changing long before float overflow.

Required fix:

Choose and document a long-run numerical policy: double storage, compensated
accumulation, periodic deterministic rescaling/discounting, or checked float
updates. Test the actual sparse merge path at precision boundaries.

### P1-12: structured root export collapses private hands into one action mix

Status: **Fixed.** Structured roots now carry a selected acting hero hand and
explicit bucket. Root strategy export reads only that private infoset rather
than averaging compatible opponent deals. Legacy one-combo player-zero ranges
remain unambiguous; multi-hand hero ranges reject missing selection. Optional
range-wide aggregation is exposed separately as a diagnostic and never
replaces the selected-hand root strategy. Regression coverage verifies the
multi-hand rejection, selected export, and separate diagnostic.

Root export averages every compatible joint deal into one
`HUNLSampledRootStrategy`. The request does not select the hero's known hand or
root bucket.

Evidence:

- deal-weighted aggregation:
  `src/solver/hunl_sampled_range.cpp:290-307`;
- result contains one action vector:
  `include/solver/hunl_sampled_solver.hpp:27-34`.

Impact:

An RTA caller can receive a range-averaged policy instead of the policy for its
actual private hand unless it manually reduces the hero range to one combo.
The API does not require or document that condition.

Required fix:

Make the selected hero private state/bucket explicit and export its policy.
Keep optional range-wide diagnostics separate.

### P1-13: moving a solver with a retained structured session leaves internal references behind [Fixed]

`HUNLSampledRangeSession::Impl` stores references to a solver's `storage` and
`profile`. `HUNLSampledSolver` does not delete or customize move operations and
contains a movable `unique_ptr` session.

Evidence:

- retained references: `src/solver/hunl_sampled_range.cpp:247-255`;
- session member: `include/solver/hunl_sampled_solver.hpp:148`;
- no explicit solver move/copy lifetime contract:
  `include/solver/hunl_sampled_solver.hpp:82-149`.

Impact:

An implicitly moved solver can retain a session referencing the moved-from
object's members. Resumption can mutate stale objects or dangle after the old
solver is destroyed.

Required fix:

Delete solver moves while a self-referential session design exists, or
implement a custom move that rebuilds/rebinds the session safely. Add
compile-time and runtime lifetime tests.

Status: **Fixed.** `HUNLSampledSolver` is non-copyable and non-movable while
its retained session holds references to its storage and profile.

## P2 findings

### P2-1: action descriptors do not encode exact targets consistently

Validation forwards `target_street_contribution` to `apply()`, but fold, check,
call, and all-in ignore it. Tests encode all-in with target zero.

Evidence:

- validation and replay: `src/solver/multiway_solver.cpp:208-220`;
- action application: `src/games/multiway_state.cpp:327-425`;
- zero-target all-in fixture: `tests/test_multiway_solver.cpp:27-33`.

Required fix:

Use an optional target only for actions where it is absent, or require the
exact resulting street contribution for every descriptor.

Status: **Fixed.** Every public action descriptor now records the acting
seat's exact resulting street contribution. Admission replays the action and
rejects descriptors whose recorded target does not equal the replayed result.

### P2-2: structured root metadata validation is incomplete

`HUNLStructuredRootRequest::validate()` does not reject unknown
`HUNLLeafValueUnits` values and allows an empty `model_version` when a depth
limit is active.

Evidence:

- validation: `src/games/hunl_solver.cpp:168-181`;
- enum and metadata contract:
  `include/solver/hunl_leaf_evaluator.hpp`.

Required fix:

Validate enum boundaries and require model identity whenever leaf values can
affect the solve.

Status: **Fixed.** Structured-root validation rejects unknown value-unit enum
values and requires a non-empty leaf model version whenever a depth limit can
invoke leaf evaluation.

### P2-3: retained structured batch IDs can wrap and replay trajectories

The retained batch cursor is `uint32_t`, incremented without a checked bound.

Evidence:

- cursor storage and increment:
  `src/solver/hunl_sampled_range.cpp:258` and `:360`;
- public cursor type: `src/solver/hunl_sampled_range.cpp:388-389`.

Required fix:

Use checked `uint64_t` batch and global trajectory counters.

Status: **Fixed.** The retained batch cursor and reported completed-batch
count are `uint64_t`; trajectory IDs use checked `uint64_t` arithmetic and
reject exhaustion before a trajectory or seed can replay.

### P2-4: preflop-to-flop chance creates six equivalent public board orders

The terminal adapter advances board chance one card at a time and preserves
insertion order. There is no decision between the three flop cards and no
canonical sorting/collapse.

Evidence:

- one-card chance edge generation:
  `src/solver/multiway_terminal_adapter.cpp:111-160`;
- public-state board equality is order-sensitive:
  `src/solver/multiway_solver.cpp:74-87`.

Impact:

Each physical flop can be admitted in up to six permutations, multiplying
public states and permitting distinct infoset identities for equivalent poker
states.

Required fix:

Emit canonical three-card flop combinations, or canonicalize the completed
flop before public-state identity and compensate chance multiplicity exactly.

Status: **Fixed.** Preflop-to-flop chance now emits one edge per sorted
three-card combination with exact combination probability. The typed parent
edge carries the full dealt-card sequence, and admission verifies that
canonical sequence against the successor board.

### P2-5: required regression gates

Status: **Component gates fixed.** Focused regression coverage now includes
trajectory-seeded sampling and a hand-computed sampled update; terminal unit
normalization and leaf-unit/provenance rejection; two-through-six-seat private
proposal sampling; batch and root-export deadline preservation; off-tree live
facing-bet roots; deterministic worker-count merge equivalence and late-cell
transactional rejection; admitted-state/deal-token provenance; raked terminal
conservation with chips-to-big-blinds conversion; structured session memory
accounting; Float64 precision-boundary rejection; and movable structured
session lifetime. The fixed-capacity terminal oracle is compared for two- and
three-seat raked toys.

The sole remaining delivery blocker is an integrated multiway traversal
runner. The repository still has no execution path that connects the
multiway request, private sampler, public-state coordinator, terminal adapter,
worker streams, and NashConv diagnostics. Therefore an exhaustive three-player
toy traversal and end-to-end NashConv comparison cannot be supplied without
implementing that new traversal; this status does not claim that such an
integrated path exists.

## Round-7 status

The four round-7 P0 findings are stale as written:

- **P0-1:** request, coordinator, sparse storage, and typed result structures
  now exist, but traversal and safe execution contracts remain incomplete.
- **P0-2:** samples expose chance, conditional, proposal, and inclusion reaches,
  but the exact normalization implementation cannot scale.
- **P0-3:** positive structured batches now execute, but they have the P0 RNG,
  unit, leaf, deadline, and live-root defects above.
- **P0-4:** `HUNLFlatMCCFR` now uses active worker delta rows. Keep it as an
  oracle/prototype; do not widen it into the multiway production path.

Round-7 P1-5 remains not fully closed:

- terminal lineage still accepts independently supplied snapshots and deals;
- commit `b784319` made typed betting edges optional and admitted disconnected
  parentless runouts.

The round-7 value-unit enum and feasibility-status fixes remain useful, but
they validate only enum/status boundaries. They do not solve value conversion
or scalable proposal construction.

## Required delivery gate

Before integrated multiway traversal:

1. fix RNG domain separation, value units, and leaf private-state identity;
2. replace exact joint-mass enumeration with a scalable proposal contract;
3. implement a complete immutable live-root snapshot for the structured
   heads-up reference path;
4. make timed batches, root export, memory admission, and failure handling
   bounded and transactional;
5. restore mandatory typed parent edges and root-connected state admission;
6. implement globally ordered transactional multiway merge;
7. freeze rake and terminal unit semantics;
8. separate allocation-free production state/terminal kernels from oracle
   APIs;
9. pass exact two-player and three-player toy-game gates before extending to
   four through six seats.

The correct conclusion is not that seven reviews are enough. The current
branch still has multiple solver-bias and production-boundary defects.
