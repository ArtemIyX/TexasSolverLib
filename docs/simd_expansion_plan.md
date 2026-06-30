# SIMD Expansion Plan

## Goal

Use SIMD more aggressively in the HUNL flat solver so that we get better single-core throughput and better per-thread throughput before and after multithreading.

The codebase already has a flat-array architecture in the HUNL backend, which is a good fit for SIMD because:

- rows are contiguous
- action counts are small and repeated
- many passes are simple arithmetic over doubles
- several stages are already separated into bulk passes

This document is the step-by-step plan for expanding SIMD use in the places where it can actually help.

## What SIMD should and should not do

SIMD is a good fit when the work is:

- arithmetic-heavy
- branch-light
- repeated over contiguous buffers
- the same operation applied to many rows

SIMD is not the right first tool when the work is:

- dominated by tree traversal and pointer chasing
- heavily branchy
- updating small irregular structures one node at a time
- spent mostly in function call overhead or synchronization

For this project, that means SIMD should focus on the flat solver buffers, not on recursive tree navigation.

## Current state in the repo

We already have:

- `util/simd` with scalar, SSE2, and AVX2 implementations
- flat HUNL regret and strategy arrays
- staged solver passes for discounting, strategy computation, regret updates, and average strategy updates
- aligned vectors for hot buffers

We do **not** yet use SIMD everywhere the flat solver could benefit.

The main opportunity is to push SIMD deeper into the solver stages where the code still uses scalar row loops.

## Success criteria

This work is successful when:

- more hot loops use SIMD-backed helpers or explicit vectorized loops
- the scalar fallback still produces identical results within tolerance
- the solver remains portable on machines without AVX2
- benchmark timings improve on the chosen HUNL postflop workload
- the new SIMD paths are covered by tests

## Step-by-step implementation plan

## Phase 0. Baseline the SIMD opportunities

Purpose:
Find the loops that are worth vectorizing before changing code.

### Step 0.1. Inventory current SIMD usage

Substeps:

- List every call site of `util::discount_regrets`, `util::discount_strategy_sum`, `util::positive_regrets_and_total`, `util::update_regret_sum`, `util::update_strategy_sum`, `util::normalize`, and `util::compute_strategy_row`.
- Identify which solver stages still use hand-written scalar loops instead of these helpers.
- Separate the hot loops into:
  - already vectorized
  - SIMD-ready but still scalar
  - not worth vectorizing yet

Why:

- we want to spend SIMD effort on the loops that repeat many times and touch contiguous memory
- we do not want to vectorize code that is dominated by control flow

Output:

- a short list of SIMD candidates ranked by expected payoff

### Step 0.2. Establish a benchmark path

Substeps:

- Pick one HUNL postflop benchmark tree for timing.
- Record the current wall-clock time and stage timings.
- Record the number of iterations and workers used.

Why:

- SIMD changes are often small individually, so we need a fixed benchmark to see whether each step actually helps

Output:

- one stable timing baseline before any SIMD expansion

## + Phase 1. Vectorize the strategy computation path

Purpose:
Reduce the cost of regret matching, because it runs once per infoset per iteration and touches contiguous rows.

### + Step 1.1. Replace scalar row scans with SIMD-backed row helpers where possible

Substeps:

- In the flat solver strategy stage, route the `InfosetActionHand` layout through SIMD-aware helpers instead of manual scalar scans where the row shape matches a contiguous slice.
- Use the existing `positive_regrets_and_total` plus `normalize` path for the per-row reduction when the memory layout matches the helper’s expected stride.
- Keep the scalar fallback for short rows or awkward tail segments.

Why:

- strategy computation is a repeated row-wise operation
- contiguous per-hand or per-action rows are ideal for SSE2/AVX2

Where:

- `src/solver/hunl_flat_dcfr.cpp` strategy stage
- `src/util/simd.cpp` helper implementations

Output:

- fewer scalar loops in the strategy stage

### + Step 1.2. Add a SIMD-friendly row reduction helper for action-major layouts

Substeps:

- Add a helper that computes:
  - positive-part sum
  - normalized strategy row
  - optional fallback to uniform
- Make it work directly on the flat row layout used by the infoset table.
- Special-case row widths of 2, 3, and 4 actions, because HUNL action sets are often small.

Why:

