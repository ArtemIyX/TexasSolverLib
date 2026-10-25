Task: Measure and reduce multiway training memory without weakening artifact validation, solver determinism, capacity safety, or checkpoint compatibility.

# Multiway Memory Measurement and Reduction Plan

**Date:** 2026-10-23
**Status:** Planning complete; implementation and runtime qualification pending
**Scope:** F1 multiway blueprint training, bucket artifact loading, solver construction, cold admission, steady-state training, and checkpoint/export transients
**Evidence boundary:** Source inspection and existing project-log measurements only. No build, test, benchmark, solver, or Tracy capture was run while creating this plan.

## Purpose

This document is the handoff for the next AI agent. The task is not to make an
isolated container-size reduction. The task is to establish trustworthy memory
attribution, select the largest avoidable cost, implement one bounded change,
and prove that the change preserves solver behavior.

Executable task prompts are indexed in `docs/memory_tasks/README.md`. Use those
files for requests such as `fully implement A`; this document remains the
architectural and measurement contract shared by every subtask.

The current 1,000-trajectory pilot reached approximately 20 GB of process
resident memory. The existing 56 GB preflight estimate is conservative and is
not directly comparable with that RSS result. Current telemetry mixes mapped
artifact size, process working set, configured capacities, retained vector
capacity, and cumulative allocations. These quantities must be separated
before choosing a memory optimization.

## Required outcome

The completed workstream must produce:

1. A reproducible normal-build and Tracy-build memory baseline.
2. Phase snapshots that distinguish startup, artifact validation, solver
   construction, cold admission, steady state, checkpointing, and export.
3. Process metrics that distinguish working set from private committed memory.
4. A subsystem ledger based on actual retained capacities, not only configured
   maxima.
5. Tracy allocation lifetime data for the diagnostic executable.
6. One measured memory reduction at a time.
7. Focused correctness tests and bounded runtime qualification.
8. A project-log entry containing only measurements actually obtained.

Do not start the 50-million-trajectory F1 run until the bounded memory gates in
this document pass.

## Existing evidence

The project log records the following bounded pilot:

| Metric | Recorded value |
| --- | ---: |
| Accepted trajectories | `1,000` |
| Internal peak RSS | `19,756,826,624` bytes |
| External peak working set | `20,650,893,312` bytes |
| Memory preflight estimate | `56,088,838,330` bytes |
| Public states | `22,995` |
| Sparse rows | `10,902` |
| Sparse values | `450,261` |

This is bounded sizing evidence. It does not identify which subsystem owns the
resident pages, and it does not qualify the 50-million-trajectory workload.

The current F1 development profile allows:

| Capacity | Value |
| --- | ---: |
| Public states | `2,000,000` |
| Sparse rows | `5,000,000` |
| Sparse values | `100,000,000` |
| Worker delta entries per worker | `1,000,000` |
| Discovery entries per worker | `4,000,000` |
| Process limit | `68,719,476,736` bytes |

These limits are safety ceilings. They are not evidence that each container
must reserve its complete ceiling at construction.

## Memory terms

Every report and comparison must use the following meanings consistently.

### Working set

Physical pages currently resident for the process. It can contain private heap
pages, executable pages, shared libraries, and file-backed mapped pages. On
Windows, the current implementation reports `PROCESS_MEMORY_COUNTERS_EX`'s
`WorkingSetSize` and `PeakWorkingSetSize`.

Working set is the correct metric for host RAM pressure, but it does not tell
the agent which bytes are private and which are backed by `buckets.bin`.

### Private committed bytes

Memory committed specifically for the process. On Windows, use
`PROCESS_MEMORY_COUNTERS_EX::PrivateUsage`. On Linux, report at least anonymous
resident memory separately from file-backed resident memory when the platform
exposes it.

Private committed bytes are the primary metric for solver-owned heap growth.

### Mapped artifact bytes

The virtual size of the mapped bucket artifact. Mapping a file does not imply
that every byte is resident. Report virtual mapped bytes separately from the
resident contribution of the mapping.

### Retained capacity bytes

Bytes retained by an owning container according to its capacity. This is a
subsystem attribution metric. It can exceed live element bytes and can remain
high after `clear`, `resize(0)`, or rewind operations.

### Cumulative allocation bytes

The sum of allocation requests over an interval. It is useful for churn but is
not live memory. Never compare cumulative allocation bytes directly with RSS.

### Preflight estimate

