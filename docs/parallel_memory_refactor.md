# Parallel CFR Memory / Data Layout Refactor Plan

## Why we should do this

Our benchmark and built-in profiling show that the current parallel CFR design is not losing because of thread creation, merge barriers, snapshot building, or batch construction. Those costs are small compared to the time spent inside worker-local CFR traversal.

What the measurements tell us:

- Parallel traversal is slower than sequential traversal.
- `merge`, `snapshot`, and frontier construction are all relatively small.
- Per-worker CFR time dominates the threaded run.
- More workers make traversal worse, not better.

That points to a memory-layout problem rather than a scheduling problem.

Today the hot path still depends heavily on:

- `std::unordered_map<InfosetKey, InfosetAccum>`
- `InfosetKey = std::string`
- repeated `state.infoset_key(player)` construction
- per-worker duplication of large local infoset tables
- lots of hash lookups, dynamic allocations, and string traffic

This is bad for parallel scaling because:

1. Threads stop being compute-bound and become memory-bound.
2. Each worker builds its own hash tables, so memory traffic scales up with thread count.
3. String hashing and allocation destroy cache locality.
4. Worker-local duplication increases allocator pressure and cache misses.
5. The merge is not the main problem; the local table construction is.

So if we want parallel CFR to scale, we need to refactor the hot path toward compact numeric identifiers and cache-friendly storage.

## Refactor goal

The goal is not just "use less memory." The goal is:

- no string creation in the hot recursive CFR path
- no string hashing in the hot recursive CFR path
- fewer heap allocations during traversal
- better cache locality for regrets and strategy sums
- predictable worker-local writes
- cheaper merge behavior

The long-term target should be:

- stable numeric infoset IDs
- dense or semi-dense per-infoset storage
- compact action-aligned regret / strategy buffers
- string keys only at API boundaries, debugging, or final export

## Step-by-step plan

### Step 1. Add a split between hot-path IDs and external string keys

Short description:
Introduce an internal numeric infoset identifier and stop treating the string key as the main identity in the solver core.

Substeps:

- Add an `InfosetId` type such as `std::uint32_t` or `std::uint64_t`.
- Keep `InfosetKey` as the external string type for output and compatibility.
- Introduce a mapping layer:
  - string key -> numeric ID
  - numeric ID -> string key for final export only
- Do not remove string keys from public outputs yet.

Expected benefit:

- lets us migrate the solver internals without breaking the library API immediately

### Step 2. Add infoset interning / registration

Short description:
Intern each unique infoset once, then reuse the numeric ID everywhere inside solving.

Substeps:

- Build an `InfosetRegistry` component.
- On first encounter of a string infoset key, register it and assign an `InfosetId`.
- Store action count and any static metadata with the registry entry.
- Return the existing ID on future lookups.
- Make the registry usable by both sequential and parallel solvers.

Expected benefit:

- string allocation and hashing move from "every hot lookup" to "first encounter only"

### Step 3. Stop constructing string keys in the recursive solver path

Short description:
Change the CFR core so it requests an infoset ID, not a string.

Substeps:

- Add a hot-path lookup function on game states or a helper layer:
  - current behavior: `state.infoset_key(player)`
  - target behavior: `lookup_infoset_id(state, player, registry)`
- For the first version, this helper may still internally build the string and intern it.
- After that works, move to a direct structural encoding path for HUNL so no string is built at all.

Expected benefit:

- isolates the expensive string path and makes the next optimization steps possible

### Step 4. Replace `unordered_map<string, InfosetAccum>` with `unordered_map<InfosetId, InfosetAccum>`

Short description:
Keep the map shape first, but eliminate string keys from the hot tables.

Substeps:

- Change:
  - global canonical infosets
  - worker-local accumulators
  - strategy snapshots
  - locked strategy tables
- Store `InfosetId` keys instead of `std::string`.
- Keep the string conversion only for final `average_strategy` export.

Expected benefit:

- immediate reduction in hash cost, key storage cost, and key comparison cost

### Step 5. Flatten `InfosetAccum`

Short description:
Move from small vectors per infoset toward contiguous action-aligned storage.

Substeps:

- Today each infoset owns:
  - `std::vector<double> regret_sum`
  - `std::vector<double> strategy_sum`
- Replace that with a flatter representation:
  - offset into a shared buffer
  - action count
  - contiguous regret buffer
  - contiguous strategy buffer
- Keep a metadata table per infoset:
  - `offset`
  - `action_count`

Expected benefit:

- fewer allocations
- denser memory access
- better cache locality during updates and merges

