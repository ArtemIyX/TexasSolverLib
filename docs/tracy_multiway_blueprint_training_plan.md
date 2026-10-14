# Tracy integration plan for multiway blueprint training

**Status:** Plan only. No Tracy code, dependency, build, test, benchmark, or solver run was added or executed.

**Repository evidence date:** 2026-10-14

## 1. Executive recommendation

Add Tracy 0.14.1 as a pinned vendored source dependency, but compile it only into dedicated diagnostic variants of the multiway trainer and scaling benchmark. Do not link Tracy into the installed `TexasSolver::texas` target.

Use two binaries from one source tree:

- `texas_multiway_train` and `texas_solver_multiway_training_scaling`: authoritative unprofiled correctness and throughput binaries.
- `texas_multiway_train_tracy` and `texas_solver_multiway_training_scaling_tracy`: diagnostic binaries built from a separate `texas_tracy` static library.

The normal target must contain no Tracy headers, client object, compile definitions, threads, sockets, or event checks. This gives the disabled path a true compile-time zero cost. The Tracy variant should default to on-demand, localhost-only collection, CPU zones, context-switch visibility, thread names, and batch-level plots. Sampling, call stacks, lock wrappers, and Tracy memory hooks should start disabled.

The first traces should answer one question: why the worker-active portion becomes more expensive as workers are added. They should not be used as throughput acceptance data. JSON/CSV remains authoritative for correctness, memory admission, deterministic fingerprints, and unprofiled performance.

## 2. Evidence found in the current repository

### 2.1 Build graph and target boundaries

- `CMakeLists.txt:10-15` defines test, example, legacy research, research example, and fixed HUNL research options. There is no current Tracy or general compile-time profiler option.
- Build type is selected through normal CMake configuration. README examples use Release for examples and Debug for tests.
- `CMakeLists.txt:22-268` explicitly classifies public headers, internal headers, production sources, platform sources, test-support sources, and research sources. `texassolver_verify_source_classification()` at `CMakeLists.txt:270-307` requires each repository header and source to have exactly one category.
- `CMakeLists.txt:352-375` creates the installed static library `texas`, alias `TexasSolver::texas`, from `TEXASSOLVER_SOURCES`. It links `pheval`, `Threads::Threads`, and `Cabinet` on Windows.
- `src/solver/multiway/evaluation/multiway_baseline.cpp` is the only current `TEXASSOLVER_TEST_SUPPORT_SOURCES` entry. It is added only to `test_multiway_baseline` at `CMakeLists.txt:547-562`.
- Research libraries are separate: `texas_legacy_vector`, `texas_hunl_flat_mccfr_research`, and `texas_hunl_fixed_research` at `CMakeLists.txt:377-401`.
- Multiway workflow and benchmark executables are research examples at `CMakeLists.txt:463-510`. `texas_multiway_train` and `texas_solver_multiway_training_scaling` currently link the stable `texas` library.
- Only `texas` and `pheval` are installed at `CMakeLists.txt:410-430`. A Tracy diagnostic library must remain unexported and uninstalled.

### 2.2 Existing profiling and telemetry

- `include/util/profiling.hpp:7-43` exposes `ScopedTimer`, `mark()`, and `TEXASSOLVER_PROFILE_SCOPE`.
- `src/util/profiling.cpp:210-266` checks a process-global runtime flag and takes timestamps only when `TEXASSOLVER_PROFILE` is enabled. When enabled, `mark()` takes a global mutex and updates `unordered_map` entries keyed by allocated strings at `src/util/profiling.cpp:220-245`.
- The existing profiler is suitable for coarse legacy timings. It is not suitable for worker traversal zones because enabled calls serialize on one mutex and allocate/map strings. Its disabled path still constructs a scope and checks the flag. It is not a compile-time no-op.
- The multiway batch runner has one coarse existing scope at `src/solver/multiway/engine/multiway_traversal.cpp:1152`.
- `MultiwayRootBatchRunner::worker_loop()` owns worker wait, dispatch, traversal, sort, and completion at `src/solver/multiway/engine/multiway_traversal.cpp:982-1143`.
- `MultiwayRootBatchRunner::run()` owns dispatch, the completion barrier, result collection, immutable discovery admission, and deterministic merge at `src/solver/multiway/engine/multiway_traversal.cpp:1145-1385`.
- `MultiwayBlueprintTrainer::run_batches()` owns batch sequencing and serial discount/prune work at `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:297-530`.
- Process CPU, current RSS, OS peak working set, and elapsed wall time are sampled at batch boundaries at `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp:492-504`.
- Per-worker active, sort, wake, CV wait, completion, tail-idle, delta, discovery, allocation, processor, affinity, and profile fields are aggregated at `src/solver/multiway/engine/multiway_traversal.cpp:1224-1300`.
- `MultiwayAllocationProbeScope` is active around worker execution at `src/solver/multiway/engine/multiway_traversal.cpp:1046`. The probe is implemented by process-wide replacement `operator new` overloads and a thread-local active pointer at `src/solver/multiway/engine/multiway_allocation_probe.cpp:16-119`.
- The current training report schema is version 5 at `include/solver/multiway/workflow/multiway_evidence.hpp:13`. JSON output includes phase, worker, allocation, CPU, RSS, working-set, affinity, and failed-batch fields at `src/solver/multiway/workflow/multiway_training_report.cpp:199-352`.
- `scripts/run_multiway_ram_1000.ps1` launches the trainer, samples external working set, records process CPU, and writes `run_metrics.json`.
- `scripts/run_multiway_worker_matrix.ps1` writes matrix JSON/CSV, calculates medians and derived shares, and gates zero discards, bounded RSS, and one deterministic merge fingerprint.
- `examples/benchmarks/multiway_training_scaling_main.cpp:166-265` uses a three-player depth-3 fixture, 1024 buckets per postflop street, immutable epochs, pinned workers, disabled internal profile, and a deterministic fingerprint gate. It is useful for a bounded smoke test but is not the 18 GiB production-like workload.

