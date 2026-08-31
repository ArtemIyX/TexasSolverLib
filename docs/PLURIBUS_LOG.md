# Pluribus Roadmap Progress Log

## F1 bucket generation - post-serialization qualification

**Status:** Complete
**Completed:** 2026-08-31

- Re-ran the 65,536-table Release end-to-end matrix after switching production
  generation to worker-side serialized chunks.
- Final pipeline rates at 1/2/4 workers were flop `8616`/`19340`/`38678`,
  turn `8596`/`18754`/`37846`, and river `8261`/`18032`/`36783` tables/s.
- Checksums matched for every street across worker counts; four workers reached
  roughly 4.5x one-worker throughput.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Release end-to-end matrix passed for all three streets and 1, 2, and 4
  workers.

### Limitations

- Serialized-mode telemetry currently reports chunk counts and queue depth but
  does not yet aggregate wait timers.

## F1 bucket generation - worker-side serialized chunks

**Status:** Complete
**Completed:** 2026-08-31

- Added serialized chunk generation that performs artifact-record encoding on
  worker threads and hands the publisher one contiguous payload.
- Production generation and the end-to-end benchmark now use the serialized
  handoff; the checked table-object API remains available.
- Added byte-identical regression coverage against table-object generation.
- Updated Release smoke rates for 1/2/4/8 workers: `10098`/`18778`/`29549`/
  `29465` tables/s on 1,024 flop tables, with matching checksums.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- Full Debug build and all tests passed.
- Release build passed.
- Serialized and table-object artifacts matched byte for byte.
- Release end-to-end benchmark passed for 1,024 flop tables at 1, 2, 4, and
  8 workers.

### Limitations

- Workers still construct table objects before encoding; fully direct encoding
  into preallocated worker buffers remains a possible later refinement.

## F1 bucket generation - topology-aware CLI default

**Status:** Complete
**Completed:** 2026-08-31

- The CLI now defaults to the smaller of four workers and detected physical
  cores, while retaining explicit `--threads` overrides.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Full Debug build and all tests passed.

### Limitations

- The four-worker cap remains an evidence-based conservative cap; a future
  host qualification may tune it independently.

## F1 bucket generation - physical topology diagnostics

**Status:** Complete
**Completed:** 2026-08-31

- Added physical-core detection on Windows using processor-core topology
  information, with a conservative hardware-thread fallback elsewhere.
- Exposed physical and logical counts in production startup and benchmark
  diagnostics, and added nonzero/ordering contract coverage.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- Full Debug build and all tests passed.

### Limitations

- Automatic worker selection remains the measured default cap of four; topology
  is reported but not yet used to derive a portable machine-specific default.

## F1 bucket generation - five-run performance gate

**Status:** Complete
**Completed:** 2026-08-31

- Completed five paired Release end-to-end runs for 65,536-table flop, turn,
  and river ranges.
- Median one-worker to two-worker throughput was flop `8108` to `20513`
  tables/s (2.53x), turn `7891` to `19698` (2.50x), and river `7573` to
  `18937` (2.50x).
- Every run produced 256 chunks and matching deterministic checksums.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Release end-to-end benchmark matrix passed: 30 runs total.
- Two-worker build-only and end-to-end throughput exceeded the plan's 1.50x
  build-only and 1.25x end-to-end minimum gates for this bounded workload.

### Limitations

- CPU topology, resident memory, disk throughput, and context-switch counters
  were not captured; the full production generation remains deferred.

## F1 bucket generation - street-separated qualification smoke matrix

**Status:** Complete
**Completed:** 2026-08-31

- Extended the bounded benchmark with an explicit global catalog start index,
  enabling isolated flop, turn, and river measurements.
- Release end-to-end 1,024-table pairs showed two-worker scaling from 1 worker
  to 2 workers on flop (`9828` to `19438` tables/s), turn (`9116` to `17926`),
  and river (`8880` to `17411`); checksums matched.
- Exposed queue and ordered-publisher wait metrics in these runs.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Full Debug build and all tests passed.
- Release build passed.
- Street-separated Release end-to-end smoke matrix passed.

### Limitations

- Samples are 1,024 tables rather than the 65,536-table qualification size;
  five repetitions, physical-core discovery, and long-range gates remain.

## F1 bucket generation - buffered resume hashing

**Status:** Complete
**Completed:** 2026-08-31

- Changed resume verification to read the checkpointed payload in 64 KiB
  blocks while retaining byte-exact FNV hashing and truncation detection.

### Files

- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`

### Validation

- Existing full resume and artifact tests passed through the full build/test
  workflow.

### Limitations

- Final artifact inspection remains stream-oriented and is not part of worker
  scaling measurements.

## F1 bucket generation - aggregate scheduler telemetry

**Status:** Complete
**Completed:** 2026-08-31

- Added optional aggregate statistics for chunks built and published, ready
  queue high-water mark, worker wait time, and ordered publication wait time.
- Exposed metrics in bounded benchmark output and added contract tests.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: build OK, all tests passed.

### Limitations

- Telemetry is aggregate only; publisher sub-stage timers and machine CPU,
  heap, disk, and context-switch counters remain external measurements.

## F1 bucket generation - measured default worker cap

**Status:** Complete
**Completed:** 2026-08-31

- Changed the default bucket worker count from 16 to 4 after bounded Release
  end-to-end measurements showed 4 workers at about `29.1k-29.6k tables/s`
  while 8 workers provided no material improvement.
- Preserved the explicit worker override for machine-specific qualification.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `examples/multiway_workflow_main.cpp`

### Validation

- Release end-to-end smoke matrix over 1,024 flop tables: 1 worker about
  `9.8k tables/s`, 2 workers about `19.5k tables/s`, 4 workers about `29.1k`
  to `29.6k tables/s`, and 8 workers about `29.0k` to `29.6k tables/s`.
- Checksums matched across all runs.

### Limitations

- The default is evidence-based for the qualification host, not a portable
  physical-core detector; longer street-separated five-run qualification is
  still required.

## F1 bucket generation - bounded end-to-end benchmark

**Status:** Complete
**Completed:** 2026-08-31

- Added a benchmark mode exercising the real artifact writer against a
  temporary bounded output, alongside the build-only sink mode.
- Release smoke testing over three paired 1,024-table runs measured about
  `9795 tables/s` at one worker and `19547 tables/s` at two workers, with
  identical deterministic checksums.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Debug full build and all tests passed.
- Release build passed.
- Release end-to-end benchmark passed for three worker-count pairs.

### Limitations

- Qualification still needs five-run street-separated matrices and physical
  versus logical topology measurements.

## F1 bucket generation - bounded build-only benchmark

**Status:** Complete
**Completed:** 2026-08-31

- Added a bounded build-only benchmark mode to the bucket executable with fixed
  table count, worker count, throughput, and deterministic checksum output.
- Kept benchmark generation separate from production artifact publication.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Full build and test workflow passed.
- Debug smoke benchmark, 1,024 flop tables: 1 worker `1694.88 tables/s`,
  2 workers `2132.66 tables/s`; checksums matched.
- Release smoke benchmark, three paired runs over 1,024 flop tables: 1 worker
  median about `10906 tables/s`, 2 workers median about `21512 tables/s`;
  all checksums matched.

### Limitations

- This is build-only Debug evidence; Release five-run qualification and
  end-to-end publication gates remain outstanding.

## F1 bucket generation - incremental catalog advancement

**Status:** Complete
**Completed:** 2026-08-31

- Changed the production fixed-board catalog iterator to unrank its first
  board once and advance subsequent combinations lexicographically.
- Added a range differential test covering exact index ordering.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_catalog.hpp`
- `tests/test_multiway_bucket_catalog.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: build OK, all tests passed.

### Limitations

- Worker assignment vectors and object handoff allocations remain; serialized
  worker buffers, telemetry, and performance qualification remain deferred.

## F1 bucket generation - allocation-free catalog handoff

**Status:** Complete
**Completed:** 2026-08-31

- Added a fixed-board catalog callback that avoids constructing a temporary
  board request vector for every catalog entry.
- Routed production generation through the allocation-free catalog callback.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_catalog.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: build OK, all tests passed.

### Limitations

- Table assignment vectors still allocate per table; serialized worker chunks
  and performance qualification remain deferred.

## F1 bucket generation - trusted fixed-board kernel

**Status:** Complete
**Completed:** 2026-08-31

- Added a fixed-board generation path that precomputes board metadata once and
  avoids per-hole public validation and board-vector scans.
- Routed catalog chunk generation through the new path.
- Added differential coverage against the checked builder.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: build OK, all tests passed.

### Limitations

- Assignment vectors and table objects remain allocated at chunk boundaries;
  full serialized worker chunks and performance qualification remain deferred.

## F1 bucket generation - bulk artifact publication

**Status:** Complete
**Completed:** 2026-08-31

- Added chunked artifact serialization and hashing with one contiguous write per
  generated chunk.
- Updated the workflow publisher while preserving artifact bytes, ordering,
  schema, checksum, checkpoint, and resume behavior.
- Added byte-identical regression coverage against single-table publication.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_catalog.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: build OK, all tests passed.

### Limitations

- Trusted allocation-free generation, telemetry, topology-aware worker
  selection, scheduler replacement, and benchmark acceptance evidence remain
  deferred.

## F1 bucket generation - startup diagnostics

**Status:** Complete
**Completed:** 2026-08-31

- Added startup output for `texas_multiway_buckets` before configuration loading,
  followed by a goal summary with profile, model identity, configuration
  fingerprint, table target, sizing, and effective worker settings.

### Files

- `examples/multiway_workflow_main.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 107/107 tests passed.
- `git diff --check` passed.

### Limitations

- None identified.

## F1 bucket generation - test-count documentation correction

**Status:** Complete
**Completed:** 2026-08-31

- Updated current README, project-state, and human handoff references from
  106 to 107 registered tests after adding the parallel-generation suite.

### Validation

- Existing full-build result: 107/107 tests passed.
- `git diff --check` passed.

## F1 bucket generation - bounded parallel publisher

**Status:** Complete
**Completed:** 2026-08-31

- Added a fixed worker-pool scheduler with 256-table chunks, bounded in-flight
  results, ordered publication, and a synchronous `--threads 1` path.
- Added configurable bucket worker count with a default of 16 and hardware
  concurrency clamping.
- Preserved serialized table order, payload hashes, checkpoints, resume, and
  atomic publication while adding checkpoint-boundary progress/ETA output.
- Added failure checkpoint preservation and reduced deterministic, resume,
  street-boundary, backpressure, and publication tests.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 107/107 tests passed.
- `git diff --check` passed.

## F1-WP5.8 - Human-command JSON escaping regression

**Status:** Complete
**Completed:** 2026-08-31

- Added coverage for quoted configuration paths in human command metadata.
- Verified the final acceptance JSON preserves valid escaped command strings.

### Files

- `tests/test_multiway_f1_acceptance.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP5.7 - Complete evidence-chain publication gate

**Status:** Complete
**Completed:** 2026-08-31

- Made all five source-report references mandatory for a passing final report.
- Added negative coverage proving a report without source references cannot be
  published.

### Files

- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `tests/test_multiway_f1_acceptance.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP5.6 - Mandatory source-report references

**Status:** Complete
**Completed:** 2026-08-31

- Required all five immutable source-report paths before a passing final
  acceptance report can be published.
- Added regression coverage for missing source references.

### Files

- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `tests/test_multiway_f1_acceptance.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP5.5 - Strict finalizer publication contract

**Status:** Complete
**Completed:** 2026-08-31

- Required producer evidence and human provenance on every published passing
  acceptance report.
- Escaped human metadata and source paths before writing JSON output.
- Added a regression test rejecting a manually constructed pass without
  evidence and verified Windows path handling.

### Files

- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `tests/test_multiway_f1_acceptance.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP5.4 - Final acceptance evidence-chain output

**Status:** Complete
**Completed:** 2026-08-31

- Extended `final_acceptance.json` with the accepted producer evidence header,
  human-run metadata, and all source-report paths.
- Added regression assertions that persisted finalization emits source evidence
  references.
- Fixed Windows test cleanup by closing the finalizer output report before
  removing it.

### Files

- `include/solver/multiway/workflow/multiway_f1_acceptance.hpp`
- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `tests/test_multiway_f1_acceptance.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP6 - Human execution handoff package

**Status:** Prepared
**Completed:** 2026-08-31

- Added the exact candidate build/test command template and expected registered
  suite result to the F1 qualification README.
- Added a YAML run-record template covering provenance, commands, exit codes,
  machine data, reports, and artifact references.
- Kept production qualification unqualified until a human supplies the run
  records and immutable evidence files.

### Files

- `docs/qualification/f1/README.md`
- `docs/qualification/f1/human_run_record.template.yaml`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP1.6 - Failure-path training evidence

**Status:** Complete
**Completed:** 2026-08-31

- Added a failure-path report publication boundary so partial telemetry is
  persisted before training rethrows a workflow failure.
- Added an explicit failed-report state that permits incomplete merge evidence
  while retaining the observed counters, high waters, and memory telemetry.
- Added regression coverage for partial failed reports and exact accepted-count
  accounting.

### Files

- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP1.5 - Accepted and discarded trajectory accounting

**Status:** Complete
**Completed:** 2026-08-31

- Added an explicit accepted-trajectory field to training reports while
  retaining scheduled/attempted trajectory totals.
- Derived accepted counts from sampler discard telemetry and made the persisted
  finalizer consume the explicit field.
- Published diagnostic training reports before the workflow raises on coverage
  or discard failure.
- Added regression coverage for attempted, accepted, and discarded counts.

### Files

- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `src/solver/multiway/workflow/multiway_sizing_report.cpp`
- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP1.4 - Checkpoint-persisted street visits

**Status:** Complete
**Completed:** 2026-08-31

- Persisted preflop, flop, turn, and river visit counters in the training
  checkpoint payload and payload hash.
- Incremented the training checkpoint format schema and restored counters after
  resume without adding work to traversal inner loops.
- Added checkpoint round-trip coverage for the counters.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `tests/test_multiway_blueprint_training.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP1.3 - Per-street visit telemetry and checkpoint persistence

**Status:** Complete
**Completed:** 2026-08-31

- Added preflop, flop, turn, and river decision-visit counters at the existing
  coordinator row-admission boundary.
- Added the counters to training status and serialized training reports.
- Persisted and restored the counters in training checkpoints with a format
  schema increment, including payload-hash coverage.
- Added report and checkpoint round-trip coverage while keeping the hot path
  free of new allocation, formatting, or filesystem work.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

## F1-WP1.2 - Artifact publication telemetry

**Status:** Complete
**Completed:** 2026-08-31

- Added checkpoint byte length and cumulative write duration telemetry at
  training checkpoint boundaries.
- Added full-blueprint byte length and export/publication duration telemetry
  on the cold publication path.
- Serialized the measurements and added regression coverage for their units and
  preservation.

### Files

- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

### Limitations

- Current solver diagnostics expose aggregate terminal and leaf visits, not
  per-street visit counters. The report still records per-street row counts;
  per-street visits require a separate diagnostics extension.

## F1-WP5.3 - Finalizer command and human handoff package

**Status:** Complete
**Completed:** 2026-08-31

- Added the `texas_multiway_finalize` research executable with explicit paths
  for every persisted report and explicit human-run provenance arguments.
- Added F1 qualification README and machine/acceptance report templates.
- Extended CMake boundary coverage for the finalizer target and preserved the
  non-installed research-workflow boundary.

### Files

- `examples/multiway_workflow_main.cpp`
- `CMakeLists.txt`
- `tests/cmake/test_example_boundary.cmake`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/qualification/f1/README.md`
- `docs/qualification/f1/acceptance_report.template.json`
- `docs/qualification/f1/machine_report.template.json`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

### Limitations

- WP6-WP12 still require human execution of the compiled build, bucket
  generation, sizing pilot, checkpoint-equivalence runs, 50-million-trajectory
  acceptance run, lookup qualification, and final evidence audit.

## F1-WP5.2 - Persisted report finalization

**Status:** Complete
**Completed:** 2026-08-31

- Added complete producer-identity fields to serialized evidence headers and
  added evidence headers to inspection and checkpoint-equivalence reports.
- Added a path-based finalizer that reads existing bucket, training,
  equivalence, and both lookup reports without rerunning solver work.
- Enforced required fields, exact schemas, report presence, matching producer
  identities, lookup parity, and atomic final publication.
- Added persisted positive, missing-report, and second-run provenance mismatch
  tests.

### Files

- `include/solver/multiway/workflow/multiway_f1_acceptance.hpp`
- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `include/solver/multiway/workflow/multiway_checkpoint_equivalence.hpp`
- `src/solver/multiway/workflow/multiway_checkpoint_equivalence.cpp`
- `src/solver/multiway/workflow/multiway_evidence.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_f1_acceptance.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

### Limitations

- The finalizer API is available as a library boundary but is not yet exposed
  as a dedicated command-line workflow in `multiway_workflow_main.cpp`.

## F1-WP3.2 - Training and inspection publication preflight

**Status:** Complete
**Completed:** 2026-08-31

- Added checked full-blueprint size and process-memory estimates before a
  training publication can allocate or publish output.
- Added report-output preflight for inspection JSON publication, including
  output-directory, disk-space, and verified-sidecar overwrite checks.
- Kept both checks on the workflow cold path and left traversal behavior
  unchanged.

### Files

- `examples/multiway_workflow_main.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

### Limitations

- The workflow executable does not yet emit a schema-versioned inspection
  evidence header or invoke the final acceptance aggregator.

## F1-WP2.2 - Bounded sizing-pilot report

**Status:** Complete
**Completed:** 2026-08-31

- Added a cold-path sizing report that preserves observed capacity high-water
  marks, RSS, elapsed time, throughput, trajectory counts, and failure stage.
- Preserved capacity and memory-limit failures as diagnostic reports with
  `passed=false`; acceptance-profile evidence cannot be used as sizing input.
- Added the finite bounded `F1-SIZING-12-v1` pilot configuration. Its limits are
  pilot defaults and require human review against the available machine
  envelope before execution.
- Added focused tests for successful sizing, capacity failure, RSS failure,
  profile separation, and atomic publication.

### Files

- `configs/multiway/f1_sizing_v1.cfg`
- `include/solver/multiway/workflow/multiway_sizing_report.hpp`
- `src/solver/multiway/workflow/multiway_sizing_report.cpp`
- `tests/test_multiway_sizing_report.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 106/106 tests passed.
- `git diff --check` passed.

### Limitations

- Pilot capacity values are intentionally bounded defaults, not acceptance
  capacities. A human must review and approve them before the sizing run.

## F1-WP5.1 - Final acceptance evidence aggregation

**Status:** Complete
**Completed:** 2026-08-31

- Added typed aggregation for bucket, training, resume-equivalence, and lookup
  evidence with shared producer-identity validation.
- Enforced acceptance-profile provenance, exact trajectory count, zero discard
  count, lookup parity, and successful human-run metadata before publication.
- Restricted atomic final-report publication to passing evidence and added
  focused positive and rejection tests.

### Files

- `include/solver/multiway/workflow/multiway_f1_acceptance.hpp`
- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `tests/test_multiway_f1_acceptance.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 105/105 tests passed.
- `git diff --check` passed.

### Limitations

- The finalizer currently consumes typed evidence supplied by the workflow; it
  does not yet read the existing JSON reports and checkpoint directories by
  path.

## F1-WP4.1 - Streaming checkpoint equivalence

**Status:** Complete
**Completed:** 2026-08-31

- Added fixed-buffer streaming comparison for continuous and resumed blueprint
  files with byte counts and payload fingerprints.
- Compared model/configuration identity, seed, schedule, coverage, street rows,
  terminal/leaf/discard counters, merge fingerprint, and completed trajectories.
- Restricted atomic equivalence publication to reports where every equality gate
  passes.
- Added focused equality, payload-difference, metadata-difference, truncation,
  and publication tests.

### Files

- `include/solver/multiway/workflow/multiway_checkpoint_equivalence.hpp`
- `src/solver/multiway/workflow/multiway_checkpoint_equivalence.cpp`
- `tests/test_multiway_checkpoint_equivalence.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 104/104 tests passed.
- `git diff --check` passed.

### Limitations

- The comparator currently consumes run metadata supplied by the workflow; it
  does not yet parse training JSON reports or checkpoint directories itself.

## F1-WP3.1 - Artifact output preflight

**Status:** Complete
**Completed:** 2026-08-31

- Added cold-path artifact preflight validation for output directories, free
  disk space, process-memory estimates, and verified-manifest overwrite refusal.
- Rejected inconsistent temporary/sidecar resume states and validated existing
  sidecars against model identity and expected table count.
- Wired preflight into the bucket workflow before writer creation, including a
  checked worst-case payload-size estimate.
- Added focused tests for valid output, verified overwrite, partial resume,
  malformed sidecars, and memory overcommit.

### Files

- `include/solver/multiway/workflow/multiway_artifact_preflight.hpp`
- `src/solver/multiway/workflow/multiway_artifact_preflight.cpp`
- `tests/test_multiway_artifact_preflight.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 103/103 tests passed.
- `git diff --check` passed.

### Limitations

- Training and inspection report publication still need their own artifact
  preflight wiring.

## F1-WP2.1 - Sizing and acceptance profile separation

**Status:** Complete
**Completed:** 2026-08-31

- Added explicit `profile_kind` parsing and configuration identity coverage.
- Enforced the fixed 50-million trajectory target for acceptance profiles.
- Enforced a smaller-than-acceptance trajectory target for sizing profiles.
- Included profile kind in workflow fingerprints and updated the F1 acceptance
  configuration.

### Files

- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `configs/multiway/f1_dev_v1.cfg`
- `tests/test_multiway_workflow_config.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 102/102 tests passed.
- `git diff --check` passed.

### Limitations

- `f1_sizing_v1.cfg` and human-approved finite sizing capacities remain
  pending.

## F1-WP1.1 - Batch-boundary training telemetry

**Status:** Complete
**Completed:** 2026-08-31

- Added configured capacity and high-water counters to blueprint training
  status.
- Added elapsed wall time, throughput calculation, compact-storage usage, and
  current/peak process RSS telemetry with availability handling.
- Added per-batch admitted-row growth samples and a validated capacity
  exhaustion stage.
- Published the telemetry in versioned training reports and validated invalid
  RSS and throughput boundaries.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 102/102 tests passed.
- `git diff --check` passed.

### Limitations

- Checkpoint/blueprint byte telemetry and per-street visit counters are still
  pending.

## F1-WP0.1 - Shared evidence identity primitives

**Status:** Complete
**Completed:** 2026-08-31

- Added version constants and profile kinds for F1 evidence schemas.
- Added validated producer identity fields for model, workflow, build, compiler,
  commit, and artifact schema provenance.
- Added matching-identity validation, deterministic header serialization, and a
  reusable evidence header.
- Attached the header to training and lookup report publication paths and made
  provenance explicit at their producer APIs.

### Files