### Step 6. Introduce a worker-local arena / slab allocator

Short description:
Avoid repeated heap churn while workers create local accumulators.

Substeps:

- Add a worker-local arena for temporary infoset storage.
- Use monotonic allocation for:
  - metadata records
  - regret / strategy buffers
- Reset the arena once per traversal phase instead of freeing many small objects.

Expected benefit:

- much lower allocator overhead
- more predictable memory layout

### Step 7. Separate static infoset metadata from dynamic values

Short description:
Do not duplicate invariant infoset information in every worker table.

Substeps:

- Store once globally:
  - action count
  - optional export key
  - optional game-specific metadata
- Store per worker only:
  - regret values
  - strategy-sum values
  - touched flag or local presence marker

Expected benefit:

- less worker-local duplication
- smaller per-worker footprint

### Step 8. Add touched-set / sparse activation tracking

Short description:
Workers should only materialize data for infosets they actually touch.

Substeps:

- Track touched infoset IDs in a compact vector per worker.
- When an infoset is first touched in a phase:
  - allocate or activate its local slot
  - append its ID to the touched list
- At merge time, iterate only the touched list.

Expected benefit:

- cheaper merge
- cheaper iteration over local state
- less dependence on full hash-table scans

### Step 9. Move from hash maps toward indexed tables where possible

Short description:
Once infoset IDs are stable, prefer indexed access over hash lookups.

Substeps:

- If the ID space is dense enough:
  - use `std::vector`-backed tables indexed by `InfosetId`
- If fully dense is too large:
  - use segmented arrays or paged vectors
- Keep sparse worker touched-lists so inactive slots are not scanned unnecessarily.

Expected benefit:

- much cheaper lookup than hash maps
- much better prefetch/cache behavior

### Step 10. Add a direct HUNL infoset encoder

Short description:
HUNL is the most important case for parallel performance, so it deserves a direct structural encoding path.

Substeps:

- Replace string assembly in the hot HUNL path with a compact encoded key structure.
- Encode only the fields needed for infoset identity:
  - player hole abstraction or raw hole representation
  - board abstraction or board representation
  - street
  - betting history encoding
- Intern that compact form directly into `InfosetId`.

Expected benefit:

- removes string formatting from the most expensive game path

### Step 11. Refactor strategy snapshots to numeric-ID storage

Short description:
After the ID migration, snapshots should no longer be `unordered_map<string, vector<double>>`.

Substeps:

- Build snapshots keyed by `InfosetId`.
- If static action metadata is global, snapshot values can become:
  - contiguous probability buffers
  - indexed by infoset offset
- Keep worker reads immutable and shared, as we already improved earlier.

Expected benefit:

- lower snapshot memory cost
- lower lookup overhead inside worker traversal

### Step 12. Export strings only at the end

Short description:
String materialization should be a final serialization step, not a solver data structure.

Substeps:

- At output time, walk final infoset IDs and map them back to string keys.
- Build:
  - `SolveOutput.average_strategy`
  - HUNL wrapper maps
- Do not use string keys during traversal, merge, or snapshot lookup.

Expected benefit:

- preserves API compatibility without paying string cost in the hot path

## Recommended implementation order

If we want the safest staged rollout, use this order:

1. `InfosetId` type and registry
2. ID-based hot-path lookup helper
3. `unordered_map<InfosetId, InfosetAccum>`
4. touched-lists for merge
5. flattened `InfosetAccum`
6. worker-local arena allocation
7. indexed tables instead of hash maps
8. direct HUNL infoset encoding

That order gives us measurable wins early without requiring a huge one-shot rewrite.

## What to measure after each step

- sequential traversal time
- parallel traversal time
- merge time
- snapshot time
- worker-local infoset count
- allocations if available
- speedup at `2`, `4`, and `8` workers

The most important rule is:

- do not wait for the full refactor before measuring

We should re-run the benchmark and built-in profile after each major step.

## Expected outcome

If this refactor works, the profile should change like this:

- worker-local CFR time drops substantially
- merge time stays small
- snapshot time stays small or gets smaller
- total traversal time decreases with `2` or `4` workers instead of increasing

If that does not happen after removing string keys and flattening storage, then the next suspect is game-state cloning / tree traversal cost rather than infoset storage.

## Bottom line

At this point the parallel solver is not blocked by scheduling. It is blocked by memory behavior.

The current design asks every worker to build and update large hash tables keyed by strings. That is exactly the kind of workload that scales poorly with more threads. The right next step is a memory/data-layout refactor centered on numeric infoset IDs, flatter storage, fewer allocations, and string export only at the edges.
