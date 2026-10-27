# PB-12 External export concurrency and Tracy investigation plan

Date: 2026-10-27
Status: Static analysis and future execution plan. No speedup or production policy equivalence demonstrated.

## Scope and evidence

Inspected branch `PB-12`, HEAD `4f0874ee77384ab7c02e1ecb55adb85a05e79537`, including relevant uncommitted source. Read `PB12_EXTERNAL_EXPORT_PERFORMANCE_HANDOFF.md` first, then repository instructions, `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`, relevant source, current CMake caches, existing script text, and preserved partial logs.

The user authorized creation of this one report as the sole exception to read-only mode. No builds, tests, benchmarks, dependency installation, configuration changes, solver/export launches, captures, staging, or commits were performed. Historical verification below comes from the handoff, not this investigation. No pre-existing source, artifacts, outputs, or project-log files were edited.

The interrupted run is `tmp/pb12_external_export_43c6672df6304e0c86f3fd87e8f6fe04`. Its final report is empty, exit code is -1, and its preserved `policy.mwfb.tmp` is 48,562,176 bytes. It is incomplete and must not be treated as a policy.

Last flushed streaming observation:

| Metric | Recorded value |
|---|---:|
| Elapsed streaming | 7,597,634,696,100 ns |
| Inclusive page-store lookup | 7,594,407,430,400 ns |
| Pages / rows / actions | 40,960 / 613,611 / 1,618,613 |
| Encoded bytes | 47,934,931 |
| Index opens / reads | 334,554,606 / 627,401,739 |
| Data reads / decoded pages | 1,839 / 1,839 |
| Synthesized zero pages | 39,121 |
| Index open / search time | 3,654,265,916,700 / 2,467,835,855,000 ns |

This identifies repeated record-run lookup, including absence checks, as the measured hot path in that partial attempt. It does not measure the latest range-pruning change or predict completed runtime. Nested timings overlap. Stream operations and requested bytes are not physical disk I/O.

The watchdog summary reports peak working set 22,958,833,664 bytes, peak private bytes 7,663,706,112, minimum free physical memory 30,981,726,208, and unchanged protected hashes. These sampled historical values do not prove capacity for extra workers or a collector.

## 1. Straightforward parallelism verdict

Adding workers around the existing page-pin or row-export APIs would serialize the dominant lookup work.

- `src/solver/multiway/engine/multiway_external_storage.cpp:2058`: ordinary lookup holds the record-store mutex while updating the cache/statistics and scanning runs.
- `:2068`: profiled lookup does the same through index opens, binary searches, and matching data reads.
- `:3610`: profiled pin holds the page-cache mutex through cache search, victim selection, record-store lookup, checksum/decode or zero synthesis. Shared `lookup_scratch_`, page slots, victim state, pin counts and statistics require synchronization.
- `:3718`: unpin takes that same page-cache mutex and scans for the page. A long miss can delay another worker's unpin.
- `:2113`: ordered row-record iteration holds its own record-store mutex across every callback. This is a different component from the page/catalog stores, but callbacks and observers must not re-enter that row store.
- `include/solver/multiway/engine/multiway_solver.hpp:1093`: ExportLease holds the coordinator traversal mutex exclusively, excluding coordinator mutations during export. Keep that lease alive until all readers and the writer finish; it is not permission to bypass component lifetime rules.

A serial reader plus parallel averaging/quantization could overlap some compute, but leaves the dominant lookup serialized. The existing partial evidence gives no basis for expecting useful benefit from that arrangement. Changing training worker count does not parallelize the serial export visitor.

Replacing mutexes with shared mutexes alone is insufficient: lookup mutates cache/statistics, and pin mutates slots and shared scratch. Unlocking around I/O alone needs explicit reserved/loading slot states and generation validation.

## 2. Ordering and correctness constraints

`multiway_external_storage.cpp:2108` merges sorted run cursors by key, resolves duplicates to the newest generation, and validates strict run ordering plus index/data key agreement. It fails closed when that component's run count exceeds configured compaction fan-in. This ordered-iteration limit does not bound random page lookup fan-out.

