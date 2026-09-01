# Multiway blueprint training scaling audit

## Scope and evidence

This is a post-activation code audit of the multiway blueprint training path.
The original baseline is retained below for historical comparison. Current
verification is recorded in the update section.

### Current verification update

The implementation and benchmark have since been updated. The current Release
qualification command was run with three repeats, 16,000 timed trajectories per
sample, identical seeds, and pinned logical processors. The median throughput was:

| Workers | Median trajectories/s |
|---:|---:|
| 1 | 24,908.1 |
| 2 | 32,232.4 |
| 4 | 60,022.1 |
| 8 | 113,225.8 |
| 16 | 151,607.0 |

The strict ordering gate passed for this run. All fingerprints were identical.
Samples now also report coordinator lock-wait nanoseconds separately from the
worker-completion barrier.
The full Debug build and CTest workflow also passed. This is interim evidence;
the audit remains incomplete until every finding and its focused test are closed.

Verified implementation commits include shared admission/read locking,
bounded telemetry, right-sized worker streams, deterministic k-way merges,
continuation indexed lookup and merge, inline private-hole sampling, fixed
terminal settlement, fused regret maintenance, atomic completion, bucket-table
caching, successor reuse, cancellation polling amortization, controlled worker
affinity, round-robin scheduling, reusable worker action-menu scratch,
bounded pending-cell scratch, targeted worker wakeups, and in-place checkpoint
canonicalization. Full Release rebuild and benchmark both passed after the
latest S3 change.

The target is repeatable throughput ordering on the Ryzen 9 9950X3D host:

```text
16 workers > 8 workers > 4 workers > 2 workers > 1 worker
```

This ordering is a qualification target, not an assumption. If the production
workload saturates memory bandwidth or a serial phase, the measured optimum may
remain below 16 until that bottleneck is removed.

## Verdict

The worker pool is persistent and its dispatch cost is bounded. Scaling is
blocked primarily by shared mutable coordinator state in traversal. Every
decision takes the same coordinator mutex for row admission and usually for
strategy lookup. Child admission, pruning lookups, and visit counters also take
that mutex. Existing-row and existing-state calls still repeat validation and
linear lookup while holding it.

The next largest risks are heap allocation while building every recursive child,
serial sort-and-merge work, unreserved compact-storage growth, continuation-row
locking, false sharing in worker scratch, and a benchmark fixture too small to
attribute production behavior.

## Findings

### S1. One coordinator mutex serializes the recursive hot path

Locations:

- `src/solver/multiway/engine/multiway_traversal.cpp:401`
- `src/solver/multiway/engine/multiway_traversal.cpp:427`
- `src/solver/multiway/engine/multiway_traversal.cpp:487`
- `src/solver/multiway/engine/multiway_traversal.cpp:491`
- `src/solver/multiway/engine/multiway_solver.cpp:674`
- `src/solver/multiway/engine/multiway_solver.cpp:700`
- `src/solver/multiway/engine/multiway_solver.cpp:750`

For each visited decision, `admit_infoset_row()` locks `traversal_mutex_`. A
missing blueprint lookup then calls `regret_matched_strategy_into()` and locks it
again. Each evaluated child locks for `admit_public_state()`. Terminal and leaf
visits lock the same mutex. Pruning adds one locked regret lookup per action.

At eight or sixteen workers, useful computation is increasingly replaced by
mutex ownership transfer and cache-line migration. This is the first blocker to
remove.

### S2. Existing objects repeat linear search and validation under the lock

Locations:

- `src/solver/multiway/engine/multiway_solver.cpp:674-695`
- `src/solver/multiway/engine/multiway_solver.cpp:700-742`
- `src/solver/multiway/engine/multiway_solver.cpp:988-994`
- `src/solver/multiway/engine/multiway_compact_storage.cpp:27-30`

