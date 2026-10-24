# Multiway streaming full-blueprint export architecture

**Date:** 2026-10-24  
**Status:** Design only. No production implementation is authorized by this document.

## Decision

Replace the training workflow's materialized `MultiwayFullBlueprintArtifact` export with a schema-2 streaming writer. Generate one row into fixed reusable scratch, validate it, update the existing logical payload hash, encode it to the temporary file, and discard it. Publish only after every planned row was emitted and the stream closed successfully.

Keep the current materialized API for compatibility, but remove its deep-copy validation path. Do not change traversal, merge, Task G capacities, quantization arithmetic, schema-2 bytes, or loader behavior in the first implementation.

The production compact backend already stores row shapes in the required public-state-major, seat-major order. It can emit directly. The Float64 reference backend stores shapes seat-major, then public-state-major, so it needs a sorted vector of 32-bit metadata indexes. It does not need sorted row payloads.

## Evidence

The completed RelWithDebInfo run is recorded in `tmp/qualification/w8_50k_relwithdebinfo/report.json` and `report.json.log`.

| Quantity | Bytes | GiB |
| --- | ---: | ---: |
| Before-export working set | 17,925,812,224 | 16.695 |
| Pre-export process peak | 18,699,706,368 | 17.415 |
| After-export working set | 26,181,734,400 | 24.384 |
| Export process peak | 48,237,969,408 | 44.925 |
| Before-export private commit | 3,379,933,184 | 3.148 |
| After-export private commit | 12,390,854,656 | 11.540 |
| Output file | 3,921,243,000 | 3.652 |
| Preflight working-set estimate | 39,212,533,712 | 36.520 |
| Bucket mapping | 15,369,522,466 | 14.314 |

The report gives 1,361,416 admitted row shapes:

- 215,128 preflop shapes with 169 buckets;
- 421,198 flop, 338,889 turn, and 386,201 river shapes with 12 buckets.

Therefore the exported row count is exactly:

```text
215,128 * 169 + (421,198 + 338,889 + 386,201) * 12
= 50,112,088 rows
```

The schema-2 file size and in-memory telemetry imply 132,531,808 actions. The fixed file overhead is 184 bytes:

```text
184 + 50,112,088 * 28 + 132,531,808 * 19
= 3,921,243,000 bytes
```

The after-export subsystem snapshot reports:

- export live element bytes: 7,047,294,784;
- export retained capacity bytes: 7,667,360,088;
- export allocation count: 50,112,089.

The allocation count is one outer row buffer plus one action allocation for each of 50,112,088 rows. The retained-minus-live gap is 620,065,304 bytes. It is row/action vector capacity waste, excluding allocator metadata.

## A. Root-cause allocation map

### Source path

1. `MultiwayBlueprintTrainer::export_full_policy()` in `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp` expands every stored shape into one `MultiwayBlueprintRow` per bucket.
2. Each row owns `std::vector<MultiwayQuantizedRootAction> actions`.
3. The compact branch does not reserve the row action vector. Both branches grow the outer `artifact.rows` vector without an exact reserve.
4. The trainer sorts all 50,112,088 full row objects, hashes them, then calls `artifact.validate()`.
5. `MultiwayFullBlueprintArtifact::validate()` constructs `MultiwayBlueprintStore(identity, rows)`. The constructor parameter is by value, so the const lvalue `rows` is deeply copied.
6. `MultiwayBlueprintStore` sorts the copy, validates it, then allocates a flat `RuntimeRow` vector and a second flat action vector.
7. `MultiwayFullBlueprintArtifacts::save_atomic()` performs `auto sealed = artifact`, causing another deep copy of all rows and action vectors.
8. `sealed.validate()` repeats step 5 and step 6 while the original artifact and `sealed` are both alive.
9. Serialization walks `sealed` and writes the schema-2 bytes. It does not allocate a full file buffer.