- `include/solver/multiway/workflow/multiway_evidence.hpp`
- `src/solver/multiway/workflow/multiway_evidence.cpp`
- `tests/test_multiway_evidence.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 python scripts\\full_build.py` passed: build succeeded and 102/102 tests passed.
- `git diff --check` passed.

### Limitations

- Sizing, checkpoint-equivalence, bucket-inspection, and final-acceptance report
  types and their aggregators remain later WP0/F1 work.

## F1 qualification harness and human execution plan

**Status:** Complete
**Completed:** 2026-08-31

- Added a detailed F1 implementation and qualification plan covering missing
  telemetry, sizing, artifact preflight, checkpoint equivalence, strict final
  evidence aggregation, and the production acceptance sequence.
- Established that LLM agents may inspect and edit repository files but only a
  human may build or execute compiled programs, tests, workflows, or workloads.
- Defined immutable evidence directories, human run records, failure handling,
  LLM task packets, and exact F1 completion gates.

### Files

- `docs/f1_human_execution_implementation_plan.md`
- `docs/project_state.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the current F1 configuration, workflow entry point, training report,
  project state, repository guidance, and resulting documentation diff.
- Ran Markdown heading and execution-boundary searches plus `git diff --check`.
- Builds, tests, and compiled programs were not run, as required by the new
  human-only execution boundary.

### Limitations

- This task creates the implementation/execution plan only. Qualification
  harness code, human validation, production artifacts, and F1 acceptance
  evidence remain pending.

## Current implementation state reconstruction

**Status:** Complete
**Completed:** 2026-08-31

- Restored the authoritative project-state document after its removal in
  `292f777`.
- Reassessed the current implementation against the Pluribus technical goal,
  README, repository structure, F1 configuration, recent change summary, and
  Git history through `292f777`.
- Distinguished implemented subsystem contracts from the still-unqualified F1
  artifact pipeline, remaining runtime-fidelity work, 64 GiB measurements, and
  strategy-quality evidence.

### Files

- `docs/project_state.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `scripts/list_recent_changes.py` completed and reported a clean worktree at
  the start of the task.
- `scripts/repo_summary.py` and `scripts/cmake_summary.py` completed.
- Reviewed the current configuration, multiway module/test surface, artifact
  directory, process state, relevant historical documents, and recent commits.
- Reviewed the documentation diff and ran `git diff --check`.
- Builds and tests were not run because this task only updates documentation
  and execution was not requested.

### Limitations

- Production artifacts and external evidence were unavailable for inspection.
- The report records current implementation maturity; it does not qualify F1,
  the 64 GiB target, convergence, exploitability, or playing strength.

## F1.28 - Verified bucket manifest publication

**Status:** Complete
**Completed:** 2026-08-28

- Successful bucket generation now reopens and fully inspects the atomically published binary.
- The workflow rejects incomplete canonical street coverage and atomically writes `buckets.manifest` with the configuration fingerprint, identity, byte length, counts, live assignments, and payload hash.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- `texas_multiway_inspect` Debug target built successfully.

### Limitations

- The active production process predates this workflow change; its final binary will be inspected and given an equivalent manifest after publication.

## F1.27 - Streaming bucket registry load and exercised production resume

**Status:** Complete
**Completed:** 2026-08-28

- Replaced the workflow's full serialized bucket byte copy with a streaming registry loader.
- Exercised the active production generator's resume path at the verified turn boundary.
- Safely drops only uncheckpointed tail bytes after sidecar-prefix verification.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_artifact.cpp`
- `tests/test_multiway_bucket_catalog.cpp`

### Validation

- `cmake --build build --target texas test_multiway_bucket_artifact --config Debug` passed.
- `test_multiway_bucket_artifact.exe` passed all 6 tests.
- `cmake --build build --target texas_multiway_train texas_multiway_evaluate --config Debug` passed.
- Production generation resumed from 292,825 verified tables after sidecar validation.

### Limitations

- River generation and all F1 training qualification workloads remain active or pending.

## F1.26 - Frozen sizing contract and resumable bucket generation

**Status:** Complete
**Completed:** 2026-08-28

- Added numeric capacity parsing, profile fingerprinting, and a hard training refusal until every sizing field is frozen.
- Wired frozen limits and the `CompactInt32` backend into the training session and saves full checkpoints at fixed batch intervals.
- Added flushed, periodic bucket sidecars and fixed rolling-hash resume validation.

### Files

- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_bucket_catalog.cpp`

### Validation

- `cmake --build build --target texas --config Debug` passed.
- `cmake --build build --target test_multiway_workflow_config test_multiway_bucket_catalog --config Debug` passed.
- `test_multiway_workflow_config.exe` passed all 4 tests.
- `test_multiway_bucket_catalog.exe` passed all 6 tests.

### Limitations

- `f1_dev_v1.cfg` remains intentionally unresolved until the active sizing run yields measured limits.
- The full bucket generator is still running; no production artifact has been published.

## F1.25 - Deterministic required-lookup qualification

**Status:** Complete
**Completed:** 2026-08-28

- Added deterministic sampled replay from an initial root with opponent policy requests routed through the full blueprint provider.
- Records and publishes hit, missing-infoset, missing-bucket, and menu-mismatch counters plus two replay fingerprints.
- Wired `texas_multiway_evaluate --artifacts <dir> --duplicates <count>` to require matching repeated replays and return nonzero on any required lookup failure.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_store.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_store.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_policy_provider.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/workflow/multiway_lookup_qualification.hpp`
- `src/solver/multiway/workflow/multiway_lookup_qualification.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_blueprint_store.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The gate has not been run against a production bucket/blueprint pair because neither artifact has been generated or published.

## F1.24 - Durable full-training checkpoint

**Status:** Complete
**Completed:** 2026-08-28

- Added versioned, hashed, atomic binary persistence for full multiway training state.
- Restores sparse row shapes and values, public descriptors, coverage, schedule/seed identity, and resume diagnostics.
- Wired `texas_multiway_train --checkpoint-dir` and `--resume` to the artifact boundary.

### Files

- `include/solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Production sizing, full bucket generation, disk-resume equivalence, and required-lookup qualification remain unexecuted.

## F1.23 - Workflow output path handling

**Status:** Complete
**Completed:** 2026-08-28

- Guarded directory creation for empty parent paths in bucket and report workflows.
- Verified filename-only output paths remain valid through the full build/test workflow.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Full checkpoint serialization and production acceptance remain pending.

## F1.22 - Executable training acceptance gates

**Status:** Complete
**Completed:** 2026-08-28

- Prevented the training workflow from publishing when it discards trajectories or lacks nonzero all-street row and terminal coverage.
- Verified the command-boundary change with the full build/test workflow.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The gate is ready for production execution but has not been satisfied by a production-scale workload.

## F1.21 - Checkpoint coverage preservation

**Status:** Complete
**Completed:** 2026-08-28

- Added per-street row counts to the public coverage manifest.
- Preserved those counters through in-memory checkpoint restore and report generation.
- Verified the change with the full build/test workflow.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The new coverage fields are not yet encoded in a disk checkpoint format.

## F1.20 - Full checkpoint validation tests

**Status:** Complete
**Completed:** 2026-08-28

- Added regression tests for sparse shape/value count mismatches and non-finite checkpoint accumulators.
- Reconfigured and verified the full build/test workflow.

### Files

- `tests/test_multiway_blueprint_training.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Disk serialization and cross-process resume equivalence remain pending.

## F1.19 - Full checkpoint invariant validation

**Status:** Complete
**Completed:** 2026-08-28

- Added validation for checkpoint identity, trajectory/batch bounds, row-shape/value cardinality, finite accumulators, and late-window baselines.
- Applied validation when creating and restoring in-memory checkpoints.
- Verified the change with the full build/test workflow.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Disk serialization of the validated full checkpoint remains pending.

## F1.18 - Storage backend in workflow identity

**Status:** Complete
**Completed:** 2026-08-28

- Included the declared storage backend in the workflow configuration fingerprint.
- Reconfigured and verified the full build/test workflow.

### Files

- `src/solver/multiway/workflow/multiway_workflow_config.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Full checkpoint serialization and production acceptance remain pending.

## F1.17 - Training report negative-path tests

**Status:** Complete
**Completed:** 2026-08-28

- Added regression coverage for impossible discard counts and missing completed-run fingerprints.
- Reconfigured and verified the full build/test workflow.

### Files

- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Full checkpoint serialization and production qualification remain pending.

## F1.16 - Training report validation

**Status:** Complete
**Completed:** 2026-08-28

- Added report validation for impossible discard counts and missing completed-run merge fingerprints.
- Applied validation before atomic JSON publication and verified the full build/test workflow.

### Files

- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Validation does not replace the required production training and replay runs.

## F1.15 - Artifact payload hash alignment

**Status:** Complete
**Completed:** 2026-08-28

- Aligned streaming inspector hashing with the writer’s serialized little-endian 32-bit assignment encoding.
- Verified the correction with the full build/test workflow.

### Files

- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- A production artifact hash comparison still requires generation and inspection of the complete artifact.

## F1.14 - Workflow configuration fingerprint

**Status:** Complete
**Completed:** 2026-08-28

- Added a stable FNV-1a fingerprint over the frozen workflow semantic fields.
- Added regression coverage proving semantic trajectory changes alter the fingerprint.
- Reconfigured and verified the full build/test workflow.

### Files

- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `tests/test_multiway_workflow_config.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Pilot capacity fields are intentionally excluded until they are resolved and frozen.

## F1.13 - Qualification report contract tests

**Status:** Complete
**Completed:** 2026-08-28

- Added focused tests for the zero-miss qualification gate, atomic report publication, and training telemetry preservation.
- Corrected test stream cleanup and verified all registered tests.

### Files

- `tests/test_multiway_qualification_reports.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all 101 tests.

### Limitations

- Tests cover report contracts, not production replay or full-training qualification.

## F1.12 - Deterministic merge fingerprint propagation

**Status:** Complete
**Completed:** 2026-08-28

- Propagated the coordinator’s actual merged-stream fingerprint into training status and report output.
- Removed the training command’s placeholder batch-count fingerprint.
- Verified the change with the full build/test workflow.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `examples/multiway_workflow_main.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Disk checkpoint equivalence still requires persistent full checkpoint serialization and two actual training runs.

## F1.11 - Discarded-trajectory telemetry

**Status:** Complete
**Completed:** 2026-08-28

- Propagated coordinator discarded-trajectory counts into training status and JSON report output.
- Removed the report’s previous non-authoritative hard-coded zero.
- Verified the change with the full build/test workflow.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The value is available after actual training execution; no production acceptance run has been performed.

## F1.10 - Qualification evidence template

**Status:** Complete
**Completed:** 2026-08-28

- Added a machine-readable template covering every final F1 acceptance predicate, execution command, machine, config hash, and commit field.
- Marked all runtime evidence unresolved until the authorized qualification workloads execute.

### Files

- `docs/qualification/f1_acceptance_report.template.json`
- `docs/qualification/f1_acceptance_v1.md`

### Validation

- Reviewed JSON structure and Markdown diff; no build was required for documentation-only changes.

### Limitations

- The template is not an acceptance report and contains no measured production evidence.

## F1.9 - Per-street training row telemetry

**Status:** Complete
**Completed:** 2026-08-28

- Added coordinator counters for preflop, flop, turn, and river sparse-row admission.
- Propagated the counters through training status and JSON report output.
- Verified the telemetry changes with the full build/test workflow.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Counters are not yet populated into a completed production qualification report until the training and replay workloads run.

## F1.8 - Required-lookup qualification report contract

**Status:** Complete
**Completed:** 2026-08-28