`admit_public_state()` validates the complete descriptor and linearly scans
`public_states_` before discovering that a state already exists.
`admit_infoset_row()` performs two binary row lookups, a linear public-state
lookup, betting-state reconstruction, full shape validation, and counter updates
on every visit. The common steady-state path therefore pays admission costs even
when no mutation is required.

### S3. Recursive traversal allocates and copies dynamic public data

Locations:

- `src/solver/multiway/engine/multiway_traversal.cpp:468-487`
- `src/solver/multiway/engine/multiway_traversal.cpp:607-621`
- `src/solver/multiway/abstraction/multiway_action_abstraction.cpp:275-303`
- `src/solver/multiway/abstraction/multiway_public_builder.cpp:257-408`

Action generation returns `std::vector`. Public child construction copies or
grows board, history, legal-action, and betting vectors. This occurs per visited
edge even when the same public state was already admitted by another trajectory.
Allocator contention and memory traffic grow with workers and violate the
repository's hot-path rules.

### S4. Compact row admission can allocate and shift rows inside the lock

Locations:

- `src/solver/multiway/engine/multiway_compact_storage.cpp:33-57`
- `src/solver/multiway/engine/multiway_solver.cpp:657-669`

The coordinator reserves merge buffers and public states, but compact storage
does not reserve `rows_`, `regrets_`, or `strategy_mass_` from its admitted
limits. New rows resize both value arrays and insert into a sorted row vector.
Growth can allocate while holding `traversal_mutex_`; sorted insertion can move
all following metadata. Initial graph expansion consequently has especially
poor scaling.

### S5. Merge is a serial `O(D log D)` barrier

Locations:

- `src/solver/multiway/engine/multiway_traversal.cpp:881-892`
- `src/solver/multiway/engine/multiway_solver.cpp:783-867`
- `src/solver/multiway/continuation/multiway_continuation_selector.cpp:171-190`

After all workers stop, the coordinator copies every regret delta, globally sorts
it, fingerprints it, and applies deltas serially. Continuation deltas allocate a
new pointer vector, sort it, then take the selector mutex once per delta through
`update_regrets_weighted()`. Increasing workers cannot accelerate this phase and
usually increases total streams and cache misses.

### S6. Continuation lookup has a second global mutex and linear row search

Locations:

- `src/solver/multiway/continuation/multiway_continuation_selector.cpp:75-102`
- `src/solver/multiway/engine/multiway_traversal.cpp:311-320`

Every continuation leaf calls `strategy()`, which locks the selector and linearly
searches all rows. This is independent of `traversal_mutex_` but becomes another
serial fraction when continuation training is enabled.

### S7. Worker scratch is vulnerable to false sharing

Locations:

- `include/solver/multiway/engine/multiway_traversal.hpp:177-198`
- `src/solver/multiway/engine/multiway_traversal.cpp:704-713`

`WorkerScratch` objects are contiguous. Workers repeatedly update adjacent
`attempted`, `accepted`, `discarded`, timing, stream-size, and profile fields.
There is no cache-line alignment or padding. Adjacent workers can invalidate the
same cache lines even though ownership is logically local.

### S8. Current timing cannot measure mutex contention

Locations:

- `src/solver/multiway/engine/multiway_traversal.cpp:836-892`
- `include/solver/multiway/engine/multiway_traversal.hpp:134-143`

`coordinator_wait_nanoseconds` is coordinator wall time waiting for all workers,
not time workers waited for coordinator locks. `worker_active_nanoseconds` is a
sum, and `maximum_worker_active_nanoseconds` is the batch critical path. Existing
profile stages combine object construction, validation, lookup, and lock wait.
The report cannot distinguish mutex contention from allocation, traversal,
imbalance, or memory bandwidth.

### S9. The qualification fixture is not representative of F1

Location: `examples/benchmarks/multiway_training_scaling_main.cpp:30-211`