### 2.3 Verified scaling and evidence problems

[R1] P1 - Production-like training becomes slower as workers are added

Location: `tmp/ram_worker_matrix_3000/worker_matrix.json`

Evidence: With pinning enabled, 3,000 trajectories fell from 96.08 trajectories/s at one worker to 92.88 at eight and 70.94 at sixteen. Unpinned throughput fell from 93.87 to 59.76. The deterministic fingerprint remained `2342594177256522980`, so the measured work passed the available identity gate.

Impact: The current production-like workload has negative parallel scaling. Sixteen workers cost 26.2% throughput versus one pinned worker and 23.6% versus eight.

Fix: Instrument worker-active traversal and the serial batch phases before changing algorithms.

[R2] P1 - Aggregate worker cost inflates sharply under concurrency and worker cost is imbalanced

Location: `tmp/ram_worker_matrix_3000/pinned/workers_1/report.json`; `tmp/ram_worker_matrix_3000/pinned/workers_16/report.json`

Evidence: Aggregate worker-active time per trajectory grows from 6.66 ms at one worker to 133.38 ms at sixteen, about 20 times higher. At sixteen workers, equal trajectory counts of 186 or 189 still produce worker-active totals from 14.59 s to 30.05 s, a 2.06 times spread. Merge remains only 28.75 ms, so the old report does not support blaming deterministic merge for this run.

Impact: The loss is inside or immediately around worker-active traversal, with a separate load-balance component. Existing counters cannot distinguish memory/cache contention, allocator activity, OS scheduling, or one expensive traversal stage.

Fix: Add coarse worker phase zones, thread naming, context-switch capture, and batch plots. Keep dynamic scheduling as a comparison, not a presumed fix.

[R3] P2 - Existing matrix artifacts are stale relative to current telemetry

Location: `tmp/ram_worker_matrix_3000/pinned/workers_16/report.json`; `tmp/ticket0/b32/unpinned/unpinned/fixed/workers_8/repeat_1/report.json`; `include/solver/multiway/workflow/multiway_evidence.hpp:13`

Evidence: The 3,000-trajectory report is schema 3 and the newer 160-trajectory report is schema 4. Current source requires schema 5. The stored files therefore omit current fields such as coordinator admission, trainer-only time, process CPU, peak working set, worker CV wait, allocation counts, and processor/affinity results.

Impact: The current source has better attribution than the retained data, but no current production-like artifact proves which phase dominates.

Fix: Rebuild and rerun an unprofiled baseline before interpreting any Tracy trace. Reject comparisons across report schema or binary provenance.

[R4] P2 - Retained performance artifacts lack usable producer provenance

Location: `tmp/ram_worker_matrix_3000/pinned/workers_16/report.json:evidence.producer`; `tmp/ticket0/b32/unpinned/unpinned/fixed/workers_8/repeat_1/report.json:evidence.producer`

Evidence: `git_commit`, `build_configuration`, and `compiler_identity` are all `unknown`. `examples/multiway_workflow_main.cpp:43-61` reads these values from environment variables, but the PowerShell wrappers do not populate them.

Impact: Results from different binaries or configurations can be compared as if they were equivalent.

Fix: Make wrappers populate producer metadata and add explicit instrumentation provenance before accepting a matrix.

[R5] P2 - `barrier_wait_nanoseconds` is not a pure barrier measurement

Location: `src/solver/multiway/engine/multiway_traversal.cpp:1107-1117`; `src/solver/multiway/engine/multiway_traversal.cpp:1193-1213`; `src/solver/multiway/engine/multiway_traversal.cpp:1310-1312`

Evidence: Worker active time ends before worker delta sorting, while coordinator wait includes wake latency, active work, sorting, completion signaling, and condition-variable reacquisition. Subtracting maximum active time therefore labels these other costs as barrier wait.

Impact: A matrix can misattribute dispatch or sort time to barrier overhead.

Fix: Use explicit Tracy zones and either rename the derived field to `coordinator_residual_wait_nanoseconds` or calculate barrier residual from the full critical-worker completion interval.

[R6] P2 - Current processor telemetry cannot reveal migration or processor-group placement

Location: `src/solver/multiway/engine/multiway_traversal.cpp:985-1003`; `src/solver/multiway/engine/multiway_traversal.cpp:1118`

Evidence: Each worker records one processor number after work. Windows affinity uses a single `DWORD_PTR` mask and no processor-group identity.

Impact: Unpinned migration and Windows processor-group placement cannot be reconstructed, so topology conclusions are unsafe.

Fix: Use Tracy thread timelines/context switches and report Windows processor group plus processor number at stable boundaries.

No correctness race was proven by this review. Immutable discovery epochs, deterministic trajectory seeds, fixed-order stream sorting, and deterministic merge fingerprints are present. Tracy integration must preserve those contracts.

## 3. Alternatives considered and rejected

