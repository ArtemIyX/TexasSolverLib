# Flop RTA Memory Optimization Plan

## Goal

Fit practical heads-up no-limit postflop solving, including flop subgames, inside a 64 GB desktop memory budget with enough iteration speed for RTA-style use.

This is not a request to make a full 100 BB, full-range, dense-action flop tree exact and cheap. The practical target is:

- HU only.
- Flop-rooted subgames with range inputs.
- Small, explicit action menus.
- Bucketed private-hand abstraction.
- Public chance compression by suit isomorphism or sampling.
- Optional depth-limited leaf values.
- No full dynamic HUNL object recursion during iterations.

## Executive Recommendation

The current solver has already implemented several CPU hot-path improvements from the previous flat CFR audit:

- normalized bucket reach is precomputed once per iteration;
- backward decision uses dot-product kernels;
- per-node allocation in backward is mostly gone;
- local bucket mass is separated from global bucket reach;
- per-depth backward scheduling is cost-aware;
- average strategy can be exported as a flat table instead of a string map.

The next bottleneck for flop is memory representation, not just arithmetic. The highest-impact plan is:

1. Replace full-size per-worker scratch arrays with depth-slice scratch.
2. Remove duplicate graph storage and per-node `std::vector` board storage.
3. Do not build and keep both `HUNLTree` and `HUNLFlatSolveGraph` for large runs.
4. Do not build per-showdown-node dense bucket matrices.
5. Use compact numeric ids for infosets; keep strings only for debug/export.
6. Use `float`/compressed storage for current strategy and average strategy; keep regret in `float` or mixed precision after validation.
7. Add public chance isomorphism for turn/river runouts.
8. Add a sampled mode: PCS/MCCFR or external-sampling MCCFR for flop when exact public chance expansion exceeds memory.
9. Add an RTA preset that constrains actions and buckets by street.

If we do only one code milestone first, do per-worker scratch slimming. It is the easiest large memory win because the active solver path only uses `player0_reach`, `player1_reach`, `chance_reach`, `bucket_reach`, row scratch, and local bucket mass from `HUNLFlatWorkerScratch`; `terminal_values`, `node_values`, and `action_values` are still allocated per worker but are not used by the current DCFR stages.

## Sources Reviewed

Project files:

- `README.md`
- `docs/hunl_flat_cfr_optimization_plan.md`
- `docs/hunl_expected_value_audit_and_optimization_plan.md`
- `docs/depth_limited_solving.md`
- `include/games/hunl.hpp`
- `src/games/hunl_tree.cpp`
- `include/games/hunl_tree.hpp`
- `src/games/hunl_flat_graph.cpp`
- `include/games/hunl_flat_graph.hpp`
- `src/solver/hunl_flat_dcfr.cpp`
- `include/solver/hunl_flat_dcfr.hpp`
- `src/solver/hunl_flat_state.cpp`
- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_bucket_map.cpp`
- `include/solver/hunl_bucket_map.hpp`
- `src/solver/hunl_bucket_terminal.cpp`
- `include/solver/hunl_bucket_terminal.hpp`
- `src/util/abstraction.cpp`
- `include/util/abstraction.hpp`
- `tests/test_hunl_solver_storage.cpp`
- `tests/test_hunl_flat_state.cpp`

External references:

- Real-Time Parallel Counterfactual Regret Minimization, Li and Huang, 2026: https://arxiv.org/abs/2605.19928
- Solving Imperfect-Information Games via Discounted Regret Minimization, Brown and Sandholm, 2018: https://arxiv.org/abs/1809.04040
- Solving Large Imperfect Information Games Using CFR+, Tammelin, 2014: https://arxiv.org/abs/1407.5042
- Safe and Nested Subgame Solving for Imperfect-Information Games, Brown and Sandholm, 2017: https://arxiv.org/abs/1705.02955
- Depth-Limited Solving for Imperfect-Information Games, Brown, Sandholm, Amos, 2018: https://arxiv.org/abs/1805.08195
- DeepStack, Moravcik et al., 2017: https://arxiv.org/abs/1701.01724
- Deep CFR, Brown et al., 2018: https://arxiv.org/abs/1811.00164
- VR-MCCFR, Schmid et al., 2018: https://arxiv.org/abs/1809.03057
- Stochastic Regret Minimization in Extensive-Form Games, Farina, Kroer, Sandholm, 2020: https://arxiv.org/abs/2002.08493
- Imperfect recall abstraction memory tradeoff, Lanctot et al., 2012: https://arxiv.org/abs/1205.0622
- OpenSpiel algorithms, including CFR and external-sampling MCCFR references: https://github.com/google-deepmind/open_spiel/tree/master/open_spiel/algorithms
- `amaster97/poker_solver`, the upstream-style Rust/Python solver this project references: https://github.com/amaster97/poker_solver
- `b-inary/postflop-solver`, efficient Rust postflop solver: https://github.com/b-inary/postflop-solver
- PokerStove evaluator/enumeration library: https://github.com/andrewprock/pokerstove

## Current Memory Model

Use these symbols:

- `N`: flat node count.
- `E`: child edge count.
- `I`: infoset count.
- `B`: total bucket slots across infosets.
- `V`: total regret/strategy values, roughly `sum_infoset(bucket_count * action_count)`.
- `W`: worker count.

Current global solver arrays:

- `player0_reach_`, `player1_reach_`, `chance_reach_`: `3 * N * 8`.
- `terminal_values_`, `node_values_`: `2 * N * 8`.
- `action_values_`: `E * 8`.
- `bucket_reach_`, `normalized_bucket_reach_`: `2 * B * 8`.
- `infoset_bucket_totals_`: `I * 8`.
- infoset table: `regret_sum_`, `strategy_sum_`, `current_strategy_`: `3 * V * 8`.

Current per-worker scratch:

- `terminal_values`, `node_values`: `2 * N * 8`.
- `action_values`: `E * 8`.
- `player0_reach`, `player1_reach`, `chance_reach`: `3 * N * 8`.
- `bucket_reach`: `B * 8`.
- small row/local buffers.

Approximate solver-array memory:

```text
global:      (5N + E + 2B + I + 3V) * 8 bytes
per worker:  (5N + E + B) * 8 bytes
total:       global + W * per_worker + graph/tree/metadata/allocator overhead
```

This model is enough to explain the 64 GB flop failure. For large `N` and `E`, every extra worker multiplies full graph-sized scratch. For large bucket counts and action counts, `3V * 8` dominates. The tree/graph vectors and terminal tables add nontrivial overhead before solving starts.

## Audit Findings

### 1. Per-worker scratch overallocates full graph arrays

`HUNLFlatWorkerScratch` has `terminal_values`, `node_values`, and `action_values`. The current solver path only uses scratch reach arrays, bucket reach, row scratch, and local bucket mass in `src/solver/hunl_flat_dcfr.cpp`.

Recommendation:

- Delete scratch `terminal_values`, `node_values`, and `action_values`.
- Change tests to assert only required scratch.
- Long term: make reach scratch depth-local, not graph-global.

Expected memory win:

```text
W * (2N + E) * 8 bytes
```

With 16 workers and 100 million nodes plus 200 million edges, this alone is tens of GB.

### 2. Reach scratch should be depth-slice local

The reach stage only needs to accumulate child reach for the next depth and bucket reach touched by current decision nodes. It does not need `3 * N` scratch arrays per worker.

Recommendation:

- Replace per-worker `player0_reach`, `player1_reach`, `chance_reach` with depth-slice buffers keyed by `depth_order` offset.
- For each depth, allocate max next-depth slice width per worker, or share one arena partitioned by worker.
- Keep an index map only if children are not contiguous in the next depth slice.
- Keep deterministic reduction order.

Expected memory:

```text
old: W * 3N * 8
new: W * 3 * max_depth_slice_nodes * 8
```

This is likely the largest memory win after removing unused scratch arrays.

### 3. Graph stores each node twice

`HUNLFlatSolveGraph` stores both `nodes` and `node_meta`. Their structs duplicate many fields:

- child/chance/action ranges;
- infoset id;
- contributions;
- terminal utility;
- board vector;
- terminal kind;
- player;
- type;
- street;
- action count.

The solver mostly reads `node_meta`, while some paths still read `nodes[node].board` or terminal values. For large flop trees this is expensive, especially because `std::vector<std::uint8_t> board` is stored per node in both structs.

Recommendation:

- Make `HUNLFlatNodeMeta` the only always-present node table.
- Remove `HUNLFlatNode` from production graphs, or compile it only under debug/export flags.
- Replace per-node `std::vector<uint8_t> board` with compact board encoding:
  - `uint32_t board_mask52_lo` plus `uint32_t board_mask52_hi`, or
  - `uint64_t card_mask`, one bit per card id, or
  - `uint8_t board_cards[5]` plus `uint8_t board_count`.
- Store board once per public state/chance node where possible; decision nodes can reference a `board_id`.

Expected memory win:

- removes one full node table;
- avoids millions of tiny board vector allocations;
- improves cache locality.

### 4. Tree construction keeps expensive dynamic state before flattening

`HUNLTree::build_node()` recursively builds `HUNLTreeNode` objects, then `HUNLFlatSolveGraph::build()` copies them into flat arrays. During construction, memory can include:

- recursive build state;
- `unordered_map<MemoKey, uint32_t>` with vector/string-heavy keys;
- `HUNLTreeNode` vectors for board, actions, chance outcomes, children, strings;
- flat graph copy.

Recommendation:

- Add `HUNLFlatGraphBuilder` that emits flat nodes directly from `HUNLState`.
- Use an arena for temporary state keys.
- Replace `MemoKey` vector/string contents with a compact canonical key:
  - packed board mask;
  - packed history codes into fixed array;
  - contributions/stacks/to-call;
  - street/player/all-in/fold flags.
- Release build-time memo immediately after graph construction.

For flop, construction peak memory matters as much as steady-state memory.

### 5. Infoset keys are strings in the production graph

`HUNLFlatInfoset` stores `std::string key`. String keys are useful for export, tests, and debugging, but a production RTA solver should not pay for them in hot memory.

Recommendation:

- Store `uint64_t infoset_hash` or compact `HUNLInfosetEncoding` id in production.
- Keep `key` behind `TEXASSOLVER_KEEP_INFOSET_STRINGS` or in an export-only side table.
- Export string keys lazily only when asked.

### 6. Showdown terminal table can explode

`HUNLBucketTerminalTable::build()` creates a dense bucket-by-bucket matrix for every showdown terminal node:

```text
showdown nodes * bucket_count_p0 * bucket_count_p1 * (8 bytes value + 4 bytes count)
```

This is dangerous for flop because there can be many river showdown leaves.

Recommendation:

- Do not build `HUNLBucketTerminalTable` by default for flop.
- Compute showdown values on demand per public board using cached board-level matrices.
- Key terminal matrices by final board and pot/contribution class, not node id.
- Use `float` values and `uint16_t` or `uint32_t` counts.
- Prefer inclusion-exclusion terminal evaluation over pairwise matrices where possible.

`amaster97/poker_solver` explicitly calls out inclusion-exclusion terminal evaluation as an O(N) per-hand optimization. `b-inary/postflop-solver` also uses precision and compression tricks rather than fully dense double storage everywhere.

### 7. Abstraction storage uses string maps

`AbstractionTables` stores board and hand lookup maps as nested `unordered_map<string, ...>`. That is convenient but heavy.

Recommendation:

- Convert loaded abstraction artifacts into compact runtime arrays:
  - canonical board id -> assignment offset;
  - canonical hand colex id -> dense hand index;
  - assignment arrays as `uint8_t` only if bucket count <= 255, else `uint16_t`.
- Keep string maps only in the loader or debug layer.
- Cache `infoset_id -> dense_bucket_ids/weights` compactly.

### 8. Current bucket counts are probably too high for flop RTA

Upstream `amaster97/poker_solver` advertises card abstraction roughly `256/128/64` for flop/turn/river. That can be reasonable for offline solving, but RTA on 64 GB should start smaller.

Recommended RTA presets:

```text
Conservative:
  flop buckets: 64
  turn buckets: 48
  river buckets: 32
  flop bets: [33, 75]
  turn bets: [50, 100]
  river bets: [75]
  raises: [3x]
  postflop raise cap: 1
  include all-in: false except SPR <= 2.5

