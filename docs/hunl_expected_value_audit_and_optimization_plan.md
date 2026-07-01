# HUNL Expected Value Audit And Optimization Plan

## Summary

The benchmark result:

```text
hunl.bench.total           ~20.56 s
hunl.bench.expected_value  ~20.55 s
```

shows that the wallclock problem is not the flat CFR solve itself. The dominant cost is post-solve expected value evaluation.

The first important audit finding is:

- The benchmark path is **not** using the HUNL-specific `expected_value(...)` in `src/solver/exploit.cpp`.
- It is using the **generic templated recursive evaluator** in [`include/solver/solver.hpp`](C:/Users/xeuse/RiderProjects/TexasSolver/include/solver/solver.hpp).

That explains why the expected-value detail profiler under `src/solver/exploit.cpp` reported no timers.

## Real Hot Path

The benchmark calls:

- [`examples/benchmarks/hunl_random_flat_main.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/examples/benchmarks/hunl_random_flat_main.cpp)
  - `core::detail::expected_value(core::HUNLState::initial(shared), strategy)`

This resolves to:

- [`include/solver/solver.hpp`](C:/Users/xeuse/RiderProjects/TexasSolver/include/solver/solver.hpp)
  - `template <class G> std::array<Value, 2> expected_value(const G&, const StrategyMap&)`

For `G = HUNLState`, this evaluator recursively does all of the following at nearly every node:

1. `state.legal_actions()`
2. `state.infoset_key(player)`
3. `strategy.find(key)` in `unordered_map<string, vector<double>>`
4. `state.next_state(action)`
5. at chance nodes, `state.chance_outcomes()`

For HUNL, each of those operations is materially expensive.

## Why It Is Slow

### 1. `infoset_key()` is string-heavy

[`src/games/hunl.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/games/hunl.cpp)

`HUNLState::infoset_key()` builds a fresh string every time. That path:

- sorts board cards
- formats cards into strings
- formats betting history
- concatenates several string segments

This is acceptable during solver construction or debugging, but very expensive inside a full recursive expected-value traversal.

### 2. `legal_actions()` allocates vectors repeatedly

[`src/games/hunl.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/games/hunl.cpp)

`HUNLState::legal_actions()` returns a new `std::vector<ActionId>` every call.

Inside expected value, this happens on essentially every decision node.

### 3. `chance_outcomes()` allocates vectors repeatedly

[`src/games/hunl.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/games/hunl.cpp)

At chance nodes the evaluator asks for `chance_outcomes()`, which:

- builds a `remaining` vector
- builds an output vector of outcomes

That is very expensive in a recursive evaluator.

### 4. `next_state()` copies the entire `HUNLState`

[`src/games/hunl.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/games/hunl.cpp)

`next_state()` delegates to `apply()`, which returns a copied `HUNLState`.

That means expected value pays for:

- state copy
- board/history vector mutation
- control-flow logic for terminal/street transitions

on every traversed edge.

### 5. Strategy lookup is map + string based

[`include/solver/solver.hpp`](C:/Users/xeuse/RiderProjects/TexasSolver/include/solver/solver.hpp)

`strategy_for_infoset(...)` does:

- `unordered_map<InfosetKey, vector<Probability>>::find`
- keyed by a newly built string

So expected value currently pays both:

- key construction cost
- hash lookup cost

per decision node.

### 6. Recursion revisits the same semantic patterns with no flattening

The generic evaluator is elegant, but for HUNL it is the wrong abstraction for performance:

- it is state-object oriented
- not flat-node oriented
- not infoset-id oriented
- not cache friendly

The flat solver already invested heavily in precomputed graph structure, but expected value is ignoring that and rebuilding dynamic game mechanics while traversing.

## Main Conclusion

The current HUNL expected value implementation is slow because it evaluates a flat-solver result through the **generic dynamic game API**, instead of through a **flat graph / flat node evaluator**.

That mismatch is the core design problem.

## Recommended Fix Strategy

## Phase A: Measure The Real Generic Path

Before optimizing, profiling must move to the actual evaluator:

Files:

- [`include/solver/solver.hpp`](C:/Users/xeuse/RiderProjects/TexasSolver/include/solver/solver.hpp)
- possibly [`src/games/hunl.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/games/hunl.cpp)

Add direct timing around these operations in the templated `expected_value(...)` path:

- total evaluator time
- terminal utility
- chance node processing
- decision node processing
- `legal_actions()`
- `infoset_key()`
- strategy lookup
- `next_state()`
- `chance_outcomes()`

Because the function is templated, this likely needs either:

1. HUNL-only profiling helpers guarded by `if constexpr (std::is_same_v<G, HUNLState>)`, or
2. a dedicated HUNL expected-value specialization.

This is the first fix because the previous instrumentation landed in the wrong implementation.

## Phase B: Stop Using The Generic Evaluator For Flat HUNL Benchmarks

This is the highest-value change.

For flat HUNL solves, expected value should use a dedicated evaluator built on:

- `HUNLFlatSolveGraph`
- flat node metadata
- exported average strategy

Instead of:

- `HUNLState`
- `legal_actions()`
- `infoset_key()`
- `next_state()`

### Target shape

Add a new HUNL-flat evaluator, for example:

- `include/solver/hunl_flat_expected_value.hpp`
- `src/solver/hunl_flat_expected_value.cpp`

Possible interface:

```cpp
std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy);
```

Better long-term interface:

```cpp
double compute_flat_expected_value_p0(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatInfosetTable& infoset_table_or_export_view,
    const FlatExpectedValueStrategyView& strategy);
```

### Why this will help

A flat evaluator can reuse:

- precomputed child lists
- precomputed chance structure
- precomputed terminal values
- precomputed infoset ids / ranges

and it can avoid:

- string key construction per node
- `HUNLState` copying per edge
- repeated legal-action generation
- repeated chance-outcome generation

## + Phase C: Add A Strategy Export/View That Avoids String Lookup

Even a flat evaluator should not rely on:

- `unordered_map<string, vector<double>>`

for its hot path.

Instead, add an export/view indexed by infoset id:

Example shape:

```cpp
struct HUNLFlatAverageStrategyView {
    std::vector<const double*> rows_by_infoset;
    HUNLFlatValueLayout layout;
};
```

or:

```cpp
struct HUNLFlatAverageStrategyTable {
    HUNLAlignedVector<double> values;
    std::vector<HUNLFlatInfosetTableMeta> meta;
    HUNLFlatValueLayout layout;
};
```

Then expected value can do:

- `infoset_id -> strategy row`

instead of:

- `infoset_key string -> hash map -> vector`

This is likely one of the biggest wins.

## Phase D: Cache Terminal Values Once

For flat expected value:

- fold terminal utility is static
- showdown utility for explicit-hole benchmarking can often be cached per terminal pattern or direct leaf

At minimum:

- precompute terminal node values once for the dealt benchmark state
- avoid repeated `state.utility()` style leaf recomputation

If expected value is evaluated on a single known initial hole-card assignment, terminal evaluation should not need dynamic state reconstruction.

## Phase E: Add Specialized Fast Paths For Benchmark Modes

The benchmark in `hunl_random_flat_main.cpp` always uses:

- a fully specified `HUNLConfig`
- known initial hole cards
- one root state
- one exported strategy

That is much simpler than the general exploit/evaluation code path.

Add a benchmark-only or flat-only evaluator that assumes:

- no missing hole cards
- no generic state cloning
- direct graph traversal
- direct strategy row access

This is a good place to be aggressively practical.

## Concrete Optimization Backlog

### Priority 1: Correct profiling target

Edit:

- [`include/solver/solver.hpp`](C:/Users/xeuse/RiderProjects/TexasSolver/include/solver/solver.hpp)

Goal:

- instrument the actual generic `expected_value(...)` used by the benchmark

Success criteria:

- profiler shows non-empty `hunl.eval.expected_value.*` rows

### Priority 2: Add flat expected value evaluator

Add:

- `include/solver/hunl_flat_expected_value.hpp`
- `src/solver/hunl_flat_expected_value.cpp`

Goal:

- evaluate the flat solver result through `HUNLFlatSolveGraph`, not `HUNLState`

Success criteria:

- benchmark `expected_value_seconds` drops dramatically
- same game value within tight tolerance

### Priority 3: Remove string-key lookup from hot path

Edit:

- [`src/solver/hunl_flat_dcfr.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/solver/hunl_flat_dcfr.cpp)
- flat expected value files
- maybe `HUNLFlatDCFR` export path

Goal:

- expose average strategy by infoset id / contiguous row

Success criteria:

- expected value no longer builds or hashes infoset strings in inner loops

### Priority 4: Optional dynamic-path cleanup

If the generic evaluator must remain important for other games:

Edit:

- [`include/solver/solver.hpp`](C:/Users/xeuse/RiderProjects/TexasSolver/include/solver/solver.hpp)
- [`src/games/hunl.cpp`](C:/Users/xeuse/RiderProjects/TexasSolver/src/games/hunl.cpp)

Possible improvements:

- avoid allocating fallback vectors for common action counts
- add small fixed-size action containers
- add cached infoset key material
- reduce `next_state()` copying where possible

But these should be secondary after the flat evaluator exists.

## Specific Code Smells To Address

### `strategy_for_infoset(...)`

Current problem:

- allocates fallback buffer
- hashes a string key

Fix:

- replace with direct indexed access for flat HUNL

### `state.infoset_key(player)`

Current problem:

- repeated string creation in hot path

Fix:

- do not call it inside flat expected value
- if generic path is kept, consider cached/interned keys

### `state.legal_actions()`

Current problem:

- allocates vector every node

Fix:

- in flat expected value, use node metadata child/action counts directly

### `state.chance_outcomes()`

Current problem:

- allocates and rebuilds outcomes

Fix:

- in flat expected value, use precomputed graph chance edges

### `state.next_state(action)`

Current problem:

- copies dynamic state object at every edge

Fix:

- in flat expected value, traverse node indices and static edges instead

## What I Expect The Real Detail Profile To Show

Once profiling is moved to the actual generic evaluator, I expect the dominant buckets to be:

1. `apply` / `next_state`
2. `infoset_key`
3. `legal_actions`
4. `strategy_lookup`

with chance and terminal logic smaller unless the tree is very chance-heavy.

If that prediction is wrong, the profile will still tell us exactly where to go next. But the current audit strongly suggests that the expensive part is not arithmetic, it is dynamic game-object reconstruction.

## Recommended Implementation Order

1. Instrument the real generic `expected_value(...)` in `solver.hpp`
2. Add flat HUNL expected value evaluator
3. Switch `hunl_random_flat_main.cpp` to use the flat evaluator for flat-solver benchmarks
4. Add row/id-based strategy view to avoid string lookups
5. Re-profile
6. Only then optimize remaining leaf/equity details if they still matter

## Bottom Line

The expected-value bottleneck is not a small bug. It is an architectural mismatch:

- **flat solver output**
- being evaluated by a **generic state-recursive string-keyed engine**

The correct fix is to move HUNL flat expected value onto the same flat graph / infoset-indexed data model as the solver itself.
