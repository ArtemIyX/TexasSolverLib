# PB-12 50M Multiway Training Memory Investigation and Implementation Plan

**Status:** Investigation complete; implementation not started  
**Date:** 2026-10-26  
**Branch:** `PB-12`  
**Target:** Complete 50,000,000 multiway training trajectories on a 64 GB RAM machine without weakening the 64 GiB process limit, 48 GiB working-set limit, 1.5 GiB system reserve, disk preflight, provenance checks, or fail-closed runtime monitoring.

## 1. Executive decision

The current RAM-only trainer cannot support the 50M workload by increasing capacities. The measured 50K-to-58K interval still discovers public states, sparse rows, and compact cells at an approximately linear rate. The current 34M/5M/100M capacity profile is therefore not a demonstrated 50M envelope.

The recommended implementation is:

1. Correct the memory estimator so it models the packed catalog and streamed checkpoint/export paths.
2. Reduce the bucket artifact from 32-bit assignments and per-table dynamic metadata to width-selected assignments and a packed mapped directory.
3. Add a bounded out-of-core training backend for public states, sparse row/page metadata, and compact accumulators.
4. Replace monolithic materialized checkpoints with atomic generation manifests over immutable storage segments and a batch WAL.
5. Keep deterministic worker streams and coordinator merge order unchanged.
6. Validate with small, gated pilots before requesting authorization for any 500K-or-larger run.

The process working-set design target is 44 GiB or less. The existing 48 GiB gate remains authoritative.

## 2. Evidence and root cause

### 2.1 4M public-state failure

The coordinator checks the current catalog size plus the incoming batch before mutation and throws when the configured capacity would be exceeded:

- `src/solver/multiway/engine/multiway_solver.cpp:1370`

The guarded attempt stopped after 58,000 accepted trajectories with:

| Metric | Value |
|---|---:|
| Public states | 3,971,363 |
| Sparse rows | 1,565,562 |
| Sparse values | 61,306,003 |
| Discarded trajectories | 0 |
| Checkpoint | Not produced |
| Blueprint | Not produced |

Evidence is preserved in:

- `tmp/pb12_qualification_500k_20261026T0542Z/report.json`
- `tmp/pb12_qualification_500k_20261026T0542Z/stderr.log`
- `tmp/pb12_qualification_500k_20261026T0542Z/terminal_result_manifest.json`

This was a real logical capacity failure, not an operating-system allocation failure.

### 2.2 34M preflight rejection

The v2 profile was rejected before constructing the trainer:

| Metric | Value |
|---|---:|
| Estimated private peak | 54,595,106,768 bytes |
| Estimated working-set peak | 69,933,107,152 bytes |
| Working-set gate | 51,539,607,552 bytes |
| Rejected stage | Worker deltas, after sparse-row admission |

The 34M estimate is dominated by conservative or obsolete capacity terms:

| Estimator component | Approximate size |
|---|---:|
| Existing working set after bucket validation | 14.86 GiB |
| 34M public states at synthetic 1 KiB/state | 32.42 GiB |
| Materialized export action allowance | 5.96 GiB |
| Sparse row metadata and compact cells | 1.30 GiB |
| Worker and discovery streams | 0.75 GiB |
| Checkpoint/export transient | 9.80 GiB |
| Minor range/runtime allocations | 0.04 GiB |
| Estimated working-set peak | 65.13 GiB |

Relevant implementation points:

- `src/solver/multiway/engine/multiway_memory.cpp:1167` charges 1 KiB per configured public state.
- `src/solver/multiway/engine/multiway_memory.cpp:1211` charges two materialized export-action buffers.
- `examples/multiway_workflow_main.cpp:860` passes the maximum blueprint estimate as both checkpoint and export transients.
- `src/solver/multiway/engine/multiway_memory.cpp:1220` adds the larger transient once.
- `src/solver/multiway/engine/multiway_solver.cpp:1184` reserves coordinator merge scratch from `max_sparse_values`, but the preflight does not model that exact allocation.

Increasing the public-state setting from 4M to 34M adds exactly 30,720,000,000 bytes to the estimate because of the 1 KiB multiplier. This explains the complete difference between the two preflights.

### 2.3 Mapping, residency, and private commitment

The bucket artifact is mapped rather than copied:

- Mapped artifact: 15,369,522,466 bytes.
- Owned registry metadata: 546,232,440 bytes.

