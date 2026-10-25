# Bounded multiway checkpoint writer implementation plan

**Date:** 2026-10-25  
**Status:** Plan only. No implementation or qualification run is authorized by this document.  
**Baseline:** `f395523`; memory-guard baseline `08335f7`.

## Decision

Add a cold-path checkpoint serializer that holds the coordinator's existing exclusive immutable lease and writes schema-5 checkpoint bytes directly from the public catalog and active storage. Do not call `session.checkpoint()` in the production workflow.

Keep the materialized `checkpoint()`, `save_atomic()`, `save_atomic_sealed()`, and `load_verified()` APIs for compatibility and small tests. The new production API must produce byte-for-byte identical files, including the existing logical payload hash. The existing reader must load the streamed file unchanged.

Use a checkpoint-specific writer. Reuse `core::publish_atomic_replace`, portable little-endian encoding, fingerprint helpers, process/system memory snapshots, and the full-blueprint writer's transaction pattern. Do not adapt `MultiwayFullBlueprintStreamWriter`; its row schema, ordering state machine, and validation contract do not match checkpoint sections.

## Current allocation and retention map

The file writer is already bounded. `BufferedCheckpointStreambuf` and `CheckpointPayloadWriter` use a 64 KiB buffer and update the logical hash during encoding. The peak occurs before writing:

1. `examples/multiway_workflow_main.cpp` calls `session.checkpoint()`.
2. `MultiwayBlueprintTrainingSession::checkpoint()` calls `MultiwayBlueprintTrainer::checkpoint()` and retains the returned object in the workflow scope.
3. `MultiwayBlueprintTrainer::checkpoint()` copies `late_window_baseline_` and calls `MultiwaySolverCoordinator::checkpoint()`.
4. `MultiwaySolverCoordinator::checkpoint()` holds `traversal_mutex_`, reconstructs every public descriptor, exports all storage, and canonicalizes the copies.
5. `MultiwayPublicCatalog::export_descriptors()` creates one owning descriptor plus nested vectors per public state.
6. Compact `MultiwayCompactStorage::export_checkpoint()` allocates and retains all of the following at once:
   - copied row shapes;
   - copied active-page metadata;
   - copied raw `int32_t` regrets;
   - copied raw `uint64_t` strategy masses;
   - a full dense `double` regret array, including zeroes for inactive pages;
   - a full dense `double` strategy-sum array, including zeroes for inactive pages.
7. Float64 checkpoint canonicalization additionally allocates offset/order vectors and a second canonical storage owner before moving it into the result.
8. Public-state canonicalization allocates two hash maps, a depth vector, and ancestry scratch, then sorts the owning descriptors.
9. `checkpoint.validate()` scans the complete owner. `save_atomic_sealed()` then scans it again while writing.
10. The checkpoint remains alive until file measurement and the after-checkpoint snapshot. `checkpoint_buffers_` reports only the retained coordinator portion and excludes the copied late-window baseline.

The approximately 25.3 GB observed working set is consistent with the training baseline plus reconstructed public descriptors, native compact copies, dense Float64 compatibility arrays, canonicalization scratch, nested-vector overhead, and touched output/cache pages. The writer does not create a full file buffer.

## Resume-required payload

All current schema-5 fields remain required because the unchanged reader, validation, equivalence checks, or resume path consumes them:

- magic `MWTC0001` and schema value `5`;
- `MultiwayModelIdentity`;
- training batches, trajectories, seed, late-window start, schedule hash, pruning counters, visit counters, feature flags, and reserved byte;
- coverage counts and coverage street visits;
- coordinator terminal, leaf, missing-lookup, merged-stream, and street-visit counters;
- canonical public descriptors, including parent edge, betting state, board/runout, reconstructed public history, and legal menu;
- canonical row shapes;
- active compact page identities;
- raw compact regrets and raw compact strategy masses for lossless compact resume;
- dense Float64 regrets and strategy sums in canonical action-major row order;
- late-window baseline values;
- trailing logical payload hash.