| Alternative | Reproducibility and offline use | Platform and ownership | Binary, link, and runtime cost | Maintenance | Decision |
|---|---|---|---|---|---|
| Vendor Tracy 0.14.1 under `external/tracy` | Exact source is present in every checkout. No configure-time network. Record tag, upstream commit, archive SHA-256, and license. | Upstream CMake supports MSVC/Windows and Linux. TexasSolver owns upgrades. BSD-3-Clause is compatible if notices are retained. | Cost exists only for diagnostic targets. Adds Tracy client compile/link time and diagnostic binary size. | Periodic explicit upgrades and license review. | Recommended. |
| CMake `FetchContent` from a tag or commit | Reproducible only if pinned, but clean offline builds fail without a populated cache. | Upstream-owned integration is simple on Windows/Linux. | Same client cost. Configure can unexpectedly access the network. | Low code maintenance, higher infrastructure risk. | Reject for the default repository path. Permit only as a documented developer fallback if later requested. |
| External package manager or installed Tracy package | Depends on local vcpkg/Conan/system state and package availability. | Version and build options vary across hosts. | Can reduce source build time but increases ABI/configuration variance. | Package recipes become another owned compatibility surface. | Reject as the authoritative path. |
| Link Tracy directly into installed `texas` when an option is enabled | Source and symbols are consistent, but the exported package may gain an undeclared dependency. | Affects all consumers and installation rules. | Diagnostic compile definitions and client code enter the stable library. Disabled macros can be cheap, but the target identity changes. | Simple CMake, high release risk. | Reject. |
| Put zones only in benchmark executable | Stable library stays untouched. | Easy on all supported platforms. | Low integration cost. | Cannot observe worker loop, traversal, admission, merge, or trainer phases inside the library. | Reject as insufficient. |
| Build a separate `texas_tracy` library and diagnostic executables | Fully pinned and offline. Normal and diagnostic binaries coexist. | Same sources compile on Windows/Linux. No installed dependency leak. | Extra build time and disk only when explicitly enabled. Normal target is unchanged. | Some CMake duplication must be centralized in a helper. | Recommended. |

Upstream reference: Tracy 0.14.1 is the current release reviewed for this plan. Upstream documents `Tracy::TracyClient`, `TRACY_ENABLE`, `TRACY_ON_DEMAND`, Windows/Linux support, compile-time no-op macros, and version-specific trace formats. See <https://github.com/wolfpld/tracy/releases/tag/v0.14.1>, <https://github.com/wolfpld/tracy/blob/v0.14.1/manual/tracy.tex>, and <https://github.com/wolfpld/tracy/blob/v0.14.1/LICENSE>.

## 4. Proposed target and dependency architecture

```text
external/tracy @ v0.14.1
        |
        +-- Tracy::TracyClient
                 |
TEXASSOLVER_SOURCES --+--> texas          --> normal tests/workflows/benchmarks
                      |
                      +--> texas_tracy    --> *_tracy diagnostic executables
```

Rules:

1. Vendor the complete upstream source release in `external/tracy`. Add `external/tracy/TEXASSOLVER_UPSTREAM.md` containing version `0.14.1`, upstream short commit `30997d5`, the verified full commit captured during import, source archive SHA-256, source URL, import date, and BSD-3-Clause notice location.
2. Add the Tracy subtree with `EXCLUDE_FROM_ALL` only when diagnostics are requested.
3. Refactor repeated library setup into `texassolver_configure_core_target(target)`. Both `texas` and `texas_tracy` receive the same source list, include directories, C++17 setting, warnings, platform source, and core dependencies.
4. `texas` never links Tracy and never receives `TEXASSOLVER_TRACY_ENABLED`.
5. `texas_tracy` links `Tracy::TracyClient` privately and receives `TEXASSOLVER_TRACY_ENABLED=1` privately.
6. No executable may link both `texas` and `texas_tracy`. Add a CMake boundary check.
7. Do not export or install `texas_tracy`, Tracy targets, Tracy headers, or diagnostic executables.
8. Keep test-support and research sources in their current categories. Do not add them to either stable core library merely for profiling.
9. Create `include/util/tracy.hpp` as a stable internal header. It exposes project-owned macros so source files never include Tracy directly. In the normal target every macro expands to `(void)0` and does not evaluate arguments.
10. Keep `TEXASSOLVER_PROFILE_SCOPE` unchanged for existing generic solver users. Do not make it forward to Tracy because that would combine two runtime and output models.

## 5. Exact file-by-file change list

| File | Planned change |
|---|---|
| `external/tracy/**` | Vendor upstream 0.14.1 source and license. No local functional edits. |
| `external/tracy/TEXASSOLVER_UPSTREAM.md` | Record tag, full commit, SHA-256, source URL, license, import procedure, and server/capture compatibility rule. |
| `CMakeLists.txt` | Add options, conditional Tracy subtree, core-target helper, `texas_tracy`, two diagnostic executables, internal-header classification, and non-install boundary. |
| `include/util/tracy.hpp` | Add compile-time no-op wrappers for zones, thread names, plots, messages, and named batch marks. Use literal/static names only. |
| `include/solver/multiway/engine/multiway_traversal.hpp` | Add no solver state. If needed, add diagnostic-only cached static metadata fields that are excluded from identity and checkpoints. Add an `allocation_probe_valid` result flag only if report provenance is implemented here. |
| `src/solver/multiway/engine/multiway_traversal.cpp` | Name worker threads and add coarse wait, dispatch, traversal, sort, completion, coordinator collection, discovery admission, and merge zones. Emit batch plots after aggregation. No per-action or per-bucket zones. |
| `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp` | Add trainer batch, discount/prune, and process sampling zones. Emit RSS/CPU plots after existing batch-boundary sampling. |
| `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp` | Add diagnostic provenance/status fields only if needed by the report. Do not add them to model identity or checkpoint payloads. |
| `examples/multiway_workflow_main.cpp` | Recognize the `_tracy` trainer executable or use a compile definition that forces the train workflow. Set the coordinator thread name and a top-level training zone. |
| `examples/benchmarks/multiway_training_scaling_main.cpp` | Add a top-level diagnostic sample zone and named batch marks through the wrapper. Keep the fingerprint gate unchanged. |
| `include/solver/multiway/workflow/multiway_evidence.hpp` | Bump training report schema from 5 to 6 only when instrumentation provenance is added. |
| `include/solver/multiway/workflow/multiway_training_report.hpp` | Add `instrumentation_kind`, `tracy_version`, `trace_capture_id`, and `allocation_probe_valid`. Keep fields diagnostic and outside strategy identity. |
| `src/solver/multiway/workflow/multiway_training_report.cpp` | Validate and serialize the new provenance. Reject a Tracy report as an unprofiled performance-qualification input. |
| `scripts/run_multiway_ram_1000.ps1` | Populate git/build/compiler metadata. Add an explicit binary path/kind. Preserve existing JSON and RSS gates. |
| `scripts/run_multiway_worker_matrix.ps1` | Require one schema and provenance tuple per comparison. Add profiled/unprofiled labeling and current phase fields to JSON/CSV. Never mix them in a throughput median. |
| `scripts/run_multiway_tracy_capture.ps1` | New optional wrapper. Validate exact Tracy capture-tool version, launch one diagnostic process, retain capture plus report and manifest under `tmp/tracy/`, and fail clearly when the capture tool is absent. |
| `tests/cmake/test_tracy_boundary.cmake` | Verify default CMake has no Tracy dependency/export and diagnostics create only the expected private targets. |
| `tests/test_multiway_parallel_training.cpp` | Verify instrumentation does not alter accepted counts, fixed/dynamic equivalence, worker-count fingerprints, or clean-batch behavior. |
| `tests/test_multiway_qualification_reports.cpp` | Verify schema 6 provenance, allocation validity, and profiled/unprofiled qualification separation. |
| `.gitignore` | Add `*.tracy` as defense in depth. `tmp/` is already ignored. |
| `docs/tracy_multiway_blueprint_training.md` | Add operator instructions, exact viewer/capture version, naming, capture retention, and interpretation rules after implementation. |