Balanced:
  flop buckets: 96
  turn buckets: 64
  river buckets: 48
  flop bets: [33, 75, 125]
  turn bets: [50, 100]
  river bets: [50, 100]
  raises: [2.5x or 3x]
  postflop raise cap: 1 or 2 by SPR

Quality/offline:
  flop buckets: 128 or 192
  turn buckets: 96
  river buckets: 64
  postflop raise cap: 2
  include all-in: true
```

The current defaults in `HUNLConfig` are too broad for flop RTA:

```text
bet_size_fractions = [0.33, 0.75, 1.00, 1.50, 2.00]
postflop_raise_cap = 3
include_all_in = true
```

For flop, every extra action multiplies `V` and tree width. The RTA preset should be explicit and separate from offline defaults.

### 9. Public chance isomorphism is mandatory

`b-inary/postflop-solver` does not use card abstraction, but it combines isomorphic turn/river chances. This is directly relevant: flop subgames explode because they include two future public-card streets.

Recommendation:

- Add a `PublicChanceClass` table for turn and river deals.
- Canonicalize future boards under suit permutations while preserving suit features relevant to current board texture.
- At chance nodes, store class representative, multiplicity, and probability.
- Expand only for export if needed.

This reduces both `N/E` and terminal work.

### 10. Exact full traversal is not always the right flop mode

MCCFR and stochastic regret minimization are designed for games too large for full traversal. OpenSpiel includes external-sampling MCCFR as a standard algorithm reference, and VR-MCCFR shows large variance reductions for sampling variants.

Recommendation:

- Keep current full-tree DCFR for river/turn and small flop.
- Add `HUNLFlatSolveMode::PublicChanceSampled` or `HUNLFlatSamplingMode`.
- First sampled mode:
  - public chance sampling over turn/river boards;
  - full action traversal at visited public states;
  - deterministic seed control;
  - maintain regret/strategy tables only for reachable sampled public states or for a fixed abstract state table.
- Second sampled mode:
  - external-sampling MCCFR for private/chance branches;
  - optional VR baselines for leaf values.

This trades variance and more iterations for a much smaller live tree.

### 11. Depth limiting needs real leaf values

The repo has `depth_limit_plies` and heuristic leaves. The user observed that depth limiting does not help enough. That is expected if the solver still builds too much tree before the cutoff or if leaf values are too weak to allow shallow cutoffs.

Recommendation:

- Make depth limiting street-aware, not only ply-aware:
  - flop solve can cut at turn public chance boundary;
  - turn solve can cut at river public chance boundary;
  - river solves exact.
- Implement multi-valued leaf estimates, not a single scalar heuristic:
  - Brown/Sandholm/Amos depth-limited solving uses opponent continuation choices to make the cutoff robust.
- Practical first version:
  - train or cache a turn/river value table by board bucket, pot size, SPR, range bucket distribution;
  - return bucket-conditioned CFVs rather than one node scalar.

DeepStack and DecisionHoldem-style systems are good precedent: real-time strength comes from continual re-solving plus leaf evaluation, not from exact full-tree expansion.

## 64 GB Budget

Reserve memory roughly like this:

```text
OS and allocator headroom:       8 GB
graph and static metadata:      10 GB max
CFR regrets/current/average:    24 GB max
reach/value working arrays:      8 GB max
terminal/evaluator caches:       6 GB max
construction/export scratch:     4 GB max
emergency headroom:              4 GB
```

Hard budget gates:

- If `3 * V * storage_bytes > 24 GB`, reduce buckets/actions or use compressed/mixed precision.
- If `graph + worker scratch > 18 GB`, enable isomorphism/sampling or refuse exact mode.
- If terminal table estimate > 6 GB, disable dense terminal matrices.
- If construction peak estimate > 56 GB, build flat graph directly or stream public chance classes.

Add a preflight estimator before solving:

```text
nodes, edges, infosets, buckets, values
global bytes
per-worker bytes
graph bytes
terminal table estimate
construction peak estimate
recommended mode
```

This should run before allocating large arrays.

## Implementation Roadmap

### Phase 0: Add memory accounting

Files:

- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `include/solver/hunl_flat_dcfr.hpp`
- `src/solver/hunl_flat_dcfr.cpp`
- benchmark examples

Add:

- `HUNLFlatMemoryEstimate`.
- `estimate_memory(graph, table, worker_count, options)`.
- preflight printing in benchmark tools.
- hard warning if estimate exceeds 48 GB.
- hard fail or automatic sampled mode if estimate exceeds 60 GB.

Acceptance:

- no solver behavior change;
- benchmark prints memory by category.

### Phase 1: Slim worker scratch

Files:

- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- tests for storage/scratch.

Change:

- remove `terminal_values`, `node_values`, `action_values` from `HUNLFlatWorkerScratch`;
- keep only reach scratch, bucket reach, row scratch, local bucket mass, dirty lists;
- update tests.

Acceptance:

- same outputs for existing deterministic tests;
- memory estimate drops by `W * (2N + E) * 8`.

### Phase 2: Depth-local reach scratch

Files:

- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `src/solver/hunl_flat_dcfr.cpp`

Change:

- allocate reach scratch for max next-depth width;
- reduce by depth-slice local offsets;
- maintain a node-index-to-local-offset map only for the next depth slice.

Acceptance:

- worker-count deterministic tests still pass;
- memory estimate drops from `W * 3N * 8` to `W * 3 * max_depth_slice * 8`.

### Phase 3: Compact graph representation

Files:

- `include/games/hunl_flat_graph.hpp`
- `src/games/hunl_flat_graph.cpp`
- tests that inspect graph nodes.

Change:

- delete or debug-gate `HUNLFlatNode`;
- make `HUNLFlatNodeMeta` trivially copyable;
- replace `std::vector<uint8_t> board` with compact board encoding;
- store infoset board via `board_id` or packed board;
- move strings to export/debug side table.

Acceptance:

- graph builds same topology;
- terminal/eval code reads board through compact accessor;
- memory estimate includes graph bytes.

### Phase 4: Direct flat builder

Files:

- new `include/games/hunl_flat_builder.hpp`
- new `src/games/hunl_flat_builder.cpp`
- `src/games/hunl_flat_graph.cpp`

Change:

- build flat graph directly;
- replace recursive `HUNLTree::build_node()` for large runs;
- compact `MemoKey`;
- use explicit stack/work queue instead of recursion.

Acceptance:

- small graphs match old builder exactly;
- construction peak drops.

This is the place to address the user's recursion concern. Recursion itself is less important than the dynamic `HUNLTreeNode` plus `MemoKey` memory, but an iterative direct flat builder solves both.

### Phase 5: Compact precision mode

Files:

- `include/solver/hunl_flat_state.hpp`
- `src/solver/hunl_flat_state.cpp`
- `src/solver/hunl_flat_dcfr.cpp`
- SIMD helpers.

Change:

- add `HUNLFlatStoragePrecision::{Float64, Float32, Compressed16}`.
- start with:
  - regrets: `float`;
  - current strategy: `float`;
  - average strategy: `float` or compressed;
  - reductions/CFV: `double`.
- Optionally store current strategy only for active worker range and recompute if cheaper.

Acceptance:

- compare exploitability/value drift on small games;
- require deterministic mode for `double`;
- enable `float` for RTA preset.

Expected memory:

```text
3V * 8 -> 3V * 4
```

This halves the largest tabular block.

### Phase 6: Terminal evaluation rewrite

Files:

- `src/solver/hunl_bucket_terminal.cpp`
- `include/solver/hunl_bucket_terminal.hpp`
- `src/games/hunl_eval.cpp`

Change:

- no dense per-node showdown matrices by default;
- cache by final board and bucket pair shape;
- implement inclusion-exclusion terminal evaluator for range/bucket values;
- use `float` matrices if matrices remain.

Acceptance:

- terminal cache estimate stays under budget;
- same tiny/range tests pass within tolerance.

### Phase 7: Public chance isomorphism

Files:

- `src/util/suit_iso.cpp`
- `include/util/suit_iso.hpp`
- flat builder / chance node creation.

Change:

- canonicalize turn/river public deals;
- store multiplicity;
- solve representative once.

Acceptance:

- exact mode remains equivalent to expanded mode on small graph;
- flop graph `N/E` drops significantly.

### Phase 8: RTA presets and guardrails

Files:

- `include/games/hunl.hpp`
- `src/games/hunl.cpp`
- examples/benchmarks.

Add:

- `HUNLConfig rta_flop_conservative()`;
- `HUNLConfig rta_flop_balanced()`;
- action menus and bucket counts as listed above;
- automatic all-in inclusion by SPR threshold;
- optional no-donk first flop action if desired for exploitability/performance tradeoff.

Acceptance:

- preset preflight estimate under 48 GB on target machine;
- exact turn/river remain unaffected.

### Phase 9: MCCFR/public-chance sampled mode

Files:

- new solver module, or extension of flat pipeline.

Change:

- public chance sampling first;
- external-sampling MCCFR second;
- optional VR baseline tables.

Acceptance:

- reproducible seeded runs;
- memory no longer scales with all turn/river public branches;
- convergence validated on toy HUNL and Leduc.

## Concrete Settings To Try First

For the first 64 GB RTA target:

```text
workers: 8, not 16, until scratch is depth-local
layout: InfosetActionHand
precision: float strategy/regret if implemented, double otherwise
flop buckets: 64
turn buckets: 48
river buckets: 32
flop bets: [0.33, 0.75]
turn bets: [0.50, 1.00]
river bets: [0.75]
raise sizes: [3.0]
postflop_raise_cap: 1
include_all_in: false unless SPR <= 2.5
depth limit: street boundary with bucket-conditioned leaf values
terminal dense matrices: off for flop
public chance isomorphism: on
```

After Phase 1 and Phase 2, increase workers. Before that, more workers can make memory worse faster than they make time better.

## What Not To Do

- Do not rely on ordinary ply depth limiting with a scalar heuristic and expect flop to become easy.
- Do not keep full `double` regret, strategy sum, and current strategy for large flop RTA unless `V` is proven small.
- Do not build per-node showdown matrices on flop.
- Do not keep string infoset keys in production memory.
- Do not preserve both recursive tree and flat graph for large runs.
- Do not add more threads before fixing per-worker scratch.

## Success Criteria

Memory:

- Conservative flop preset estimates below 40 GB before allocation.
- Actual peak RSS below 56 GB.
- No dense terminal table exceeds 6 GB.
- Worker count from 1 to 8 increases memory sublinearly after depth-local scratch.

Correctness:

- Existing tests pass in default double exact mode.
- Expanded chance and isomorphic chance match on small graphs.
- Float mode value drift is measured and documented.
- Sampled mode converges on Kuhn/Leduc/tiny HUNL before being trusted on flop.

Performance:

- No per-iteration dynamic allocation in hot stages.
- Exact turn/river remain fast.
- Flop RTA preset can complete useful iterations inside the target decision budget.