The path also makes four complete logical hash passes: trainer sealing, trainer validation, save sealing, and save validation. It sorts full rows once in the trainer and sorts copied rows again in each validation. Serialization is a fifth full traversal. These passes are primarily export-duration costs, while the nested copies and flat store drive the peak.

### Simultaneous ownership at the save-time peak

The following uses the observed counts and the current x64 sizes: `MultiwayBlueprintRow` 56 bytes, `MultiwayQuantizedRootAction` 32 bytes, and `MultiwayBlueprintStore::RuntimeRow` 48 bytes.

| Owner | Calculation | Bytes | GiB | Lifetime |
| --- | ---: | ---: | ---: | --- |
| Training process before export | measured | 17,925,812,224 | 16.695 | Whole export |
| Original artifact row objects | 50,112,088 * 56 | 2,806,276,928 | 2.614 | Generation through workflow return |
| Original artifact action payload | 132,531,808 * 32 | 4,241,017,856 | 3.950 | Generation through workflow return |
| Original artifact unused capacity | measured retained minus live | 620,065,304 | 0.578 | Generation through workflow return |
| `sealed` deep-copy live storage | rows plus actions | 7,047,294,784 | 6.563 | Whole `save_atomic()` |
| Validation parameter deep copy | rows plus actions | 7,047,294,784 | 6.563 | `sealed.validate()` |
| Validation flat runtime rows | 50,112,088 * 48 | 2,405,380,224 | 2.240 | `sealed.validate()` |
| Validation flat actions | 132,531,808 * 32 | 4,241,017,856 | 3.950 | `sealed.validate()` |
| Stream/file buffer | implementation-sized, normally KiB | negligible | negligible | Serialization |

The known simultaneous heap and baseline total is about 46.33 GB, or 43.15 GiB. The measured peak is 48.24 GB, or 44.93 GiB. The 1.90 GB decimal residual includes allocator metadata and fragmentation from roughly 150 million simultaneously live per-row action allocations, outer-vector growth history, temporary strategy vectors, and working-set variation while the file and mapped bucket pages are touched.

This ownership graph explains the peak without treating the 3.65 GiB file as an in-process serialization buffer.

### Other contributors

- Sorting does not deep-copy action payloads because row/vector moves transfer ownership, but sorting 50.1 million rows is expensive and the outer vector can briefly overlap old and new row-object buffers during growth.
- `average_strategy()` returns a new vector per emitted row. This is bounded in peak size but causes 50.1 million repeated temporary allocations.
- `payload_hash()` is allocation-free and linear. It is called before validation and again during validation.
- `row.validate()` is allocation-free.
- `std::ofstream` buffering is not a material RAM owner.
- The 14.31 GiB bucket mapping is already resident in the before-export baseline because eager assignment validation touched it. It remains an important baseline owner but is not an export-created copy.
- Windows file cache can retain up to a material fraction of the 3.65 GiB temporary write. That affects machine-wide use and can explain why system use approached 63 GiB. It must not be labeled process-private memory or added blindly to process working set.

## B. Streaming-export design

### API boundary

Add a cold-path API that writes directly from a stable coordinator snapshot:

```cpp
struct MultiwayFullBlueprintExportPlan
{
    std::uint64_t row_count;
    std::uint64_t action_count;
    std::uint64_t serialized_bytes;
    std::uint64_t order_index_bytes;
};

struct MultiwayFullBlueprintExportResult
{
    MultiwayFullBlueprintExportPlan plan;
    std::uint64_t payload_hash;
    std::uint64_t peak_scratch_bytes;
    core::AtomicPublishMode publication_mode;
};

MultiwayFullBlueprintExportResult
MultiwayBlueprintTrainingSession::save_full_policy_atomic(
    const std::filesystem::path& path);
```

`examples/multiway_workflow_main.cpp` must call this API instead of:

```cpp
const auto artifact = session.export_full_policy();
MultiwayFullBlueprintArtifacts::save_atomic(output_path, artifact);
```

Keep `export_full_policy()` for small tests and compatibility callers. It is no longer used by the production training workflow.

### Snapshot and mutual exclusion