The fixture is three-player, river-only, one bucket, deterministic leaf, and
maximum decision depth one. Its storage limits are tiny. It is useful for a
repeatable regression signal, but it does not exercise board transitions,
production bucket lookup, continuation work, large graph admission, deep
recursion, or production memory pressure. It cannot select the production worker
count by itself.

### S10. Thread placement and CCD locality are uncontrolled

Location: `src/solver/multiway/engine/multiway_traversal.cpp:708-713`

Workers are ordinary `std::thread`s with no topology-aware placement. On the
target host, placement can change shared-cache locality and cross-CCD traffic.
Affinity is not the first fix because the global mutex already forces shared-line
migration, but it must be a controlled qualification variable after the shared
write path is removed.

### S11. Static partitioning ignores trajectory cost variance

Locations: `src/solver/multiway/engine/multiway_scheduler.cpp:42-66`,
`src/solver/multiway/engine/multiway_traversal.cpp:756-779`

Workers receive equal contiguous trajectory counts, but sampled branches, public
chance, terminal depth, and leaf work have different costs. Expensive partitions
become the batch tail while completed workers idle. Count-based min/max telemetry
cannot reveal this execution-time imbalance.

### S12. Worker streams are sorted and globally sorted again

Locations: `src/solver/multiway/engine/multiway_traversal.cpp:782-784`,
`src/solver/multiway/engine/multiway_solver.cpp:811-815`

Each worker sorts its stream. The coordinator then copies all already-sorted
streams and runs another full `std::sort` instead of a k-way merge. Sorting work
and memory traffic are duplicated for every batch.

### S13. Compact merge searches the row for every delta

Locations: `src/solver/multiway/engine/multiway_solver.cpp:817-824`,
`src/solver/multiway/engine/multiway_compact_storage.cpp:59-86`

Compact merge calls `apply_delta()` per entry. Each call binary-searches row
metadata even when adjacent sorted deltas target the same row or cell. The serial
apply phase is `O(D log R)` and repeatedly reloads metadata.

### S14. Every worker reserves full capacity for two streams

Locations: `include/solver/multiway/engine/multiway_traversal.hpp:178-181`,
`src/solver/multiway/engine/multiway_solver.cpp:625-628`,
`src/solver/multiway/continuation/multiway_continuation_selector.cpp:40-44`

Every worker reserves `max_worker_delta_entries` for both regret and continuation
deltas. Capacity does not shrink with that worker's partition or use separate
high-water marks. Moving from four to sixteen workers quadruples both large
reservations although each worker receives fewer trajectories.

### S15. Merge duplicates the complete regret stream

Location: `src/solver/multiway/engine/multiway_solver.cpp:801-814`

Worker deltas remain resident while all entries are copied into `merge_deltas_`.
This doubles resident regret-delta storage and adds a full serial memory-bandwidth
pass before sorting and applying.

### S16. Pending-cell scratch is sized for every delta

Locations: `src/solver/multiway/engine/multiway_solver.cpp:666-669`,
`src/solver/multiway/engine/multiway_solver.cpp:825-861`

`pending_merge_cells_` reserves aggregate delta capacity although it needs only
one entry per distinct updated cell. Repeated updates to common cells make the
reservation much larger than use, reducing memory and cache headroom.

### S17. Unlimited training reads the clock per trajectory

Location: `src/solver/multiway/engine/multiway_traversal.cpp:756-761`

The inner loop calls `steady_clock::now()` for every trajectory even when the
deadline is `time_point::max()`, the blueprint trainer default. Every worker pays
this unnecessary hot-loop call for the complete offline run.

### S18. Cancellation uses a shared acquire load per trajectory

Location: `src/solver/multiway/engine/multiway_traversal.cpp:757-761`

Every trajectory performs an acquire load from `cancelled_`. Normal offline
training leaves it false for the whole batch, yet all workers repeatedly access
the shared synchronization line at maximum frequency.

### S19. Completion creates a pool-mutex convoy

Location: `src/solver/multiway/engine/multiway_traversal.cpp:795-799`