The dense arrays are redundant for compact restore, but they are required by the current schema and loader cardinality checks. The streaming writer must synthesize them without owning them. Removing them requires a separately approved schema migration and is a stop condition for this task.

Runtime `MultiwayCheckpointTelemetry` is not serialized or hashed.

## Schema, ordering, validation, and hashing contract

Preserve the current byte sequence exactly:

1. magic and schema;
2. identity;
3. training metadata;
4. coverage base counters;
5. coverage street visits;
6. coordinator base counters;
7. coordinator street visits;
8. public-state count and canonical public states;
9. row-shape count and canonical shapes;
10. active-page count and canonical active pages;
11. compact regret count and values;
12. compact strategy-mass count and values;
13. dense regret count and values;
14. dense strategy-sum count and values;
15. late-window baseline count and values;
16. trailing hash.

Canonical public-state order is depth first, then `id.value`, matching `MultiwaySolverCoordinator::checkpoint()`. Canonical row order is public-state first, then seat. Active pages are row order, then page index. Values remain action-major within each row or page.

The payload hash is not a raw-byte hash. Preserve the current append order, including these exceptions:

- magic, schema, metadata `reserved`, and trailing hash are not hashed;
- coverage and coordinator street visits are written in the byte order above, but hashed after coordinator base counters in the current `payload_hash()` order;
- signed values are extended through `int64_t` to `uint64_t` before appending;
- doubles append their exact bit patterns;
- all vector lengths and logical narrow fields append as canonical `uint64_t` values.

Factor the current materialized and streaming encoders through the same field-level writer and hasher. Keep an independent small golden-byte fixture so shared helpers cannot make both paths wrong in the same way.

`load_verified()` needs no change to parse a streamed file. It already reads sequentially, validates schema/count limits/trailing data, reconstructs the materialized checkpoint, calls `validate()`, and checks identity, schedule, seed, and payload hash.

## Recommended architecture

### 1. Immutable checkpoint lease

Reuse the lock semantics of `MultiwaySolverCoordinator::ExportLease`. Add `acquire_checkpoint_lease()` returning the same immutable lease type, and extend the lease with read-only diagnostics and checkpoint views. Keep `acquire_export_lease()` unchanged.

The lease begins before planning and remains alive through close and atomic publication. This prevents traversal, merge, admission, checkpoint creation, and policy export from mutating or separately snapshotting the source. It adds no operation to traversal, merge, admission, or training loops.

### 2. Canonical bounded views

Add cold, non-owning checkpoint views:

- `MultiwayPublicCatalog::CheckpointView`
  - indexed packed-node access;
  - ID-to-node-index lookup;
  - legal-menu view;
  - parent access;
  - direct fixed betting/board/edge fields;
  - reusable fixed or bounded history scratch, maximum 512 entries.
- `MultiwaySparseRowStorage::CheckpointView`
  - metadata count and indexed metadata;
  - raw regret and strategy pointers for a metadata row;
  - no ownership.
- `MultiwayCompactStorage::CheckpointView`
  - ordered row count and row shape access;
  - ordered active-page metadata;
  - raw compact regret/mass pointers for one page;
  - page bucket/action counts and offsets needed to synthesize dense values.

Use explicit pointer/count views because the project is C++17. Raw pointers are non-owning and valid only while the lease lives.

Build only bounded metadata order scratch:

- public state: `uint32_t` node order plus `uint32_t` depth per state;
- Float64 rows: `uint32_t` row order;
- compact rows: no row-order allocation;
- one reusable public-history scratch buffer;
- one 64 KiB encoder block.

Reject counts above `uint32_t` before allocation. Do not create descriptor vectors, checkpoint vectors, dense compatibility arrays, or a full serialized buffer.

### 3. Exact planning and validation