`:3264` emits ascending public states and seats. The trainer at `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:1455` looks up/validates a menu per shape, visits pages, then buckets in order. It unpins on both success and exceptions. Record callback payloads are borrowed only for the callback duration (`include/solver/multiway/engine/multiway_external_storage.hpp:255`); bindings are also callback-local objects. Copy validated work descriptors before asynchronous dispatch.

`src/solver/multiway/blueprint/multiway_artifact.cpp:289` defines row ordering by public state, seat, bucket and action-menu ID. `:963` maintains strict previous-key checks, counts, two rolling hashes, and one buffered encoder. Hash initialization/appending at `:730` and `:754` is order-dependent. Concurrent writer calls, completion-order output, or naïvely combining worker hashes are invalid.

Keep one ordered commit owner for the existing writer/hash/encoder. Workers must own output storage until consumed; they cannot enqueue views into reused stack arrays or unpinned pages. Keep the exact per-row averaging, zero-mass behavior, action order, tie-breaking, quantization and floating-point operation order. Parallelize independent rows/pages rather than reductions within a row.

Finish at `multiway_artifact.cpp:1044` checks counts and serialized size, appends the checksum, flushes/closes, then publishes atomically. Sharded export (`:1217`) also has a global ordered policy hash and fixed shard boundaries. Parallel shard writing is a separate design, not a drop-in change.

## 3. Lookup and concurrency candidates

Evaluate in this order, with separate correctness and performance evidence.

| Candidate and location | Opportunity | Correctness, determinism and resource risks |
|---|---|---|
| Measure current endpoint pruning, `multiway_external_storage.cpp:2084,2224` | Avoids opening runs outside the requested key range while preserving reverse-generation search. | Still scans all run metadata on a miss. Broad overlapping ranges can leave most opens intact. No production measurement exists for this version. |
| Immutable flat membership or key-location directory, `:2241`, Run definition in header `:277` | An export-session sorted table of unique key to newest run/ordinal can answer absence and avoid repeated searches. Particularly relevant to the many zero pages observed. | Build only from the verified active manifest. Retained checkpoint files are not necessarily active runs. Resolve duplicates newest-first, preserve errors/checksums, validate key/offset provenance, and invalidate on generation/restore changes. Budget construction peak, entries, sorting/merge scratch and lifetime before allocation. A dense page-ID bitmap costs by maximum ID, not number of present records. |
| Bounded per-run membership filters or interval candidate directory, `:2084` | Reject sparse misses within endpoint ranges or avoid scanning every range. | Filters must have no false negatives; a positive still needs exact lookup. Candidate ordering must remain newest-first. Bind all metadata to the manifest, cap memory, handle overlapping intervals, and account for filter construction. A negative cache only helps repeated missing keys and must be invalidated on mutation. |
| Bounded persistent index handles / decoded index blocks, `:2241,2273` | Amortizes repeated open and seek overhead without requiring parallel export. | Do not keep one buffered stream per run without a handle/memory limit. A shared stream's seek cursor is unsafe across workers. Use worker-owned handles or positional reads with bounded handle/block pools. Keep generation lifetime stable and propagate read errors. Reopening a storage object may write recovery/cleanup state, so do not create one mutable store per worker. |
| Frozen export readers with worker-local page buffers, `:3430,3610`; trainer `:1455` | Allows concurrent read/decode and independent page preparation after removing shared mutable lookup/cache dependencies. | Requires a dedicated immutable read session, not lock removal. Retain schema/checksum/full page identity validation and valid absence-to-zero synthesis. Worker-local stats need deterministic aggregation; use fixed-size preallocated buffers and bounded queues. Storage saturation and scheduling costs remain unknown. |
| Short-lock cache with loading-slot reservation, `:3615` | Releases cache lock during slow reads and decoding. | More complex than export-only readers. Reserve/pin a slot before unlock; coordinate duplicate loads; protect scratch, generation and eviction state; release reservations on exceptions/cancellation. Cap simultaneous pins so no-clean-slot failure and ordered-consumer deadlock are avoided. |
| Bounded page-ID read-ahead plus sequence-ordered writer, trainer `:1475` | Batch nearby page reads and overlap reader/preparation/writer stages while retaining output order. | Public-state order does not imply page-ID order. Tag requests with canonical output sequence and restore that order. Bound reorder bytes, not only job count; prioritize the earliest outstanding job to avoid all capacity being consumed by later completions. Stop, join, unpin and suppress publication on any failure. |
| Streaming merged page directory / export-only consolidated view, `:2108,2613` | Can replace many per-key run probes with a merged newest-record index. | Existing ordered API has a fan-in gate; it cannot simply traverse arbitrary page runs. Full page-ID reordering may require an inadmissible output-sized buffer or scratch files. Existing compaction mutates manifests/generations and can delete unpinned inputs (`:2737`); never compact the original store as a diagnostic shortcut. Any isolated consolidation needs explicit authorization, disk budgeting and checkpoint-identity review. |