A conservative admission estimate derived from configured maxima and transient
allowances. It is a safety model, not an observation. The report must label it
as an estimate and must not present it as measured RAM.

## Current architecture and known gaps

### 1. Bucket artifact loading touches every assignment

`load_multiway_bucket_registry(...)` maps `buckets.bin`, constructs table
metadata, and calls `validate_mapped_assignments_parallel(...)`. That validator
reads every assignment in every table. The parallel scan can make most or all
of a large mapped artifact resident before training begins.

Relevant source:

- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_model.cpp`

This is the leading explanation for a large startup working-set peak, but it is
not yet proven to explain the full 20 GB measurement. Do not remove the scan
without implementing an equivalent integrity contract.

### 2. Preflight can count mapped pages conservatively twice

The workflow loads and validates the bucket registry before sampling
`resident_baseline_bytes`. It then passes both that RSS baseline and the full
`mapped_bucket_artifact_bytes` value into preflight. The preflight adds both.

If artifact pages are already resident after validation, their resident share
is represented in the RSS baseline and the complete mapping is added again.
This explains part of the gap between the observed 20 GB working set and the
56 GB estimate. It affects admission accuracy, not necessarily actual process
memory.

Relevant source:

- `examples/multiway_workflow_main.cpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/engine/multiway_memory.hpp`

### 3. Tracy memory profiling is disabled

The Tracy build currently uses:

```text
-DTEXASSOLVER_TRACY_MEMORY=OFF
```

The Tracy wrapper exposes zones, plots, messages, and frame marks, but it does
not expose allocation/free macros. Tracy reports deliberately set
`allocation_probe_valid=false`.

Relevant source:

- `scripts/build_multiway_tracy.ps1`
- `include/util/tracy.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `src/solver/multiway/engine/multiway_allocation_probe.cpp`

The current allocation probe counts allocation requests inside selected
scopes. It does not track frees or live retained bytes. It is disabled as valid
evidence in Tracy reports.

### 4. Worker streams reserve configured maxima

Each worker constructs:

- one `MultiwayWorkerDeltaStream` reserved to
  `max_worker_delta_entries`;
- one `MultiwayContinuationDeltaStream` reserved to the same capacity;
- one `MultiwayDiscoveryStream` whose public-state vector and row vector both
  reserve `max_discovery_entries`.

The F1 development profile therefore applies one maximum to two delta element
types and one discovery maximum to two different discovery element types for
every effective worker. Element sizes differ significantly. The current model
does not prove that all four reservations need the same ceiling.

Relevant source:

- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `include/solver/multiway/continuation/multiway_continuation_selector.hpp`
- `src/solver/multiway/continuation/multiway_continuation_selector.cpp`

### 5. Compact row metadata reserves its maximum immediately

`MultiwayCompactStorage` grows regret and strategy arrays only when pages are
admitted, but `rows_` reserves `max_rows` in its constructor. With the current
F1 limit this reserves capacity for five million row metadata entries before
the bounded pilot admits 10,902 rows.

Relevant source:

- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`

### 6. Memory snapshots start too late

The trainer samples process memory after completed batches. It plots current
RSS and peak working set, but the report lacks named startup snapshots. The
bucket load and validation peak occurs before the training session runs, so a
single process peak cannot attribute it.

Relevant source:

- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `examples/multiway_workflow_main.cpp`
- `scripts/run/run_multiway_ram_1000.ps1`

### 7. Subsystem `memory_bytes()` coverage is incomplete

The compact storage and public catalog expose retained-capacity estimates.
Worker scratch, continuation streams, discovery streams, coordinator merge
scratch, and admission scratch do not expose one complete structured snapshot.
The current ledger therefore cannot reconcile private process memory with
solver-owned containers.

## Ranked hypotheses

Treat these as hypotheses until the baseline captures prove them.

| Rank | Hypothesis | Expected symptom | Required proof |
| ---: | --- | --- | --- |
| 1 | Full mapped-assignment validation creates the startup RSS peak | Large working-set jump after validation without a comparable private-byte jump | Snapshot before map, after map, and after validation |
| 2 | Per-worker stream reserves dominate private committed memory | Private bytes jump during worker-pool construction and scale with worker count | 1/2/4-worker construction snapshots plus stream capacity ledger |
| 3 | Preflight substantially overstates required memory | Estimate remains far above observed private plus mapped-resident bytes | Reconciled estimate with non-overlapping categories |
| 4 | Compact row metadata reserve is material | Private bytes rise during coordinator construction even before rows are admitted | Row metadata capacity bytes at construction |
| 5 | Cold admission scratch remains at a large high-water capacity | Private bytes remain high after the first batch while live counts fall | Before/after cold batch retained-capacity snapshots |
| 6 | Checkpoint/export creates a separate transient peak | Peak rises only during checkpoint or export | Named snapshots immediately before, during, and after the operation |

## Telemetry design

### Process snapshot

Add one platform-neutral structure with supported/unsupported flags. Avoid
reusing ambiguous field names.

Required fields:

```text
current_working_set_bytes
peak_working_set_bytes
private_committed_bytes
peak_private_committed_bytes       # when the platform exposes it
virtual_address_bytes              # optional, clearly labeled
anonymous_resident_bytes           # Linux when available
file_resident_bytes                # Linux when available
sample_valid
```

On Windows, use `K32GetProcessMemoryInfo` for working set and `PrivateUsage`.
Do not infer private bytes by subtracting the artifact size from RSS.

### Named phase snapshots

Record snapshots at these exact boundaries:

1. `process_start`
2. `before_bucket_map`
3. `after_bucket_map_metadata`
4. `after_bucket_assignment_validation`
5. `before_memory_preflight`
6. `after_coordinator_construction`
7. `after_worker_pool_construction`
8. `after_cold_batch`
9. `after_steady_batch`
10. `before_checkpoint`
11. `checkpoint_peak_or_completion`
12. `after_checkpoint`
13. `before_export`
14. `after_export`

Store a bounded fixed-size list or explicit fields. Do not allocate or format
inside traversal, row update, terminal, or merge inner loops.

### Subsystem capacity snapshot

Add retained-capacity accounting for:

- bucket registry owned metadata;
- bucket artifact virtual mapping;
- public catalog persistent nodes, indexes, menus, and scratch;
- compact rows, pages, regrets, and strategy masses;
- worker regret delta streams per worker;
- worker continuation delta streams per worker;
- worker discovery public states per worker;
- worker discovery rows per worker;
- worker action-menu scratch;
- coordinator merge cells and stream cursors;
- coordinator admission scratch vectors;
- checkpoint/export buffers;
- continuation cache and scratch when enabled.

Report both `size_bytes` and `capacity_bytes` where meaningful. Use checked
integer multiplication and saturating aggregation. Memory reporting methods
must be `noexcept` and must not allocate.

Do not count the same allocation in two subsystem totals. If a parent total
includes children, either report the parent as a grouping-only field or label
the child fields as a breakdown.

### Tracy allocation lifetime capture

Enable Tracy memory support only for Tracy diagnostic targets. Keep the normal
production target unchanged.

Required changes:

1. Add wrappers in `include/util/tracy.hpp` for allocation and free events.
2. Instrument the existing global allocation boundary in
   `multiway_allocation_probe.cpp` for Tracy builds, including aligned paths.
3. Emit exactly one allocation and one free event per pointer.
4. Use the no-callstack form first. Callstack capture can materially perturb a
   high-allocation workload.
5. Verify that profiler internals do not recurse through the instrumented
   allocator. If recursion occurs, add a thread-local reentrancy guard and
   document excluded profiler allocations.
6. Keep scoped allocation counters and Tracy allocation lifetimes as separate
   evidence. Do not set `allocation_probe_valid=true` merely because Tracy
   memory capture is enabled.

The initial capture needs global allocation lifetimes and call sites. Named
subsystem attribution should come from explicit capacity snapshots. Do not add
custom allocators to hot-path vectors until the baseline proves they are
necessary.

### Report schema

Add a versioned memory section rather than changing meanings of existing
fields silently.

Suggested shape:

```json
{
  "memory_schema_version": 2,
  "memory_process_snapshots": [],
  "memory_subsystems": {
    "bucket_registry_owned_bytes": 0,
    "bucket_mapping_virtual_bytes": 0,
    "public_catalog_capacity_bytes": 0,
    "compact_storage_capacity_bytes": 0,
    "worker_delta_capacity_bytes": 0,
    "continuation_delta_capacity_bytes": 0,
    "discovery_public_capacity_bytes": 0,
    "discovery_row_capacity_bytes": 0,
    "coordinator_scratch_capacity_bytes": 0,
    "checkpoint_capacity_bytes": 0,
    "export_capacity_bytes": 0
  },
  "memory_preflight": {
    "estimated_private_bytes": 0,
    "mapped_virtual_bytes": 0,
    "estimated_peak_working_set_bytes": 0
  }
}
```

The exact JSON layout can differ, but the semantics must remain non-overlapping
and backward-compatible with existing readers.

## Implementation phases

### M0: Freeze a reproducible bounded baseline

Before changing memory behavior:

1. Record commit, compiler, build type, Tracy version, worker count, scheduler,
   pinning mode, batch count, trajectories per batch, and bucket artifact size.
2. Use the existing F1 sizing artifact and fixed seed.
3. Run the normal binary and Tracy binary with identical workload inputs.
4. Record external working-set samples and the training report.
5. Preserve fingerprint, checkpoint bytes when enabled, accepted trajectories,
   and discarded trajectories.

Do not compare Debug normal results against RelWithDebInfo Tracy results as a
memory optimization claim. Build a matching non-Tracy RelWithDebInfo target or
label the comparison diagnostic-only.

### M1: Correct process and phase telemetry

Implement the process snapshot API and named phase boundaries. Add report
serialization and validation. This phase must not alter capacities, storage,
artifact validation, or training behavior.

Acceptance:

- supported process fields are nonzero;
- snapshots appear in the required order;
- working set and private bytes are never mislabeled;
- existing report readers still accept old reports;
- deterministic training results remain unchanged.

### M2: Complete retained-capacity attribution

Add allocation-free `memory_snapshot()` or narrowly scoped `memory_bytes()`
methods to the worker, coordinator, compact storage, and public catalog
boundaries. Aggregate them after construction and after batches.

Acceptance:

- every major owning container has one owner in the ledger;
- totals use capacity, not only size;
- per-worker totals scale correctly with worker count;
- rewinds change live sizes without falsely changing retained capacities;
- unit tests cover empty, partially filled, and retained-after-rewind states.

### M3: Enable Tracy allocation lifetimes

Enable `TEXASSOLVER_TRACY_MEMORY` for diagnostic targets and instrument
allocation/free events. Update the build and capture scripts so the manifest
records whether memory events are enabled.

Acceptance:

- the capture contains allocation events;
- no recursion, crash, double free event, or missing aligned free path occurs;
- normal builds retain current behavior;
- the Tracy report still marks the scoped allocation probe according to its
  real validity;
- capture overhead is reported and not used as a production throughput result.

### M4: Run the attribution matrix

Use bounded runs only:

| Case | Workers | Batches | Scheduler | Purpose |
| --- | ---: | ---: | --- | --- |
| A | 1 | 1 | fixed | Startup and cold admission |
| B | 1 | 5 | fixed | Retained steady-state memory |
| C | 4 | 1 | fixed | Per-worker construction scaling |
| D | 4 | 5 | fixed | Per-worker steady-state scaling |
| E | 4 | 5 | dynamic | Scheduler-equivalent memory check |

Run normal RelWithDebInfo evidence for process memory. Use Tracy runs for
allocation attribution, not as the sole source of peak RAM.

The analyzer must produce:

- phase deltas for working set and private bytes;
- subsystem capacity totals;
- unexplained private-byte remainder;
- mapped artifact size and observed startup working-set delta;
- one-worker to four-worker private-memory slope;
- cold to steady retained-memory delta;
- deterministic equivalence results.

### M5: Correct preflight accounting

Split the estimate into at least:

- existing resident baseline;
- additional private commitment;
- mapped virtual bytes;
- expected mapped resident allowance;
- transient checkpoint/export peak.

Do not add the complete mapped artifact to a baseline that already includes
resident mapped pages unless the field explicitly models worst-case additional
residency. If a worst-case bound is required, present it as a separate bound.

This phase changes admission estimates only. It must not be presented as an
actual RAM reduction.

### M6: Optimize exactly one measured owner

Select one candidate using M4 evidence. Do not combine candidates in one
performance commit.

#### Candidate A: bucket validation residency

Use only if the after-validation working-set jump dominates and private bytes
remain comparatively flat.

Possible designs, in preferred order:

1. Validate the artifact through a bounded streaming buffer or temporary
   window mappings, release each validation window, then establish the final
   read-only mapping without rescanning all assignments. This preserves eager
   validation while preventing the persistent mapping from being warmed in
   full.
2. Add a versioned payload-integrity section and verify it during the same
   bounded streaming pass. A checksum alone is not a memory fix because it
   still requires reading the payload.
3. Validate assignment blocks lazily on first table use with a thread-safe,
   bounded validation state if eager validation cannot be retained within the
   memory target.

Constraints:

- corrupted bucket IDs must still be rejected before their first lookup;
- table identity and model identity behavior must not change;
- artifact schema changes require backward-compatibility or an explicit
  version rejection path;
- no validation branch may enter the per-hole lookup hot path after a table is
  validated;
- never treat mapping size reduction and working-set reduction as equivalent.

#### Candidate B: worker stream right-sizing

Use only if private memory jumps during worker-pool construction and stream
capacity bytes dominate.

Possible design:

- introduce separate regret-delta, continuation-delta, discovery-state, and
  discovery-row limits;
- preserve the old configuration fields as compatibility defaults;
- size per-worker capacity from measured peaks plus an explicit safety margin;
- retain allocation-free traversal after construction;
- reject capacity exhaustion deterministically with the existing stage model.

Do not replace preallocation with unreserved `push_back` in hot paths.

#### Candidate C: compact row metadata paging

Use only if the five-million-row metadata reserve is material.

Possible design:

- grow row metadata in deterministic chunks;
- reserve the next chunk before coordinator mutation;
- keep row lookup ordering and checkpoint order unchanged;
- include growth transient memory in admission checks.

Do not trade the eager reserve for repeated small allocations during row
admission.

#### Candidate D: retained admission scratch

Use only if the cold batch leaves large scratch capacity resident during
steady state.

Possible design:

- establish an evidence-based retained-capacity ceiling;
- release or replace unusually large cold scratch only at a coordinator batch
  boundary;
- perform any allocation outside traversal and merge inner loops;
- keep a stable reusable steady-state reserve.

#### Candidate E: checkpoint/export transients

Use only if checkpoint or export creates the dominant peak after normal
training memory has been corrected.

Possible design:

- stream canonical sections;
- reuse one bounded serialization buffer;
- avoid simultaneous full logical and canonical copies;
- preserve byte-identical schema-2 output and atomic publication.

## Correctness invariants

Every implementation must preserve:

- accepted and discarded trajectory counts;
- deterministic merge fingerprints;
- fixed/dynamic equivalence for identical workloads;
- public-state identity and catalog ordering;
- menu handles and sparse-row identity;
- CompactInt32 regret and strategy-mass values;
- checkpoint schema, hashes, and resume equivalence unless a separately
  approved schema migration is performed;
- capacity exhaustion stage and transactional rejection;
- artifact corruption rejection;
- allocation-free traversal, row update, terminal, and merge inner loops.

## Tests

Add focused tests before runtime qualification.

### Process metrics

- supported fields are nonzero and internally ordered;
- unsupported platforms return explicit availability flags;
- serialization round-trips new memory fields;
- old reports remain readable.

### Capacity accounting

- empty streams report reserved capacity correctly;
- partial streams distinguish size and capacity;
- rewind preserves retained capacity;
- aggregate worker bytes equal the sum of workers;
- overflow saturates or rejects safely;
- no allocation occurs while obtaining a snapshot.

### Preflight

- mapped virtual bytes are not silently counted as private bytes;
- resident baseline and mapped-resident allowance do not overlap;
- checkpoint/export transient maximum is counted once;
- warning, operating, and reject boundaries remain ordered;
- existing capacity rejection fixtures retain their stage and message.

### Selected optimization

- add candidate-specific contract and regression tests;
- preserve checkpoint equivalence and deterministic fingerprints;
- test capacity failure and rollback;
- test one-worker and multiple-worker construction;
- test fixed and dynamic scheduling when worker storage changes.

## Runtime qualification gates

Do not claim a memory improvement unless all applicable gates pass.

### Measurement gates

- normal and Tracy runs record the same commit and workload identity;
- external and internal working-set peaks are within a documented sampling
  tolerance;
- private committed memory is available on the primary Windows host;
- all required phase snapshots are present;
- subsystem totals contain no known double counting;
- Tracy allocation events are present for Tracy evidence;
- the unexplained private-memory remainder is reported, not hidden.

### Correctness gates

- `failed=false`;
- discarded trajectories `0`;
- accepted trajectories match the requested bounded workload;
- deterministic fingerprints match the baseline;
- fixed and dynamic outputs remain equivalent;
- focused tests and the relevant CTest suites pass;
- checkpoint equivalence passes when checkpoint code is affected.

### Performance gates

- compare matched RelWithDebInfo normal builds;
- report median results from at least three repeats for the selected case;
- working set and private bytes must both be reported;
- retain a candidate only when its target subsystem decreases materially and
  the process-level metric moves in the expected direction;
- reject a candidate that shifts memory into an unreported mapping, cache, or
  transient;
- report throughput change and reject material regression unless separately
  justified by a larger required memory gain.

Set the final numerical reduction threshold after M4 establishes measurement
noise. The threshold must exceed repeat-to-repeat noise by a clear margin.

## Suggested files

Likely production files:

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `include/util/tracy.hpp`
- `src/solver/multiway/engine/multiway_allocation_probe.cpp`
- `examples/multiway_workflow_main.cpp`

Likely tooling files:

- `scripts/build_multiway_tracy.ps1`
- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_tracy_training_capture.ps1`
- a new bounded memory analyzer under `scripts/` if existing analyzers cannot
  express the new schema cleanly.