Add `MultiwayTrainingCheckpointStreamPlan` with:

- schema and identity;
- public-state, shape, active-page, compact-value, dense-value, and baseline counts;
- exact serialized bytes;
- public-state order/depth bytes;
- Float64 row-order bytes;
- fixed history and encoder scratch bytes;
- total retained scratch bytes;
- backend kind and compact-direct-order flag.

Planning occurs under the lease and before temporary-file creation. It must:

- validate identity, training/coverage/coordinator counter consistency, and schedule/seed inputs;
- validate parent chains, root-first canonical order, menu handles, vector/count bounds, state field limits, and history depth;
- validate shapes, strict canonical uniqueness, action/bucket counts, active-page ordering/ranges, and exact value cardinalities;
- scan Float64 values and the late-window baseline for finite values;
- require nonnegative strategy sums where restore already requires them;
- validate compact source offsets and exact page coverage;
- calculate every variable descriptor byte and section byte with checked arithmetic;
- reject plans over the existing 60 GiB reader limit;
- calculate exact output bytes without hashing or opening `.tmp`.

For compact storage, stream raw compact arrays page by page in canonical page order. Stream dense arrays row/action/bucket order, writing zero for inactive pages and using the exact existing conversions:

```text
regret_double = static_cast<double>(stored_int32) / 1024.0
mass_double   = static_cast<double>(stored_uint64) /
                static_cast<double>(MultiwayCompactStorage::kMassScale)
```

For Float64 storage, sort only row indexes and stream the existing raw row slices in canonical order.

### 4. Separate checkpoint stream writer

Extend `multiway_training_checkpoint_artifact.cpp` with `MultiwayTrainingCheckpointStreamWriter`. It owns:

- destination and `<destination>.tmp` paths;
- RAII temporary cleanup;
- the existing 64 KiB buffered stream;
- exact byte count;
- incremental logical hash;
- section counts and a strict section-order state machine;
- optional test-only write, close, and publication failure injection.

The writer is constructed only after both gates admit. Its `finish()` must verify all planned counts and bytes, append the hash, flush, close, verify stream state and file size, publish atomically, and disarm cleanup only after successful publication.

Keep materialized `save_atomic*()` using the same field encoder. Do not remove it.

### 5. Trainer/session production API

Add:

```cpp
struct MultiwayTrainingCheckpointWriteResult
{
    MultiwayTrainingCheckpointStreamPlan plan{};
    std::uint64_t payload_hash = 0U;
    std::uint64_t serialized_bytes = 0U;
    std::uint64_t peak_scratch_bytes = 0U;
    core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
    MultiwayCheckpointWriteTelemetry telemetry{};
};

[[nodiscard]] MultiwayTrainingCheckpointWriteResult
MultiwayBlueprintTrainer::save_checkpoint_atomic(
    const std::filesystem::path& path) const;

[[nodiscard]] MultiwayTrainingCheckpointWriteResult
MultiwayBlueprintTrainingSession::save_checkpoint_atomic(
    const std::filesystem::path& path) const;
```

The trainer snapshots only scalar metadata and non-owning references under the lease. It streams `late_window_baseline_` directly. The session does not set `checkpoint_buffers_` because no checkpoint owner exists. Retain `checkpoint()` for compatibility callers and tests.

### 6. Checkpoint-specific gates and telemetry

Add checkpoint-specific plan, scratch, process-gate, system-gate, observed-delta, and write-telemetry types beside the export telemetry in `multiway_memory.hpp`.

Do not overload export field names. Reuse `MultiwayExportGateStatus`, `MultiwayProcessMemorySnapshot`, and `MultiwaySystemMemorySnapshot` where their semantics are identical.

Process estimate:

```text
estimated_process_checkpoint_peak
= fresh_current_working_set_after_plan_scratch
 remaining_encoder_and_stream_scratch
 explicit_checkpoint_headroom
 explicit_mapped_residency_allowance
```