## 6. CMake options, links, definitions, and defaults

Proposed project options:

```cmake
option(TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS
    "Build non-installed Tracy diagnostic targets" OFF)
option(TEXASSOLVER_TRACY_ON_DEMAND
    "Collect Tracy events only while a compatible collector is connected" ON)
option(TEXASSOLVER_TRACY_SAMPLING
    "Enable Tracy statistical sampling in diagnostic targets" OFF)
option(TEXASSOLVER_TRACY_CALLSTACKS
    "Enable Tracy call stacks in diagnostic targets" OFF)
option(TEXASSOLVER_TRACY_LOCKS
    "Enable selected Tracy lock instrumentation" OFF)
option(TEXASSOLVER_TRACY_MEMORY
    "Enable explicit solver allocation events in Tracy" OFF)
```

When `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=ON`:

- Force upstream `TRACY_ENABLE=ON`.
- Map `TEXASSOLVER_TRACY_ON_DEMAND` to upstream `TRACY_ON_DEMAND`.
- Set `TRACY_ONLY_LOCALHOST=ON` and `TRACY_NO_BROADCAST=ON` by default.
- Set `TRACY_NO_SAMPLING=ON` unless project sampling is enabled.
- Set `TRACY_NO_CALLSTACK=ON` unless project call stacks are enabled. If enabled later, start with depth 8.
- Keep `TRACY_LTO=OFF` initially to reduce integration and link variance.
- Keep `TRACY_NO_EXIT=OFF` so short diagnostic runs never hang waiting for a collector.
- Add `external/tracy` with `EXCLUDE_FROM_ALL`.
- Link only `texas_tracy PRIVATE Tracy::TracyClient`.
- Define only on `texas_tracy`: `TEXASSOLVER_TRACY_ENABLED=1`.

Default behavior:

- All options are OFF except on-demand, which has no effect unless diagnostic targets are enabled.
- Default configure/build/install remains byte-for-byte source equivalent except for CMake metadata.
- `texas` retains no Tracy include path or transitive link dependency.
- A normal build can succeed with `external/tracy` absent only if the repository import policy allows a documented placeholder. The preferred final repository state vendors the source, so no network is needed in either mode.

## 7. Tracy zone, thread, frame, and plot naming

Use fixed ASCII literals with a `mw.` prefix. Dynamic text is prohibited in hot paths.

### 7.1 Threads

| Name | Owner | Rule |
|---|---|---|
| `mw.train.main` | `run_train()` in `examples/multiway_workflow_main.cpp:504` | Set once on the workflow thread. |
| `mw.worker.00` through `mw.worker.15` | `MultiwayRootBatchRunner::worker_loop()` at line 982 | Format once into a fixed stack buffer before the loop. No heap allocation. |

Report processor placement separately as group, processor, affinity group, affinity mask, and pin success. A thread name must not encode a processor because unpinned threads can migrate.

### 7.2 CPU zones

| Tracy zone | Exact owner | Boundary and reason |
|---|---|---|
| `mw.train.run` | `run_train()` | End-to-end diagnostic context around the training workflow only. |
| `mw.trainer.batch` | `MultiwayBlueprintTrainer::run_batches()` at lines 306-529 | One zone per batch. Batch frequency is low enough and is the natural JSON boundary. |
| `mw.trainer.late_window_snapshot` | `run_batches()` at lines 308-313 | Separates a possible full root-sum copy from traversal. |
| `mw.trainer.discount_prune` | `run_batches()` at lines 337-365 | Measures serial regret maintenance that workers cannot accelerate. |
| `mw.telemetry.process_sample` | `run_batches()` at lines 492-504 | Measures CPU/RSS/working-set query overhead. |
| `mw.batch.dispatch_wait` | `MultiwayRootBatchRunner::run()` at lines 1169-1207 | Coordinator dispatch through final worker completion. |
| `mw.worker.cv_wait` | `worker_loop()` at lines 1014-1033 | Shows sleep and wake behavior. No lock-type replacement. |
| `mw.worker.active` | `worker_loop()` at lines 1041-1110 | Worker critical interval excluding stream sort. |
| `mw.worker.traversal` | `worker_loop()` around `process_range` at lines 1048-1106 | Attributes the proven worker-active inflation without one zone per trajectory. |
| `mw.worker.delta_sort` | `worker_loop()` at lines 1111-1117 | Separates per-worker stream sorting. |
| `mw.worker.complete` | `worker_loop()` at lines 1129-1140 | Measures atomic completion and final notification. |
| `mw.coordinator.collect` | `MultiwayRootBatchRunner::run()` at lines 1209-1301 | Separates result vector population and profile merging. |
| `mw.coordinator.discovery_copy` | `run()` at lines 1321-1334 | Measures allocations and copies into coordinator-owned discovery vectors. |
| `mw.coordinator.discovery_sort` | `run()` at lines 1335-1355 | Measures public-state and row sorting. |
| `mw.coordinator.discovery_admit` | `run()` at lines 1338-1358 | Measures the serial parent-resolution/admission loop. |
| `mw.coordinator.delta_merge` | `run()` at lines 1363-1376 | Measures deterministic regret and continuation merge. |