- Added typed counters for lookup hits, missing infosets, missing buckets, and action-menu mismatches.
- Added the explicit zero-miss gate and atomic JSON report publication boundary.
- Verified the package with the full build/test workflow.

### Files

- `include/solver/multiway/workflow/multiway_lookup_qualification.hpp`
- `src/solver/multiway/workflow/multiway_lookup_qualification.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Deterministic replay integration and actual zero-miss qualification runs remain pending.

## F1.7 - Executable bounded training workflow

**Status:** Complete
**Completed:** 2026-08-28

- Wired `texas_multiway_train` to load a bucket artifact, construct six uniform all-combination ranges, start at the preflop root, run bounded batches, export the full blueprint, and publish a JSON report.
- Kept production-scale capacity and acceptance claims unresolved until the authorized sizing and qualification runs.

### Files

- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Training still loads the bucket artifact into memory, does not resume full checkpoints, and does not perform required-lookup qualification.

## F1.6 - Training report serialization boundary

**Status:** Complete
**Completed:** 2026-08-28

- Added an atomic JSON training-report writer and a typed report projection from current trainer telemetry.
- Kept unavailable street-row and discarded-trajectory values non-authoritative until sampler telemetry is exposed.
- Verified the new workflow module with the full build/test workflow.

### Files

- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The report projection is not yet wired into a real training command and does not claim street-level or discard qualification.

## F1.5 - Resumable bucket generation progress

**Status:** Complete
**Completed:** 2026-08-28

- Added a versioned progress sidecar with model identity, expected count, next index, rolling hash, and byte length.
- Added verified append/resume support that rejects mismatched or truncated temporary artifacts.
- Preserved atomic final publication and verified the package with the full build/test workflow.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The production bucket command persists a latest sidecar at street boundaries and automatically resumes matching temporary artifacts.

## F1.4 - Bucket artifact inspection boundary

**Status:** Complete
**Completed:** 2026-08-28

- Added identity-checked streaming artifact inspection with exact per-street table counts and live-assignment telemetry.
- Registered the inspector in the production library and verified it through the full build/test workflow.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The executable inspect workflow now emits deterministic telemetry and can atomically publish a JSON report.

## F1.3 - Strict versioned workflow configuration

**Status:** Complete
**Completed:** 2026-08-28

- Added a typed parser for the F1 workflow configuration.
- Added duplicate-key, unknown-key, malformed-value, unresolved-capacity, and semantic-profile rejection.
- Added focused parser tests and registered the workflow module in CMake.

### Files

- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `CMakeLists.txt`

### Validation

- `git diff --check` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- The executable workflows do not yet consume this parser; checkpoint persistence and production qualification remain pending.

## F1.2 - Bounded bucket artifact writer

**Status:** Complete
**Completed:** 2026-08-28

- Added incremental serialization of bucket tables without retaining the full artifact in memory.
- Added table-count, shape, byte-length, rolling payload-hash, and atomic-publication guards.
- Added a byte-for-byte regression test against the existing reference serializer.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `tests/test_multiway_bucket_catalog.cpp`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Resumable sidecars, streaming inspection, and production artifact generation remain pending.

## F1.1 - Deterministic postflop bucket catalog and acceptance contract

**Status:** Complete
**Completed:** 2026-08-28

- Added bounded lexicographic board enumeration for flop, turn, and river.
- Added exact catalog counts, shard validation, and stable catalog fingerprints.
- Added the versioned F1-DEV-12-v1 configuration template and acceptance predicates.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_catalog.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_catalog.cpp`
- `tests/test_multiway_bucket_catalog.cpp`
- `configs/multiway/f1_dev_v1.cfg`
- `docs/qualification/f1_acceptance_v1.md`
- `CMakeLists.txt`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Streaming artifact generation, persistent training checkpoints, production workflows, and qualification runs remain pending.

## F2 pruning exploration correction

**Status:** Complete
**Completed:** 2026-08-28

- Applied the configured 5% deterministic per-trajectory recovery probability
  to traversal-time action pruning.
- Preserved warmup, recovery-batch, immediate-terminal, and river exemptions.

### Files

- `src/solver/multiway/engine/multiway_traversal.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- F1-F3 remain foundation work; full-scale artifact qualification and target-memory measurement are still pending.

## F1 global training defaults

**Status:** Complete
**Completed:** 2026-08-28

- Changed the blueprint training defaults to traverse the full bounded
  preflop-to-river route: all three public chance boundaries and the supported
  decision-depth cap.
- Retained explicit shallow limits for constrained DEV profiles and tests.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- A complete canonical bucket artifact and measured qualification workload are still required for F1 acceptance.

## F3 compact quantization hardening

**Status:** Complete
**Completed:** 2026-08-28

- Hardened compact regret and strategy-mass quantization against extreme finite
  deltas before integer conversion.
- Added saturating accumulation coverage for compact storage.

### Files

- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Compact storage still requires measured DEV artifact qualification against the 64 GB target.

## Pluribus solver finish audit

**Status:** Complete
**Completed:** 2026-08-27

- Compared the declared project state and Pluribus technical requirements with the current multiway implementation.
- Recorded fourteen prioritized implementation gaps with dependencies, acceptance conditions, finish criteria, and scope boundaries.
- Identified global preflop-to-river training, pruning fidelity, compact storage, dual online policies, production card abstraction, and executable workflows as the primary blockers.

### Files

- `docs/pluribus_solver_finish_audit.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the relevant multiway headers, implementations, workflow entry points, documentation, and resulting Markdown diff.
- Build and tests were not run because this task only creates audit documentation and execution was not requested.

### Limitations

- The audit is based on static inspection. Runtime performance, convergence, and policy strength remain unverified.

## Project README expansion

**Status:** Complete
**Completed:** 2026-08-27

- Replaced the brief repository overview with a detailed Pluribus project guide for users, developers, and LLM agents.
- Documented project goals, maturity, architecture, solver families, build and package usage, helper scripts, the vendored evaluator boundary, repository skills, development constraints, and remaining milestones.

### Files

- `README.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- Build and tests were not run because this change only updates Markdown documentation.

### Limitations

- Production strategy quality, calibrated artifacts, and target-hardware performance remain unproven as documented in the README and project state.

## Solver layout migration

**Status:** Complete
**Completed:** 2026-08-27

- Moved all solver headers and implementations into mirrored ownership subdirectories.
- Updated repository includes, CMake classifications, boundary fixtures, examples, README, and project-state paths.

### Files

- `CMakeLists.txt`
- `include/solver/`
- `src/solver/`
- `include/`, `src/`, `tests/`, `examples/`
- `README.md`
- `docs/project_state.md`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- None identified.

Update this file after each completed roadmap part. Record completed scope,
files, verification, and any limitations. Do not claim an item is complete
until its implementation and required validation are finished.

## F7 continuation policy and search-depth calibration

**Status:** Complete
**Completed:** 2026-08-27
**Implementation commit:** `3e754ea feat(multiway): add continuation calibration gates`

- Added typed calibration cases for blueprint and fixed continuation policies.
- Added rollout-sample, cache-limit, street-boundary, bias-factor, and versioned identity configuration.
- Added held-out absolute error, bias, variance, and nested-improvement gates against blueprint error.
- Added finite-input validation and regression coverage.

### Files

- `CMakeLists.txt`
- `include/solver/multiway_continuation_calibration.hpp`
- `src/solver/multiway_continuation_calibration.cpp`
- `tests/test_multiway_continuation_policy.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 cmake -S . -B build` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Production promotion still requires real stratified leaf rollouts, serialized multi-worker continuation comparisons, and target-hardware measurements.

## F6 future-bucket calibration

**Status:** Complete
**Completed:** 2026-08-27

- Added deterministic held-out sample splitting for future-street bucket calibration.
- Added artifact-size, missing-bucket, within-bucket variance, and held-out policy-loss gates.
- Added deterministic selection of the smallest passing future-bucket profile.
- Reused the existing immutable producer, serializer, disk boundary, and identity checks.
- Added repeated-board regression coverage and corrected missing-bucket accounting during review.

### Implementation commits

- `4297f2d`: future-bucket calibration API, metrics, and tests.
- `c3f19cf`: held-out loss and within-bucket variance correction.
- `89de8c6`: smallest passing profile selection.
- `c8a3acd`: missing-bucket accounting fix.

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 cmake -S . -B build` passed.
- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Production-quality promotion still requires a real private-card-independent training and held-out dataset plus target deployment measurements.

## F3-F5 full-hand evaluation and action calibration

**Status:** Complete
**Completed:** 2026-08-27

- Completed full-hand lifecycle regression coverage for folds, all-ins, side pots, settlement, street reroots, and frozen-policy decisions.
- Added independent AIVAT evaluation with deterministic identity validation and frozen-prior support.
- Added representative action-abstraction calibration across contextual menus, pseudo-harmonic translation, deviation expansion, artifact coverage, policy value, memory, and latency gates.
- Added statistical frozen-baseline profile selection and an adapter from F4 Nash-convergence results to calibration quality.

### Implementation commits

- `c9f4da6`, `63383bf`, `60b1ca7`, `c1f855f`: F3 lifecycle contracts and dedicated regressions.
- `2067f89`: independent AIVAT evaluation.
- `9ce51fd`, `cc1d2a0`, `8506d46`, `b58c2f2`, `51046ee`, `4e1c79a`, `8da4a58`: F5 calibration, selection, evaluation, artifact-coverage, and latency gates.

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all tests.

### Limitations

- Production profile promotion still requires real trained artifacts and target-hardware measurements.

## Pluribus audit round 5 remediation

**Status:** Complete
**Completed:** 2026-08-27

- Fixed exact-hand range export and separated exact solver row ids from abstract blueprint buckets.
- Bound blueprint and stable-root fallbacks to complete public, seat, bucket, and private-hand keys.
- Preserved resolver seat order and odd-chip metadata, allowed folded-seat search, and discarded deadline-interrupted batches before merge.
- Added focused regression coverage for the corrected contracts.

### Files

- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_resolver_budget.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_p8_differential.cpp`
- `tests/test_multiway_recursive_traversal.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- `powershell -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/full_build.py` passed: Debug build and all 93 tests.

### Limitations

- None identified.

## Pluribus audit round 2 remediation

**Status:** Complete
**Completed:** 2026-08-26

- Kept canonical private-hand rows for every decision on the root street.
- Learned and regret-mixed all four continuation policies at typed leaf boundaries.
- Extended exact-row capacity admission to same-street descendants.

### Files

- `include/solver/multiway_continuation_selector.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_continuation_selector.cpp`
- `src/solver/multiway_solver.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_continuation_selector.cpp`
- `tests/test_multiway_recursive_traversal.cpp`

### Validation

- `cmake --build build --config Debug --parallel -- /nologo /v:q "/clp:ErrorsOnly;NoSummary"` passed.
- `python .agents/skills/cpp-build-test-fixer/scripts/ctest_compact.py --build-dir build --configuration Debug` passed.

### Limitations

- Continuation-row updates are serialized while worker-local continuation delta merging is deferred.

## Error-log multiway admission and resolver remediation

**Status:** Complete
**Completed:** 2026-08-26

- Added explicit exact-private-hand root metadata so resolver roots do not use continuation bucket validation.
- Accounted for exact root storage in resolver memory and sparse-value budgets.
- Preserved bucketed continuation rows and made sparse-row allocation rollback-safe.

### Files

- `include/solver/multiway_solver.hpp`
- `src/solver/multiway_solver.cpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_traversal.cpp`

### Validation

- `git diff --check` completed successfully.
- Builds and tests were not run per repository instructions.