Startup validates every mapped assignment in `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp:618`. The validation faults most artifact pages into the process working set:

| Phase | Working set |
|---|---:|
| Before mapping | 5.8 MB |
| After metadata scan | 12.46 GB |
| After assignment validation | 15.95 GB |

Therefore:

- The 14.31 GiB mapping is virtual file-backed storage.
- Most of it is also resident after validation.
- It is not private commit.
- The 0.51 GiB table registry is owned private memory.
- The baseline already contains resident mapped pages, so the estimator must not add the complete mapping a second time.

### 2.4 Checkpoint and export peaks

The 50K reference produced:

| Item | Value |
|---|---:|
| Training RSS | approximately 17.93 GB |
| Ordinary report working-set peak | approximately 18.7 GB |
| Wrapper checkpoint high-water | approximately 25.3 GB |
| Checkpoint file | 4,576,234,700 bytes |
| Blueprint file | 3,921,243,000 bytes |

The wrapper high-water is authoritative for capacity planning. See `docs/pb9_multiway_training_scaling_findings.md`.

The production compact export is already streamed with fixed strategy/action scratch arrays in `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:1049`. The memory preflight still models a former materialized export shape.

The schema-5 checkpoint writer is streamed, but it still:

- allocates state order and state depth arrays;
- reconstructs dynamic public descriptors;
- makes a complete counting pass before the real write;
- is capped at 100M states, 100M rows, and 60 GiB.

See `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp:27` and `:778`.

## 3. 50M capacity assessment

### 3.1 Observed novelty

Between 50K and 58K trajectories:

| Metric | Increase | Increase per trajectory |
|---|---:|---:|
| Public states | 530,818 | 66.35 |
| Sparse rows | 204,146 | 25.52 |
| Sparse values | 7,863,242 | 982.91 |

These slopes are not proof of final 50M cardinality. They are a stress envelope demonstrating that no useful saturation has yet been measured.

If the interval slope continued, 50M would reach approximately:

| Metric | Stress-envelope count |
|---|---:|
| Public states | 3.32 billion |
| Sparse rows | 1.28 billion |
| Sparse values | 49.15 billion |

Compact cells alone use 12 bytes per active cell: one `int32_t` regret and one `uint64_t` strategy mass. The cell stress envelope is therefore about 549 GiB before catalog, indexes, rows, checkpoints, or exports.

### 3.2 Decision

The current architecture is infeasible for 50M on 64 GB RAM. This conclusion does not depend on accepting the full linear extrapolation:

- The current 100M cell capacity is already 61% consumed at 58K trajectories.
- The checkpoint format cannot represent more than 100M states or rows.
- The checkpoint file cannot exceed 60 GiB.
- The workflow configuration ceiling is 34M states, 8M rows, and 200M values.
- There is no measured saturation curve supporting those ceilings for 50M.

The trainer needs storage whose RAM usage is bounded independently of admitted state cardinality.

## 4. Target architecture

### 4.1 Memory budget

The implementation must expose explicit cache and scratch limits. Initial targets:

| Subsystem | Working-set target |
|---|---:|
| Compact bucket mapping and directory | 4.5 GiB |
| Public catalog and index cache | 6 GiB |
| Sparse row/cell page cache | 16 GiB |
| Worker delta and discovery streams | 4 GiB |
| Coordinator merge/admission workspace | 4 GiB |
| Runtime, ranges, allocator overhead | 2 GiB |
| Checkpoint/export scratch | 1 GiB |
| Unallocated process headroom | 6 GiB |
| Total target | 43.5 GiB |

Guards remain:

- 48 GiB process working set.
- 64 GiB process-memory configuration limit.
- 1.5 GiB minimum system-available memory.
- Fail-closed unavailable-counter behavior.
- Exact disk preflight.

### 4.2 Disk envelope

The out-of-core design must preflight:

1. Live public-state segments.
2. Public index runs.
3. Row and page metadata.
4. Regret and strategy-mass pages.
5. WAL and manifest generations.
6. Worst-case compaction overlap.
7. Final blueprint export.
8. A fixed free-space reserve.

The measured novelty rate implies that 100 GiB is not a credible 50M disk requirement. Initial hardware planning should assume 2–3 TB of fast NVMe until safe pilots establish a lower bound. Raising the configured disk requirement strengthens the existing guard; it does not bypass it.

## 5. Detailed implementation plan