Do not place zones inside `MultiwayRootExternalSamplingTraversal::traverse_decision()`, per-action loops, per-bucket loops, row updates, terminal inner loops, merge cell loops, or allocation hooks in the initial integration. If `mw.worker.traversal` dominates, use sampling or add one temporary compile-time diagnostic sub-zone around a whole traversal stage, never one event per item.

### 7.3 Batch marks and plots

Emit one named frame mark `mw.batch` after a clean batch commits. This is justified because batches are the repeated unit used by reports and comparisons. Do not emit graphics frame marks elsewhere.

Emit plots once per committed batch from the coordinator/trainer thread:

- `mw.trajectories.accepted`
- `mw.worker.active_ns.total`
- `mw.worker.active_ns.max`
- `mw.worker.tail_idle_ns.total`
- `mw.delta.regret_entries`
- `mw.delta.continuation_entries`
- `mw.discovery.public_states`
- `mw.discovery.rows`
- `mw.phase.admission_ns`
- `mw.phase.merge_ns`
- `mw.phase.trainer_only_ns`
- `mw.memory.rss_bytes`
- `mw.memory.peak_working_set_bytes`
- `mw.process.cpu_ns.delta`

Do not plot trajectory IDs, seed values, per-action data, or one point per allocation. Allocation count/bytes remain JSON-only until the allocation isolation rule in section 8 is implemented and validated.

### 7.4 Features not justified initially

- GPU zones: no GPU work exists in the reviewed multiway training path.
- Fibers: workers are persistent `std::thread` instances, not fibers.
- Per-zone call stacks: too much overhead for the first attribution pass.
- Tracy memory hooks: conflict with the current process-wide `operator new` probe and risk double counting.
- Lock wrappers: changing `pool_mutex_` to a Tracy lock type is incompatible with the current `std::condition_variable` contract without changing synchronization types. Use wait zones first.

## 8. Runtime and memory overhead controls

1. Normal binaries have compile-time no-op project macros and no Tracy client. This is the only path used for performance acceptance.
2. Diagnostic binaries are clearly suffixed `_tracy` and report `instrumentation_kind="tracy"` plus `tracy_version="0.14.1"`.
3. On-demand collection prevents unbounded pre-connection event buffering during long training runs. Bind to localhost and disable discovery broadcast.
4. Initial zones occur at worker-batch, coordinator-phase, or trainer-batch granularity. No per-trajectory zone is required to prove where aggregate worker time is spent.
5. Sampling and call stacks require separate opt-in builds. Never compare a sampling build's throughput with an instrumentation-only build.
6. First captures are bounded to warmup plus three measured batches. Stop if a capture exceeds 2 GiB or the diagnostic process exceeds the memory preflight estimate.
7. Tracy memory allocation reporting remains off. The existing solver allocation probe remains authoritative only in unprofiled binaries.
8. In a Tracy build, set `allocation_probe_valid=false` and do not use worker allocation counts for acceptance. This prevents Tracy client allocations from being presented as solver allocations. A paired unprofiled run supplies the solver allocation counts.
9. If explicit Tracy memory events are later needed, instrument named subsystem allocators directly. Do not call `TracyAlloc` from replacement `operator new`, because Tracy may allocate while recording and the current hook has no recursion guard.
10. Plots and zones must consume already computed counters. They must not add string construction, vectors, hashing, or clock reads to per-action/per-bucket loops.

## 9. JSON/CSV telemetry and schema interaction

Tracy complements the reports. It does not replace them.

JSON/CSV remains authoritative for:

- accepted/discarded trajectory counts;
- capacity exhaustion;
- memory preflight and RSS gates;
- requested/effective worker counts;
- deterministic merge fingerprint;
- checkpoint and blueprint sizes/hashes;
- exact build, compiler, model, and workflow provenance;
- unprofiled throughput medians.

Tracy supplies:

- overlap and ordering of worker/coordinator phases;
- worker critical-path and idle intervals;
- OS scheduling and context-switch evidence;
- coarse phase duration distributions;
- optional sampling within a proven dominant coarse zone.

Schema plan:

1. Treat existing schema 3 and 4 artifacts as historical only.
2. Establish a fresh unprofiled schema 5 baseline before code changes.
3. When instrumentation provenance fields are added, bump the training report to schema 6.
4. Add `instrumentation_kind` (`none` or `tracy`), `tracy_version`, `trace_capture_id`, and `allocation_probe_valid`.
5. Add `binary_sha256` to the sidecar `run_manifest.json`. Keep it outside solver/checkpoint identity.
6. Require matching report schema, git commit, compiler, build configuration, model identity, workflow fingerprint, scheduler, pinning, seed, batch size, and binary kind before aggregating results.
7. A profiled report may pass correctness/fingerprint checks, but must be rejected by the throughput qualification aggregator.
8. Store a stable capture ID in both report and manifest, not a trace path in checkpoint or blueprint data.