### Limitations

- The listed regression suite remains unexecuted.

## Pluribus audit round 1 remediation

**Status:** Complete
**Completed:** 2026-08-26
**Implementation commits:** `da61dcf`, `308b93b`, `28611f3`

- Indexed live current-round root rows by exact canonical private-hand ID.
- Rejected parallel rollout evaluation when its mutable context would be shared.
- Added keyed continuation regret rows with deterministic regret-matched selection.
- Added one focused regression test for each audit finding.

### Files

- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_traversal.cpp`
- `include/solver/multiway_continuation_selector.hpp`
- `src/solver/multiway_continuation_selector.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_multiway_continuation_selector.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` completed for each commit.
- Builds and tests were not run because the user explicitly prohibited them.

### Limitations

- Parallel rollout remains unsupported until worker-local context construction is added.

## HUNL Flat DCFR NPZ CRC mismatch repair

**Status:** Complete
**Completed:** 2026-08-26

- Corrected the HUNL Flat DCFR fixture to retain the full 32-bit CRC-32 state.
- Added local-header and central-directory metadata consistency validation.
- Added entry names and expected/calculated values to CRC mismatch diagnostics.

### Files

- `src/util/abstraction.cpp`
- `tests/test_hunl_flat_dcfr.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static diff and whitespace checks completed.
- Build and tests were not run because they were not requested.

### Limitations

- No runtime validation was performed.

## Sampled boundary classification check repair

**Status:** Complete
**Completed:** 2026-08-26

- Scoped the sampled boundary check to the installed public-header list.
- Preserved research-only sampled headers without treating their classification as installation.

### Files

- `tests/cmake/test_hunl_sampled_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static diff and whitespace checks completed.
- Build and tests were not run because they were not requested.

### Limitations

- No runtime validation was performed.

## HUNL Flat DCFR NPZ CRC mismatch analysis

**Status:** Complete
**Completed:** 2026-08-26

- Documented the fixture-side CRC-32 truncation that prevents HUNL bucket-map
  range tests from reaching production range logic.
- Recorded production-side integrity-preserving remediation steps.

### Files

- `docs/hunl_flat_dcfr_crc_mismatch_analysis.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static source-path and CRC implementation review completed.
- Build and tests were not run because they were not requested.

### Limitations

- The malformed fixture remains unchanged; the documented direct repair is a
  separate test change.

## Test failure repair plan

**Status:** Complete
**Completed:** 2026-08-26

- Repaired CRC32 fixture generation and added a full-width CRC regression check.
- Enforced public decision-log provenance and engine consistency.
- Fixed Windows test stream lifetimes, manifest-version expectations, baseline runtime-search setup, bucket identity hashing, cache menu coverage, sampled-root contract coverage, and the sampled research-header classification.

### Files

- `CMakeLists.txt`
- `src/solver/multiway_artifact.cpp`
- `src/solver/multiway_blueprint_trainer.cpp`
- `tests/test_abstraction.cpp`
- `tests/test_abstraction_fixture.hpp`
- `tests/test_multiway_artifact.cpp`
- `tests/test_multiway_baseline.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_ranges_solver_integration.cpp`

### Validation

- `git diff --check` completed.
- Build and tests were not run per user instruction.

### Limitations

- Compiler and test confirmation remain deferred.

## Compile error namespace and resolver repair

**Status:** Complete
**Completed:** 2026-08-26

- Moved process-memory observation into the production multiway memory module.
- Updated affected tests to owning namespaces and active canonical-combo and resolver-limit APIs.
- Restored the preflop research test boundary and source classification.

### Validation

- Static namespace/API/source-list checks and `git diff --check` completed.
- Build and tests were not run per repository instructions.

### Limitations

- Compiler and test confirmation remain deferred.

## Multiway bucket artifact compile-name fix

**Status:** Complete
**Completed:** 2026-08-26

- Added direct poker and canonical-combo dependencies.
- Restored explicit lookup for `canonical_combos`, `rank_of`, and `suit_of` in the multiway artifact implementation.
- Preserved artifact behavior and avoided broad legacy namespace imports.

### Files

- `src/solver/multiway_bucket_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static diff review and `git diff --check` completed.
- Build and tests were not run per repository instructions.

### Limitations

- Compiler and test confirmation remain deferred until explicitly authorized.

## CMake configure source-list fix

**Status:** Complete
**Completed:** 2026-08-26

- Removed the nonexistent `multiway_search_profile.cpp` entry from the stable source list.
- Preserved the header-only search-profile API and successful source classification.

### Files

- `CMakeLists.txt`
- `docs/PLURIBUS_LOG.md`

### Validation

- `cmake -S . -B build` completed successfully.
- `git diff --check` completed.

### Limitations

- Build and tests were not run.

## CMake source classification ordering fix

**Status:** Complete
**Completed:** 2026-08-26

- Moved the stable utility source list before the source-classification check so all first-party `.cpp` files are classified before validation.

### Files

- `CMakeLists.txt`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static diff review and `git diff --check` completed.
- Configure, build, and tests were not run per repository instructions.

### Limitations

- CMake configure remains to be rerun by the user.

## CMake and legacy remediation

**Status:** Complete
**Completed:** 2026-08-26

- Restored resolver policy linkage, explicit source classification, research-target ownership, stable sampled coverage, portable non-loader coverage, evaluator isolation, and public-header fixture coverage.
- Removed retired resolver perturbation code, the tracked machine log, and dead fixed-research state.
- Defined the compatibility-sizing decision and narrowed legacy namespace imports behind an explicit allowlist.

### Files

- `CMakeLists.txt`
- `include/`, `src/`, `tests/`, `docs/`, and evaluator CMake integration

### Validation

- Static diff, source/header classification, symbol, option, target, and documentation-link review completed.
- Configure, build, test, benchmark, install, and solver commands were not run at user request.

### Limitations

- T24 validation matrix remains deferred until explicit authorization.

## CMake and legacy-code static audit

**Status:** Complete
**Completed:** 2026-08-26

- Audited stable, internal, research, legacy, platform, test, install, and documentation boundaries without running build or test commands.
- Recorded ten confirmed build/validation findings and twelve legacy-migration debts.
- Produced a dependency-ordered remediation plan with 24 short tasks for future agents.

### Files

- `docs/cmake_legacy_audit.md`
- `docs/cmake_legacy_remediation_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static CMake, source/header inventory, caller, test-registration, history, documentation-link, and diff checks completed.
- Markdown structure, finding IDs, task IDs, and whitespace checked.
- Configure, build, tests, benchmarks, install, and solver commands were not run at user request.

### Limitations

- Build, link, package-consumer, cross-platform, runtime performance, and solver-quality conclusions remain unverified until their corresponding commands are explicitly authorized.

## P9.3 - Consolidate duplicated hot-path logic

**Status:** Complete
**Completed:** 2026-08-23

- Consolidated standalone and sparse action-major regret matching behind one scalar reference kernel with caller-owned output.
- Preserved action-major row layout, uniform fallback, finite-value checks, scaled normalization, and residual-probability closure without traversal allocation.

### Files

- `include/solver/multiway_cfr.hpp`
- `src/solver/multiway_cfr.cpp`
- `src/solver/multiway_solver.cpp`
- `tests/test_multiway_cfr.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed shared-kernel and sparse-row call sites; static diff checks completed.
- Build and tests not run at user request.

### Limitations

- No performance claim is made without an authorized profile run.

## P9.2 - Remove or isolate superseded perturbation logic

**Status:** Complete
**Completed:** 2026-08-23

- Removed the resolver's bounded deterministic perturbation loop and its synthetic batch counters.
- `LegacyStatic` and `SearchShadow` now deliver only stable-root, blueprint, or static-legal fallback policies. They report `NoRuntimeSearch`; legacy provenance and engine values remain readable for historical artifacts only.

### Files

- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_baseline.hpp`
- `include/solver/multiway_artifact.hpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_multiway_p8_differential.cpp`
- `docs/multiway_release_runbook.md`
- `docs/project_state_report.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed source and focused test changes; static diff checks completed.
- Build and tests not run at user request.

### Limitations

- Release-profile fixture matrix and rollback comparison remain pending authorization.

## P9.1 - Migrate default resolver mode

**Status:** Complete
**Completed:** 2026-08-23

- Added `ReleaseDefault` as the resolver default. It delivers root external-sampling search only with verified root and full-blueprint artifacts, matching buckets, and a complete runtime-search configuration.
- Incomplete release configuration and ineligible requests use the established fallback chain. `LegacyStatic`, `SearchShadow`, and explicit `SearchActive` remain available for rollback and evaluation.

### Files

- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_config.json`
- `docs/multiway_release_runbook.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed source and focused test changes; JSON release configuration parsed successfully.
- Build and tests not run at user request.

### Limitations

- Release-profile fixture matrix and rollback comparison remain pending authorization.

## MinGW Windows Cabinet linkage

**Status:** Complete
**Completed:** 2026-08-23

- Linked the Windows Compression API import library through the `texas` CMake target.
- The dependency is restricted to Windows builds; existing non-Windows linkage is unchanged.

### Files

- `CMakeLists.txt`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static CMake and source review completed.
- CMake configuration, build, and tests not run. The task did not authorize them.

### Limitations

- MinGW UCRT64, MSVC, and non-Windows build validation remains pending authorization.

## MinGW Windows portability audit

**Status:** Complete
**Completed:** 2026-08-23

- Audited first-party platform branches, CMake linkage, examples, tests, scripts, and vendored evaluator configuration for MinGW UCRT64 assumptions.
- Identified the missing CMake Cabinet link required by the Windows Compression API.

### Files

- `docs/mingw_windows_portability_audit.md`
- `docs/mingw_windows_verification_checklist.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static source, CMake, and diff checks completed.
- CMake configuration, build, and tests not run. The task did not authorize them.

### Limitations

- R1 remains unimplemented and runtime/compiler validation remains pending.

## MinGW Windows environment API portability

**Status:** Complete
**Completed:** 2026-08-23

- Used `_putenv_s` for Windows benchmark and environment-dependent test paths.
- Retained POSIX `setenv` and `unsetenv` paths on non-Windows platforms.

### Files

- `examples/benchmarks/main.cpp`
- `examples/benchmarks/hunl_random_flat_main.cpp`
- `tests/test_hunl_regressions.cpp`
- `tests/test_hunl_state.cpp`
- `tests/test_parallel_dcfr.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static source and diff checks completed.
- Build and tests not run. The task did not authorize them.

### Limitations

- MinGW, MSVC, and non-Windows runtime validation remains pending authorization.

## MinGW Windows portability fixes

**Status:** Complete
**Completed:** 2026-08-23

- Passed the immutable deflate input through the Windows Compression API's mutable pointer declaration without changing ownership or cleanup.
- Used `localtime_s` on all Windows compilers while retaining `localtime_r` on POSIX.
- Guarded `NOMINMAX` against redefinition.

### Files

- `src/util/abstraction.cpp`
- `src/util/profiling.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static source and diff checks completed.
- Build and tests not run. The task explicitly prohibited them.

### Limitations

- MinGW, MSVC, and non-Windows runtime validation remains pending authorization.

## Compact card test-fixture corrections

**Status:** Complete
**Completed:** 2026-08-12

- Updated invalid-card assertions to use values outside the compact `[0, 51]` encoding.
- Aligned multiway resolver and artifact fixture boards with their lookup and root boards.
- Qualified DCFR exploitability helpers after the namespace migration.

### Files