## Phase 0: Establish contracts and fixtures

### Step 0.1: Define exact invariants

Document and encode the behavior that must remain unchanged:

- Public-state identity and parent relationships.
- Sparse infoset ordering.
- Compact `int32_t` regret arithmetic.
- `uint64_t` strategy-mass arithmetic.
- Worker stream sort order.
- Coordinator merge order.
- Fixed scheduler behavior.
- Policy-content hash for equivalent final state.
- Zero trajectory discards.
- Checkpoint restart equivalence.

Why: storage layout may change, but solver semantics and deterministic ordering must not.

Files:

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_checkpoint_equivalence.cpp`

### Step 0.2: Add deterministic sizing fixtures

Create synthetic fixtures with known state, row, page, and cell counts. They must exercise large logical capacities without allocating corresponding memory.

Why: estimator and disk-plan tests must not depend on expensive training jobs.

Add:

- `tests/test_multiway_storage_sizing.cpp`
- fixture helpers under `tests/support/` if existing conventions permit.

Acceptance:

- Counts and expected bytes are exact.
- Overflow saturates or rejects before allocation.
- Tests use no multiway training workload.

## Phase 1: Repair memory and disk accounting

### Step 1.1: Introduce backend-specific capacity plans

Add a `MultiwayTrainingStoragePlan` containing:

- configured logical capacities;
- fixed retained allocations;
- growth allocations;
- maximum reallocation transient;
- private committed estimate;
- resident working-set estimate;
- mapped virtual bytes;
- mapped residency allowance;
- checkpoint scratch;
- export scratch;
- output and disk-cache allowance;
- disk live, compaction, WAL, export, and reserve bytes.

Why: one formula cannot accurately model RAM vectors and bounded external storage.

Change:

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`

### Step 1.2: Correct the in-memory catalog estimate

Replace the 1 KiB public-state multiplier with exact terms for:

- `MultiwayPackedPublicNode` capacity;
- live `IndexEntry` capacity;
- retained merged-index scratch;
- menu vector headers and payloads;
- batch admission scratch;
- vector reallocation peaks.

Do not assume configured capacity is resident if the implementation grows on demand. Report configured maximum separately from retained and committed capacity.

Why: the present 34M rejection is driven primarily by a representation that is no longer used.

### Step 1.3: Correct compact-storage accounting

Model:

- upfront `rows_.reserve(max_rows_)`;
- page metadata growth;
- regret and strategy array growth;
- merged row/page vectors during batch admission;
- coordinator `pending_merge_cells_` reserve;
- old-plus-new vector memory during reallocation.

Why: the current preflight overcounts some export allocations while missing real merge and reallocation peaks.

### Step 1.4: Correct checkpoint/export accounting

For compact streamed export, charge:

- fixed strategy array;
- fixed quantized-action array;
- encoder buffer;
- configured headroom;
- no materialized output-sized private buffer.

For schema-5 checkpoint, charge:

- state order array;
- state depth array;
- optional row order;
- reusable descriptor capacity;
- writer buffer;
- allocator overhead.

Charge output bytes to disk and the system-cache gate, not private process memory.

Why: streamed output size and process scratch are separate resource classes.

### Step 1.5: Add estimator reconciliation telemetry

At process start, after bucket load, coordinator construction, worker construction, selected batch boundaries, before/after checkpoint, and before/after export, report:

- estimated private bytes;
- observed private commit;
- estimated working set;
- observed working set;
- retained component bytes;
- mapped virtual bytes;
- file-backed resident bytes when available;
- unattributed private bytes;
- output/cache allowances.

Why: safe pilots need to prove the new estimator remains conservative.

Tests:

- Extend `tests/test_multiway_memory.cpp`.
- Extend `tests/test_multiway_qualification_reports.cpp`.
- Add exact 4M and 34M ledger fixtures without training.
- Verify checkpoint and export outputs are not charged as private buffers.
- Verify merge scratch and reallocation peaks are included.

Phase acceptance:

- Every retained allocation has one ledger owner.
- No mapped byte is counted as private commit.
- No resident mapped byte is double-counted.
- Estimate remains fail-closed when required counters are unavailable.
- Synthetic estimates do not exceed observed peaks by an unexplained output-sized buffer.

## Phase 2: Compact the bucket artifact

### Step 2.1: Define bucket artifact schema 5

Add a fixed header containing:

- schema and model identity;
- table count;
- assignment width: 1, 2, or 4 bytes;
- directory offset and length;
- assignment offset and length;
- payload hash and section hashes.

Use the smallest assignment width that represents `bucket_count - 1`. The 12-bucket F1 profile uses one byte.

Why: the current artifact spends four bytes on values in `[0, 11]`.

### Step 2.2: Replace per-table dynamic metadata

Use a fixed directory entry containing:

- street;
- packed canonical board key;
- bucket count;
- assignment offset;
- table identity or a derivable identity field.

Store directory entries in canonical sorted order and binary-search the mapped array. Avoid one `std::vector` and heap allocation per table.

Why: 2,891,785 table objects currently retain roughly 546 MB of metadata.

### Step 2.3: Preserve mapped lookup

Update `MultiwayBucketTable`/`MultiwayBucketRegistry` so traversal receives non-owning mapped views. Lookup must decode the width without allocation, formatting, logging, hashing, or virtual dispatch.

Change:

- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_model.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`

### Step 2.4: Preserve validation and compatibility

- Keep schema-4 loading read-only.
- Produce schema 5 for new artifacts.
- Validate section bounds before exposing mapped views.
- Hash every assignment during artifact production.
- Permit full assignment validation as a qualification option.
- Report validation-caused residency separately.

Tests:

- Schema-4 versus schema-5 lookup equivalence for all canonical tables and hole combinations.
- Width boundaries at 256 and 65,536 buckets.
- Truncation, offset overflow, bad hash, duplicate board, and identity mismatch.
- No allocation during repeated lookup.

Phase acceptance:

- F1 artifact assignment payload is approximately one quarter of schema 4.
- Registry metadata is below 128 MiB.
- Lookup results and payload identity are equivalent.
- Startup working-set baseline is measured and below 6 GiB on the target machine.

## Phase 3: Define the external storage API

### Step 3.1: Extend `StorageBackend`

Add an explicit backend such as `ExternalCompactInt32`. Do not silently change `CompactInt32` behavior.

Configuration fields:

- storage directory;
- public catalog cache bytes;
- row/page cache bytes;
- maximum WAL bytes;
- segment target bytes;
- compaction fan-in;
- compaction scratch bytes;
- disk reserve bytes;
- synchronous durability mode.

Why: backend choice and resource identity must be part of provenance, checkpoint compatibility, and deterministic reports.

Change:

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- workflow identity and report serializers.

### Step 3.2: Create storage interfaces at cold boundaries

Define separate interfaces for:

- public-state lookup and append;
- sparse row lookup and append;
- compact page lookup, pin, dirty marking, and flush;
- batch transaction begin/commit/abort;
- immutable snapshot generation;
- deterministic ordered iteration for checkpoint/export.

Hot traversal must receive raw pointers or spans to pinned pages. It must not perform filesystem operations, allocate, use `std::function`, or hash strings per action.

Suggested files:

- `include/solver/multiway/engine/multiway_training_storage.hpp`
- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`

### Step 3.3: Keep the existing backend as reference

Adapt the current `MultiwayPublicCatalog` and `MultiwayCompactStorage` behind the interface without changing their behavior.

Why: differential testing requires a scalar, in-memory reference implementation.

Phase acceptance:

- Existing tests pass using the in-memory adapter.
- A small fixture produces identical ordered records through both interfaces.
- Backend selection is in workflow fingerprints and resume compatibility checks.

## Phase 4: Implement durable public-state storage

### Step 4.1: Define fixed packed records

Create a versioned disk record from `MultiwayPackedPublicNode` with explicit little-endian fields and no compiler padding. Use 64-bit record counts and offsets.

Why: native struct dumps are not portable and billions of records approach 32-bit limits.

### Step 4.2: Implement append-only node segments

- Append new nodes only after coordinator validation.
- Segment at a configured size.
- Seal each segment with count, hash, and index bounds.
- Never mutate sealed segments.
- Keep only the active segment writable.

### Step 4.3: Implement deterministic sorted index runs

- Key: `MultiwayPublicStateId`.
- Value: segment ID and record offset.
- Batch discoveries already arrive in deterministic sorted order.
- Write one sorted run per committed batch or bounded run group.
- Search newest memtable, then mapped runs using binary search.
- Compact runs synchronously at deterministic batch boundaries.
- Select compaction inputs by generation and level, never timing.

Why: deterministic LSM runs avoid retaining a multi-billion-entry in-memory index.

