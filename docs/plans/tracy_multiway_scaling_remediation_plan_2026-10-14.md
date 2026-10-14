# Tracy Multiway Scaling Remediation Plan

**Date:** 2026-10-14
**Source report:** `docs/reports/tracy_multiway_training_scaling_qa_2026-10-14.md`
**Scope:** Checkpoint output, coordinator admission, and multi-worker throughput
**Validation boundary:** Source analysis only. No build, test, benchmark, or solver run was performed.

## Baseline

- Four workers improve batch wall time by only 12.3% and throughput by 14.1%.
- Worker traversal critical path improves from 1.033 seconds to 486 milliseconds.
- Coordinator admission grows from 1.140 seconds to 1.404 seconds.
- Fixed scheduling leaves up to 176.9 milliseconds of worker tail idle time.
- Checkpoint output takes 198-240 seconds.
- The checkpoint is 137,380,835 bytes, approximately 131 MiB, not 134 KiB.

The core code paths have not materially changed since the report's source
commit. The current findings therefore apply to the captured implementation.

## Findings

### R1 - Checkpoint timing includes several expensive operations

`MultiwayTrainingCheckpointArtifacts::save_atomic()` is timed together with
`session.checkpoint()`. The reported duration therefore includes:

- coordinator snapshot locking;
- public catalog reconstruction;
- compact storage expansion;
- canonicalization and full-buffer copies;
- two checkpoint validations;
- payload hashing;
- byte-at-a-time portable encoding;
- file close and publication.

Compact storage export performs a `page_for_bucket()` lookup for each logical
bucket/action cell. It then constructs dense `double` arrays from compact page
storage. The coordinator subsequently constructs another canonical copy.

### R2 - Discovery admission repeats avoidable work

The captured batch generated 85,996 public-state candidates but finished with
69,195 states. It generated 47,173 row candidates but admitted 31,070 rows.
Approximately 19.5% of state candidates and 34.1% of row candidates are
duplicates or already represented.

The current coordinator:

- concatenates worker vectors without exact pre-reservation;
- deep-copies dynamic descriptors;
- sorts the combined vectors;
- repeatedly scans every state until its parent is admitted;
- reconstructs parent descriptors for membership checks;
- acquires coordinator locks per candidate;
- inserts catalog index entries individually into a sorted vector.

### R3 - Worker scratch capacity has conflicting semantics

Memory admission budgets `worker_count * max_worker_delta_entries`, indicating
that the setting is a per-worker limit. `MultiwayRootBatchRunner` instead
divides the value by worker count before allocating worker streams. Increasing
workers therefore reduces per-worker headroom while memory preflight charges
for the larger allocation.

This did not exhaust capacity in the captured run, but it makes skewed dynamic
scheduling unsafe and can become a scaling boundary.

## Plan 1 - Checkpoint output

### CP1 - Split checkpoint telemetry

Add independent Tracy zones and report fields for:

1. Coordinator snapshot lock wait.
2. Public catalog export.
3. Compact storage export.
4. Canonicalization.
5. Validation.
6. Payload hashing.
7. Binary encoding and file write.
8. Stream close or flush.
9. Atomic publication.

Record bytes for public states, row shapes, regrets, strategy sums, and the
late-window baseline. The sub-phase total should explain at least 95% of the
existing checkpoint duration.

Files:

- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`

### CP2 - Make compact snapshot export linear

Replace per-cell page lookup with sequential page export:

1. Compute canonical row offsets and total logical cell count once.
2. Allocate final regret and strategy arrays once.
3. Zero-initialize inactive logical cells.
4. Walk sorted compact pages sequentially.
5. Copy active page ranges into action-major destinations.

Make compact export guarantee canonical row order. Remove the coordinator's
second full canonical storage copy for that backend. Keep the Float64 reference
path unchanged.

Expected complexity is `O(rows + logical cells + active pages)`.

### CP3 - Remove redundant full scans

Introduce an internal sealed checkpoint representation:

- validate the constructed snapshot once;
- compute the payload hash while encoding;
- avoid a separate hash pass over all arrays;
- retain validation in the public `save_atomic()` API for arbitrary callers;
- use the sealed path only from the trusted training workflow.

### CP4 - Add buffered portable serialization

Replace per-byte `ostream::put()` serialization for checkpoint arrays with a
fixed 64 KiB to 1 MiB encoding buffer:

- preserve schema-2 little-endian bytes exactly;
- encode regret, strategy, and baseline arrays in blocks;
- update the payload hash during encoding;
- check stream state after each block;
- avoid compression in this phase.

### CP5 - Avoid full-copy publication on Windows

Report whether publication used rename or the copy fallback. Use a checked
Windows replacement operation for existing destination files so checkpoint
publication does not copy the complete temporary file a second time.

### CP6 - Optional compact schema

Only if CP2-CP5 remain insufficient, introduce a new checkpoint schema that:

- stores packed public catalog nodes directly;
- stores shared action menus once;
- avoids repeated full history serialization;
- retains schema-2 loading for resume compatibility.

### Checkpoint validation

Required tests:

- schema-2 byte compatibility for the buffered writer;
- save/load round trip for compact and Float64 storage;
- identical payload hash and resumed training state;
- invalid, truncated, and non-finite checkpoint rejection;
- replacement of an existing checkpoint;
- large sparse-page export equivalence against the scalar reference export.

Performance gate on the same machine:

- snapshot plus save below 5 seconds;
- sustained output above 50 MiB/s;
- no additional peak allocation beyond the checkpoint memory budget.

## Plan 2 - Coordinator admission

### AD1 - Add admission diagnostics

Record:

- raw candidates per worker;
- worker-local unique candidates;
- globally unique candidates;
- already-admitted and newly admitted candidates;
- parent-resolution passes and probes;
- descriptor reconstructions;
- lock acquisitions;
- catalog index insertion time;
- row admission time.

### AD2 - Prepare worker discoveries in parallel

After traversal, have each worker:

- sort states by stable state ID and deterministic tie-break;
- sort rows by infoset ID;
- deduplicate local candidates;
- reject conflicting descriptors or row shapes;
- retain reusable preallocated buffers.

This work belongs beside worker delta sorting, outside traversal inner loops.

### AD3 - Deterministic k-way merge

Merge sorted worker discovery streams without concatenating and globally
sorting a fifth vector:

- select states by ID and the current deterministic tie-break;
- collapse cross-worker duplicates;
- verify duplicate compatibility;
- merge rows by infoset and verify shape compatibility;
- emit one canonical state and row stream.

An initial low-risk patch may reserve exact combined sizes before insertion,
but the k-way merge is the intended endpoint.

### AD4 - Compute parent-first order once

Build a flat ID-sorted index for unique new states:

1. Resolve candidate parents by binary search.
2. Treat existing catalog parents as depth zero.
3. Memoize candidate depth.
4. Sort admission order by `(depth, state_id)`.
5. Reject cycles and unresolved parents before coordinator mutation.

This replaces the repeated `while (remaining)` scans.

### AD5 - Add bulk coordinator admission

Add a prepared-batch API that:

- acquires one exclusive coordinator lock;
- preflights unique state, row, page, and value capacities;
- validates each new descriptor once;
- admits parent-first states;
- admits canonical unique rows;
- updates diagnostics before releasing the lock.

Extend `MultiwayPublicCatalog` to append packed nodes contiguously and merge
old and new sorted index entries once. Avoid one sorted-vector insertion per
state.

### AD6 - Stop rediscovering epoch-known states

Expose a lock-free immutable-epoch state-membership query analogous to
`has_infoset_row_epoch()`. Workers should append a discovery only if the state
is absent from the epoch catalog.

Preserve state-ID conflict detection with a compact payload fingerprint or one
structural validation per unique ID. Direct coordinator admission must retain
its existing strict validation behavior.

### Admission validation

Required tests:

- shuffled multi-level parent/child input;
- duplicates within and across worker streams;
- conflicting duplicate descriptors and row shapes;
- unresolved parents and cycles;
- capacity rejection before partial mutation;
- fixed and dynamic worker-count equivalence;
- identical admitted sets, merge fingerprint, checkpoint hash, and policy.

Performance gate for the captured cold batch:

- four-worker admission no more than 10% slower than one-worker admission for
  the same candidate set;
- admission below 250 milliseconds;
- steady-state batches produce near-zero existing-state admission work.

## Plan 3 - Multi-worker throughput

### TH1 - Separate cold and steady-state measurements

The one captured batch includes major graph growth. Use:

- one untimed graph-growth warmup batch;
- four measured batches;
- five repeats;
- 1, 2, 4, and 8 workers;
- fixed and dynamic scheduling;
- separate pinned and unpinned groups;
- checkpoint and blueprint output disabled during batch measurement.

Keep identical seeds, workload, model identity, and binary provenance.

### TH2 - Repair scratch capacity semantics

Treat `max_worker_delta_entries` consistently as a per-worker limit:

- allocate the configured capacity for every worker;
- retain memory preflight as `workers * per-worker capacity`;
- create a separate discovery-capacity limit;
- include both actual allocations in the memory ledger and report;
- test dynamic skew where one worker processes more chunks.

### TH3 - Qualify dynamic scheduling

The dynamic scheduler already exists. After the capacity fix:

- sweep chunk sizes 1, 2, 4, 8, and 16;
- measure queue overhead, worker critical path, and tail idle time;
- select the smallest chunk that reduces the tail without increasing aggregate
  worker CPU materially;
- include chunk policy in scheduler identity or versioning;
- preserve global trajectory IDs and fixed-order stream merging.

The immediate target is to reduce maximum tail idle time from 176.9
milliseconds to approximately one worst-case chunk.

### TH4 - Profile traversal after admission is fixed

Enable detailed worker phase profiling only in the isolated diagnostic binary.
If worker aggregate CPU still grows materially with worker count, inspect:

- dynamic public descriptor construction;
- history and action-menu vector allocation;
- discovery-stream deep copies;
- bucket lookup locality;
- terminal evaluation;
- false sharing in worker scratch and telemetry.

If descriptor allocation is confirmed, emit packed fixed-size discovery
records, retain parent-linked history, reuse worker buffers, and materialize
dynamic descriptors only at coordinator validation boundaries.

### TH5 - Make pinning topology-aware

Do not make the current logical-CPU `0..N-1` pinning default. If pinning proves
useful:

- identify physical cores and processor groups;
- place one worker per physical core first;
- keep the coordinator off the busiest worker core when possible;
- report requested and resolved placement;
- fall back to unpinned execution when topology discovery fails.

### Throughput validation

Required gates:

- zero discarded trajectories;
- identical deterministic merge fingerprints;
- identical exported policies and checkpoint hashes where applicable;
- four-worker throughput at least 2.5 times one-worker throughput after
  warmup;
- stretch target of 3 times or better;
- admission below 20% of batch wall time;
- merge below 10% of batch wall time.

## Recommended implementation order

1. CP1 checkpoint phase telemetry.
2. CP2 linear compact snapshot export.
3. CP3-CP5 scan, serialization, and publication fixes.
4. AD1 admission diagnostics.
5. AD2-AD4 worker preparation and parent-first merge.
6. AD5-AD6 bulk admission and epoch-known filtering.
7. TH1 steady-state benchmark protocol.
8. TH2 capacity contract repair.
9. TH3 dynamic scheduler qualification.
10. TH4-TH5 traversal and topology work only when supported by measurements.

Checkpoint work is independent and can land first. Coordinator remediation is
the prerequisite for meaningful worker-scaling qualification.