System estimate:

```text
required_available_physical
= exact_serialized_checkpoint_bytes
 remaining_writer_scratch
 configured_system_reserve
```

The exact output allowance is system file-cache pressure. Do not add it to process-private memory.

Update startup preflight so `checkpoint_peak_bytes` is a configured worst-case bound for order indexes plus fixed writer scratch and headroom, not `estimated_blueprint_bytes`. Update the runtime ledger with exact checkpoint scratch/output/gate/observed values after each write.

All sampling, formatting, and telemetry updates remain at the checkpoint boundary.

## Write sequence and ownership

1. At a completed batch boundary, create the destination parent directory if needed.
2. Enter `save_checkpoint_atomic()` and acquire the exclusive checkpoint lease.
3. Copy only identity, training metadata, coverage, and coordinator scalar counters to small local values.
4. Build public-state and Float64 row order indexes. Compact storage uses direct row order.
5. Validate every source section and calculate the exact stream plan and byte count.
6. Capture a fresh process snapshot. The snapshot includes the retained order indexes.
7. Capture a fresh system-memory snapshot.
8. Evaluate the checkpoint process gate.
9. Evaluate the checkpoint system/file-cache gate.
10. If either gate blocks, release scratch and lease, report the reason, and return without creating, truncating, or removing `<destination>.tmp`.
11. Construct the checkpoint writer. This is the first operation allowed to create or truncate `<destination>.tmp`.
12. Emit fixed metadata and counters.
13. Emit public states by canonical node index using one reusable history scratch.
14. Emit shapes and active pages from non-owning views.
15. Emit compact raw values by page.
16. Emit dense regrets, then dense strategy sums, directly from Float64 rows or synthesized from compact pages.
17. Emit the late-window baseline directly.
18. Finalize the logical hash, verify counts and exact bytes, flush, close, and verify file size.
19. Publish with a strict atomic mode.
20. Capture after-write process/system snapshots, update telemetry, release the lease and scratch, and return the result.

At no point may a `MultiwayBlueprintTrainingCheckpoint`, `MultiwayCoordinatorCheckpoint`, descriptor vector, dense checkpoint array, or whole-file buffer exist in this path.

## Atomic publication and failures

Add a strict publication policy to `core::publish_atomic_replace` that permits only the initial same-filesystem rename or the checked Windows replacement operation. Under this policy, reject both direct-copy and backup-rename fallbacks before changing the destination.

Failure contract:

- planning or gate failure: no temp file is created; destination is unchanged;
- open, generation, validation, hash, count, byte, flush, close, or injected failure: RAII removes temp; destination is unchanged;
- unsupported atomic replacement: remove temp and fail; destination is unchanged;
- successful atomic replacement: destination contains the complete verified file and temp cleanup is disarmed.

Do not delete a pre-existing destination as a retry strategy. Tests must cover both missing and existing destinations.

## Exact files and symbols to change

### Core transaction

- `include/core/atomic_publish.hpp`
  - add a policy that rejects every non-atomic fallback.
- `src/core/atomic_publish.cpp`
  - fail before copy or backup rename under the strict policy; retain existing callers' behavior.

### Catalog, coordinator, and storage views

- `include/solver/multiway/engine/multiway_solver.hpp`
  - add public-state checkpoint view/scratch types;
  - add `MultiwayPublicCatalog::CheckpointView`;
  - add `MultiwaySparseRowStorage::CheckpointView`;
  - extend `MultiwaySolverCoordinator::ExportLease` with diagnostics and checkpoint views;
  - add `MultiwaySolverCoordinator::acquire_checkpoint_lease()`.
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
  - implement packed state access, parent/depth planning, direct state views, and reusable history reconstruction.