### Step 4.4: Implement a bounded node/index cache

- Preallocate cache slots.
- Cache immutable node pages, not individual heap objects.
- Use deterministic replacement metadata that cannot affect solver arithmetic.
- Report hits, misses, bytes, evictions, and read latency.
- Fail before exceeding configured cache bytes.

### Step 4.5: Preserve parent reconstruction

Provide a bounded reconstruction scratch buffer and iterative parent reads. Add a maximum validated ancestry depth. Pin records used by one reconstruction operation.

Tests:

- Duplicate detection across memtable and all run levels.
- Parent-first admission.
- Lookup after restart and compaction.
- Byte-identical run generation for identical batches.
- Injected write, flush, and publication failures.
- Cache limits and eviction correctness.

Phase acceptance:

- Catalog RAM remains within its configured cache plus fixed scratch.
- State count can exceed 100M in synthetic metadata tests.
- Restart reproduces catalog count and content hash.

## Phase 5: Implement external compact row and cell storage

### Step 5.1: Persist ordered row metadata

Store fixed records containing:

- infoset public-state ID;
- seat;
- bucket count;
- action count;
- first page ID;
- page count.

Use deterministic sorted runs as with public states.

### Step 5.2: Persist fixed accumulator pages

Each page contains:

- page header and identity;
- `int32_t` regrets;
- `uint64_t` strategy masses;
- checksum/version;
- fixed or bounded encoded length.

Do not narrow strategy masses without a separate overflow proof and compatibility decision.

### Step 5.3: Implement a bounded dirty page cache

- Preallocate the cache arena and metadata.
- Resolve and pin the page once per row/bucket update.
- Pass raw cell pointers into the action loop.
- Apply worker deltas in the current deterministic coordinator order.
- Flush dirty pages at batch commit.
- Never evict a pinned page.
- Block or fail before exceeding the dirty-page limit.

Why: filesystem or index lookup inside the per-action loop violates the hot-path contract and would make external storage impractical.

### Step 5.4: Add batch WAL

WAL records contain:

- batch number and deterministic merge fingerprint;
- new catalog records;
- new row/page records;
- ordered cell deltas or durable page after-images;
- checksums;
- commit marker.

Commit order:

1. Write WAL records.
2. Flush WAL.
3. Apply/flush active data segments.
4. Write generation manifest temporary file.
5. Flush manifest and containing directory where supported.
6. Atomically replace the current manifest.
7. Reclaim obsolete WAL generations only after publication.

### Step 5.5: Make compaction bounded

- Reserve disk space before compaction.
- Bound input/output buffers.
- Merge fixed-size windows.
- Publish new runs atomically before deleting obsolete runs.
- Retain the prior generation until the new manifest is durable.

Tests:

- In-memory versus external cell values after every batch.
- Cache size 1-page, small-cache, and normal-cache fixtures.
- Dirty eviction and pinning.
- WAL replay idempotence.
- Kill points at every commit step.
- Disk-full failure before live generation mutation.
- Compaction output equivalence.

Phase acceptance:

- Row/cell RAM is bounded by configured cache and scratch.
- Policy-content hash matches the in-memory backend for deterministic fixtures.
- Restart after every injected failure returns either the previous or new complete batch, never a partial batch.

## Phase 6: Replace monolithic checkpointing

### Step 6.1: Add checkpoint schema 6 manifest

The checkpoint manifest contains:

- model and workflow identity;
- backend schema;
- training metadata and schedule identity;
- committed batch and trajectory counts;
- immutable segment identities and hashes;
- WAL generation and committed offset;
- cache-independent state/row/page/cell counts;
- deterministic merge fingerprint;
- policy-content state hash where available.

### Step 6.2: Publish snapshots without copying live data

External-backend checkpoint:

1. Complete the current batch transaction.
2. Flush dirty pages and WAL.
3. Seal active segments if required.
4. Write and validate a checkpoint manifest.
5. Atomically publish it.
6. Retain all referenced immutable segments.

Do not duplicate the full database during routine checkpoint creation.

### Step 6.3: Separate portable archive export

If a movable checkpoint is required, provide an explicit archive command that copies/hard-links all referenced segments into a new directory and verifies hashes. It is not part of routine training checkpoints.

Why: portable copying has a different disk and time budget from crash-safe local resume.

### Step 6.4: Retain schema-5 compatibility