Range bounds are populated from sorted append endpoints (`:2022`), checksum-verified reopened index endpoints (`:2425`), and merged endpoints (`:2731`). The range optimization preserves newest-first lookup. Tests at `tests/test_multiway_external_storage.cpp:161` cover skips, cache precedence and reopening, but they were not run in this investigation.

Observability gap: `MultiwayExternalRecordLookupProfile::run_range_skips` exists and is incremented, but `copy_page_profile` in the trainer (`:80`) does not copy it into the exported telemetry. The normal CLI progress/final report therefore cannot currently establish range-skip effectiveness. A future diagnostic change should expose skips, ranges visited, candidate probes, hit generation, and absence counts without per-probe events.

## 4. Read-only Tracy assessment

### Existing configuration and artifacts

- `CMakeLists.txt:479` builds separate non-installed `texas_tracy`; ordinary `texas` is separate.
- `:641` defines `texas_multiway_train_tracy` and `texas_solver_multiway_training_scaling_tracy`. Their enclosing gates also require `TEXASSOLVER_BUILD_EXAMPLES=ON` and `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON`.
- Active `build/CMakeCache.txt`: diagnostics OFF, on-demand ON, sampling/callstacks/locks/memory OFF; examples and research examples ON. Visual Studio multi-config includes Debug and RelWithDebInfo.
- `external/tracy/CMakeLists.txt` and `public/tracy/Tracy.hpp` exist. Wrapper `include/util/tracy.hpp` identifies version 0.14.1.
- Older Debug Tracy executables/PDBs remain in `build/Debug` despite diagnostics being disabled in the current cache. Do not assume they represent the current dirty source.
- Separate `build-tracy/CMakeCache.txt` enables diagnostics/on-demand/memory and disables sampling/callstacks/locks. Its RelWithDebInfo training executable has an October 25 timestamp, preceding this handoff.
- Local capture/capture-daemon and CSV/memory export executables exist in `build-tracy-capture/RelWithDebInfo` and `build-tracy-csvexport/RelWithDebInfo`. Presence was checked; versions, launch behavior and binary provenance were not executed or verified.

### What existing scopes can show

Inspect `mw.train.run` on thread `mw.train.main` (`examples/multiway_workflow_main.cpp:728`). Checkpoint scopes include `mw.checkpoint.training_validation`, `artifact_validation`, `binary_encoding_write`, `payload_hash`, `stream_close_or_flush`, and `atomic_publication`; coordinator checkpoint export scopes are available where their paths execute. These describe checkpoint work, not policy-stream internals.

Batch scopes/plots such as `mw.trainer.batch`, `mw.telemetry.process_sample` and batch memory plots are driven by training batches. A zero-batch resume/export cannot rely on those plots for continuous export telemetry.

There are no dedicated Tracy zones in the policy artifact writer or External storage implementation, and the trainer export loop uses aggregate chrono timers, not export Tracy zones. `--profile-external-export` is separate from Tracy. Existing scopes alone will not split index open/search, lock wait/hold, checksum/decode, or writer flush cost.