- `include/solver/solver.hpp`
- `tests/test_hunl_card_validation.cpp`
- `tests/test_hunl_state.cpp`
- `tests/test_preflop_equity_validation.cpp`
- `tests/test_solver_exploitability_contract.cpp`
- `tests/test_multiway_baseline.cpp`
- `tests/test_multiway_future_bucket.cpp`
- `tests/test_multiway_p8_differential.cpp`
- `tests/test_multiway_phase5_p53_p56.cpp`
- `tests/test_multiway_range_belief.cpp`
- `tests/test_multiway_resolver.cpp`

### Validation

- Static diff, fixture identity, and whitespace checks completed.
- Build and tests not run. Repository instructions prohibit them unless explicitly requested.

### Limitations

- Runtime validation remains pending user authorization.

## Namespace migration qualification fixes

**Status:** Complete
**Completed:** 2026-08-12

- Qualified utility helpers and private implementation namespaces after the `texas` namespace split.
- Centralized `MultiwayValueUnits` in `texas::solver::multiway` and updated terminal result typing.

### Files

- `include/games/multiway_terminal.hpp`
- `include/core/lib.hpp`
- Solver, game, preflop, example, and test references using migrated private namespaces

### Validation

- Static reference and whitespace checks completed.
- Build and tests not run. Repository instructions prohibit them unless explicitly requested.

### Limitations

- Full compiler verification remains pending user authorization.

## Full namespace migration

**Status:** Complete
**Completed:** 2026-08-12

- Moved first-party APIs from `core::` into the `texas` subsystem hierarchy.
- Renamed the installed CMake target and binary from `texas_core` to `texas`.

### Files

- `include/core/namespaces.hpp`
- `include/`, `src/`, `tests/`, `examples/`, and project documentation
- `CMakeLists.txt`

### Validation

- Static namespace, source/header pairing, and legacy-reference checks completed.
- Build and tests not run. Repository instructions prohibit them unless explicitly requested.

### Limitations

- This intentionally breaks consumers using `core::` or `TexasSolver::texas_core`.

## Card encoding consolidation

**Status:** Complete
**Completed:** 2026-08-12

- Standardized runtime, evaluator, canonical-combo, rollout, and multiway bucket cards on compact `[0, 51]` indices.
- Removed HUNL offset constants and all multiway compact/HUNL adapter APIs.
- Added a checked byte-sized `Card` construction API and versioned bucket artifacts to schema 3.

### Files

- `include/core/card.hpp`
- `include/core/canonical_combo.hpp`
- `include/solver/multiway_bucket_model.hpp`
- `src/games/hunl.cpp`
- `src/solver/multiway_bucket_model.cpp`
- `tests/test_card.cpp`

### Validation

- Static diff review completed.
- Build and tests not run. Repository instructions prohibit them unless explicitly requested.

### Limitations

- Existing public containers retain byte storage while consuming the single compact encoding.

## P8.5-P8.6 Follow-up - Expose AIVAT record facade declarations

**Status:** Complete
**Completed:** 2026-08-12

- Included the AIVAT evaluation-record declaration header before re-exporting its types from `core/lib.hpp`.
- Changed the focused record test to include the public facade, preventing stale facade exports.

### Files

- `include/core/lib.hpp`
- `tests/test_multiway_aivat_record.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static code review completed with no actionable findings.
- Debug build attempted twice but blocked before compilation by duplicate `Path`/`PATH` environment keys in MSBuild.

### Limitations

- CTest was not run because the required build did not complete.

## P8.5-P8.6 - Produce AIVAT-compatible evaluation records and update release runbook and configuration

**Status:** Complete
**Completed:** 2026-08-12

- Added a protected, integrity-sealed AIVAT evaluation-record schema with public history, sampled actions, policy/action-value estimates, raw chip outcomes, model identity, and deterministic decision seeds.
- Added a host-owned protected sink boundary. No AIVAT estimator is implemented or used by runtime traversal.
- Updated the release profile and runbook for full-blueprint and root-fallback artifacts, future-bucket identity, search and deterministic settings, compatibility policy, promotion, and paired rollback.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_evaluation.hpp`
- `src/solver/multiway_evaluation.cpp`
- `tests/test_multiway_aivat_record.cpp`
- `docs/multiway_release_config.json`
- `docs/multiway_release_runbook.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- JSON configuration parsed successfully.
- Static code review completed with no actionable findings.
- Build and tests not run. Repository instructions prohibit them unless explicitly requested.

### Limitations

- The record is an external-estimator boundary only; AIVAT correctness is not claimed.

## P8.1-P8.4 - Differential suites and resolver evaluation adapter

**Status:** Complete
**Completed:** 2026-08-12

- Added chip-exact fixed-versus-dynamic terminal settlement coverage, canonical range duplicate coverage, and artifact identity/hash round-trip coverage.
- Added request-local resolver evaluation candidates for static legal, blueprint-only, search-disabled, and search-enabled policies.
- Each adapter decision derives a deterministic seed and returns resolver policy, status, and provenance to the host-owned evaluation callback.
- Expanded `test_multiway_p8_differential` to 108 deterministic cases across P8.1-P8.4.
- Added the direct bucket-artifact include required by the resolver fixture.
- Made the terminal fixture's hand strengths explicit `Strength` values.
- Added 886 differential assertions and candidate-configuration rejection guards across the P8 fixture matrix.
- Generated range differential cards from the valid HUNL card encoding interval.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver_evaluation.hpp`
- `src/solver/multiway_resolver_evaluation.cpp`
- `tests/test_multiway_p8_differential.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Not run. Repository instructions prohibit build and test commands unless explicitly requested.
- Static code review completed with no remaining actionable findings.

### Limitations

- Cross-play, NashConv, and off-tree orchestration remain callback-owned by the existing evaluation harness.

## P7.5 Follow-up - Guard SIMD row dispatch inputs

**Status:** Complete
**Completed:** 2026-08-12

- Restored scalar-equivalent null and empty-row guards before Float32 and Float64 SIMD dispatch.
- Prevents null input or output pointers from reaching vector loads and stores.

### Files

- `src/solver/hunl_sampled_simd.cpp`
- `tests/test_hunl_p75_row_math.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Built and ran `test_hunl_p75_row_math`: 56 tests passed.
- Completed static code review with no actionable findings.

### Limitations

- No further crash reports or reproducer were supplied.

## P7.5 - Optimize row math only after profile evidence

**Status:** Complete
**Completed:** 2026-08-12

- Profiled a 20-iteration, 1326-bucket HUNL flat run; strategy, regret, and average-row stages accounted for 18.36 ms of 33.22 ms solve time.
- Added AVX2 action-major regret matching with scalar fallback and routed Float64 action-major DCFR strategy rows through the dispatched kernel.
- Preserved scalar NaN behavior and retained Float32 and hand-action paths.

### Files

- `src/solver/hunl_sampled_simd.cpp`
- `src/solver/hunl_flat_dcfr.cpp`
- `tests/test_hunl_p75_row_math.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Built and ran `test_hunl_p75_row_math`: 54 tests passed.
- Built and ran `test_hunl_flat_dcfr`: 35 tests passed.
- Completed static code review with no actionable findings.

### Limitations

- AVX2 compilation remains blocked by a pre-existing intrinsic type error in `src/util/simd.cpp`; no local SIMD speed claim is made.

## P7.1-P7.4 - Dedicated contract test expansion

**Status:** Complete
**Completed:** 2026-08-12

- Added 139 independently registered tests in four dedicated P7 contract suites.
- Covered numeric profile checkpoints, allocation-free partition and delta-stream boundaries, staged memory admission, and versioned deterministic run identity.
- Kept all fixtures small, deterministic, and separately selectable by test name.

### Files

- `tests/test_multiway_p7_profile_contracts.cpp`
- `tests/test_multiway_p7_hot_path_contracts.cpp`
- `tests/test_multiway_p7_memory_contracts.cpp`
- `tests/test_multiway_p7_determinism_contracts.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Counted 139 unique `TEST_CASE` registrations across the four files.
- Confirmed the top-level files match the existing CMake `tests/test_*.cpp` discovery rule.
- Completed static code review with no actionable findings.
- Build and tests were not run because repository instructions prohibit them unless explicitly requested.

### Limitations

- Runtime compilation and execution remain unverified until explicitly requested.

## P7.4 - Validate deterministic worker scheduling and merge

**Status:** Complete
**Completed:** 2026-08-12
**Implementation commit:** `d2dd8d9 feat(multiway): expose deterministic run identity`

- Added versioned deterministic run metadata for worker count, trajectory partition, per-trajectory seed derivation, action sampling, public chance order, and merge order.
- Added canonical merged-stream fingerprints and exact 1-, 2-, and 4-worker replay coverage.
- Kept worker output local until the fixed-order coordinator merge and rejected unimplemented relaxed run modes.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_scheduler.hpp`
- `include/solver/multiway_solver.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_scheduler.cpp`
- `src/solver/multiway_solver.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_recursive_traversal.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_multiway_scheduler.cpp`

### Validation

- Added deterministic same-run, cross-worker, seed, schedule, merged-stream, policy, and worker-local mutation coverage.
- Completed static code review with no remaining actionable findings.
- Build and tests were not run because repository instructions prohibit them unless explicitly requested.

### Limitations

- No relaxed throughput mode is exposed. Deterministic mode remains the only accepted runtime mode.

## P7.3 - Complete memory preflight and staged admission

**Status:** Complete
**Completed:** 2026-08-12
**Implementation commits:** `51544ec feat(multiway): enforce staged memory admission`, `aa8977e fix(multiway): preflight before root allocation`

- Extended preflight to full-blueprint storage, six range rows and compiled copies, future-bucket reservations, off-tree menus, continuation scratch/cache, worker and merge buffers, sparse rows, and root export.
- Added staged root, row, worker-delta, and optional-continuation-cache admission using 48 GiB warning, 56 GiB operating, and 60 GiB hard-cap defaults.
- Moved active-search and runtime-session rejection before request-local root/range construction and disabled typed rollout caching when its optional stage is not admitted.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_blueprint_store.hpp`
- `include/solver/multiway_memory.hpp`
- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_blueprint_store.cpp`
- `src/solver/multiway_memory.cpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_blueprint_store.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_resolver.cpp`

### Validation

- Added component accounting, optional-cache degradation, staged rejection, synthetic maximum, blueprint-memory, resolver rejection, and runtime-session rejection coverage.
- Reviewed the preflight-order fix with no remaining actionable findings.
- Build and tests were not run because repository instructions prohibit them unless explicitly requested.

### Limitations

- Host-owned future bucket and non-rollout continuation capacities must be supplied through the resolver configuration when nonzero.

## P7.2 - Remove hot-path dynamic allocation and textual lookup

**Status:** Complete
**Completed:** 2026-08-12
**Implementation commit:** `1085c76 perf(multiway): preallocate batch merge scratch`

- Added caller-owned deterministic scheduler output and retained partition/thread buffers across root batches.
- Preallocated coordinator merge-stream views, globally ordered delta storage, and transactional pending cells at construction.
- Preserved stable integer identities, contiguous row spans, fixed action arrays, and allocation-free per-cell CFR updates.

### Files

- `include/solver/multiway_scheduler.hpp`
- `include/solver/multiway_solver.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_scheduler.cpp`
- `src/solver/multiway_solver.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_scheduler.cpp`
- `tests/test_multiway_solver.cpp`

### Validation

- Added reusable partition-storage and stable merge-capacity coverage.
- Completed static hot-path review with no remaining actionable findings.
- Build and tests were not run because repository instructions prohibit them unless explicitly requested.

### Limitations

- Lazy public-state admission remains a cold coordinator boundary and retains the existing descriptor ownership model.

## P7.1 - Establish end-to-end search profile checkpoints

**Status:** Complete
**Completed:** 2026-08-12
**Implementation commit:** `e291976 feat(multiway): add search profile checkpoints`

- Added opt-in numeric checkpoints for private deal sampling, public chance, action menus, graph admission, row lookup, regret matching, terminals, continuation leaves, merge, and root export.
- Aggregated worker-local checkpoints after join and added deterministic numeric bottleneck ranking.
- Kept profiling disabled by default so ordinary runs perform no checkpoint clock reads.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_baseline.hpp`
- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_search_profile.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_baseline.cpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_baseline.cpp`

### Validation

- Added checkpoint activation, call-count, aggregation, and bottleneck-ranking coverage.
- Existing baseline surfaces continue to supply wall time, process CPU time, resident memory, peak RSS, and worker imbalance.
- Build and tests were not run because repository instructions prohibit them unless explicitly requested.

### Limitations

- Allocation bytes and hardware cache/branch counters remain unavailable without host allocator or platform profiler integration.

## P6.4 Regression - Public-chance turn leaf bucket fixtures

**Status:** Complete
**Completed:** 2026-08-12

- Updated public-chance traversal fixtures that intentionally reach turn leaves to include canonical turn bucket tables.
- Preserved deterministic path, delta, lazy-admission, and continuation-bucket assertions without changing production traversal behavior.

### Files

- `tests/test_multiway_public_chance_traversal.cpp`

### Validation

- Diagnosed the two reported failures against the leaf bucket lookup and fixture registry contents.
- Build and tests were not rerun because the repository instructions prohibit execution unless explicitly requested.

### Limitations

- Runtime confirmation of the two corrected cases remains pending an explicit test-run request.

## P6.4 Test Coverage - Continuation cache contracts

**Status:** Complete
**Completed:** 2026-08-12

- Added 77 deterministic continuation-cache contract cases with 92 assertions.
- Covered every cache-key validity, context-equivalence, and ordering field; entry and byte caps; deterministic lookup; malformed result rejection; and repeated-seed variance aggregation.
- Kept the suite allocation-bounded and independent of rollout execution fixtures.

### Files

- `tests/test_multiway_continuation_cache.cpp`

### Validation

- Confirmed 77 unique registered test names and reviewed the test diff with `git diff --check`.
- Build and tests were not run because the repository instructions prohibit them unless explicitly requested.

### Limitations

- Runtime execution remains pending an explicit request to run the test suite.

## P6.4 - Add continuation cache and variance diagnostics

**Status:** Complete
**Completed:** 2026-08-12

- Added a fixed-capacity request-local continuation cache keyed by canonical public/future context, model versions, range/reach identity, opaque sampled-deal identity, rollout limits, and seed batch.
- Added private-safe diagnostics for seed, runout mode, samples, selected policy, leaf outcomes, cache activity, per-policy means, and repeated-seed variance.
- Added traversal-generated range and sampled-deal identities so cached values cannot cross incompatible private contexts.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_leaf_evaluator.hpp`
- `include/solver/multiway_rollout_leaf.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_rollout_leaf.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_rollout_leaf.cpp`