Add a movable coordinator read lease that owns a `std::unique_lock<std::shared_mutex>` on `MultiwaySolverCoordinator::traversal_mutex_` for plan construction and emission.

The lease exposes only cold export operations:

- row-shape count and indexed row-shape access;
- non-owning legal-menu pointer and count for a public state;
- `average_strategy_into()` for a supplied fixed buffer;
- backend kind and current storage identity.

Checkpoint already takes the same mutex exclusively in `MultiwaySolverCoordinator::checkpoint()`. A unique export lease therefore makes training mutation, checkpoint creation, and another export mutually exclusive. Do not use `find_public_state()` inside the lease because it would try to acquire the same mutex again. Access packed catalog nodes and menu storage through a private no-lock epoch method.

Holding the lock for the file write is intentional. It provides one coherent transaction without copying solver state. Export is a cold workflow boundary after worker completion. If later requirements need concurrent training and export, that is a separate snapshot/versioning design and will require additional memory.

### Ordering

The required serialized order is:

```text
public_state_id, seat, bucket
```

`action_menu_id` is functionally determined by the public state. Validation can include it as the final uniqueness field, matching `MultiwayBlueprintStore`.

The production compact backend maintains shapes with `checkpoint_infoset_less`, which is public-state-major then seat-major. Buckets are already generated from zero upward. It can emit directly with no order buffer.

The Float64 reference backend maintains metadata using `MultiwayInfosetId::operator<`, which is seat-major then public-state-major. For that backend:

1. Allocate `std::vector<std::uint32_t>` with one ID per stored shape.
2. Fill it with `0..shape_count-1`.
3. Sort IDs by public state then seat.
4. Iterate the referenced shapes and buckets.

Observed order memory is:

```text
1,361,416 * 4 = 5,445,664 bytes = 5.19 MiB
```

At the configured five-million-shape limit it is 20,000,000 bytes, or 19.07 MiB. This is sufficient. Sorting full row payloads, external merge sorting, and a 50-million-entry row index are unnecessary.

### Exact plan before opening the temporary file

While the lease is held, scan ordered shapes once and use checked arithmetic to compute:

```text
row_count       = sum(shape.bucket_count)
action_count    = sum(shape.bucket_count * shape.action_count)
serialized_size = 184
                + row_count * 28
                + action_count * 19
```

During the same pass validate:

- nonzero public state, valid seat, nonzero bucket and action counts;
- public state exists in the packed catalog;
- legal-menu count equals `shape.action_count`;
- menu is nonempty and has a nonzero menu ID;
- each action's index and menu ID are canonical;
- ordered shape keys are strictly increasing;
- row, action, serialized-size, and index counts do not overflow.

This pass provides an exact disk requirement and the export memory-gate inputs before `.tmp` is opened.

### Quantization

Use reusable stack scratch sized by `MULTIWAY_MAX_ABSTRACTED_ACTIONS`, currently eight:

```cpp
std::array<Probability, MULTIWAY_MAX_ABSTRACTED_ACTIONS> strategy;
std::array<MultiwayQuantizedRootAction, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions;
```

Add non-allocating `average_strategy_into()` methods to both storage backends. Their arithmetic must be statement-for-statement equivalent to current `average_strategy()`:

- CompactInt32 must convert mass using `kMassScale`, sum the current integer masses, normalize through the same double operations, then run the compact export's second total and clamp sequence.
- Float64 must clamp negative strategy sums to zero, use the same uniform fallback, normalize by the same double total, then run its existing floor and clamp sequence.

Do not replace either path with an integer-ratio shortcut. A mathematically equivalent formula can cross a `floor()` boundary and change bytes.

Factor the two existing quantization loops into tested cold-path helpers only after equivalence tests exist. Traversal's `regret_matched_strategy_into()` and merge kernels remain unchanged.

### Incremental payload hash

The schema-2 payload hash is not a hash of raw file bytes. It appends each logical field to FNV-1a as a canonical `uint64_t`, including narrow serialized values. Preserve that contract exactly.