- many infosets have small fixed action counts
- small row sizes are common enough that branchless SIMD can still help

How:

- build a helper that loads a small block once
- compute positive regrets and total in vector registers
- normalize the row with a single divide pass
- finish any tail elements scalarly

Output:

- a dedicated SIMD row routine for the flat infoset table

### + Step 1.3. Reuse the same row helper in average-strategy export

Substeps:

- When exporting average strategy from the flat table, use the same row-normalization logic for contiguous rows.
- Avoid reimplementing the same “sum then normalize” loop in the export path.

Why:

- export happens after solve, but it still touches large tables and should not duplicate scalar logic

Output:

- one shared row-normalization path for solve-time and export-time use

## + Phase 2. Vectorize regret and strategy update buffers

Purpose:
Turn the post-backprop updates into tight bulk array operations.

### + Step 2.1. Use SIMD update helpers for regret accumulation

Substeps:

- Replace any remaining scalar per-action regret accumulation loops with `update_regret_sum` or a specialized SIMD equivalent.
- Ensure the buffer passed in is contiguous in the same order used by the table layout.
- Keep a scalar tail path for action counts not divisible by the SIMD lane width.

Why:

- regret update is a pure arithmetic loop over a contiguous row
- it is one of the easiest places to gain cheap speedup

Where:

- `src/solver/hunl_flat_dcfr.cpp` regret update stage
- `src/util/simd.cpp` regret update helpers

Output:

- vectorized regret accumulation on the hottest row updates

### Step 2.2. Use SIMD update helpers for strategy-sum accumulation

Substeps:

- Replace scalar strategy-sum accumulation loops with `update_strategy_sum`.
- Keep the same contiguous row layout so the helper can use AVX2/SSE2 naturally.
- Pad or tail-handle short rows as needed.

Why:

- strategy-sum accumulation repeats every iteration and is mathematically simple
- it should be one of the cleanest SIMD wins in the solver

Output:

- vectorized average-strategy accumulation

### Step 2.3. Vectorize discounting over flat tables consistently

Substeps:

- Keep the current discount helpers as the canonical SIMD path for regrets and strategy sums.
- If any stage still discounts rows manually, replace it with the shared helper.
- Make sure discounting is called on contiguous spans, not on scattered slices.

Why:

- discounting is a perfect “same math, many elements” SIMD workload
- it also improves cache friendliness when done over flat arrays

Output:

- all table discounting goes through one SIMD-capable path

## Phase 3. Vectorize terminal and backward passes where the layout allows it

Purpose:
Use SIMD for the parts of the staged backend that still do row arithmetic after the tree traversal.

### Step 3.1. Vectorize terminal value filling for dense terminal buffers

Substeps:

- If terminal values are copied or initialized in contiguous chunks, use bulk fill operations and, where useful, SIMD-friendly memory writes.
- Keep the terminal logic separate from the evaluator itself.

Why:

- terminal-stage work is mostly memory movement and initialization
- dense contiguous writes are cheaper than many scalar assignments

Output:

- a cheaper terminal buffer setup stage

### Step 3.2. Vectorize action-value propagation for contiguous child spans

Substeps:

- Where child action values are copied from node values into contiguous spans, use chunked loads and stores.
- Use SIMD only where the child span is long enough to make it worthwhile.

Why:

- action-value arrays are flat and indexed by child span
- copying and combining child values is often bandwidth-bound and benefits from bulk operations

Output:

- faster action-value staging and less scalar copy overhead

### Step 3.3. Vectorize node-value reductions when action count is small but repeated

Substeps:

- Add a small reduction helper for combining a node’s action values into a node value.
- Special-case common action counts so the compiler can emit tight code.
- Use SIMD when the row width and alignment make it worthwhile, otherwise keep scalar code.

Why:

- node-value reduction is repeated everywhere in backward passes
- many nodes have small but consistent action counts

Output:

- faster backward value computation for repeated row reductions

## Phase 4. Improve hand-row layout for SIMD efficiency

Purpose:
Make sure the data layout helps SIMD instead of fighting it.

### Step 4.1. Favor row-contiguous access for the hottest stage

Substeps:

- Keep the current flat infoset table layout as the baseline.
- Measure whether `InfosetActionHand` or `InfosetHandAction` is better for the SIMD-heavy stage mix.
- If one layout makes the common loops more contiguous, prefer it even if the other is slightly simpler elsewhere.