This finding is resolved. Workers increment `completed_workers_` atomically and
notify only the final waiter; completion does not reacquire `pool_mutex_`.

### S20. Dispatch wakes inactive configured workers

Locations: `src/solver/multiway/engine/multiway_traversal.cpp:738-750`,
`src/solver/multiway/engine/multiway_traversal.cpp:836-840`

This finding is resolved. Each worker has a dedicated condition variable and the
runner notifies only workers in `active_batch_count_`; completion also waits on
that active count.

### S21. Private-deal sampling allocates per trajectory

Location: `src/solver/multiway/abstraction/multiway_terminal_adapter.cpp:139-157`

Sampling starts with fixed `MultiwayPrivateWorkerScratch`, then copies holes into
`MultiwayJointPrivateSample::holes` using `vector::assign`. The fixed path becomes
heap-backed once per trajectory, creating allocator contention across workers.

### S22. Terminal settlement copies dynamic arrays per terminal

Location: `src/solver/multiway/abstraction/multiway_terminal_adapter.cpp:514-548`

Fold settlement copies contributions and folded flags and allocates strengths.
Showdown settlement copies board, holes, contributions, and folded flags. These
operations execute for terminal edges, including every traverser action branch.

### S23. Bucket tables are rediscovered at every decision

Locations: `src/solver/multiway/engine/multiway_traversal.cpp:385-393`,
`src/solver/multiway/abstraction/multiway_bucket_model.cpp:139-155`

Every postflop decision binary-searches the registry and compares dynamic board
vectors. The table is invariant for an admitted public state but is not cached in
its immutable metadata, so every trajectory repeats the search.

### S24. Blueprint misses perform up to three searches

Locations: `src/solver/multiway/blueprint/multiway_blueprint_policy_provider.cpp:32-43`,
`src/solver/multiway/blueprint/multiway_blueprint_store.cpp:69-113`

After `find()` misses, `strategy_into()` calls `has_infoset_bucket()` and possibly
`has_infoset()`. Each starts another binary search over the same row store before
the fallback path takes the coordinator mutex.

### S25. Action generation duplicates successor construction

Location: `src/solver/multiway/abstraction/multiway_action_abstraction.cpp:275-422`

Action generation reconstructs `MultiwayState`, allocates a menu, and calls
`state.apply()` per candidate to normalize targets. Traversal later calls
`betting_state.apply()` again for evaluated actions, duplicating transition work
at every visited decision.

### S26. Regret discounting is a serial full-array pass

Locations: `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:319-321`,
`src/solver/multiway/engine/multiway_compact_storage.cpp:88-97`

At each discount interval, the training thread scales every regret cell after
workers join. This unreported bandwidth pass has no parallel partition and its
serial cost grows with production storage.

### S27. Negative-regret pruning is another serial full-array pass

Locations: `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:322-330`,
`src/solver/multiway/engine/multiway_compact_storage.cpp:99-113`

Scheduled pruning scans and writes the complete regret array on the coordinator
thread. It is excluded from traversal and merge timings and prevents worker-count
speedup for batches where pruning is due.

### S28. Per-batch telemetry grows without a bound

Location: `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:371`

Every batch appends to `admitted_rows_per_batch` without reserve or retention
limit. Long runs accumulate one entry per batch and periodically reallocate and
copy the history on the coordinator path; report creation copies it again.

### S29. Public-state storage eagerly reserves its worst-case limit

Location: `src/solver/multiway/engine/multiway_solver.cpp:657-665`

The coordinator immediately reserves `max_public_states` large descriptors.
Sizing for worst-case graph growth therefore allocates a large contiguous
metadata block before observed demand, reducing memory available to worker and
merge streams.

### S30. Checkpointing copies and canonicalizes the whole solver

Locations: `examples/multiway_workflow_main.cpp:572-583`,
`src/solver/multiway/engine/multiway_solver.cpp:1001-1094`