## 10. Test and validation plan

No validation command was run while writing this plan.

### 10.1 Static and CMake boundaries

- Default configure succeeds without building Tracy targets.
- `TexasSolverTargets.cmake` contains no Tracy reference.
- Installed headers do not include `util/tracy.hpp` or upstream Tracy headers.
- `texas` has no `TRACY_ENABLE` or `TEXASSOLVER_TRACY_ENABLED` definition.
- `texas_tracy` is absent unless diagnostics are enabled.
- Diagnostic executables link `texas_tracy` and never `texas`.
- Source/header classification test includes the new internal wrapper exactly once.

### 10.2 Focused contracts

- Existing one through sixteen worker deterministic equivalence tests pass for normal targets.
- Add a diagnostic one/four/eight worker smoke that compares accepted/discarded counts, merge fingerprints, exported root policy, and checkpoint hash with the normal binary.
- Verify adding thread names, zones, plots, and batch marks does not change schedule fingerprints, random seeds, merge order, status counters, checkpoint payloads, or blueprint payloads.
- Verify profiled reports are labeled and excluded from unprofiled medians.
- Verify a diagnostic executable exits normally with no Tracy server connected.
- Verify allocation fields are marked invalid in Tracy runs and valid in normal runs.
- Verify schema 3/4/5 reports cannot be silently combined with schema 6.

### 10.3 Commands to use during implementation

Default full workflow:

```powershell
& .\scripts\codex_powershell.ps1 python .\scripts\full_build.py
```

Diagnostic configure and build:

```powershell
& .\scripts\codex_powershell.ps1 cmake -S . -B build-tracy `
  -DTEXASSOLVER_BUILD_EXAMPLES=ON `
  -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON `
  -DTEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=ON
& .\scripts\codex_powershell.ps1 cmake --build build-tracy --config RelWithDebInfo `
  --target texas_multiway_train_tracy texas_solver_multiway_training_scaling_tracy
```

Focused tests:

```powershell
& .\scripts\codex_powershell.ps1 ctest --test-dir build -C Debug `
  -R "test_(tracy_boundary|multiway_parallel_training|multiway_qualification_reports)" `
  --output-on-failure
```

No-server smoke:

```powershell
& .\build-tracy\RelWithDebInfo\texas_multiway_train_tracy.exe --help
```

## 11. Benchmark and capture protocol

### 11.1 Baseline gate

1. Build current unprofiled Release or RelWithDebInfo code.
2. Run one bounded schema 5 production-like baseline before any Tracy change.
3. Require known git/build/compiler metadata, zero discards, no capacity exhaustion, one nonzero fingerprint, and RSS below preflight.
4. Retain JSON/CSV and manifests under `tmp/baseline_unprofiled/`.

### 11.2 Paired diagnostic matrix

For each selected case:

1. Run an unprofiled fresh process first.
2. Run the matching `_tracy` fresh process with instrumentation-only settings and the v0.14.1 collector.
3. Use identical config, seed, worker count, scheduler, pinning, batch count, and trajectories per batch.
4. Start with workers 1, 4, 8, and 16, fixed scheduling, pinned and unpinned, one warmup batch plus three measured batches.
5. Add dynamic scheduling only after the fixed trace shows a critical-worker imbalance.
6. Use at least five fresh-process unprofiled repeats for throughput. One or two captures per configuration are sufficient for attribution.
7. Never merge profiled values into performance medians.

### 11.3 Required artifacts

```text
tmp/tracy/<git-commit>/<workload>/<pinning>/<scheduler>/workers_<n>/repeat_<n>/
  capture.tracy
  report.json
  run_metrics.json
  run_manifest.json
  stdout.log
  stderr.log
```

`run_manifest.json` must contain Tracy client/viewer version, binary SHA-256, report schema, git commit, compiler, configuration, CMake options, command line, capture ID, hostname, CPU model, logical processor count, Windows processor groups, start/end UTC, and whether sampling/call stacks/locks/memory events were enabled.

Tracy changes trace format between releases. Retain the matching v0.14.1 capture utility/viewer release reference in the manifest. Do not assume a newer viewer can load an older capture. Do not commit captures; `tmp/` and `*.tracy` remain ignored.

### 11.4 Interpretation order

1. Confirm fingerprint and count equivalence.
2. Compare batch wall time against coordinator wait, admission, merge, and trainer-only JSON fields.
3. Inspect `mw.worker.active` overlap and critical worker.
4. Inspect `mw.worker.traversal` duration distribution and context switches.
5. Determine whether idle time is load imbalance or preemption.
6. If worker traversal grows while workers remain runnable, run one sampling-enabled capture to identify the dominant function family.
7. Only then choose an optimization ticket. Do not infer cache or memory bandwidth saturation from wall time alone; confirm with available platform counters or a separate hardware-counter tool if Tracy cannot provide the counter.

## 12. Rollback and failure criteria

Rollback the integration or the last instrumentation ticket if any condition holds:

- Default `texas` target or installed package gains a Tracy dependency, symbol, include path, thread, socket, or compile definition.
- Normal-build deterministic fingerprints, checkpoint hashes, blueprint hashes, accepted counts, or merge order change.
- Normal unprofiled throughput regresses by more than 2% median across five fresh-process repeats without a separately explained environment change.
- Diagnostic and normal binaries differ in solver outputs for the same seed/configuration.
- A no-server diagnostic run hangs at shutdown.
- Tracy allocations appear as valid solver allocation counts.
- A bounded instrumentation-only capture exceeds 2 GiB or pushes RSS beyond memory admission.
- A zone requires strings, allocation, logging, hashing, virtual dispatch, or per-item clock reads in a documented hot loop.
- Tracy source or client/server version cannot be reproduced from the vendor record.
- Windows and Linux diagnostic builds cannot use the same project wrapper contract.