- `src/solver/multiway/engine/multiway_solver.cpp`
  - implement Float64 raw row views and checkpoint lease access;
  - keep `checkpoint()` and `restore_checkpoint()` unchanged except for sharing validation helpers when byte-neutral.
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
  - add compact row/page/value checkpoint views.
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
  - implement ordered page access and dense scalar/block emission helpers using current conversion statements;
  - keep `export_checkpoint()` for compatibility and equivalence tests.

### Checkpoint artifact and orchestration

- `include/solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp`
  - add stream plan/result/options and the checkpoint writer surface;
  - keep all existing materialized APIs.
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
  - share field encoders and logical hashing;
  - add exact planning helpers, section-state validation, bounded block writes, RAII temp cleanup, injected failures, and strict publication;
  - leave `load_verified()` byte contract unchanged.
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
  - add trainer/session `save_checkpoint_atomic()`;
  - add result/telemetry exposure;
  - retain `checkpoint()`.
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
  - orchestrate lease, planning, fresh snapshots, gates, writer, after snapshots, and ledger updates;
  - remove no compatibility API.

### Memory, workflow, and reports

- `include/solver/multiway/engine/multiway_memory.hpp`
  - add checkpoint scratch/plan/gate/observed telemetry and config;
  - add checkpoint fields to `MultiwayMemoryLedger` and `MultiwayWorkingSetMonitor`.
- `src/solver/multiway/engine/multiway_memory.cpp`
  - add checked checkpoint estimates and gate evaluation;
  - keep export gates unchanged.
- `examples/multiway_workflow_main.cpp`
  - replace `session.checkpoint()` plus `save_atomic_sealed()` with `session.save_checkpoint_atomic()`;
  - preserve interval behavior, timing, file-size reporting, and final process phase snapshots;
  - configure checkpoint process/system gates from the existing limits/reserve;
  - never create `.tmp` before the trainer gate.
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
  - report exact checkpoint plan, scratch, gates, before/after values, result bytes/hash, duration, publication mode, and failure reason.
- `include/solver/multiway/workflow/multiway_evidence.hpp`
- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
  - bump the training-report schema and accept the previous schema with absent checkpoint-gate fields defaulted, if report compatibility is required.
- `tests/cmake/test_multiway_workflow_boundary.cmake`
  - require plan, snapshots, both gates, and writer construction in that order;
  - reject `session.checkpoint()` in the production training workflow;
  - preserve streaming policy export assertions.

No CMake source-list change is needed if the writer stays in the existing artifact files.

## Tests

### Unit and artifact

- `tests/test_multiway_blueprint_training.cpp`
  - materialized versus streamed complete checkpoint bytes and payload hash for compact and Float64 backends;
  - unchanged `load_verified()` reads streamed files;
  - loaded checkpoint resumes and produces the same next-batch state and policy;
  - repeated streamed writes are deterministic;
  - checkpoint lease excludes mutation and policy export.
- `tests/test_multiway_compact_storage.cpp`
  - view output equals `export_checkpoint()` for shapes, pages, compact arrays, and dense arrays;
  - inactive pages emit exact positive zero bits;
  - action-major order, multi-page rows, reordered admissions, saturated regrets, and masses above `2^53` remain byte-equivalent.
- `tests/test_multiway_public_catalog.cpp`
  - packed checkpoint view equals `reconstruct()` for root, betting, chance, and street transitions;
  - canonical depth/id order and reconstructed history match the current checkpoint path;
  - malformed parent chains and count overflow reject during planning.
- Add checkpoint artifact cases to `tests/test_multiway_blueprint_training.cpp`, or create `tests/test_multiway_training_checkpoint_artifact.cpp` only if separation materially improves review:
  - checked-in small schema-5 golden bytes;
  - exact planned versus actual file size;
  - logical hash order, including unhashed fields and street-visit exception;
  - corrupt/truncated/trailing data rejection through the unchanged loader;
  - write, close, size, and publication failure injection;
  - temp cleanup and byte-identical previous destination after every failure;
  - gate rejection proves `.tmp` was never created.