Why:

- SIMD speed depends heavily on how often we can load contiguous lanes

Output:

- a layout choice based on SIMD and cache behavior, not just code convenience

### Step 4.2. Add alignment-aware tail handling

Substeps:

- Align buffers to cache-friendly boundaries.
- Add a fast path for rows that are naturally aligned and wide enough.
- Add a clean scalar tail path for the remainder of each row.

Why:

- aligned loads are easier for the compiler and the CPU to optimize
- tails are inevitable, but they should not dominate the hot path

Output:

- predictable fast paths plus safe scalar cleanup

### Step 4.3. Reduce layout conversions between stages

Substeps:

- Avoid reshaping the same data between different row orders more than once.
- Keep the chosen layout stable from table build through stage updates and export.

Why:

- copying or reindexing data can erase the gains from SIMD

Output:

- fewer layout-conversion overheads

## Phase 5. Add SIMD-aware tuning for small action counts

Purpose:
Make sure the solver benefits even when rows are short.

### Step 5.1. Add explicit fast paths for tiny rows

Substeps:

- Add specialized code paths for action counts of 2, 3, and 4.
- Use the smallest vector width that matches the machine and the row size.
- Only fall back to the generic vector loop when the row is longer.

Why:

- HUNL action rows are often short
- generic vector loops can be underwhelming on tiny rows if we do not specialize

Output:

- better SIMD utilization on common HUNL action counts

### Step 5.2. Keep scalar fallbacks as first-class paths

Substeps:

- Preserve scalar behavior for portability and correctness.
- Make sure the scalar path is exercised by tests, not just compiled.

Why:

- not every machine has AVX2
- we need reliable behavior on all supported hardware

Output:

- portable SIMD with a trustworthy fallback

### Step 5.3. Consider a compiler-friendly wrapper layer

Substeps:

- If the code becomes awkward with direct intrinsics, add small wrapper helpers for row operations.
- Keep the wrapper names descriptive and keep the actual SIMD code in one place.

Why:

- this makes it easier to maintain the vectorized paths as the solver evolves

Output:

- SIMD code that is easier to extend without scattering intrinsics everywhere

## Phase 6. Add tests for the new SIMD usage

Purpose:
Verify that the SIMD paths are correct, selected as expected, and do not regress the solver.

### Step 6.1. Add unit tests for SIMD helpers

Substeps:

- Test scalar vs SSE2 vs AVX2 helper outputs on the same inputs.
- Cover:
  - discounting
  - positive-regret extraction
  - normalization
  - regret update
  - strategy update
- Include edge cases:
  - zero-length rows
  - rows with all non-positive regrets
  - rows with NaN or infinities if the helper supports them

Why:

- low-level helpers are where SIMD mistakes usually show up first

Output:

- confidence that SIMD math matches scalar math

### Step 6.2. Add solver-stage regression tests

Substeps:

- Verify that the flat solver still produces valid normalized strategies after SIMD-enabled passes.
- Compare the SIMD-enabled backend against the previous scalar behavior on small benchmark trees.
- Check that the stage profiles still complete without throwing.

Why:

- helper correctness is not enough; we also need end-to-end solver stability

Output:

- solver-level proof that the SIMD integration is safe

### Step 6.3. Add benchmark checks for SIMD-sensitive workloads

Substeps:

- Measure the benchmark tree before and after SIMD changes.
- Record whether the optimized path is actually faster.
- If possible, print which backend was selected: scalar, SSE2, or AVX2.

Why:

- SIMD should improve wall time, not just exist in the code

Output:

- benchmark evidence that the new SIMD paths are worthwhile

## Recommended implementation order

This is the safest order:

1. Inventory current SIMD usage
2. Vectorize strategy computation
3. Vectorize regret and strategy-sum updates
4. Vectorize terminal and backward buffer work
5. Tune layout and tail handling
6. Add small-row fast paths
7. Add tests for the new SIMD usage

## Bottom line

The best SIMD wins in this codebase are the ones that operate over:

- flat regret rows
- flat strategy rows
- flat strategy-sum rows
- flat action-value buffers

That lines up well with the current HUNL flat architecture, so we should keep pushing SIMD into the bulk array stages rather than into the tree traversal itself.