Failure to capture because no compatible Tracy collector is installed is not a solver failure. Preserve the unprofiled report and record the missing external tool in the manifest/log.

## 13. Ordered implementation tickets

Each ticket is intended to be one small commit.

### Ticket T1. Vendor Tracy and create the build fence

**Goal:** Make Tracy 0.14.1 available offline and prove it cannot enter the installed/default target.

**Files and symbols:** `external/tracy/**`; `external/tracy/TEXASSOLVER_UPSTREAM.md`; `CMakeLists.txt`; `tests/cmake/test_tracy_boundary.cmake`; `include/util/tracy.hpp` with no-op wrappers only.

**API/configuration changes:** Add `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS`; create `texas_tracy`; add internal wrapper classification. No public API change.

**Determinism and hot-path impact:** None. No zones yet. Normal sources compile without a Tracy definition or include.

**Checks:** Inspect exported targets and link interfaces. Build default and diagnostic targets. Run CMake boundary test.

**Commands:** Run `full_build.py`; configure/build `build-tracy` with the commands in section 10; run `ctest -R test_tracy_boundary`.

**Expected `tmp/` artifacts:** `tmp/build.log`, `tmp/ctest.log`, and any compact error summaries. No capture.

**Acceptance:** Default install metadata contains no Tracy; `_tracy` targets build offline; no-server `--help` exits.

**Rollback:** Any dependency/export leak or default build change.

### Ticket T2. Add provenance and diagnostic binary identity

**Goal:** Prevent stale or profiled data from entering an unprofiled matrix.

**Files and symbols:** `include/solver/multiway/workflow/multiway_evidence.hpp`; `include/solver/multiway/workflow/multiway_training_report.hpp`; `src/solver/multiway/workflow/multiway_training_report.cpp`; `examples/multiway_workflow_main.cpp::make_evidence_header`, `main`; both PowerShell wrappers; `tests/test_multiway_qualification_reports.cpp`.

**API/configuration changes:** Bump training report to schema 6. Add instrumentation kind, Tracy version, capture ID, binary kind/hash sidecar, and allocation validity.

**Determinism and hot-path impact:** Report-only. Do not add fields to `MultiwayModelIdentity`, scheduler identity, checkpoints, or blueprint artifacts.

**Checks:** Round-trip/validation tests for normal and Tracy reports; wrapper rejection of unknown producer data and mixed matrices.

**Commands:** Run focused qualification report tests, then `full_build.py`.

**Expected `tmp/` artifacts:** `tmp/tracy_schema_test/report.json`; `tmp/tracy_schema_test/run_manifest.json`.

**Acceptance:** Every matrix row has known producer and binary kind; profiled rows cannot contribute to unprofiled medians.

**Rollback:** Any checkpoint/model identity change or incompatibility outside the versioned report boundary.

### Ticket T3. Instrument the worker lifecycle and name threads

**Goal:** Expose worker wait, wake, active traversal, sort, completion, and processor scheduling without inner-loop events.

**Files and symbols:** `include/util/tracy.hpp`; `MultiwayRootBatchRunner::worker_loop`; `MultiwayRootBatchRunner::run`; `tests/test_multiway_parallel_training.cpp`.

**API/configuration changes:** Internal macros only. Add processor-group telemetry if implemented in this ticket.

**Determinism and hot-path impact:** One worker-batch traversal zone, not one per trajectory. Thread names are set once. No seed, scheduler, stream, or merge code changes.

**Checks:** Normal versus Tracy one/four/eight-worker fingerprint and policy equivalence. Validate no per-item dynamic names.

**Commands:** Run targeted parallel training tests, `full_build.py`, diagnostic build, and no-server smoke.

**Expected `tmp/` artifacts:** `tmp/tracy/t3_smoke/report.json`; optional `capture.tracy` only when the matching collector is available.

**Acceptance:** Timeline shows named workers and the five worker lifecycle phases. Outputs match the normal binary.

**Rollback:** Output mismatch, event volume proportional to actions/buckets, or default target contains Tracy code.

### Ticket T4. Instrument coordinator and trainer serial phases

**Goal:** Attribute the roughly 11 s outside old coordinator wait and separate admission, merge, discount/prune, and telemetry sampling.

**Files and symbols:** `MultiwayRootBatchRunner::run`; `MultiwayBlueprintTrainer::run_batches`; `run_train`; `BenchmarkFixture::run`; `include/util/tracy.hpp`.

**API/configuration changes:** Add the zones, one batch mark, and batch-level plots from section 7. No solver API change.

**Determinism and hot-path impact:** Events are emitted only at batch/phase boundaries and consume existing counters.

**Checks:** Verify JSON phase sums are directionally consistent with Tracy zones, allowing clock/collector overhead. Compare normal and Tracy fingerprints.

**Commands:** Run focused parallel/report tests, `full_build.py`, diagnostic build, and one bounded capture if tooling exists.

**Expected `tmp/` artifacts:** `tmp/tracy/t4_smoke/capture.tracy`, `report.json`, and `run_manifest.json`.

**Acceptance:** Each required serial phase is separately visible and no batch is missing/duplicated.

**Rollback:** More than one plot/event per batch for aggregate metrics or any solver-state dependency on collector connection.

### Ticket T5. Isolate allocation measurements

**Goal:** Ensure Tracy client allocations cannot be reported as solver worker allocations.

**Files and symbols:** `MultiwayAllocationProbeScope`; `MultiwayRootBatchResult`; `MultiwayBlueprintTrainingStatus`; training report types and tests.