Create a small `MultiwayFullBlueprintPayloadHasher` used by materialized and streaming paths:

1. `begin(schema, identity, training, row_count)` performs the existing header appends in the current order.
2. `append_row(row_view)` appends public state, signed seat cast, bucket, menu ID, action count, each action descriptor, and probability in the current order.
3. `finish()` calls the existing fingerprint `finish()` once.

For each streamed row, validate it, append its logical fields to the hasher, then encode the same fields to the output stream. After the exact planned row count is reached, finalize and serialize the hash as the last eight bytes.

Do not hash the raw 8/16/32-bit binary bytes. That would change every payload hash even if the file body stayed identical.

### Streamed validation

Validation remains at least as strong without constructing `MultiwayBlueprintStore`:

- validate schema and identity before file creation;
- validate all shape/menu relationships during planning;
- validate each generated row's completeness, action indexes, menu IDs, positive probabilities, and exact 65,535 sum;
- compare each full row key with the previous emitted key to prove strict order and uniqueness;
- count rows and actions and compare against the plan;
- recompute the full logical payload hash incrementally;
- verify output byte count equals `plan.serialized_bytes` before publication;
- close the stream and verify its state;
- optionally open the temporary file in a bounded verification pass for tests and qualification, but do not reload 50 million rows in production.

The planning pass plus strict emitted-key check replaces the current sort-and-construct-store validation. Identity and payload hash checks are not weakened.

For the compatibility `MultiwayFullBlueprintArtifact::validate()` path, validate rows in place. If rows are not already sorted, use a 32-bit row-index vector to detect duplicates without copying row payloads. Continue accepting unsorted caller artifacts because the current validation sorts a copy rather than rejecting them.

### Binary writer and buffering

Keep magic `MWFB0002`, schema version 2, field widths, little-endian order, row order, and trailing payload hash unchanged.

Use one bounded 64 KiB or 1 MiB output buffer and bulk `ostream::write()` calls. The current portable helpers call `put()` once per byte, which is correct but needlessly expensive for a 3.65 GiB file. A bounded little-endian encoder can improve throughput without changing bytes. Prove byte equivalence before enabling this optimization.

### Atomic publication and cleanup

The writer owns `<destination>.tmp` through an RAII cleanup guard.

1. Perform plan validation and memory/disk gates before opening the temp file.
2. Open the temp file with truncation.
3. Emit header, rows, actions, and trailing hash.
4. Verify counts, byte count, stream close, and final stream state.
5. Call `core::publish_atomic_replace(temporary, destination, ...)` only after all checks pass.
6. Disarm temp cleanup only after publication succeeds.
7. On generation, validation, hash, write, close, or publication failure, remove the temp file and leave the previous destination untouched.

`core::publish_atomic_replace()` contains `CopyFallback` for a missing destination. Copying directly to the destination is not atomic if the fallback copy itself fails. The implementation phase must either prove that full-blueprint publication cannot select this mode, or add a strict publication mode that fails instead of using `CopyFallback`. Do not silently claim transactional publication while allowing a partial destination.

## Export-phase memory gate

The startup preflight is not export protection. Add a separate gate immediately after acquiring the export lease and building the exact plan.

### Process estimate

Capture a fresh `MultiwayProcessMemorySnapshot` at `before_export`. Estimate:

```text
estimated_process_export_peak
= current_working_set
 order_index_capacity
 fixed_strategy_and_row_scratch
 bounded_encoder_buffer
 measured allocator/export headroom
 explicit additional mapped-residency allowance
```

For the production compact backend, `order_index_capacity` is zero. For the observed reference-sized shape count it is only 5.19 MiB. Start with a conservative, explicit 64 MiB exporter headroom until matched runs establish a smaller bound. Record estimated and observed deltas rather than hiding the margin in `allocator_overhead_bytes`.

Compare this fresh estimate against `process_working_set_limit_bytes`. Fail closed when an enabled required metric is unavailable, consistent with the existing runtime monitor.