`TEXASSOLVER_TRACY_LOCKS` is declared at CMake line 54 but has no implementation wiring or relevant lock wrappers. Setting it ON alone will not expose these mutexes.

Sampling and call stacks are currently disabled. CMake leaves context switches enabled, but the local Windows backend (`external/tracy/public/client/TracySysTrace.cpp:183`) checks administrator privilege and ETW startup; availability must be established, not assumed or silently elevated.

### Future capture steps, all requiring separate authorization

1. Record current source diff, binary SHA-256, compiler/configuration, symbols, input/config/checkpoint hashes, active manifests and snapshot audit. Check capture tool/client compatibility using the local 0.14.1 source/toolchain. Do not trust artifact filenames or cached source revision alone.
2. Obtain authorization for any configuration/build/instrumentation, fixture creation, process launch or capture. No existing capture script is a read-only inspection tool.
3. Use the training CLI diagnostic target for checkpoint-only exporter work. The scaling diagnostic target is useful for training instrumentation smoke coverage, but does not represent this production export.
4. If a new diagnostic build is authorized, use a separate build tree, explicitly enable diagnostics/examples/research examples and on-demand collection. Start with memory events OFF. Enable sampling/call stacks only in a separately recorded bounded variant, with symbols and verified Windows support. Do not treat a Debug to RelWithDebInfo change as an algorithmic improvement.
5. First use a bounded representative synthetic export-only workload: multiple sparse/overlapping runs; present and absent pages; oldest/newest hits; cache misses and hits; reopened manifests; nonmonotonic page-ID requests. The existing one-page fixture had zero index opens and is unsuitable for bottleneck measurement.
6. If source instrumentation is authorized, add coarse export planning, ordered visit, finish/publication and buffered-flush scopes; sampled page miss/read/decode zones; and separate mutex acquisition versus hold scopes. Prefer fixed aggregates for run probes/skips. Do not emit events for hundreds of millions of index probes, every action, or every allocation.
7. Compare identical bounded workloads with aggregate profiling OFF/ON, Tracy disconnected/connected, and sampling separately. Record wall/CPU time, counts, event rate, queue/collector memory, working set/private peaks, free physical memory and output/trace disk space. Separate overlapping totals and sampled values; record sampling frequency and residuals.
8. Keep a short on-demand window aligned to streaming. Existing capture wrappers start at process launch and may capture only bucket loading/restore. Validate the alignment. A collector time limit does not stop the exporter; provide an independent duration/process watchdog. Existing scripts are references, not ready production-export wrappers.
9. Retain aggregate External counters alongside the trace. Analyze open/search versus CPU/runtime overhead, absence checks, actual observed wait/hold, writer backpressure and decode cost. Physical disk claims require OS-level evidence, not C++ stream counts.
10. Stop before production if the representative case fails to reproduce fan-out, profiling overhead is unbounded, support is missing, memory gates cannot admit the run, or correctness is unresolved.

`scripts/build_multiway_tracy.ps1` configures/builds, defaults to RelWithDebInfo and memory events ON, and also builds capture/export tools. It must not be invoked unchanged for a low-overhead investigation. `scripts/run/run_multiway_tracy_training_capture.ps1` defaults to a RAM training script and accepts positive training batches, not checkpoint-only production export. `run_multiway_tracy_capture.ps1` launches the training scaling diagnostic. `run_tracy_capture_daemon.ps1` starts a persistent collector. None was executed.

### Memory and overhead constraints

The aggregate exporter profile adds a fixed structure plus 4 KiB allowance (`multiway_blueprint_trainer.cpp:1269,1334`). This is not a Tracy buffer budget.

Tracy allocates its own queue/buffers and threads (`external/tracy/public/client/TracyProfiler.cpp`). No bound or solver-ledger charge for these event buffers is established. The collector also consumes system memory and trace storage. On-demand mode reduces collection outside a connection but does not establish a total memory bound.