### Validation

- Added focused same-seed equality, different-seed variance, cache identity, hit/miss, invalid/capped, runout-mode, and memory-cap coverage.
- Build and tests were not run because the repository instructions prohibit them unless explicitly requested.
- Reviewed the task diff with `git diff --check` on task files.

### Limitations

- Overall resolver memory-preflight accounting for the optional continuation cache remains P7.3 work; this cache enforces its own configured entry-payload cap.

## P6.1-P6.3 - Continuation policy transformation, selection, and rollout leaf integration

**Status:** Complete
**Completed:** 2026-08-12

- Formalized validated scalar continuation-row transforms for fold, check/call, and bet/raise/all-in classes.
- Added a fixed continuation selector keyed only by public state, acting seat, street, future bucket, and model versions.
- Passed selected policy, opaque sampled deal, terminal adapter, reaches, buckets, and model provenance through typed traversal leaf requests.
- Recorded the configured fixed continuation mode in blueprint model identity while preserving direct leaf-callback compatibility.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_blueprint_config.hpp`
- `include/solver/multiway_continuation_policy.hpp`
- `include/solver/multiway_continuation_policy_kind.hpp`
- `include/solver/multiway_continuation_selector.hpp`
- `include/solver/multiway_leaf_evaluator.hpp`
- `include/solver/multiway_rollout_leaf.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_blueprint_config.cpp`
- `src/solver/multiway_continuation_policy.cpp`
- `src/solver/multiway_continuation_selector.cpp`
- `src/solver/multiway_model_identity.cpp`
- `src/solver/multiway_rollout_leaf.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_continuation_policy.cpp`
- `tests/test_multiway_continuation_selector.cpp`
- `tests/test_multiway_model_identity.cpp`
- `tests/test_multiway_recursive_traversal.cpp`

### Validation

- Tests were added but not run because this request did not ask for command execution.

### Limitations

- Deadline and clean-batch fallback integration remains P7 runtime-budget work.

## P5.3-P5.6 Test Coverage - Local expansion and future bucket artifacts

**Status:** Complete
**Completed:** 2026-08-12

- Added 66 deterministic dedicated contract cases for P5.3 and P5.6.
- Covers expansion admission, limits, invalid inputs, feature determinism, profile validation, artifact reproducibility, round trips, and malformed payload rejection.

### Files

- `tests/test_multiway_phase5_p53_p56.cpp`

### Validation

- Tests were added but not run because this request did not ask for command execution.

### Limitations

- No runtime performance or policy-quality calibration is asserted.

## P5.3-P5.6 - Local expansion, lossless keys, and future bucket artifacts

**Status:** Complete
**Completed:** 2026-08-12

- Added configurable cold-path classification and transactional planning for important off-tree local expansion.
- Enforced schema-v2 lossless current-round public keys in live search sessions.
- Added versioned potential-aware offline features, deterministic Lloyd clustering, and immutable future-bucket artifact loading.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_action_abstraction.hpp`
- `include/solver/multiway_future_bucket.hpp`
- `include/solver/multiway_public_builder.hpp`
- `include/solver/multiway_search_session.hpp`
- `src/solver/multiway_action_abstraction.cpp`
- `src/solver/multiway_future_bucket.cpp`
- `src/solver/multiway_public_builder.cpp`
- `src/solver/multiway_search_session.cpp`
- `tests/test_multiway_action_abstraction.cpp`
- `tests/test_multiway_future_bucket.cpp`
- `tests/test_multiway_public_builder.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Tests were added but not run because this request did not ask for command execution.

### Limitations

- Future-cluster profile calibration remains an offline evaluation task; runtime performs lookup only.

## P5.2 Test Fixture Correction - Upper pseudo-harmonic boundary

**Status:** Complete
**Completed:** 2026-08-12

- Corrected the upper-size boundary fixture: 214 is within the 10% pseudo-harmonic threshold for a 225 target; 204 translates and 203 rejects.

### Files

- `tests/test_multiway_action_abstraction.cpp`

### Validation

- Compact CTest reproduced the original incorrect 214 rejection expectation.
- The post-fix Debug build was blocked before compilation by duplicate `Path` and `PATH` environment variables.

### Limitations

- The corrected fixture has not been rerun in a clean build environment.

## P5.1-P5.2 Test Coverage - Menu profiles and translation boundaries

**Status:** Complete
**Completed:** 2026-08-12

- Added deterministic context-profile reproducibility and exact legal-target checks.
- Added pseudo-harmonic lower and upper boundary fixtures, policy-identity threshold coverage, and invalid translation-configuration checks.

### Files

- `tests/test_multiway_action_abstraction.cpp`

### Validation

- Tests were added but not run because repository instructions prohibit test commands unless explicitly requested.

### Limitations

- No runtime policy-quality calibration is covered by these unit tests.

## P5.1-P5.2 - Contextual action menu profiles and pseudo-harmonic translation

**Status:** Complete
**Completed:** 2026-08-12

- Added versioned contextual menu-profile identities derived from sizing templates and position/situation context.
- Added a cold-path pseudo-harmonic off-tree translator with configurable distance threshold, versioned policy identity, exact legality checks, and exact target contributions.
- Preserved observed-action metadata separately from the translated blueprint lookup action in request-local search sessions.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_action_abstraction.hpp`
- `include/solver/multiway_search_session.hpp`
- `src/solver/multiway_action_abstraction.cpp`
- `src/solver/multiway_search_session.cpp`
- `tests/test_multiway_action_abstraction.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused profile, translation-boundary, legality, and session-metadata coverage.
- Tests were not run because this request did not ask for execution.

### Limitations

- The threshold is configurable; production tuning requires separate policy-quality evidence.

## P4 Test Coverage - Runtime lifecycle contracts

**Status:** Complete
**Completed:** 2026-08-12

- Added 54 deterministic Phase 4 runtime-session contract cases covering construction, policy export, freeze clearing, same-street rerooting, and rejected non-transition street rerooting.

### Files

- `tests/test_multiway_phase4.cpp`

### Validation

- Tests were added but not run because this request did not ask for execution.

### Limitations

- Existing build-environment `Path`/`PATH` duplication still blocks MSBuild validation.

## P4 Regression - Runtime session default solver limits

**Status:** Complete
**Completed:** 2026-08-12

- Initialized bounded runtime-session solver limits when the resolver is configured in legacy mode and has no active-search limits.

### Files

- `src/solver/multiway_resolver.cpp`

### Validation

- Attempted the required Debug build twice.
- Validation was blocked before compilation because MSBuild received duplicate `Path` and `PATH` environment variables.

### Limitations

- The compact test suite was not run because the required build could not start.

## P4.1-P4.5 - Resolver-managed runtime hand lifecycle

**Status:** Complete
**Completed:** 2026-08-12

- Added a request-local runtime session factory on `MultiwayResolver` for validated postflop snapshots.
- Added an owning runtime session that applies observed-action Bayes updates and replaces the round state on street or qualifying same-street reroots.
- Rerooting carries legal posterior ranges forward and clears the previous actual-hand freeze.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_runtime_session.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_runtime_session.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused resolver factory and round-replacement coverage.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- Full-hand terminal replay remains owned by the existing rules and replay suites; this lifecycle layer does not duplicate terminal settlement.

## P3.5, P4.4-P4.5 - Resumable blueprints and posterior rerooting

**Status:** Complete
**Completed:** 2026-08-12

- Added deterministic offline checkpoints for admitted public descriptors and canonical sparse regret/strategy rows.
- Restored compatible trainer state before exporting the same full blueprint artifact.
- Added posterior-belief transfer to next-street and same-street reroot roots; the new session owns fresh rows and no prior actual-hand freeze.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_blueprint_trainer.hpp`
- `include/solver/multiway_search_session.hpp`
- `include/solver/multiway_solver.hpp`
- `src/solver/multiway_blueprint_trainer.cpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_solver.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused sparse checkpoint, full-artifact resume, and posterior-reroot coverage.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- Checkpoints are an in-memory offline training boundary; persistence remains a future artifact-format extension.

## P3.4, P4.2-P4.3 - Runtime blueprint prior and hero policy lifecycle

**Status:** Complete
**Completed:** 2026-08-12

- Connected the immutable full-blueprint store to runtime traversal as the non-traverser policy prior.
- Added request-local hero-range export, actual-hand policy freeze, and explicit freeze clearing for a new round root.
- Preserved singleton actual-hand behavior when callers do not supply a hero range.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_search_session.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_search_session.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused hero policy export and freeze coverage.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- P3.5 full checkpoint/resume equivalence and P4.1/P4.4/P4.5 root reconstruction, observed-action orchestration, and street rerooting remain separate work.

## Implementation workflow skill

**Status:** Complete
**Completed:** 2026-08-12

- Added a project-local skill that orchestrates navigation, implementation,
  focused tests, code review, project logging, and one scoped commit.

### Files

- `.agents/skills/implementation-workflow/SKILL.md`
- `.agents/skills/implementation-workflow/agents/openai.yaml`

### Validation

- Skill structure validation passed.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- The workflow applies only when `$implementation-workflow` is invoked or its
  implementation trigger matches.

## P3.2-P3.4 - Blueprint coverage, artifact, and traversal provider

**Status:** Complete
**Completed:** 2026-08-12

- Added sparse training coverage counters and a public-only coverage manifest.
- Added an atomic, hash-verified full-blueprint row artifact beside the root fallback artifact.
- Added immutable lock-free blueprint lookup for non-traverser traversal decisions, with explicit miss and incompatible-menu outcomes.

### Files

- `include/solver/multiway_blueprint_trainer.hpp`
- `src/solver/multiway_blueprint_trainer.cpp`
- `include/solver/multiway_artifact.hpp`
- `src/solver/multiway_artifact.cpp`
- `include/solver/multiway_blueprint_policy_provider.hpp`
- `src/solver/multiway_blueprint_policy_provider.cpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_traversal.cpp`
- `include/solver/multiway_solver.hpp`
- `src/solver/multiway_solver.cpp`
- `include/core/lib.hpp`
- `tests/test_multiway_blueprint_store.cpp`
- `tests/test_multiway_artifact.cpp`

### Validation

- Added focused artifact and lookup-provider tests.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- P3.5 full checkpoint/resume equivalence remains incomplete.

## P3.1 - Define full blueprint row and index schema

**Status:** Complete
**Completed:** 2026-08-12
**Implementation commits:** `ed5188c`, `fce9455`

- Added an immutable, identity-bound blueprint row store with deterministic
  sorted lookup by public state, seat, bucket, and action-menu identity.
- Rows reject duplicate keys, malformed menus, and non-normalized policies.

### Files

- `include/solver/multiway_blueprint_store.hpp`
- `src/solver/multiway_blueprint_store.cpp`
- `tests/test_multiway_blueprint_store.cpp`

### Validation

- Added a 128-row deterministic lookup fixture and malformed-row checks.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- P3.2-P3.5 serialization, trainer integration, artifact loading, and resume
  work remain incomplete.

## P2.5 - Replace bounded perturbation only for eligible requests

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commits:** `9d4fbd7`, `f4dda35`

- Added typed runtime-search eligibility diagnostics and host-configured seat
  and root-menu limits.
- Active search now requires a supported postflop root and complete live
  non-hero ranges; otherwise it uses the established fallback chain.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_runbook.md`