### Machine-wide write-cache gate

On Windows, add a cold-path system-memory snapshot using `GlobalMemoryStatusEx`. Record available physical memory before export. Treat `plan.serialized_bytes` as a conservative dirty-file-cache allowance plus an explicit system reserve. Reject or require operator override when:

```text
available_physical
< serialized_bytes + exporter_scratch + configured_system_reserve
```

Report this separately from process working set and private commit. Windows page cache is reclaimable system memory, not process-private storage. Do not add it to the process ledger.

### Ledger correction

Split the current single startup estimate into:

- training admission estimate;
- checkpoint transient estimate;
- streaming-export scratch estimate;
- exact planned output bytes;
- process export-gate estimate from a fresh snapshot;
- system file-cache allowance and available physical bytes;
- observed before/after/peak process values.

The current `estimate_full_blueprint_bytes()` computes one hypothetical materialized artifact as:

```text
maximum_sparse_rows * (sizeof(row) + 64 * sizeof(action))
= 10,520,000,256 bytes
```

It is then counted once as `export_peak_bytes`. The estimate misses:

- bucket expansion from 1.36 million shapes to 50.11 million rows;
- the original artifact's vector capacity;
- `save_atomic`'s deep copy;
- `validate()`'s deep copy;
- validation's flat runtime-row and action arrays;
- the 1.97 GB growth between startup preflight and the actual before-export snapshot;
- machine-wide dirty file-cache pressure.

Although the 64-action assumption overestimates one logical artifact, counting only one artifact underestimates the simultaneous ownership graph. Replace this conflated value with phase-specific estimates.

## C. Files, symbols, and tests to change

### Production files

- `include/solver/multiway/blueprint/multiway_artifact.hpp`
  - Add export plan/result types and the bounded atomic stream-writer surface.
  - Keep schema version 2 and the existing materialized API.
- `src/solver/multiway/blueprint/multiway_artifact.cpp`
  - Add shared incremental hasher and row encoder.
  - Add streamed row validation and RAII temp cleanup.
  - Remove `auto sealed = artifact` from the materialized writer.
  - Replace `MultiwayBlueprintStore(identity, rows)` validation with non-owning validation plus a bounded index only when required.
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
  - Add `save_full_policy_atomic()` to trainer/session.
  - Retain `export_full_policy()` for compatibility and tests.
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
  - Add plan construction, direct compact ordering, reference order IDs, fixed scratch generation, exact quantization, and streamed emission.
  - Stop using `sort_blueprint_rows()` in the production workflow.
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
  - Add allocation-free cold `average_strategy_into()` with unchanged arithmetic.
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
  - Add the exclusive export lease, no-lock packed-menu view, and Float64 `average_strategy_into()`.
  - Do not change traversal, merge, admission, or Task G capacities.
- `examples/multiway_workflow_main.cpp`
  - Replace materialize-then-save with `save_full_policy_atomic()`.
  - Run the exact export gate immediately before temp creation.
  - Record result bytes, hash, duration, and gate telemetry.
- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
  - Add export-gate inputs/results and Windows system-memory sampling.
  - Keep process, mapping, and system cache categories separate.
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
  - Serialize exact export plan, scratch estimate, fresh gate result, system allowance, publication mode, and observed export delta.
  - Version the report schema if readers require exact field sets. This does not require an artifact schema migration.

### Tests

- `tests/test_multiway_artifact.cpp`
  - golden schema-2 bytes;
  - old materialized versus streamed bytes and hash;
  - incremental hash equivalence;
  - duplicate, unsorted, malformed, identity, and corruption rejection;
  - injected write/close failure and temp cleanup;
  - existing destination remains byte-identical after failure;
  - loader reads streamed output.
- `tests/test_multiway_blueprint_training.cpp`
  - exact compact and Float64 quantization equivalence;
  - direct compact ordering and reference index ordering;
  - streamed versus `export_full_policy()` on the same trained state;
  - checkpoint/export exclusion and repeated export determinism.