Explicit memory events route allocations through the project's allocation hooks; Tracy reports mark allocation-probe results invalid (`multiway_training_report.cpp:196`). They are unsuitable as proof of production allocation behavior. Do not enable them by default for this lookup investigation.

Any future design must budget shared immutable metadata, transient index construction, all worker scratch/decoded pages, thread stacks, OS stream buffers, handle pools, job descriptors, owned output blocks, reorder window, profiler/collector headroom, mapped/input residency and file-cache allowance. Do not multiply the 256 MiB page-cache budget by worker count without admission. Include both steady and construction peaks with checked arithmetic.

Preserve the 48 GiB working-set and 64 GiB private-memory watchdog limits, 1.5 GiB system-memory/disk reserves and fail-closed behavior documented in the handoff/static preparation. Retain configured storage and export gates, including the configured 100 GiB disk-space requirement and current process/system estimates. New profiler costs must fit those limits; historical spare memory does not authorize raising them.

## 5. Evidence and authorization before production retry

1. Obtain explicit user authorization for the exact next phase. Analysis/report authorization does not authorize implementation, builds, representative runs, Tracy processes, snapshot creation, or a production launch. A bounded capture and a full export need distinct scope/duration approval.
2. Establish current binary/source/config/compiler provenance for the dirty worktree. Reconfirm checkpoint SHA-256 `098F37CB91D202C46AE6A969FE54515FFF3FDFF211C3CFFF718F4F95569A5524`, bucket input identity, and config SHA-256 `D23F10CB27287BE9BE110FEF4F24F8ED1BB8AE2709ECA793D991F9AAF02760B5` against the preserved source. These expected values are historical, not fresh hashes computed here.
3. Re-audit the isolated snapshot or create a fresh fully verified copy with authorization. Check active generation pointers, lengths/checksums, retention pins and transaction/rollback/temporary pin markers. The stopped run may have mutated the old copy. Preserve original store/checkpoint/config/logs/partial outputs. Restore and checkpoint capture can write manifests and pins.
4. If lookup/concurrency code changes, require focused regression evidence for newest-generation precedence; absent/present/cached/reopened pages; overlapping ranges; corrupt/truncated inputs; checksum/full identity checks; queue cancellation and unpin cleanup; no publication on failure; strict row order; quantized actions; repeatability across worker counts; and byte equality only where artifact identity is identical. Existing historical tests do not validate a future parallel design.
5. Require a representative bounded profile of the current range-pruned path, including skips and remaining fan-out, before choosing an optimization. A future candidate needs same-build/config before/after evidence and overhead calibration. The recorded tiny fixture and interrupted production attempt are insufficient.
6. Review all new memory/handle/disk admission estimates and runtime watchdog behavior, including collector memory and independent export timeout. Keep reserves and fail-closed gates.
7. Prepare an exact checkpoint-only invocation using `--batches 0 --resume`, the approved isolated `--external-storage-snapshot-dir`, and a fresh unique output/report/checkpoint directory. The training executable takes options directly; do not repeat the failed `train` subcommand. Prove no output/source path overlap. Do not reuse or remove the old partial policy.
8. Obtain explicit authorization for that concrete production command, binary/configuration, snapshot, resource/time envelope and optional capture. Only then launch.
9. Require successful process exit, final report/export success, atomic publication, no guard trip, unchanged protected inputs, and complete artifact validation. Compare all targets below. Matching a partial prefix or a report field alone is insufficient.
10. Make throughput or equivalence claims only after completed evidence supports them, stating build configuration and profiler overhead. Until then, describe all candidates as hypotheses.

Required completed-export targets from the handoff:

| Target | Required value |
|---|---:|
| Policy content hash | `11500758613148677101` |
| Accepted trajectories | 1,000 |
| Discarded trajectories | 0 |
| Rows | 2,434,250 |
| Actions | 6,403,142 |
| Serialized bytes | 189,818,882 |

No production speedup or policy equivalence is established by this analysis.

\n