Every checkpoint copies public descriptors and storage, builds an unordered
state index and several vectors, sorts states and rows, then serializes them. This
stop-the-world phase grows with the entire model and is outside traversal and
merge scaling measurements.

## Required architecture

Use immutable batch epochs:

```text
coordinator prepares immutable epoch view
  -> workers traverse without coordinator writes
  -> worker-local state/row discoveries and diagnostics
  -> deterministic discovery reduction
  -> deterministic regret/continuation merge
  -> publish next immutable epoch view
```

The immutable view must provide direct or binary indexed access to public-state
metadata and row metadata. Workers may read regret arrays concurrently because
they are not modified until the barrier. Newly discovered states and rows become
visible at the next deterministic epoch boundary. Preserve the scalar path and
fixed trajectory/update ordering.

## Step-by-step plan

### Phase 0. Establish actionable measurements

1. Add per-worker counters for coordinator lock acquisitions and wait/hold
   nanoseconds, separated into state admission, row admission, regret lookup,
   pruning lookup, and visit counters.
2. Add allocation counts/bytes for action-menu and public-descriptor construction
   using a benchmark-only allocator hook or ETW/WPA capture.
3. Report batch wall time as traversal critical path, worker imbalance, delta
   sort, regret merge, continuation merge, graph admission, and checkpoint time.
4. Record process RSS, page faults, CPU utilization per logical processor, and
   effective CPU frequency.
5. Extend the benchmark with a production-shaped fixed artifact fixture while
   retaining the tiny deterministic fixture.

Exit gate: at least 90% of batch wall time is attributable to named stages, and
mutex wait is measured rather than inferred.

### Phase 1. Remove unnecessary locked work without changing epochs

1. Move terminal, leaf, missing-lookup, and street counters into `WorkerScratch`;
   reduce them after join.
2. Add a fast existing-row API that performs one lookup and returns immutable row
   metadata and strategy in one coordinator call.
3. Split row admission from steady-state lookup. Do not validate full row/state
   contracts after a row is already known.
4. Cache row handles in the admitted public decision descriptor or an immutable
   side index. Avoid a second binary lookup for regret matching.
5. Reserve compact row and value storage to the admitted capacity, or allocate
   fixed blocks before workers start.
6. Align each `WorkerScratch` to `std::hardware_destructive_interference_size`
   with a 64-byte fallback and verify addresses in a unit test.

Exit gate: existing-state traversal performs no counter lock and no compact
storage growth. Eight workers must no longer regress against four on both
fixtures.

### Phase 2. Remove heap allocation from recursive traversal

1. Add `make_legal_actions_into(span)` returning a count into the existing fixed
   action buffer.
2. Introduce a compact traversal-only public node containing fixed board cards,
   fixed history edge/reference, compact betting state, and fixed action menu.
3. Construct full `MultiwayPublicStateDescriptor` only for a genuinely new state
   during deterministic admission.
4. Add worker-local reusable construction scratch for terminal settlement and
   bucket lookup. Confirm no allocation occurs after warm-up in a trajectory.

Exit gate: the recursive decision/chance/terminal path has zero heap allocations
for already-admitted graph states.

### Phase 3. Introduce immutable batch epochs

1. Freeze public-state descriptors, row metadata, and regret arrays for the
   duration of a batch.
2. Give each worker read-only spans plus local discovery streams. No worker calls
   `admit_public_state()`, `admit_infoset_row()`, or visit-counter coordinator APIs.
3. Deduplicate discoveries after join by stable state/infoset key. Validate and
   admit in fixed key order.
4. Publish stable indices/offsets for the next batch. Never retain pointers across
   a storage relocation.
5. Keep all regret writes after the traversal barrier.

Exit gate: steady-state worker traversal takes zero coordinator mutexes and is
bitwise equivalent across 1/2/4/8/16 workers.

### Phase 4. Replace global delta sort when it becomes dominant