- `tests/test_multiway_compact_storage.cpp`
  - vector-returning versus `_into` strategy bits for empty, uniform, and populated pages.
- `tests/test_multiway_memory.cpp`
  - exact index/scratch arithmetic, saturation, current-snapshot gate, system allowance, and hard-limit boundaries.
- `tests/test_multiway_qualification_reports.cpp`
  - new export telemetry schema and validation.
- `tests/cmake/test_multiway_workflow_boundary.cmake`
  - production workflow calls the streaming path and retains phase/report output.

## D. Memory estimate and target

### Current observed export

| Phase component | GiB |
| --- | ---: |
| Fresh before-export working set | 16.695 |
| Original artifact retained capacity | 7.141 |
| `sealed` copy live storage | 6.563 |
| Validation copy and flat store | 12.753 |
| Known simultaneous total | 43.152 |
| Unattributed allocator/paging residual | 1.773 |
| Measured process peak | 44.925 |

### Streaming production compact backend

| Component | Expected bound |
| --- | ---: |
| Order index | 0 bytes |
| Strategy and quantized action scratch | less than 1 KiB |
| Row encoder buffer | 64 KiB to 1 MiB |
| Stream/library state | less than 1 MiB |
| Explicit initial export headroom | 64 MiB |
| Materialized rows/actions | 0 bytes |
| Artifact deep copies | 0 bytes |
| Validation flat store | 0 bytes |

For the Float64 fallback, add 5.19 MiB at the observed shape count or 19.07 MiB at the configured maximum.

Expected process working set is approximately the fresh before-export value plus tens of MiB. Qualification acceptance should use:

- primary target: export-phase working-set increase no more than 1 GiB;
- target total process peak: no more than 20 GiB on the matched 50K workload;
- stretch target: export does not exceed the existing 17.42 GiB training peak;
- retained export buffer after completion: no full-artifact capacity, only bounded writer telemetry;
- allocation count: independent of emitted row count after lease/order setup.

The 3.65 GiB temporary output can still increase machine-wide cache use. The separate system gate and matched system telemetry must qualify that effect.

## E. Ranked memory-reduction backlog

### 1. Cheap measured wins

1. Remove `save_atomic`'s deep copy. Saves about 6.56 GiB live at this workload.
2. Replace store-building validation with non-owning row checks. Saves about 12.75 GiB during each validation.
3. Stop materializing rows in the training workflow. Saves 7.14 GiB retained and about 50.1 million allocations.
4. Add `_into` average-strategy APIs. Removes about 50.1 million sequential temporary vector allocations and should reduce 148.7-second export time.
5. Use a bounded bulk binary encoder. It targets per-byte stream-call overhead, not peak RAM.

Items 1 through 4 should land together behind equivalence tests because the final architecture makes their ownership boundaries explicit.

### 2. Structural wins after streaming export

1. Add a streaming verified loader that builds `MultiwayBlueprintStore`'s flat runtime rows/actions directly from schema-2 bytes. Current load materializes nested rows, validates by creating a temporary store, and callers can create another store.
2. Expose packed public-menu views at cold boundaries. Current export reconstructs an entire descriptor, including unrelated history, board, betting, and nested vectors, for each shape.
3. Replace nested on-disk load ownership with flat contiguous numeric storage while keeping schema-2 compatibility. This benefits deployment and lookup qualification, not training export.
4. Revisit bucket assignment validation residency as its own measured task. It accounts for the 14.31 GiB baseline mapping and must preserve eager corruption detection.
5. Add mid-export working-set/system samples at bounded row intervals. Sampling must not allocate or format per row.

### 3. Lower-priority measured cleanup

- The materialized compatibility path can reserve exact row count before generation and exact actions per row. This reduces its 620 MB observed capacity waste but does not solve its fundamental size.
- Public-catalog and loader vector capacities should be changed only where subsystem snapshots show retained-minus-live waste.
- PMR or a pool is justified only for a remaining clearly bounded cold lifetime after streaming. It is not needed to manage rows that no longer exist.