Likely tests:

- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_bucket_artifact.cpp`

Do not edit every listed file automatically. Touch only the files required by
the active phase.

## Operator commands

Future agents must follow repository policy and use the project scripts. These
commands are templates, not evidence that they were run for this plan.

### Build and tests

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
```

Use focused targets and focused CTest expressions first. Run the full build
after the focused memory tests pass.

### Tracy build

```powershell
.\scripts\build_multiway_tracy.ps1 -Configure -Configuration RelWithDebInfo
```

### Bounded normal memory run

```powershell
.\scripts\run\run_multiway_ram_1000.ps1 `
  -Workers 1 `
  -Batches 1 `
  -TrajectoriesPerBatch 1000 `
  -Scheduler fixed `
  -BinaryKind normal `
  -RunDirectory tmp\memory-matrix\normal-fixed-1w-cold
```

### Bounded Tracy capture

```powershell
.\scripts\run\run_multiway_tracy_training_capture.ps1 `
  -Workers 1 `
  -Batches 1 `
  -TrajectoriesPerBatch 1000 `
  -Scheduler fixed `
  -CaptureId memory-fixed-1w-cold `
  -OutputDirectory tmp\memory-matrix\tracy-fixed-1w-cold
```

Never run the 50-million-trajectory workload merely to validate telemetry or a
single memory optimization.