- Continue loading existing schema-5 checkpoints into the in-memory backend.
- Do not claim schema-5 compatibility for counts beyond its limits.
- Make schema/backend mismatch fail closed.

Tests:

- Manifest publication failure.
- Missing, truncated, extra, or hash-mismatched segment.
- Resume equivalence across compaction generations.
- Retention/reclamation of referenced segments.
- Archive copy equivalence.

Phase acceptance:

- Routine checkpoint scratch is below 1 GiB.
- Routine checkpoint time is proportional to dirty data and manifest size, not total database size.
- No checkpoint file has a 60 GiB monolithic limit.

## Phase 7: Stream final export from external storage

### Step 7.1: Add ordered external iteration

Perform a bounded k-way merge over row runs in canonical infoset order. For each row:

- load/pin the required compact pages;
- calculate average strategy into fixed scratch;
- quantize into fixed action scratch;
- write immediately through `MultiwayFullBlueprintStreamWriter`.

### Step 7.2: Shard very large blueprints

Add a manifest-based blueprint schema when estimated output exceeds a safe single-file bound:

- deterministic shard boundaries;
- per-shard hashes and counts;
- overall policy-content hash;
- atomic top-level manifest publication;
- lookup provider capable of mapping shards lazily.

### Step 7.3: Strengthen export preflight

Require:

- exact output estimate from row/action counts;
- temporary plus final publication space;
- system-cache allowance;
- process scratch;
- system reserve;
- no active compaction overlap unless explicitly budgeted.

Tests:

- In-memory and external export policy-content hash equivalence.
- Deterministic shard boundaries.
- Missing/corrupt shard rejection.
- Export disk-full cleanup.
- Process and system gate boundary tests.

Phase acceptance:

- Export process working set stays within the configured training/cache budget.
- Export does not materialize all rows or actions.
- Failed export leaves the prior published blueprint intact.

## Phase 8: Workflow integration and safeguards

### Step 8.1: Extend configuration ceilings only after backend support

Do not raise `MULTIWAY_WORKFLOW_MAX_PUBLIC_STATES`, rows, values, or disk ceiling until:

- external counters and file formats use 64-bit sizes;
- estimator and disk plan cover the requested values;
- synthetic boundary tests pass;
- the in-memory backend retains its existing conservative limits.

Why: configuration ceilings are not a substitute for storage support.

### Step 8.2: Add backend-specific preflight

Preflight must verify:

- storage path authorization;
- filesystem capacity and free bytes;
- atomic rename behavior on the target volume;
- required file-size support;
- process and cache budgets;
- existing store identity/provenance;
- no aliasing with bucket, blueprint, report, or checkpoint outputs;
- recovery status of any incomplete generation.

### Step 8.3: Extend fail-closed runtime monitoring

At every batch boundary and before checkpoint, compaction, and export:

- sample working set and private commit;
- sample system available memory;
- verify cache retained bytes;
- verify disk free space against the next operation;
- verify WAL/dirty-page bounds;
- abort before starting an operation that cannot finish safely.

### Step 8.4: Extend reports

Add:

- backend identity and schema;
- segment/run counts;
- disk live and retained bytes;
- compaction read/write amplification;
- WAL bytes;
- cache hit/miss/eviction counts;
- dirty peak;
- state/row/page/cell novelty per batch;
- checkpoint manifest and referenced bytes;
- projected capacity at configured target;
- hard-stop reason and phase.

Phase acceptance:

- All original provenance and memory guards remain active.
- External storage adds stronger disk and transaction gates.
- Missing required memory or disk telemetry blocks execution.

## Phase 9: Validation sequence

No 500K, 1M, 5M, or 50M job is authorized by this plan.

### Stage 9.1: Unit and synthetic tests

Run focused tests for:

- estimator arithmetic;
- bucket schema equivalence;
- external catalog;
- external compact storage;
- WAL recovery;
- checkpoint manifests;
- sharded export;
- workflow config/report contracts.

Hard stops:

- Any semantic difference.
- Any unbounded allocation.
- Any incomplete generation becoming current.
- Any arithmetic saturation not causing rejection.

### Stage 9.2: Full build and CTest

Use:

```powershell
scripts/codex_powershell.ps1 python scripts/python/full_build.py
```

Hard stop: any build or test failure.

### Stage 9.3: 1K deterministic differential pilot

Run the same seed, scheduler, worker count, and batch size using both backends.