### Validation

- Added focused active-search eligibility and fallback coverage.
- Build and tests were not run because the task explicitly prohibited them.
- Reviewed staged commits with `git diff --cached --check`.

### Limitations

- Full blueprint runtime lookup remains P3 work.

## P2.4 - Add resolver feature flag and shadow mode

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commits:** `9d4fbd7`, `f4dda35`

- Added privacy-safe shadow elapsed-time and observed-memory diagnostics.
- Preserved the legacy output in shadow mode while reporting completed search
  counters, merge volume, and policy divergence.

### Files

- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_runbook.md`

### Validation

- Added focused shadow diagnostics coverage.
- Build and tests were not run because the task explicitly prohibited them.
- Reviewed staged commits with `git diff --cached --check`.

### Limitations

- Shadow mode may increase request latency and memory; it remains opt-in.

## P2.3 - Wire worker-local deltas into session rows

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commits:** `f6a0f3a`, `db9a8e4`

- Added request-local row metrics and immutable clean-batch snapshots.
- Resolver runtime and shadow search now export only through the latest clean
  session snapshot and report non-private merge provenance.
- Added deterministic replay and rejected-snapshot preservation coverage.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_search_session.hpp`
- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_search_session.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_runbook.md`

### Validation

- Added focused session and resolver regression tests.
- Build and tests were not run because the task explicitly prohibited them.
- Reviewed each staged commit with `git diff --cached --check`.

### Limitations

- P3 full-blueprint artifact work remains separate.
- Cross-worker comparisons retain the existing normalized-policy tolerance;
  bitwise replay is defined for a fixed worker layout.

## P2.2 - Implement runtime budget and clean-batch semantics

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commit:** `227a6eb feat(multiway): add resolver runtime budget`

- Added a request-local runtime budget with an internal deadline derived from
  the external deadline and reserve.
- Enforced batch, trajectory, sparse-row, sparse-value, cancellation, and
  clean-merge checks before root-policy export.
- Preserved the last clean batch as a partial result when the deadline expires
  after that batch, and rejected requests with no clean batch.
- Added direct budget contract tests and resolver deadline regression coverage.

### Files

- `include/solver/multiway_resolver_budget.hpp`
- `src/solver/multiway_resolver_budget.cpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver_budget.cpp`
- `tests/test_multiway_resolver.cpp`

### Validation

- Not run. The repository instruction explicitly prohibited build and test
  commands for this task.
- Reviewed staged diff with `git diff --cached --check`.

### Limitations

- Budget cancellation is cooperative at bounded batch checkpoints. Interrupting
  an in-flight trajectory remains future scheduler/traversal work.

## M2 - Isolated real sampled root search: clean batches and shadow comparison

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commit:** `b5b5b4b fix(multiway): enforce clean root search batches`

- Added an explicit clean flag to root batch results, set only after worker
  streams are merged by the coordinator.
- Rejected active resolver searches that produce no accepted trajectory or no
  merged delta, preserving legal fallback behavior.
- Added shadow-mode completion counters and normalized-policy L1 divergence
  diagnostics.
- Added regression coverage for clean batches, no-progress search fallback,
  and shadow comparison.

### Files

- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_recursive_traversal.cpp`
- `tests/test_multiway_resolver.cpp`

### Validation

- Not run. The repository instruction explicitly prohibited build and test
  commands for this task.
- Reviewed staged diff with `git diff --cached --check`.

### Limitations

- Broader M2 runtime-budget integration remains. Runtime search is still
  root-only; rerooting, off-tree expansion, future abstraction, and
  continuation-policy integration remain future M4-M6 work.

## P1.1 - Canonical Combination IDs

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commit:** `cb88695 Add canonical HUNL combination indexing`

- Added one fixed 1,326-entry HUNL-card combination view with unordered
  pair-to-ID, ID-to-pair, and dead-card legal-mask operations.
- Updated HUNL joint-range normalization and range enumeration to use the
  shared mapping while retaining board-relative range indices where required.
- Separated compact bucket-artifact cards from HUNL runtime cards with explicit
  adapters; updated resolver, traversal, range-update, and fixture boundaries.
- Added exhaustive combination, mask, and compact-artifact/HUNL-adapter tests.

### Validation

- Static staged and working-tree diff checks passed.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass.

## P1.2 - Fixed-Array Range Beliefs

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commits:**

- `964233c Add multiway range belief storage`
- `82c8824 Harden multiway range belief resets`
- `2c5cfbd Test multiway range belief storage`

- Added a session-owned six-seat belief container with fixed 1,326-combination
  `double` rows, legal masks, source, mass, action, and revision metadata.
- Added transactional uniform and supplied range initialization. Supplied
  entries merge by canonical ID, apply blockers before normalization, and fail
  explicitly on invalid or zero legal mass.
- Added read-only C++17 non-owning views and controlled belief copying while
  preventing implicit owner copy and move operations.
- Kept resolver, sampler, and public descriptors unchanged.

### Validation

- Added deterministic initialization, blocker, normalization, duplicate merge,
  transaction, copy, view, and bounds regression tests.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass.

## P1.3 - Bayes Action-Observation Updates

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commits:**

- `8592f88 Apply multiway range observations`
- `20fda71 Harden multiway range observations`
- `4f14c42 Test multiway range observations`

- Added fixed-row, two-pass Bayes updates for observed actions with explicit
  source, public-state, menu, table, action-index, and revision provenance.
- Updates validate policy/table identity before mutation, preserve illegal
  combinations at zero, and return a typed no-posterior result unchanged.
- Preserved legacy action metadata compatibility and isolated the new path from
  resolver, traversal, sampler, and public descriptors.

### Validation

- Added exact posterior, table-binding, blocker, transaction, seat-isolation,
  and repeat-update regression coverage.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED`.

## P0.1 - Resolver and Traversal Baselines

**Status:** Complete  
**Completed:** 2026-08-09  
**Commit:** `8c6ca324b0f8adecaaca321ab2c46be6def8918a`

Implemented privacy-safe deterministic baseline reporting for existing multiway
resolver and root-traversal paths. This does not change production inference.

- Added resolver and traversal baseline report APIs.
- Added fixture labels for valid, invalid, off-tree, no-artifact,
  deadline-exhausted, and max-row scenarios.
- Reports record status, fallback source, normalized policy, sampled action,
  batch/trajectory and row counters, timing, and observed resident memory.
- Reports omit hero cards, opponent ranges, and sampling seeds.
- Deterministic comparison and serialization exclude timing and memory.
- Added an isolated resolver fixture harness so retained stable-root state does
  not affect fixture order.
- Traversal reports use coordinator counter deltas rather than lifetime totals.
- Added resolver and traversal profiling scopes.

### Validation

- Added `tests/test_multiway_baseline.cpp`.
- Tests cover all P0.1 fixture labels, fixture isolation, privacy-safe stable
  serialization, memory measurement precedence, reused traversal coordinators,
  deterministic reports, and max-row behavior.
- Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass because direct execution was
  blocked by host policy.

### Deferred

None in Phase 0 implementation.

## P0.2 - Policy Provenance and Status Observability

**Status:** Complete
**Completed:** 2026-08-09

- Added distinct resolver statuses for partial and budget-rejected outcomes.
  They are forward-compatible values only; the legacy resolver does not emit
  either one yet.
- Added policy provenance for legacy adjustment, stable-root, blueprint, and
  static-legal policy paths.
- Added the legacy search-engine identifier/version and request artifact
  identity to resolver diagnostics.
- Extended public decision logs with provenance and search-engine metadata.

## P0.3 - Centralized Semantic Model Identity Inputs

**Status:** Complete
**Completed:** 2026-08-09

- Added explicit identity components for range semantics, future buckets,
  off-tree policy, continuation policy, and runtime search schema.
- Centralized their hashing in `make_multiway_model_identity`.
- Bound the new components into artifact identity hashing and bumped the
  manifest schema version.
- Diagnostic formatting remains outside the model identity contract.

## P0.4 - Baseline Allocation and Timing Profile

**Status:** Complete
**Completed:** 2026-08-09

- Added cold-boundary profiler scopes for resolver and root batches. Detailed
  baseline reports cover the contained traversal, private sampling, terminal,
  row-admission, and merge work without adding locks or allocations to those
  hot operations.
- Baseline reports now collect wall-clock time, process CPU time when exposed
  by the platform, current resident-memory snapshots, and process peak RSS on
  Windows/Linux. Peak RSS is explicitly unavailable on other platforms.
- Root-batch reports include accepted trajectories and deterministic worker
  minimum/maximum trajectory counts for imbalance measurement.
- Allocation-byte telemetry is explicitly unavailable until the host supplies
  an allocator profiler; RSS is not treated as allocation volume.

### Validation

- Added resolver, public-decision-log, semantic-identity, artifact-rejection,
  and baseline-environment regression coverage.
- Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass because direct execution was
  blocked by host policy.