## Commit boundaries

Use small commits in this order:

1. `PB-6 Memory: Add process phase snapshots`
2. `PB-6 Memory: Report retained subsystem capacities`
3. `PB-6 Tracy: Capture allocation lifetimes`
4. `PB-6 Tooling: Analyze bounded memory captures`
5. `PB-6 Memory: Correct preflight accounting`
6. One candidate-specific optimization commit
7. Tests for the selected candidate, if they cannot safely remain in the same
   candidate commit
8. `PB-6 Docs: Record bounded memory qualification`

Do not mix telemetry, preflight semantics, artifact format changes, worker
capacity changes, and checkpoint changes in one commit.

## Stop rules

Stop and document the result when any of these applies:

- process metrics cannot distinguish private memory from file-backed working
  set on the qualification host;
- the Tracy capture lacks allocation events or fails to connect;
- normal and Tracy workloads are not identical;
- the candidate changes fingerprints, checkpoint bytes, rejection order, or
  capacity behavior;
- the measured reduction is within baseline noise;
- the target component decreases but process memory does not move and the
  difference cannot be explained;
- an artifact-validation optimization cannot preserve corruption detection;
- a capacity reduction causes bounded workload exhaustion;
- a proposed fix adds allocation, strings, formatting, hash lookup, virtual
  dispatch, or logging to a hot inner loop.

## Future-agent checklist

1. Confirm branch, HEAD, and working-tree state.
2. Read this document and the newest `docs/PLURIBUS_LOG.md` entries.
3. Inspect current source because line numbers and telemetry schemas may have
   changed.
4. Implement M1 before selecting a memory fix.
5. Run focused tests, then obtain M4 baseline evidence.
6. Rank candidates using measured private bytes, working set, and retained
   capacities.
7. Implement exactly one candidate.
8. Repeat the same bounded matrix.
9. Review the C++ diff independently.
10. Update `docs/PLURIBUS_LOG.md` with actual results and limitations.

## Current conclusion

The most likely working-set cause is the full mapped bucket validation scan.
The most likely avoidable private-memory cause is per-worker over-reservation.
The preflight estimate also appears to overlap resident baseline and mapped
artifact accounting. These are separate problems and must be measured and
fixed independently.

No production memory change is authorized by this document alone. The next
task is M1: correct process and phase telemetry.