**API/configuration changes:** Add `allocation_probe_valid`. In Tracy variants do not activate the global-new probe for acceptance. Keep `TEXASSOLVER_TRACY_MEMORY=OFF`.

**Determinism and hot-path impact:** Normal probe behavior is unchanged. Tracy runs explicitly mark allocation counters invalid.

**Checks:** Normal allocation contract tests remain valid; Tracy report validation rejects `allocation_probe_valid=true` until an isolation test proves it.

**Commands:** Run hot-path, parallel training, and qualification report tests, then `full_build.py`.

**Expected `tmp/` artifacts:** paired normal/Tracy allocation reports under `tmp/tracy/t5_allocation_pair/`.

**Acceptance:** No report can confuse profiler memory with solver memory. Normal counts remain unchanged.

**Rollback:** Normal probe count changes or Tracy recursively calls the replacement allocator hook.

### Ticket T6. Add the capture wrapper and paired-run protocol

**Goal:** Produce reproducible, version-matched diagnostic bundles without assuming an installed server.

**Files and symbols:** new `scripts/run_multiway_tracy_capture.ps1`; both existing PowerShell wrappers; `.gitignore`; operator documentation.

**API/configuration changes:** Script parameters for collector path, diagnostic binary, capture ID, worker count, scheduler, pinning, batches, trajectories, and output root.

**Determinism and hot-path impact:** External orchestration only.

**Checks:** Missing collector fails before launch with a clear message. Version mismatch fails. Capture, report, and manifest IDs match.

**Commands:** PowerShell syntax/static check, no-server smoke, then one bounded capture when v0.14.1 tooling is supplied.

**Expected `tmp/` artifacts:** complete directory shape from section 11.

**Acceptance:** A fresh operator can reproduce a capture offline after supplying the matching upstream tool.

**Rollback:** Wrapper mutates source configs/artifacts or permits a version/provenance mismatch.

### Ticket T7. Run the attribution matrix and select one optimization

**Goal:** Determine the dominant cause of worker-active inflation using current schema and paired evidence.

**Files and symbols:** No C++ change. Update `docs/multiway_worker_matrix_telemetry_report.md` or create a dated Tracy findings report.

**API/configuration changes:** None.

**Determinism and hot-path impact:** None.

**Checks:** Validate fingerprints, counts, schema, binary hashes, RSS, capture IDs, and unprofiled/profiling separation before analysis.

**Commands:** Use `run_multiway_worker_matrix.ps1` for unprofiled runs and `run_multiway_tracy_capture.ps1` for diagnostic runs.

**Expected `tmp/` artifacts:** paired 1/4/8/16-worker bundles for fixed pinned/unpinned cases.

**Acceptance:** One bottleneck is demonstrated by both Tracy timing and existing telemetry. The next code ticket has a measurable entry/exit gate.

**Rollback:** If captures disagree with JSON or instrumentation overhead changes phase ordering, reduce instrumentation and rerun. Do not optimize from the invalid trace.

### Ticket T8. Optional sampling or lock instrumentation

**Goal:** Narrow a dominant worker zone only if T7 cannot identify its function family.

**Files and symbols:** CMake Tracy options and selected coordinator lock declarations only. Never change `pool_mutex_`/`condition_variable` types in this ticket.

**API/configuration changes:** Opt-in sampling/callstack or selected diagnostic lock wrapper. Default stays off.

**Determinism and hot-path impact:** Diagnostic variant only. No event per action/bucket/update.

**Checks:** Repeat one T7 case with and without the optional feature and quantify diagnostic overhead.

**Commands:** Configure a separate `build-tracy-sampling`; build the two diagnostic targets; run one bounded capture.

**Expected `tmp/` artifacts:** paired instrumentation-only and sampling captures.

**Acceptance:** Added detail identifies a function/lock responsible for a material share and does not exceed the capture limits.

**Rollback:** No new attribution, excessive overhead, synchronization-type changes, or trace explosion.

## 14. Risks, open questions, and assumptions

- The retained production-like matrices are schema 3/4, not current schema 5. A fresh baseline is mandatory.
- The existing artifacts report unknown git/build/compiler identity. Their numeric evidence is useful for problem discovery, not final qualification.
- The 18 to 20 GiB resident set can make cache/TLB/memory-bandwidth effects dominant. Tracy zones can localize the affected code but may not supply all hardware counters. ETW/WPA, Windows Performance Counters, or Linux `perf` may be required after Tracy identifies the phase.
- The synthetic benchmark scales well and must not be treated as proof that the production workload scales.
- Tracy's exact full v0.14.1 commit and source archive SHA-256 must be recorded during vendoring. The reviewed release page exposes short commit `30997d5`.
- A v0.14.1 collector/viewer is not currently assumed to be installed. Capture execution remains operator-supplied.
- `TRACY_ON_DEMAND` reduces retained event memory but changes which zones appear across connection boundaries. Capture must connect before measured batches.
- Windows processor groups matter only above 64 logical processors, but reporting the group now avoids another schema change later.
- Recompiling all stable sources into `texas_tracy` increases diagnostic build time. It is preferred over leaking compile definitions into the installed library.
- Vendor size is material. Sparse vendoring of only client files would reduce it but increase ownership and upgrade risk, so full upstream source is preferred.
- Lock instrumentation is deferred because `std::condition_variable` constrains the mutex type and immutable epochs should already remove coordinator writes from worker traversal.

## First implementation recommendation

Implement T1 first: vendor Tracy 0.14.1, add the dedicated `texas_tracy` target, add compile-time no-op wrappers, and prove the install/default dependency boundary before adding any zone.

This is the smallest safe step because it tests offline reproducibility, MSVC/Linux CMake compatibility, licensing, symbol ownership, and target isolation without touching solver execution, scheduling, deterministic state, reports, or hot paths.