- `tests/test_multiway_artifact.cpp`
  - strict core publication policy rejects fallback without changing the destination.

### Memory and report

- `tests/test_multiway_memory.cpp`
  - exact scratch arithmetic for zero, compact, and Float64 order indexes;
  - checked overflow and configured-cap bounds;
  - process admission at limit and rejection one byte above;
  - unavailable required metrics fail closed;
  - system output/cache allowance plus reserve;
  - output bytes are excluded from process-private estimate;
  - scratch capacity is independent of descriptor payload bytes and dense value count.
- `tests/test_multiway_qualification_reports.cpp`
  - all checkpoint plan/gate/result fields serialize and validate;
  - previous report schema remains readable when supported;
  - checkpoint and export telemetry remain distinct.
- `tests/test_multiway_p7_memory_contracts.cpp`
  - startup preflight uses bounded checkpoint scratch, not a materialized artifact estimate.

### Workflow, loader, and resume

- `tests/cmake/test_multiway_workflow_boundary.cmake`
  - production checkpoint call is streaming only;
  - admission ordering is lease, plan, fresh snapshots, process gate, system gate, writer;
  - streaming policy export remains unchanged.
- `tests/test_multiway_checkpoint_equivalence.cpp`
  - continuous versus streamed-checkpoint-resumed training preserves blueprint bytes/hash, coverage, street rows, visit/discard counters, merge fingerprint, and completed trajectories.
- Existing loader and report-reader tests remain in the focused set.

### Measured memory acceptance after focused validation

- checkpoint scratch allocation count must be independent of dense value count after order setup;
- no checkpoint retained buffer may scale with public descriptors, pages, or dense values;
- final checkpoint working-set increase target: no more than 1 GiB;
- matched total process peak target: no more than 20 GiB;
- output bytes and payload hash must match the materialized reference for the same state;
- policy-export bytes, hash, gates, and peak behavior must remain unchanged.

Do not run the large matched qualification until separately authorized.

## Memory bound and expected reduction

For compact production storage:

```text
checkpoint scratch
= public_state_count * (sizeof(uint32_t order) + sizeof(uint32_t depth))
 + fixed public-history scratch (at most 512 entries)
 + 64 KiB encoder buffer
 + stream/library state
 + explicit checkpoint headroom
```

Compact row order is zero bytes. Float64 adds `shape_count * sizeof(uint32_t)`.

Eliminated simultaneous owners include reconstructed descriptor vectors, nested descriptor allocations, copied shapes/pages, copied compact arrays, both full dense double arrays, public-state hash maps, and canonical storage copies. The output file remains a machine-wide cache consideration, not a process owner.

Expected peak is the fresh trained-process working set plus tens of MiB of indexes/buffers and explicit headroom. Based on the isolated streaming-export result, the intended matched result is near the 18.7 GB training/export baseline instead of approximately 25.3 GB. Qualification, not the estimate, decides acceptance.

## Phased implementation and commits

1. **Checkpoint byte contract tests**
   - Add golden schema-5 bytes, current materialized reference coverage, hash-order tests, and strict failure expectations.
   - Commit: `test(multiway): lock checkpoint byte and transaction contract`.
2. **Non-owning checkpoint views**
   - Add catalog, Float64, compact, and lease views with equivalence tests.
   - No artifact or workflow behavior change.
   - Commit: `feat(multiway): add bounded checkpoint source views`.
3. **Bounded artifact writer**
   - Add plan, shared field encoder/hasher, state machine, bounded block emission, RAII cleanup, and strict atomic policy.
   - Prove materialized/streamed bytes and unchanged loader behavior.
   - Commit: `feat(multiway): stream schema-5 training checkpoints`.
4. **Checkpoint memory gates**
   - Add exact process/system estimates, fail-closed gates, observed deltas, and ledger fields.
   - Commit: `feat(multiway): gate bounded checkpoint publication`.