1. Emit worker deltas ordered by compact row-cell id and trajectory id.
2. Replace copy plus global `std::sort` with a deterministic k-way merge of the
   already-sorted worker streams.
3. Apply contiguous cell runs directly. Keep fixed worker and trajectory tie
   ordering.
4. Merge continuation deltas in one selector critical section, or at the epoch
   barrier without a mutex, using an indexed row table instead of linear search.
5. Only if application remains dominant, partition cell-id ranges and reduce with
   a fixed reduction tree.

Exit gate: merge is below 15% of batch wall time at 16 workers, with unchanged
fingerprints and checkpoint payloads.

### Phase 5. Memory locality and topology qualification

1. Compare compact arrays allocated normally, page-prefaulted, and large-page
   eligible. Do not retain a change without measured benefit.
2. Compare physical-core placement within one CCD, spread physical cores across
   CCDs, and SMT placement. Record the exact logical CPU set.
3. Partition immutable state/row metadata to reduce remote reads where stable
   keys permit it. Keep ownership fixed for the complete batch.
4. Measure L3 misses, memory bandwidth, context switches, migrations, and mutex
   wait with ETW/WPA or an equivalent profiler.

Exit gate: the selected 16-worker placement has stable medians and does not depend
on incidental Windows scheduling.

### Phase 6. Production qualification matrix

For each controlled implementation stage, run 1/2/4/8/16 workers with batch
sizes 4,000, 16,000, 64,000, and one production-selected size. Use one warm-up,
at least five timed repeats, identical seeds and trajectory ids, and a fresh
process for memory/topology experiments.

Record:

- median and p10/p90 trajectories/s;
- speedup and parallel efficiency relative to one worker;
- traversal, lock wait/hold, allocation, sort, merge, and checkpoint share;
- worker critical-path imbalance;
- peak RSS, page faults, CPU utilization, frequency, and topology;
- deterministic merge fingerprint, checkpoint hash, and export hash.

Qualification gates:

- `T16 > T8 > T4 > T2 > T1` by stable median on the production-shaped fixture;
- at least 10% median gain from 8 to 16 workers;
- at least 70% CPU utilization across the selected 16 physical cores during the
  traversal stage;
- mutex wait below 5% and merge below 15% of batch wall time;
- zero steady-state recursive allocations;
- observed RSS below memory admission with at least 10% headroom;
- bitwise stable outputs across repeats and worker counts;
- no acceptance, discard, capacity, cancellation, or checkpoint regression.

## Implementation order

| Findings | Remediation phase |
|---|---|
| S1, S2, S8 | Phase 0 instrumentation, then Phase 1 lock reduction |
| S3, S21, S22, S25 | Phase 2 allocation-free traversal data |
| S4, S16, S29 | Phase 1 capacity and storage-layout correction |
| S5, S12, S13, S15 | Phase 4 deterministic k-way cell merge |
| S6, S24 | Phase 3 immutable indexed lookup and barrier merge |
| S7, S18, S19, S20 | Phase 1 worker and pool synchronization cleanup |
| S9 | Phase 0 production-shaped benchmark fixture |
| S10 | Phase 5 topology qualification |
| S11 | Phase 0 active-time evidence, then deterministic cost-aware scheduling |
| S14 | Phase 1 partition-derived independent stream capacities |
| S17 | Phase 1 deadline-disabled fast path |
| S23 | Phase 3 immutable state-to-bucket-table handle |
| S26, S27 | Phase 4 fixed-partition deterministic maintenance passes |
| S28 | Phase 1 bounded/downsampled telemetry storage |
| S30 | Phase 4 incremental or snapshot checkpoint pipeline |

Implement Phase 0 first. Then apply Phase 1 items independently and measure after
each item. Phase 2 is the lowest-risk structural optimization. Phase 3 is the
main scaling redesign. Start Phase 4 only when telemetry proves merge is the new
limit. Run topology experiments only after shared coordinator writes are absent,
otherwise affinity results will mostly measure mutex cache-line movement.