Expected measurements:

- identical accepted/discarded counts;
- identical state, row, and value counts;
- identical deterministic merge fingerprint;
- identical final policy-content hash;
- checkpoint/resume equivalence;
- complete memory and disk ledger.

Hard stops:

- Any count or hash mismatch.
- Any discard.
- Any cache-limit violation.
- Any recovery mismatch.

### Stage 9.4: 10K cache and checkpoint pilot

Require at least two checkpoint generations and one deterministic compaction.

Expected measurements:

- process working set remains bounded as stored bytes grow;
- cache hit rate stabilizes;
- checkpoint manifest creation does not scale with total stored bytes;
- disk amplification remains below 3x live bytes;
- observed peak does not exceed the estimator.

Hard stops:

- Working set above 40 GiB.
- System available memory below 3 GiB.
- Private commit above 48 GiB.
- Disk amplification above 3x without an explained bounded phase.
- P99 batch time grows monotonically with database size.

### Stage 9.5: 60K prior-boundary pilot

This crosses the previous 58K failure only after all earlier stages pass.

Expected measurements per 1K trajectories:

- new public states;
- new rows/pages/cells;
- live and retained disk bytes;
- bytes per admitted object;
- read/write amplification;
- cache misses and latency;
- checkpoint and compaction duration;
- private commit and working set.

Hard stops:

- Working set at or above 44 GiB.
- Private commit at or above 56 GiB.
- System reserve guard reaches its 1.5 GiB boundary.
- Any configured logical or cache capacity reaches 70% without a reviewed next-stage plan.
- Projected disk requirement plus 2x compaction and final export exceeds available disk.
- Any discard, corruption, nondeterminism, or fail-open monitor.

### Stage 9.6: 100K and 250K sizing pilots

Fit at least:

- linear upper bound;
- power-law/Heaps model;
- finite occupancy saturation model.

Use the conservative upper confidence bound for disk and count planning. Do not select the most optimistic model.

Hard stops:

- Novelty shows no supported decline and conservative projected storage exceeds provisioned NVMe.
- Batch throughput falls below an agreed operational floor.
- Checkpoint/recovery time exceeds the permitted interruption window.
- Any safety gate approaches its boundary.

### Stage 9.7: Authorization gate

Before any 500K-or-larger workload, produce a review packet containing:

- source and binary provenance;
- all focused/full test results;
- measured novelty curves;
- conservative 50M state/row/cell bounds;
- RAM ledger and maximum observed working set;
- disk live/compaction/export budget;
- projected duration and write endurance;
- checkpoint/recovery evidence;
- exact stop conditions;
- explicit user authorization.

## 6. Proposed change sequence

Keep commits small and independently reviewable:

1. Correct estimator and add synthetic ledger tests.
2. Add schema-5 bucket artifact and compatibility tests.
3. Add storage interfaces with in-memory adapters.
4. Add external public catalog and recovery tests.
5. Add external compact accumulator storage and cache tests.
6. Add WAL and deterministic compaction.
7. Add checkpoint manifest schema.
8. Add external streaming/sharded export.
9. Add workflow configuration, provenance, reports, and disk gates.
10. Run focused tests, full build, and staged pilots separately.

Each implementation commit should use the `cpp-worker`, `cpp-unit-tests`, `cpp-code-review`, `git-commit-hygiene`, and `update-project-log` project skills as applicable.

## 7. Completion criteria

Implementation is complete only when:

- RAM usage is bounded by configuration rather than admitted cardinality.
- The measured process high-water is at most 44 GiB in the largest authorized pilot.
- Existing 48 GiB working-set and 1.5 GiB system-reserve gates remain unchanged.
- Disk preflight includes live data, WAL, compaction overlap, export, and reserve.
- Deterministic policy-content hashes match the in-memory reference for differential fixtures.
- Checkpoint recovery is atomic at every injected failure point.
- No trajectory is discarded.
- The conservative 50M disk and runtime projection fits provisioned hardware.
- A 50M run receives separate explicit authorization.

## 8. Current limitations

- No out-of-core mutable training backend exists yet.
- No measured saturation curve supports a final 50M cardinality estimate.
- The current bucket artifact consumes most of the startup working set.
- Current checkpoint state/row and file-size limits are too small for the observed trend.
- Current 100 GiB disk requirement is not a credible 50M storage envelope.
- This document does not authorize or execute a training job.