5. **Trainer and workflow integration**
   - Add `save_checkpoint_atomic()`, replace the production materialized call, and preserve interval/resume behavior.
   - Extend the workflow boundary test.
   - Commit: `feat(multiway): publish checkpoints without materialization`.
6. **Reports and compatibility**
   - Add checkpoint plan/gate/result telemetry and compatible report reading.
   - Commit: `feat(multiway): report bounded checkpoint telemetry`.
7. **Focused and full validation**
   - Run the commands below, perform C++ review, update `docs/PLURIBUS_LOG.md` and the task tracker, then make one documentation-only commit if needed.
8. **Separately authorized qualification**
   - Run the matched checkpoint-enabled workload and Tracy evidence only after all stop conditions pass and explicit large-run authorization is provided.

## Validation commands

Focused Debug build:

```powershell
scripts/codex_powershell.ps1 python scripts/python/compact_build.py `
  --target test_multiway_blueprint_training `
  --target test_multiway_compact_storage `
  --target test_multiway_public_catalog `
  --target test_multiway_artifact `
  --target test_multiway_memory `
  --target test_multiway_p7_memory_contracts `
  --target test_multiway_checkpoint_equivalence `
  --target test_multiway_qualification_reports `
  --target texas_multiway_train
```

Focused tests:

```powershell
scripts/codex_powershell.ps1 ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_blueprint_training|test_multiway_compact_storage|test_multiway_public_catalog|test_multiway_artifact|test_multiway_memory|test_multiway_p7_memory_contracts|test_multiway_checkpoint_equivalence|test_multiway_qualification_reports|test_multiway_workflow_boundary)$"
```

Full repository validation:

```powershell
scripts/codex_powershell.ps1 python scripts/python/full_build.py
scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py
git diff --check
git status --short
```

Run the project `cpp-code-review` skill after the final implementation diff. Do not run the large qualification as part of these commands.

## Risks and unresolved questions

- Public-state order must match the current depth/id sort exactly. Admission order is not an acceptable substitute without a proof and byte-equivalence test.
- Compact raw arrays are stored by allocation offset while page metadata is canonical. The writer must follow canonical pages and dereference their source offsets; dumping the raw backing vectors can change bytes.
- Compact dense doubles must use the current floating conversions. Algebraic replacements can change bits.
- Holding the exclusive lease for the full disk write pauses mutation. This is acceptable for the synchronous batch-boundary workflow. Concurrent training plus checkpoint writing requires a different snapshot design and is out of scope.
- The unchanged loader still materializes a complete checkpoint during resume. This task bounds writing only. A bounded direct-restore reader is a separate project.
- Exact large-run checkpoint bytes/hash are not recorded in Task E because that run disabled checkpoints. Implementation must establish small byte equivalence first and record the first authorized matched reference later.
- Report schema evolution must not silently make existing acceptance evidence unreadable. Prefer an additive reader that accepts the prior training-report schema.
- Strict atomic replacement requires destination and temp on the same filesystem and an available atomic OS primitive. Unsupported environments must fail without publication.

## Stop conditions

Stop and report before continuing if any of the following is observed:

- schema-5 bytes or logical payload hash differ for identical state;
- the unchanged `load_verified()` cannot read and validate the streamed file;
- any required resume field must be removed or reinterpreted;
- a public descriptor vector, full storage checkpoint, dense value array, or whole-file buffer remains necessary;
- exact ordering requires full checkpoint materialization;
- strict atomic replacement cannot be provided while retaining the previous destination;
- any process or system gate would run after temporary-file creation or truncation;
- traversal, merge, admission, quantization, policy export, Task G capacities, or training hot loops must change;
- compact and Float64 reference checkpoint bytes cannot be made equivalent to the existing writer;
- a required memory metric is unavailable and the configured gate would not fail closed.