### Rejected ideas

- Do not retain all rows in a small-buffer vector. Fifty million row objects alone consume about 2.61 GiB, so SBO only removes some action allocations.
- Do not sort full rows or write an external sorted row run. Shape metadata is already sufficient.
- Do not memory-map the output for writing. Dirty mapped pages would inflate process working set and obscure the file-cache boundary.
- Do not hash raw serialized bytes. It changes the schema-2 payload hash contract.
- Do not quantize directly from integer mass ratios without bit-equivalence proof.
- Do not use `shrink_to_fit` in training or export. It adds copies and does not provide a deterministic release contract.
- Do not change Task G capacities. The measured stream capacities are unrelated to this export peak.
- Do not add allocator replacement or Windows heap tuning before Tracy proves fragmentation remains material after full-row ownership is removed.
- Do not use glibc allocator settings on Windows.
- Do not clear the bucket working set with OS trimming calls. That shifts cost and weakens predictable residency rather than fixing ownership.

## F. Regression and validation plan

### Correctness tests

1. Produce one artifact with the current materialized reference encoder and one with the streamed encoder from the same small coordinator state. Compare complete file bytes and payload hashes.
2. Compare a checked-in schema-2 golden byte sequence so shared new helpers cannot make both paths wrong in the same way.
3. Export the same state repeatedly and compare bytes, hash, row count, action count, and size.
4. Cover compact direct order and Float64 order-vector order.
5. Compare every quantized probability between materialized and streamed export, including zero-mass uniform rows and clamp boundary cases.
6. Load streamed output through the unchanged `load_verified()` and use it through `MultiwayBlueprintStore` lookup.
7. Corrupt header, identity, row key, action index, menu ID, probability sum, body byte, trailing hash, and trailing data. Preserve current rejection behavior.

### Transaction tests

1. Inject stream failure before header completion, mid-row, before hash, and on close.
2. Start with a known-good destination.
3. Assert destination bytes are unchanged and no `.tmp` remains.
4. Inject publication failure and assert the same.
5. Exercise Windows replacement of an existing destination.
6. Reject or separately fix `CopyFallback` before claiming strict atomic publication for a missing destination.

### Memory tests and telemetry

1. Unit-test that stream scratch capacity is independent of emitted row count.
2. Unit-test observed and maximum order-index bytes.
3. Record before-export, bounded during-export, and after-export process working set/private commit.
4. Record available physical memory and the file-cache allowance separately.
5. Repeat the matched 8-worker, 50-batch, 50,000-trajectory RelWithDebInfo run at least three times.
6. Require output bytes and hash to match the existing 3,921,243,000-byte reference artifact for the same deterministic state.
7. Require no capacity exhaustion, no discards, unchanged fingerprint, and unchanged Task G capacity reporting.
8. Capture one Tracy run to confirm row-count-independent allocation count and no hidden full-size owner.

### Build and review sequence

1. Focused build and tests for artifact, blueprint training, compact storage, memory, report, and workflow boundary targets.
2. `scripts/codex_powershell.ps1 scripts/python/full_build.py`.
3. Full CTest through the repository compact CTest script.
4. `git diff --check`.
5. Final C++ review with the project `cpp-code-review` skill.
6. Record measured old/new bytes, hashes, peak working set, private commit, system available-memory minimum, allocation count, and duration in `docs/PLURIBUS_LOG.md`.

## Implementation stop conditions

Stop and report instead of merging if any of these occurs:

- schema-2 bytes or payload hash change for an identical state;
- compact and reference quantization differ from the existing exporter;
- loader compatibility breaks;
- previous destination can be partially replaced after any failure;
- export is not mutually exclusive with checkpoint and training mutation;
- emitted order requires full row materialization;
- export peak moves into another unbounded buffer;
- traversal or merge hot paths gain allocations, logging, strings, virtual dispatch, or hash lookup;
- Task G capacities or capacity-exhaustion behavior change.
