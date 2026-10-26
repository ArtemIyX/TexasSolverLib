# Pluribus Roadmap Progress Log

## PB-12 Phase 5.7 - Resolve external row buckets to pages

**Status:** Complete
**Completed:** 2026-10-26

- Added a bounded row-index operation that resolves an infoset bucket to its stable page ID, page index, in-page bucket count, and action count.
- Covered the final short page and out-of-range bucket behavior, providing a single cold-boundary lookup before callers pin a page.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build for external storage, workflow config, solver, and memory tests: passed.
- Focused CTest for external storage, workflow config, solver, and memory: passed (4/4).
- `git diff --check`: passed.

### Limitations

- This resolver is not yet called from coordinator traversal or merge paths.

## PB-12 Phase 4.7 - Reconstruct external catalog descriptors

**Status:** Complete
**Completed:** 2026-10-26

- Added cold-boundary reconstruction of a full public-state descriptor from a bounded parent chain, including betting state, board and incoming edge, restored action menu, and chronological betting history.
- Verified descriptor menu fields survive catalog reopen and reconstruction.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- The coordinator still uses the in-memory public catalog. External descriptor reconstruction is not yet on the runtime path.

## PB-12 Phase 4.6 - Persist external catalog action menus

**Status:** Complete
**Completed:** 2026-10-26

- Extended external catalog records with a versioned, checksummed fixed-width legal-action menu payload, so packed nodes no longer depend on an in-memory menu handle after restart.
- Added descriptor-based append and bounded caller-buffer lookup. Retained the standalone packed-node codec; the node-only catalog append path is explicitly menu-less.
- External catalog records are now 381 bytes: the 225-byte node codec plus a 156-byte menu record.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build for external storage, workflow config, solver, and memory tests: passed.
- Focused CTest for external storage, workflow config, solver, and memory: passed (4/4).
- `git diff --check`: passed.

### Limitations

- Catalog action-menu persistence is a storage primitive only. Coordinator admission, descriptor reconstruction, and external backend runtime selection remain unintegrated and fail-closed.

## PB-12 Phase 5.6 - Post-commit cleanup failure coverage

**Status:** Complete
**Completed:** 2026-10-26

- Added failure injection after the aggregate generation is published but before transaction backup cleanup completes.
- Verified reopening selects the fully published generation and all three child stores expose the committed records.

### Files

- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- Coordinator batch integration is still outstanding; this test covers transaction recovery in isolation.

## PB-12 Phase 5.5 - External cell differential fixture

**Status:** Complete
**Completed:** 2026-10-26

- Added a deterministic per-cell comparison between external compact pages and unchanged `MultiwayCompactStorage` for exact fixed-point regret and strategy-mass inputs.
- The fixture checks action-major cell offsets, negative regrets, persisted values, and the full-width `uint64` strategy-mass representation.

### Files

- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- This proves page encoding and persistence against the reference for a bounded fixture. Coordinator merge routing and batch-level worker-delta equivalence remain unimplemented.

## PB-12 Phase 5.4 - External multi-store batch transaction

**Status:** Complete
**Completed:** 2026-10-26

- Added a root-level batch journal and backup protocol that recovers catalog, row-index, and page-store `CURRENT` manifests as one generation before child stores reopen.
- Added aggregate-manifest commit validation and rollback tests for partial child publication and failed global publication.
- Added opt-in input-run retention during deterministic compaction so component generations can be compacted before the aggregate commit without breaking rollback.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build for external storage, workflow config, solver, and memory tests: passed.
- Focused CTest for external storage, workflow config, solver, and memory: passed (4/4).
- `git diff --check`: passed.

### Limitations

- The coordinator does not yet use this transaction or external stores. External solver execution remains fail-closed. Runtime crash injection and filesystem durability behavior remain platform dependent.

## PB-12 Phase 3 foundation - External storage contracts

**Status:** Complete
**Completed:** 2026-10-26

- Added an explicitly selected `ExternalCompactInt32` configuration with bounded catalog/page caches, WAL, segments, compaction scratch, disk reserve, and durability settings. Included its resource identity in configuration fingerprints and reports.
- Added a fixed-width sorted-run store with checksummed generation manifests, batch WAL recovery, atomic publication, deterministic compaction, disk-reserve checks, and a bounded pinned page cache. External solver execution and preflight remain fail-closed until catalog and row integration is complete.

### Files

- `CMakeLists.txt`
- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `tests/test_multiway_external_storage.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build for external storage, workflow config, solver, and memory tests: passed.
- Focused CTest for external storage, workflow config, solver, and memory: passed (4/4).
- `git diff --check`: passed.

### Limitations

- Public-state catalog and sparse row/cell stores are not yet connected to the coordinator. External execution remains rejected; the current memory ledger is conservative and does not establish large-scale feasibility.
- Crash injection currently covers partial WAL rollback and committed-WAL replay after segment failure, not every publication boundary.

## PB-12 Phase 4.1 - Packed public-node record codec

**Status:** Complete
**Completed:** 2026-10-26

- Added a versioned 225-byte little-endian codec for packed public nodes. The format is independent of compiler struct padding and rejects invalid counts, enums, and boolean fields.
- Added deterministic round-trip and schema-corruption coverage. Catalog segment/index integration remains outstanding.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- The codec is not yet used by a durable public catalog. External execution remains rejected.

## PB-12 Phase 4.2 - Durable public catalog primitive

**Status:** Complete
**Completed:** 2026-10-26

- Added a sorted append-only external catalog over fixed node records, duplicate-ID rejection, deterministic compaction at batch boundaries, and bounded iterative parent-chain reconstruction.
- Verified lookup and parent-first reconstruction after reopening the generation manifest.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- This catalog primitive is not wired into `MultiwaySolverCoordinator` or its hot lookup paths. Existing solver execution remains on in-memory storage; the explicit external mode remains rejected.

## PB-12 Phase 5.1 - External row metadata index

**Status:** Complete
**Completed:** 2026-10-26

- Added fixed-width state records containing per-seat row shape and deterministic page ID ranges. The index retains all 64 bits of the public-state key and supports later seat-row additions by publishing a newer state record.
- Row admission validates ordering and shape, bounds generated page identities, and advances the caller's page counter only after the metadata batch is committed.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- Fixed accumulator cell pages are not implemented, and the row index is not connected to coordinator admission. External solver execution remains rejected.

## PB-12 Phase 5.2 - Bounded external compact page cache

**Status:** Complete
**Completed:** 2026-10-26

- Added fixed-width checksummed compact page records with separate `int32` regret and `uint64` strategy-mass arrays, fixed action-major page stride, identity validation, and before-image rollback.
- Added a preallocated bounded typed page cache. Dirty capacity is capped by cache, WAL, and segment limits; pages must be pinned before mutation and cannot be evicted while pinned or dirty.
- Added page-cache hit, miss, and eviction counters and verified the one-page cache remains within its configured byte bound.
- Persisted the row page-ID allocator in the same sorted row-index store generation, restoring it after restart to avoid page-ID reuse.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build for external storage, workflow config, solver, and memory tests: passed.
- Focused CTest for external storage, workflow config, solver, and memory: passed (4/4).
- `git diff --check`: passed.

### Limitations

- Row/page stores remain primitives and are not connected to coordinator row admission or worker merge. No in-memory differential solver fixture has been run; external execution remains rejected.

## PB-12 Phase 5.3 - Aggregate generation manifest format

**Status:** Complete
**Completed:** 2026-10-26

- Added a fixed-width checksummed top-level manifest record for committed batch identity and catalog, row, and page generation/count references. Publication uses an atomic replace and optional durable file/directory flush.
- Added round-trip, checksum rejection, and failed-publication preservation tests.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused external-storage build and CTest: passed (1/1).
- `git diff --check`: passed.

### Limitations

- The aggregate manifest is not yet the coordinator's commit point. Catalog, row, and page stores still publish independently; external execution remains rejected until a recovery-safe multi-store batch transaction is integrated.

## PB-12 Phase 2.3 - Packed mapped table views

**Status:** Complete
**Completed:** 2026-10-26

- Replaced schema-5 per-table board vectors with 40-byte non-owning views and made registry lookup binary-search the mapped, sorted directory. Kept schema-4 mapped record loading and stable `MultiwayBucketTable` references.
- Accounted the mapped view vector and one shared model identity separately, and reject schema-5 mappings whose table metadata allocation would exceed 128 MiB. The deterministic full-catalog sizing test verifies `table_count * sizeof(MultiwayBucketTable) + sizeof(MultiwayModelIdentity)` remains below that limit.
- Resolved sentinel collisions at full 256- and 65,536-bucket widths by deriving blocked assignments from the board and canonical combo index. Verified the highest valid bucket decodes correctly at each width transition.
- Added exhaustive assignment and lookup comparisons across schema-4 owned/mapped and schema-5 owned/mapped fixtures, plus duplicate-directory, offset-overflow, hash, and identity rejection cases.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_model.cpp`
- `tests/test_multiway_bucket_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build and CTest for bucket artifact, generation, and model: passed (3/3).
- Earlier related focused CTest for bucket catalog, future bucket, range belief, search session, solver, artifact, and artifact preflight: passed (7/7).
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed; all 118 CTest tests passed.
- `git diff --check`: passed.

### Limitations

- The 128 MiB metadata result is a deterministic view-vector capacity formula, not a target-machine residency measurement. No large bucket catalog generation or target-machine benchmark was run.

## PB-12 Phase 2.2 - Stream schema 5 bucket artifacts

**Status:** Complete
**Completed:** 2026-10-26

- Kept resumable schema-4 staging records, then converted them at finish to schema 5 using an assignment scratch file and one bounded directory buffer. Newly published streaming artifacts now use the selected 1/2/4-byte assignment width.
- Preserved ordered worker publication, progress hashes, resume behavior, and atomic final publication. Conversion validates canonical board order and assignment ranges.
- Generation tests confirm identical schema-5 output for serial and parallel writers and equivalent mapped inspection hashes.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `tests/test_multiway_bucket_generation.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused artifact and generation targets built successfully.
- CTest `multiway_bucket_artifact` and `multiway_bucket_generation`: passed after final edits.
- `git diff --check`: passed.

### Limitations

- No target-machine residency measurement or large artifact qualification was run.

## PB-12 Phase 2.1 - Schema 5 compact bucket encoding

**Status:** Complete
**Completed:** 2026-10-26

- Added schema-5 directory and assignment sections with width-selected little-endian assignments, reserved invalid sentinels, section hashes, and table identity checks.
- Retained schema-4 deserialization and mapped loading. Schema-5 inspection validates through the mapped loader and reports content hashes.
- Exhaustively compared all 1,326 hole assignments for the three canonical fixture tables across schema 4 and schema 5. Verified width transitions at 256/257 and 65,536/65,537 buckets, schema-5 mapping, and section hash rejection.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `tests/test_multiway_bucket_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `compact_build.py --target test_multiway_bucket_artifact --target test_multiway_bucket_generation --parallel 6`: passed.
- CTest `multiway_bucket_artifact` and `multiway_bucket_generation`: passed.
- `git diff --check`: passed.

### Limitations

- No large artifact generation or target-machine residency measurement was run.

## PB-12 Phase 0–1 - Repair memory accounting

**Status:** Complete
**Completed:** 2026-10-26
**Implementation commit:** `f1db9b4 PB-12 (memory) Multiway: Correct training estimates`

- Replaced the synthetic 1 KiB public-state charge with packed node, sorted index, menu, and admission-scratch capacity terms. Added catalog and compact-storage reallocation peaks, page metadata, and exact coordinator pending-merge cell sizing.
- Replaced output-sized checkpoint/export private estimates with stream scratch bounds. Added distinct file-cache and disk-output ledger fields, and bumped the estimate schema to version 3.
- Added deterministic 4M/34M sizing fixtures. The 34M fixture retains the 5M-row/100M-cell profile and rejects the unchanged 51,539,607,552-byte working-set gate after a 15.95 GB post-validation baseline. No training workflow ran.

### Files

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_p7_memory_contracts.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_storage_sizing.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused memory, storage sizing, qualification report, and P7 memory contract tests passed.
- `scripts/python/full_build.py`: build passed; all 118 CTest tests passed.
- No training, benchmark, or pilot workload was run.

### Limitations

- 34M conservatism is supported by synthetic maximum-capacity accounting, not a measured 34M runtime peak or feasibility claim. External storage, disk admission integration, and runtime reconciliation remain in later phases.

## PB-12 - 50M memory investigation and implementation plan

**Status:** Complete
**Completed:** 2026-10-26

- Documented the 4M admission failure, 34M preflight rejection, measured mapping residency, checkpoint/export peaks, and current 50M architecture limits.
- Defined a bounded out-of-core design, corrected memory-accounting requirements, compact bucket format, durable checkpoint/export strategy, focused tests, and staged validation gates.

### Files

- `docs/pb12_50m_memory_scaling_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Documentation diff reviewed. No build, test, benchmark, or training job was run because this task changed documentation only.

### Limitations

- Implementation has not started. No 500K, 1M, 5M, or 50M workload was run or authorized.

## PB-12 - Guarded 500K qualification

**Status:** Stopped by capacity and memory gates
**Completed:** 2026-10-26
**Implementation commits:** `0b9f683`, `1fc46d4`, `2565c6c`

- Added versioned 500K sizing profiles and extended the guarded runner to 500 batches. The first profile retained the 4,000,000 public-state cap.
- The first guarded 500K run stopped after 58,000 accepted trajectories with zero discards because public-state admission exceeded that cap. It recorded 3,971,363 states, 1,565,562 sparse rows, and 61,306,003 sparse values.
- The v2 profile raised the public-state ceiling to 34,000,000. The config parser ceiling was raised to match, with a boundary test. The retry then stopped at memory preflight before any batch: estimated working set 69,933,107,152 bytes exceeded the unchanged 51,539,607,552-byte limit.
- Both terminal manifests and the logs were preserved. Neither attempt produced a checkpoint or final blueprint.

### Files

- `configs/multiway/f1_qualification_500k_v1.cfg`
- `configs/multiway/f1_qualification_500k_v2.cfg`
- `scripts/run/run_multiway_ram_1000.ps1`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `tests/test_multiway_workflow_config.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed.
- RelWithDebInfo training binary rebuilt with clean source provenance matching PB-12.
- Bucket inspection matched the configured identity and payload hash.
- Debug full build and compact CTest passed after the config ceiling change.
- The v2 run passed provenance and disk preflight, then the training memory preflight rejected the configured workload before training.

### Limitations

- 500K remains unqualified. The configured 34M public-state profile exceeds the current memory gates; no 1M, 5M, or 50M run was started.
- No checkpoint or blueprint exists to load or validate. Further work requires a larger-memory system or a memory-footprint reduction, with the existing guards retained.

## Restore PB-9 worker-scaling findings

**Status:** Complete
**Completed:** 2026-10-26

- Restored the final pre-deletion PB-9 multiway training scaling report from Git history.
- Repaired the existing README documentation link without changing its target.

### Files

- `docs/pb9_multiway_training_scaling_findings.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Restored file blob matches commit `b1c4263` exactly.
- `git diff --check`: passed.
- Builds and tests were not run because this change restores Markdown documentation only.

### Limitations

- None identified.

## PB-10 - Complete TexasLogging application logging

**Status:** Complete
**Completed:** 2026-10-26

- Added the non-installed `TexasSolver::logging` target with strict TOML configuration, category and sink filtering, rotation, append-only training audit output, terminal-safe progress rendering, and compile-time/runtime disable paths.
- Integrated multiway workflow logging while preserving workflow files, reports, exit codes, progress options, machine-readable streams, and solver package boundaries.
- Added focused logging, progress, disabled-mode, package-boundary, source-classification, and workflow-boundary coverage.

### Files

- `CMakeLists.txt`
- `README.md`
- `vcpkg.json`
- `config/logging.example.toml`
- `include/logging/config.hpp`
- `include/logging/log.hpp`
- `include/logging/progress.hpp`
- `src/logging/config.cpp`
- `src/logging/config_stub.cpp`
- `src/logging/log.cpp`
- `src/logging/log_stub.cpp`
- `src/logging/progress.cpp`
- `src/logging/progress_stub.cpp`
- `src/logging/terminal.cpp`
- `src/logging/terminal.hpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_logging.cpp`
- `tests/test_logging_config.cpp`
- `tests/test_logging_disabled.cpp`
- `tests/test_logging_progress.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `tests/cmake/test_package_config_contract.cmake`
- `tests/cmake/test_source_classification.cmake`
- `docs/application_and_script_io.md`
- `docs/texas_logging.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Enabled focused CTest: 7/7 passed; concurrent progress test repeated 20/20.
- Enabled workflow and logging targets built successfully.
- Logging-off workflow and disabled logging targets built successfully; logging-off focused CTest: 4/4 passed, with only `test_logging_disabled` registered among logging tests.
- `python scripts/python/full_build.py`: build passed and all 117 CTest tests passed.
- `git diff --check`: passed.

### Limitations

- Progress remains a single synchronous line; Unicode width handling, structured JSON logs, network sinks, runtime category registration, and asynchronous logging remain deferred.

## Security remediation findings

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commits:** `1614db4`, `3accd7a`, `f4d60cf`, `368f59d`, `6976bc4`, `f928768`, `b8bb30e`, `ab781b5`

- Fixed test allocation ownership and added checked first-party arithmetic.
- Added configuration-driven workflow resource preflight and centralized path authorization with traversal, link, overwrite, and alias checks.
- Replaced the flagged symbol-parser regex with bounded linear parsing.
- Applied tracked minimal Tracy arithmetic patches and made Tracy MCP loopback-only by default with explicit remote opt-in.
- Preserved exact-mode behavior and deterministic workflow interfaces.

### Files

- `CMakeLists.txt`
- `include/core/checked_arithmetic.hpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `include/solver/multiway/workflow/multiway_workflow_path_policy.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `src/solver/multiway/workflow/multiway_workflow_path_policy.cpp`
- `examples/benchmarks/hunl_mccfr_scaling_main.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_checked_arithmetic.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_workflow_path_policy.cpp`
- `scripts/python/extract_symbols.py`
- `scripts/python/test_extract_symbols.py`
- `external/tracy/extra/mcp/start_mcp.sh`
- `external/tracy/extra/mcp/tracy_mcp.py`
- `external/tracy/extra/mcp/tracy_mcp_bind.py`
- `external/tracy/extra/mcp/test_tracy_mcp_bind.py`
- `external/tracy/profiler/src/BackendWayland.cpp`
- `external/tracy/profiler/src/profiler/TracyView_ZoneInfo.cpp`
- `external/tracy/profiler/src/stb_image.h`
- `external/tracy/profiler/src/stb_image_resize.h`
- `external/tracy/public/client/TracyKCore.cpp`
- `docs/security_findings_dispositions_2026-10-25.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused CTest for arithmetic, ownership, workflow configuration, and path policy: 4/4 passed.
- Workflow boundary/configuration/path tests: 3/3 passed.
- MSVC AddressSanitizer ownership test: passed with `ASAN_OPTIONS=allocator_may_return_null=1`.
- Tracy MCP tests: 4/4 passed.
- Symbol-parser tests and pathological-input checks: 3/3 passed.
- Tracy diagnostics targets compiled successfully.
- `scripts/codex_powershell.ps1 scripts/python/full_build.py`: build passed and all 113 tests passed.
- Independent C++ review: no remaining actionable findings.

### Limitations

- CodeQL was unavailable locally, so alert closure was not rerun.
- Symlink coverage is conditional when the host denies symlink creation.
- Path policy remains a preflight around existing path-based APIs and cannot make every concurrent filesystem race atomic.

## PB9.16.09 - Qualification tooling

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `94e12d7 PB-9 Extend qualification tooling: Preserve comparable matrix evidence`

- Extended the RAM and worker-matrix runners with explicit scheduler chunk, placement, checkpoint, warmup/order, and deterministic randomized case controls.
- Added strict one-binary provenance validation and run gates for comparable configuration, payload hashes, and policy-content hashes.
- Added median/dispersion, phase, placement, migration, page-fault, and provenance summaries without starting benchmark or solver-training runs.

### Files

- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_worker_matrix.ps1`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/application_and_script_io.md`
- `docs/tasks/PB9_16_09_qualification_tooling.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`

### Validation

- PowerShell parser validation passed for both qualification runners.
- Focused `test_multiway_qualification_reports` and `test_multiway_workflow_boundary` passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed with the build-only `/FS` configuration.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed; all tests passed.
- `git diff --check`: passed.
- No C++ files changed; cpp-code-review boundary was empty.

### Limitations

- No worker matrix, benchmark, or solver training run was started.

## PB9.16.08 - Policy-content hash

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commits:** `d60654c PB-9 Add policy-content hash: Preserve full payload compatibility`, `4583804 PB-9 Add policy-content report field: Expose canonical hash`

- Added a versioned canonical policy-content hash over row identity, action descriptors, and quantized probabilities.
- Preserved the existing full payload hash, worker-sensitive execution identity, schema-2 artifact bytes, and checkpoint compatibility rules.
- Propagated and validated both hashes in full-blueprint export telemetry, training reports, and lookup qualification reports.

### Files

- `include/solver/multiway/blueprint/multiway_artifact.hpp`
- `src/solver/multiway/blueprint/multiway_artifact.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/workflow/multiway_lookup_qualification.hpp`
- `src/solver/multiway/workflow/multiway_lookup_qualification.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_artifact.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_f1_acceptance.cpp`
- `docs/tasks/PB9_16_08_policy_content_hash.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`

### Validation

- Focused artifact, blueprint-training, qualification-report, and F1 acceptance tests passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed with the build-only `/FS` configuration.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed; all tests passed.
- `git diff --check`: passed.
- Independent C++ review: no actionable findings.

### Limitations

- No qualification matrices, benchmarks, or solver training were run.

## PB9.16.07 - Tournament delta merge

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `fac02ea PB-9 Add tournament delta merge: Preserve exact worker accumulation order`

- Added explicit reference and deterministic tournament merge paths for worker delta streams, with automatic selection only above the four-stream threshold.
- Preserved worker-index ties, duplicate rejection, Float64 accumulation order, compact storage, policy output, fingerprints, and preallocated merge scratch.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/tasks/PB9_16_07_tournament_delta_merge.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`

### Validation

- Focused solver, parallel-training, and traversal-boundary tests passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed with the build-only `/FS` configuration.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed; all tests passed.
- `git diff --check`: passed.
- Independent C++ review: no actionable findings.

### Limitations

- No policy-content hashing, qualification matrices, benchmarks, or solver training were run.

## PB9.16.06 - Tournament discovery merge

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `ca8cc80 PB-9 Add tournament discovery merge: Preserve deterministic public-state and row streams`

- Added caller-owned preallocated reference and tournament merge paths for public-state and row discovery streams.
- Selected the tournament path only above the four-stream threshold while preserving worker-index ties, duplicate handling, ordered outputs, diagnostics, and production output reuse.

### Files

- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/tasks/PB9_16_06_tournament_discovery_merge.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`

### Validation

- Focused solver and parallel-training tests passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed, 111/111 tests.
- `git diff --check`: passed.
- Independent C++ review: no actionable findings.
- The requested full-build script was attempted but hit MSVC PDB contention (`C1041`) across unrelated translation units.

### Limitations

- No tournament delta merge, benchmarks, policy-content hashing, qualification matrices, or solver training were run.

## PB9.16.05 - Deterministic scheduler balance

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `90e9510 PB-9 Add deterministic scheduler balance: Version static blocks and expose dynamic chunk evidence`

- Added versioned static-block scheduling while preserving the default fixed round-robin behavior.
- Exposed scheduler mode and dynamic chunk size through workflow configuration, CLI, identity, checkpoint compatibility, run metadata, and JSON reports.
- Preserved trajectory seeds, worker-index tie order, deterministic partitioning, globally ordered merges, exact storage, policy rows, fingerprints, and Float64 accumulation order.

### Files

- `include/solver/multiway/engine/multiway_scheduler.hpp`
- `src/solver/multiway/engine/multiway_scheduler.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_scheduler.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/tasks/PB9_16_05_deterministic_scheduler_balance.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`

### Validation

- Focused scheduler, parallel-training, workflow-config, blueprint-training, and qualification-report tests passed.
- Fixed, static-block, and dynamic modes matched exact storage and policy rows across 1, 2, 4, 8, and 16 workers.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py` passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py` passed.
- `git diff --check` passed.
- C++ review: no actionable findings.

### Limitations

- No benchmarks, qualification matrix, tournament merges, policy-content hashing, or solver training were run.

## PB9.16.04 - Safe placement policies

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `c7f7a15 PB-9 Add safe placement policies: Use topology-backed opt-in worker placement`

- Added topology-backed opt-in OS-default, physical-compact, physical-spread, CPU-set-hint, and diagnostic hard-affinity policies.
- Reserved the coordinator's physical core, selected unique physical cores before SMT siblings, preferred Windows CPU Sets for desktop policies, and added fallback plus amortized batch-boundary migration sampling.
- Added requested/resolved CPU-set, physical-core, SMT-sibling, and cache-domain report identities while preserving scheduler behavior, merge ordering, checkpoint bytes, artifact hashing, and PB9.16.02 telemetry compatibility.

### Files

- `include/solver/multiway/engine/multiway_worker_topology.hpp`
- `src/solver/multiway/engine/multiway_worker_topology.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `docs/tasks/PB9_16_04_safe_placement_policies.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused multiway parallel-training, workflow-config, and blueprint-training tests: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

### Limitations

- No benchmark, qualification matrix, placement-performance measurement, or solver training was run.
- OS APIs can leave physical, CPU-set, SMT, or cache-domain identities unavailable; later scheduler balance, tournament merges, policy-content hashing, and qualification tasks are deferred.

## PB9.16.03 - Topology inventory

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `1014b4d PB-9 Add topology inventory: Discover Windows and synthetic identities`

- Added a cold-path topology inventory with structured CPU-set, logical processor, physical-core, SMT-sibling, NUMA, cache-domain, efficiency, and allocation fields.
- Added Windows CPU-set and processor-core relationship discovery with an explicit logical-only fallback when topology APIs are unavailable.
- Preserved OS-reported `LastLevelCacheIndex` values without inferring V-Cache labels or changing worker placement and scheduler behavior.

### Files

- `CMakeLists.txt`
- `include/solver/multiway/engine/multiway_worker_topology.hpp`
- `src/solver/multiway/engine/multiway_worker_topology.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `docs/tasks/PB9_16_03_topology_inventory.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused `test_multiway_parallel_training`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

### Limitations

- No placement policy, scheduler change, benchmark, qualification matrix, or solver training was implemented or run.
- Existing worker behavior and PB9.16.02 telemetry compatibility remain unchanged because the inventory has no runtime consumer in this task.

## PB9.16.02 - Authoritative phase telemetry

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `6d85d5f PB-9 Add authoritative phase telemetry: Split phase timings and validate process snapshots`

- Added distinct training, checkpoint, export, and total-wrapper durations. Training throughput remains based only on training time.
- Added phase-boundary working-set, private-commit, and page-fault telemetry with explicit unsupported-counter availability.
- Preserved legacy report fields, checkpoint bytes, artifact hashes, scheduler behavior, topology policy, merge order, and hot paths.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/tasks/PB9_16_02_authoritative_phase_telemetry.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Full build wrapper: passed.
- Compact CTest: passed.
- Focused multiway memory, qualification-report, blueprint-training, and workflow-boundary tests: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

### Limitations

- Linux reports minor and major page faults; Windows reports the supported total page-fault counter. Unsupported counters remain unavailable.
- No qualification matrix, benchmark, or solver training was run.

## PB9.16.01 - Compile-time provenance and runner gate

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commit:** `66f6690eded19dc4002a9e0d1d122f75b8fc9be4`

- Embedded source commit, dirty state, compiler, configuration, report schema, and checkpoint schema at compile time.
- Separated binary provenance from runtime checkout identity and added executable SHA-256 stability gates to the RAM runner and worker matrix.
- Preserved legacy report fields and checkpoint bytes.

### Files

- `CMakeLists.txt`
- `include/solver/multiway/workflow/multiway_binary_provenance.hpp`
- `include/solver/multiway/workflow/multiway_evidence.hpp`
- `src/solver/multiway/workflow/multiway_evidence.cpp`
- `src/solver/multiway/workflow/multiway_f1_acceptance.cpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `examples/multiway_workflow_main.cpp`
- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_worker_matrix.ps1`
- `tests/test_multiway_evidence.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/tasks/PB9_16_01_provenance_runner_gate.md`
- `docs/tasks/PB9_16_TASK_MANAGER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Full build wrapper: passed.
- Compact CTest: passed.
- Focused evidence and workflow boundary tests: passed.
- PowerShell syntax checks: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

### Limitations

- No qualification benchmark, matrix run, or solver training was performed.

## PB-9.16 - Worker scaling work breakdown

**Status:** Complete
**Completed:** 2026-10-25

- Split the PB-9.16 scaling analysis into ten independently trackable implementation and qualification tasks.
- Added a task manager with dependencies, state, blockers, invariants, and completion procedure.
- Kept performance experiments blocked pending explicit authorization.

### Files

- `docs/tasks/PB9_16_TASK_MANAGER.md`
- `docs/tasks/PB9_16_01_AGENT_PROMPT.md`
- `docs/tasks/PB9_16_01_provenance_runner_gate.md`
- `docs/tasks/PB9_16_02_authoritative_phase_telemetry.md`
- `docs/tasks/PB9_16_03_topology_inventory.md`
- `docs/tasks/PB9_16_04_safe_placement_policies.md`
- `docs/tasks/PB9_16_05_deterministic_scheduler_balance.md`
- `docs/tasks/PB9_16_06_tournament_discovery_merge.md`
- `docs/tasks/PB9_16_07_tournament_delta_merge.md`
- `docs/tasks/PB9_16_08_policy_content_hash.md`
- `docs/tasks/PB9_16_09_qualification_tooling.md`
- `docs/tasks/PB9_16_10_authorized_qualification_experiments.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Documentation structure and links were reviewed.
- Build and tests were not run because this task changes documentation only.

### Limitations

- No implementation, build, benchmark, or training qualification was performed.

## CI - Build, test, and formatting workflows

**Status:** Complete
**Completed:** 2026-10-25

- Enabled the normal CTest suite in CI and made an empty test registration fail.
- Added Debug and Release coverage on Ubuntu and Windows.
- Ordered the workflow as format, four builds, then tests on the archived Ubuntu Debug build.
- Added a changed-file clang-format gate and a manually dispatched formatter that commits formatting changes.

### Files

- `.github/workflows/c-cpp.yml`
- `.github/workflows/format-apply.yml`
- `scripts/python/format_cpp.py`
- `docs/PLURIBUS_LOG.md`

### Validation

- `python -m py_compile scripts/python/format_cpp.py`: passed.
- `git diff --check`: passed.
- `format_cpp.py project --check`: passed for all 344 C++ files after the formatting baseline commit.
- Build and CTest were not run locally because this task did not request them.

### Limitations

- The formatter workflow is manually dispatched and can create a broad formatting-only commit when run against the entire repository.

## PB-8 - Bounded schema-5 checkpoint writer

**Status:** Complete
**Completed:** 2026-10-25
**Implementation commits:** `94d7446`, `2c05b50`, `ca5cae9`, `ba31520`, `dd2ad03`

- Replaced the production materialized checkpoint workflow with direct schema-5 streaming from an immutable coordinator lease.
- Preserved materialized compatibility APIs, checkpoint bytes, payload hashes, loader behavior, and strict atomic publication.
- Added exact pre-write planning, process/system gates, transactional cleanup coverage, and checkpoint write telemetry in reports.
- Completed the authorized checkpoint-enabled qualification: 50,000/50,000 trajectories accepted, 4,576,234,700-byte checkpoint, 18,706,137,088-byte peak working set, and unchanged final policy bytes/hash.

### Files

- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/tasks/TRACKER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused streamed checkpoint byte/hash, loader, gate, and transactional-failure tests passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- Authorized RelWithDebInfo qualification: 8 workers, 50 batches, 50,000 trajectories; passed with checkpoint creation enabled.
- Qualification output: 3,921,243,000 bytes, payload hash `5946562739452589295`, final policy SHA-256 `96F9FD460B1FFF68407A8D2163E0A28DA7D079767021E62680B92BF1D93DAFA2`, checkpoint SHA-256 `936BDD5AB1F5923B81FF60D0A8C54E214AAA6B911B4332657C621C72C9BACC50`.
- Qualification peak working set: 18,706,137,088 bytes; peak RSS: 17,932,279,808 bytes; no temporary files remained.

### Limitations

- The large run used the existing RelWithDebInfo binary `B6E36BFAF97740C13BF0E5B68103BED083E9F1772FC485C2E3C72AD015A253CB`, built before `dd2ad03`; its runtime report did not include the newer extended checkpoint telemetry fields. Focused report tests cover those fields. Rebuild and repeat only if runtime telemetry evidence is required.

## PB-7 - Task E: Streaming export qualification

**Status:** Complete
**Completed:** 2026-10-25

- Added an explicit checkpoint-disabled qualification mode to the RAM runner so the measured workload isolates the integrated bounded streaming export path.
- Relaxed Tracy evidence validation to require allocation events while documenting free events as optional for captures that end with active allocations.
- Preserved the schema-2 artifact bytes, payload hash, loader/report-reader surfaces, system guard, atomic rename publication, and all existing training behavior.

### Files

- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_tracy_training_capture.ps1`
- `scripts/analyze/analyze_multiway_memory_matrix.ps1`
- `tests/cmake/test_tracy_boundary.cmake`
- `docs/application_and_script_io.md`
- `docs/tasks/E-streaming-export-qualification.md`
- `docs/tasks/TRACKER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Authorized RelWithDebInfo run: 8 workers, 50 batches, 50,000 trajectories; 50,000 accepted and no failures.
- Output bytes: `3,921,243,000`; payload hash: `5946562739452589295`; SHA-256: `96F9FD460B1FFF68407A8D2163E0A28DA7D079767021E62680B92BF1D93DAFA2`.
- Publication mode: `rename`; process and system export gates passed; external peak working set: `18,618,073,088` bytes; report peak: `18,700,632,064` bytes.
- Tracy evidence smoke produced allocation events, CSV output, and a `222,261,090`-byte trace.
- Focused RelWithDebInfo CTest: 5/5 passed, including workflow and Tracy boundaries.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: Debug build and compact CTest passed.
- `git diff --check`: passed.
- C++ review: no new C++ changes; existing Task E guard code remained unchanged.

### Limitations

- The default checkpoint-enabled path retains the existing full checkpoint snapshot and previously exceeded 20 GiB during checkpointing. Tasks A-E do not change checkpoint serialization; a bounded checkpoint writer would be a separate task.

## PB-7 - Task D: Workflow integration and publication

**Status:** Complete
**Completed:** 2026-10-24

- Replaced the production materialize-then-save workflow with `save_full_policy_atomic()` and preserved lease, exact-plan, fresh-snapshot, process/system-gate, and post-admission temporary-file ordering.
- Preserved strict atomic publication and previous-destination retention on rejected export admission.
- Published exact plan/result bytes, payload hash, duration, publication mode, gate reasons, and process/system telemetry through the existing report and log surfaces.
- Preserved schema-2 bytes, ordering, quantization, loader compatibility, training behavior, task capacities, and hot-path boundaries.

### Files

- `examples/multiway_workflow_main.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/tasks/TRACKER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused Debug build for the production workflow and artifact, training, compact-storage, memory, qualification-report, and workflow-config tests: passed.
- Focused CTest for eight targets, including the workflow boundary: 8/8 passed.
- Report-reader compatibility and workflow boundary CTest: 2/2 passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: Debug build and compact CTest passed, 111/111 tests.
- `git diff --check`: passed.
- C++ review: no actionable findings.

### Limitations

- No matched large-workload Windows machine-memory qualification run was performed; that remains Task E.

## PB-7 - Task C: Export memory gate and reporting

**Status:** Complete
**Completed:** 2026-10-24

- Added versioned export plan, bounded scratch, fresh process estimate, Windows available-physical-memory gate, distinct rejection reasons, and separate observed export deltas.
- Integrated lease-plan-snapshot-gate ordering into streamed trainer export before temporary-file creation, while preserving schema-2 bytes, payload hashes, atomic publication, and export exclusion.
- Added report serialization for plan, process/system values, observed deltas, result bytes/hash/duration, publication mode, and separate ledger categories.

### Files

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/tasks/TRACKER.md`

### Validation

- Focused Debug build and CTest for `test_multiway_memory`, `test_multiway_qualification_reports`, and `test_multiway_blueprint_training`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: Debug build and compact CTest passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

### Limitations

- Production workflow configuration replacement and large-workload qualification remain Task D and later.
- System-gate behavior is covered with deterministic injected snapshots; no large Windows machine-memory qualification run was performed.

## PB-7 - Task B: Trainer streaming emission

**Status:** Complete
**Completed:** 2026-10-24

- Added allocation-free compact and Float64 `average_strategy_into()` APIs with bit-equivalent arithmetic.
- Added an exclusive coordinator export lease and no-lock packed-menu views.
- Added exact bounded export planning and trainer/session `save_full_policy_atomic()` using Task A's schema-2 streaming writer, without materializing a full policy artifact.
- Preserved compact direct ordering, Float64 `uint32_t` shape ordering, quantization bytes, checkpoint behavior, and compatibility `export_full_policy()` coverage.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/tasks/TRACKER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused Debug build for `test_multiway_compact_storage` and `test_multiway_blueprint_training`: passed.
- Focused CTest for both targets: passed.
- `scripts/codex_powershell.ps1` `scripts/python/full_build.py`: Debug build and compact CTest passed.
- `scripts/codex_powershell.ps1` `scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- `cpp-code-review`: No actionable findings.

### Limitations

- Export memory/system gates and telemetry remain Task C.
- Production workflow replacement and large-workload qualification remain Tasks D and E.

## PB-7 - Task A: Artifact streaming writer

**Status:** Complete
**Completed:** 2026-10-24

- Added the bounded schema-2 streaming writer, non-owning row views, exact plan/result accounting, shared incremental logical payload hashing, in-place materialized validation, and RAII temporary cleanup.
- Removed the materialized save deep copy and rejected non-atomic copy fallback for full-blueprint publication.
- Preserved schema-2 bytes, payload hash ordering, and loader behavior with focused regression coverage.

### Files

- `include/core/atomic_publish.hpp`
- `src/core/atomic_publish.cpp`
- `include/solver/multiway/blueprint/multiway_artifact.hpp`
- `src/solver/multiway/blueprint/multiway_artifact.cpp`
- `tests/test_multiway_artifact.cpp`
- `docs/tasks/TRACKER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused Debug artifact build and `test_multiway_artifact`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: build and compact CTest passed.
- Explicit `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- `cpp-code-review`: No actionable findings.

### Limitations

- Trainer integration, export memory gates, workflow integration, and large-workload qualification remain for Tasks B through E.

## PB-7 - Split streaming full-blueprint export into tracked tasks

**Status:** Complete
**Completed:** 2026-10-24

- Split the approved streaming-export architecture into five dependency-scoped implementation and qualification tasks.
- Added a tracker with current status, dependencies, blockers, completion rules, and task-level acceptance/stop conditions.

### Files

- `docs/tasks/README.md`
- `docs/tasks/TRACKER.md`
- `docs/tasks/A-artifact-streaming-writer.md`
- `docs/tasks/B-trainer-streaming-emission.md`
- `docs/tasks/C-export-memory-gate-reporting.md`
- `docs/tasks/D-workflow-integration-publication.md`
- `docs/tasks/E-streaming-export-qualification.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Cross-checked task ownership, dependencies, tests, and stop conditions against `docs/multiway_streaming_full_blueprint_export_architecture.md`.
- No build or tests were run because this task changes planning documentation only.

### Limitations

- No implementation task has started; A and C are ready, while B, D, and E remain dependency-blocked.

## PB-7 - Architect streaming full-blueprint export

**Status:** Complete
**Completed:** 2026-10-24

- Traced the 50K export peak to the materialized 50,112,088-row artifact, `save_atomic` deep copy, and validation's copied rows plus flattened runtime store.
- Designed a schema-2-compatible streaming exporter with deterministic metadata ordering, incremental logical hashing, bounded validation, atomic temp cleanup, and a fresh export-phase process/system memory gate.
- Preserved quantization semantics, checkpoint/export exclusion, loader compatibility, traversal and merge hot paths, and Task G capacities in the design.

### Files

- `docs/multiway_streaming_full_blueprint_export_architecture.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Cross-checked the design against the current trainer, artifact, store, compact/reference storage, memory preflight, workflow, atomic publication, and artifact tests.
- Reconciled the supplied run with `tmp/qualification/w8_50k_relwithdebinfo/report.json`: 3,921,243,000 output bytes, 7,667,360,088 retained export bytes, and 48,237,969,408 peak working-set bytes.
- No build or tests were run because this task changes documentation only and explicitly defers implementation.

### Limitations

- Runtime peak reduction and byte/hash equivalence remain to be proven by implementation and matched qualification runs.
- `core::publish_atomic_replace()` has a non-atomic direct-copy fallback for a missing destination; strict full-blueprint publication must reject or harden that mode.

## PB-6 - Add optional training progress speed telemetry

**Status:** Complete
**Completed:** 2026-10-24

- Added `--progress-speed true|false` to report interval and overall trajectories per second on clean training progress lines.
- Kept speed measurement outside traversal and merge kernels.

### Files

- `examples/multiway_workflow_main.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused Debug build for `texas_multiway_train` passed.
- `test_multiway_workflow_boundary` passed.

### Limitations

- Rates are batch-boundary wall-clock telemetry, not a per-worker profiler.

## PB-6 - Fix multiway chance-runout catalog equivalence

**Status:** Complete
**Completed:** 2026-10-24

- Fixed packed public-catalog compatibility so an identical public payload reached through equivalent non-betting chance parents is accepted, matching the coordinator descriptor rule.
- Preserved validation of public payloads, betting-action ancestry, fixed merge order, transactional failed batches, memory gates, checkpoint/export exclusion, and Task G capacities.
- Added bounded conflicting-descriptor diagnostics containing identity, parent, canonical history, board/runout, betting snapshot, action menu, and incoming edge fields.

### Files

- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused Debug build and focused solver/parallel training tests passed.
- Fixed-order worker coverage passed for 1, 2, 4, and 8 workers.
- Reproduced the original 8-worker failure at 5,000 trajectories, then the fixed replay completed 6,000 trajectories with zero discards and `failed=false`.
- Full Debug build and full CTest passed.
- `git diff --check` passed.

### Limitations

- The post-fix artifact replay stops after the former sixth-batch failure boundary; no large qualification workload was run.

## PB-6 - Add optional training progress memory telemetry

**Status:** Complete
**Completed:** 2026-10-24

- Added `--progress-memory true|false` to opt into current working-set, peak working-set, and configured hard-gate bytes on training progress lines.
- Reports `working_set=unavailable` when the platform telemetry is unavailable and leaves default progress output unchanged.

### Files

- `examples/multiway_workflow_main.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused Debug build and focused parallel-training test passed.
- Full Debug build and full CTest passed.
- `git diff --check` passed.

### Limitations

- Memory values are sampled at clean batch boundaries and are process working-set telemetry, not private-memory estimates.

## PB-6 - Add live multiway training progress reporting

**Status:** Complete
**Completed:** 2026-10-24

- Added a batch-boundary progress callback to the multiway blueprint trainer and session.
- Training now reports `batches_completed=1/N` style progress by default, with `--progress=false` and `--progress-every N` controls.
- Kept checkpoint chunking, deterministic fingerprints, failed-batch behavior, and existing inspection progress compatibility unchanged.

### Files

- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- Focused Debug build and focused parallel-training test passed.
- Full Debug build and full CTest passed.

### Limitations

- Progress is reported only after clean batch boundaries. The 50M workload remains gated and was not run.

## PB-6 - Task L staged large-F1 qualification with hard RAM safety gates

**Status:** Complete with 50M authorization gate
**Completed:** 2026-10-24

- Added deterministic staged qualification at 50K, 500K, 5M, and explicitly authorized 50M trajectories; the 50M stage is not automatic.
- Added a conservative 48 GiB process working-set gate by default, configurable resident allowance, preflight rejection reporting, and fail-closed runtime monitoring with preflight/runtime phase reporting.
- Preserved the configurable legacy 64 GiB process limit, Task G capacities, checkpoint/export mutual exclusion, deterministic fingerprints, and transactional failed-batch behavior.
- Documented that the approximately 21 GB private estimate is conservative accounting rather than observed RAM, bounded Task K passed, 50M remains staged, and Task E corrects accounting rather than RAM usage.

### Files

- `CMakeLists.txt`
- `configs/multiway/f1_dev_v1.cfg`
- `docs/memory_tasks/TRACKER.md`
- `docs/memory_tasks/L_staged_large_f1_qualification.md`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/workflow/multiway_staged_qualification.hpp`
- `src/solver/multiway/workflow/multiway_staged_qualification.cpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_staged_qualification.cpp`
- `tests/test_multiway_workflow_config.cpp`

### Validation

- Focused Debug build and focused CTest passed, including staged progression, preflight/runtime gates, conservative/compatibility limits, overflow saturation, compatibility, zero-discard success, determinism, and transactional failure.
- Full Debug build passed and all 111 CTest tests passed.
- Analyzer self-test passed.
- `git diff --check` passed and final C++ review found no actionable findings.

### Limitations

- No 50M workload was executed; it remains gated by explicit user approval and fresh preflight/runtime safety checks.
- The working-set monitor fails closed when an enabled platform metric is unavailable.
- Runtime samples occur before and after clean batch boundaries so a rejected batch remains transactional.

## PB-6 - Task K final bounded memory qualification

**Status:** Complete with limitations
**Completed:** 2026-10-24

- Added baseline-versus-final qualification analysis with medians, ranges, working-set/private deltas, throughput/CPU deltas, fingerprints, Tracy allocation evidence, preflight categories, and candidate attribution.
- Added an explicit source-config input to the bounded normal, Tracy, and matrix runners so the final matrix exercised the selected Task G capacities without changing the preserved baseline artifact.
- Final decision: `bounded_memory_qualification_pass_with_limitations`.

### Before and after

| Case | Median peak private baseline -> final | Reduction | Median throughput baseline -> final |
| --- | ---: | ---: | ---: |
| A1 | 3441930240 -> 1379082240 | 2062848000 | 685.741 -> 687.753 trajectories/s |
| A2 | 4449996800 -> 2373361664 | 2076635136 | 713.341 -> 714.608 trajectories/s |
| A3 | 9948704768 -> 1691877376 | 8256827392 | 1363.18 -> 1370.69 trajectories/s |
| A4 | 10928185344 -> 2668765184 | 8259420160 | 1346.4 -> 1386.3 trajectories/s |
| A5 | 10985193472 -> 2672914432 | 8312279040 | 1308.63 -> 1520.21 trajectories/s |

- The one-to-four-worker private slope fell from 2164262229.33 to 100242773.33 bytes, a 2064019456-byte reduction. The stream-capacity slope fell from 2160000000 to 100003264 bytes.
- All 25 normal runs completed with zero discards and `failed=false`. All five Tracy captures contained allocation and free events. Final fingerprints matched the baseline for every case and fixed/dynamic equivalence held.
- Final explicit preflight categories preserved the 15369522466-byte mapped virtual artifact separately. A1 estimated 21090186760 private peak and 36427740680 working-set peak bytes; A3 estimated 21405970416 private peak and 36743528432 working-set peak bytes. Checkpoint and export transients were each 10520000256 bytes and counted by maximum.

### Files

- `scripts/analyze/analyze_multiway_memory_matrix.ps1`
- `scripts/run/run_multiway_memory_matrix.ps1`
- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_tracy_training_capture.ps1`
- `docs/memory_tasks/TRACKER.md`
- `tmp/memory-attribution/final/memory_summary.json`
- `tmp/memory-attribution/final/memory_samples.csv`
- `tmp/memory-attribution/final/evidence.md`

### Validation

- Analyzer self-test passed.
- Full Debug build and all 110 CTest tests passed.
- RelWithDebInfo normal and Tracy binaries were built successfully.
- `git diff --check` passed and final C++ correctness/performance review found no actionable findings.

### Limitations

- The qualification is bounded to the five-case matrix at 1,000 trajectories per batch and does not qualify a 50M-trajectory production run.
- Tracy allocation counts are diagnostic evidence, not production throughput measurements.
- Working-set peaks remained within measurement noise because the mapped artifact and validation residency were unchanged; the measured reduction is in private commitment.
- Task E corrects accounting and is not a RAM optimization.

## PB-6 - Task E correct memory preflight accounting

**Status:** Complete
**Completed:** 2026-10-24

- Replaced overlapping preflight totals with explicit existing working-set, existing private-commitment, additional private-capacity, mapped virtual, mapped resident allowance, mutually exclusive transient, allocator-overhead, and private/working-set peak fields.
- Preserved legacy ledger/config names, added estimate schema version 2, serialized both peak estimates, and reported rejected category plus admission stage.
- Used Task A baselines and Task B retained allocations without treating mapped virtual bytes as private commitment. This corrects accounting and is not a RAM optimization.

### Files

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/memory_tasks/TRACKER.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused build passed for `test_multiway_memory`, `test_multiway_workflow_config`, `test_multiway_qualification_reports`, and `test_multiway_artifact_preflight`.
- Focused CTest passed all four tests. Full Debug build and full CTest passed.
- RelWithDebInfo `texas_multiway_train` build passed.
- Final normal F1 A1 and A3 runs passed with zero discards, `failed=false`, and matching fingerprint `9806774443936985566`; estimates were 38,690,515,208 and 45,794,495,472 working-set peak bytes.
- `git diff --check` passed. C++ review found no actionable findings.

### Limitations

- Runtime A1/A3 evidence was collected as bounded 1,000-trajectory runs; no RAM reduction claim is made.

## PB-6 - Task G worker stream right-sizing

**Status:** Complete
**Completed:** 2026-10-24

- Replaced shared worker stream ceilings with explicit regret-delta, continuation-delta, discovery-public-state, and discovery-row capacities throughout workflow parsing, solver limits, preflight accounting, worker construction, identity, reports, and exhaustion telemetry.
- Preserved legacy `worker_delta_capacity` and `discovery_capacity` fallback behavior and added transactional failed-batch handling with split capacity-exhaustion flags.
- Sized the F1 development profile from all Task D captured batches and attempts: 64,000 regret entries, 1,024 continuation entries, 200,000 discovery public states, and 100,000 discovery rows per worker.
- Task G evidence: `tmp/memory-attribution/task-g-explicit-analysis/evidence.md`.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/engine/`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `configs/multiway/f1_dev_v1.cfg`

### Validation

- Focused compact build passed for all five required targets.
- Focused CTest passed all five tests. Full build and full CTest passed.
- The explicit-capacity matrix passed 15 normal samples with zero discarded trajectories, zero failed reports, and no capacity exhaustion.
- All five normal-case deterministic fingerprints match `baseline-final`; A3 and A4 Tracy fingerprints match their normal runs.
- One-to-four-worker private-memory slope fell from 2.016 GiB to 0.093 GiB. Stream-capacity slope fell from 2.012 GiB to 0.093 GiB. Both reductions are approximately 95.4%.
- One-worker median throughput remained within noise or improved: A1 685.94 to 689.40 trajectories/s, A2 715.33 to 716.31 trajectories/s. One-worker peak RSS remained unchanged within measurement noise.
- `git diff --check` passed and the C++ review found no actionable findings.

### Limitations

- Tracy A3 and A4 captures were collected as required; the normal matrix analyzer was run with Tracy completeness disabled because only those two Tracy cases are required for Task G.

## PB-6 - Task D bounded memory attribution matrix

**Status:** Complete
**Completed:** 2026-10-24

- Restored the direct Tracy capture build path and kept raw memory-event timestamps unchanged while clamping only the derived plot timeline.
- Executed the A1-A5 matrix with three normal repeats and five Tracy captures; the analyzer selected G from the one-to-four-worker private-memory and stream-capacity slope.

### Files

- `external/tracy/server/TracyWorker.cpp`
- `scripts/build_multiway_tracy.ps1`
- `scripts/run/run_multiway_tracy_training_capture.ps1`
- `scripts/analyze/analyze_multiway_memory_matrix.ps1`
- `tmp/memory-attribution/baseline-final/`

### Validation

- Rebuilt `texas_multiway_train_tracy`, `tracy-capture`, and `tracy-memoryexport` in `RelWithDebInfo`.
- A1 and A3 direct Tracy captures passed with non-empty traces, allocation/free events, CSV export, and `failed=false` reports.
- Full A1-A5 matrix passed: 15 normal samples and five Tracy samples.
- Analyzer self-test and `baseline-final` analysis passed; selected candidate G.
- `scripts/codex_powershell.ps1 scripts/python/full_build.py` and `git diff --check` passed.

### Limitations

- Tracy captures stop at the configured 60-second capture window, so longer cases can retain live allocations at disconnect.

## PB-6 - Fix Windows process memory snapshot capture

**Status:** Complete, diagnostic infrastructure fix
**Completed:** 2026-10-24

- Normalized a transient Windows `PeakWorkingSetSize` under-report so the initial process snapshot remains valid.
- Preserved strict phase-order and snapshot validation for malformed data.
- Confirmed the Tracy smoke trainer creates a valid report for 1,000 trajectories.

### Files

- `src/solver/multiway/engine/multiway_memory.cpp`
- `tests/test_multiway_memory.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Rebuilt `texas_multiway_train_tracy` in `RelWithDebInfo` successfully.
- `test_multiway_memory.exe` passed all 16 tests.
- Focused CTest passed `test_multiway_memory`.
- `run_multiway_ram_1000.ps1` passed the 1,000-trajectory smoke and created `tmp/tracy/smoke-fix/training/report.json`.

### Limitations

- Tracy daemon capture still requires a host run because this sandbox cannot connect to localhost sockets.

## PB-6 - Restructure repository scripts

**Status:** Complete, utility tooling
**Completed:** 2026-10-24

- Added `scripts/python/`, `scripts/analyze/`, and `scripts/run/` categories.
- Moved Python utilities, analysis scripts, and run/Tracy scripts into their matching directories.
- Updated repository-root resolution, sibling-script paths, tests, README, and workflow documentation.

### Files

- `scripts/python/`
- `scripts/analyze/`
- `scripts/run/`
- `README.md`
- `AGENTS.md`
- `docs/application_and_script_io.md`
- `docs/memory_tasks/`
- `docs/multiway_memory_measurement_and_reduction_plan.md`
- `tests/cmake/test_tracy_boundary.cmake`

### Validation

- PowerShell parser validation passed for moved PowerShell scripts.
- Python compile validation passed for moved Python scripts.
- No build, test, benchmark, or solver run was performed.

### Limitations

- Historical entries in `docs/PLURIBUS_LOG.md` retain their original script paths.

## PB-6 - Add headless Tracy capture daemon launcher

**Status:** Complete, utility tooling
**Completed:** 2026-10-24

- Added a foreground PowerShell launcher for the Tracy capture daemon.
- The launcher resolves the repository-local daemon, creates a timestamped output directory, uses the standard port 8086, and preserves graceful Ctrl+C shutdown for trace flushing.

### Files

- `scripts/run/run_tracy_capture_daemon.ps1`

### Validation

- PowerShell parser validation passed.

### Limitations

- The daemon was not started during validation.

## PB-6 - Implement Task C: Tracy allocation lifetimes

**Status:** Complete, diagnostic only
**Completed:** 2026-10-23

- Enabled allocation and free lifetime events only on Tracy diagnostic targets.
- Added no-op normal-build wrappers, guarded scalar, array, aligned, sized, and nothrow paths, and report/manifest provenance.
- Bounded capture `memory-task-c-smoke` used one worker, one batch, 1000 trajectories, fixed scheduling, and no worker pinning.
- Trace artifact: `tmp/memory-task-c-smoke/texas_multiway_train_tracyexe_127.0.0.1_8086.tracy` with 245,879,075 bytes.
- Memory inspection: `tmp/memory-task-c-smoke/memory_events.json` reported 43,207,267 allocations, 43,207,267 frees, and 0 active allocations.
- The report and manifest recorded `tracy_memory_events_enabled=true` and `allocation_probe_valid=false`; no production target received Tracy memory definitions.

### Validation

- Normal allocation and report tests passed, including null nothrow handling, delete pairing, reentrancy coverage, no-op wrappers, and contradictory provenance rejection.
- Focused CTest passed 2/2 and the Tracy boundary script passed.
- Tracy diagnostic targets, capture daemon, and memory exporter built successfully.
- Full Debug solution build passed. Full CTest passed 110/110 with normal filesystem access; two atomic-publish tests were sandbox-denied when run without elevation and passed when rerun with normal filesystem access.
- `git diff --check` passed.

### Limitations

- This task adds diagnostic instrumentation only. It makes no memory-reduction claim and does not present Tracy overhead as production throughput.
- The capture daemon returned its normal shutdown sentinel after producing the complete trace; artifact inspection verified the capture and allocation/free pairing.

## PB-6 - Implement Task B: Retained subsystem capacity attribution

**Status:** Complete
**Completed:** 2026-10-23
**Implementation commits:** `84a3c31`, `787e845`

- Added fixed-layout `noexcept` live and retained-capacity snapshots for all
  Task B owners, including per-worker and aggregate fields.
- Added checked arithmetic, grouping-only totals, phase capture, versioned JSON,
  validation, and derived `unattributed_private_bytes` when process data is valid.
- Kept this task telemetry-only and did not change capacities or claim memory reduction.

### Files

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_model.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_public_catalog.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- Focused `scripts/compact_build.py --parallel 1` passed for all five Task B targets.
- Focused CTest passed 5/5.
- Serial full `scripts/compact_build.py --parallel 1` passed for all targets.
- Serial full `scripts/compact_ctest.py --parallel 1` passed 110/110 tests.
- `git diff --check` passed.
- Independent C++ review found no actionable findings.

### Limitations

- The prescribed `scripts/full_build.py` wrapper was also attempted, but its nested default 16-job build stalled without child output in this environment; the equivalent serial repository build and complete CTest run passed.
- Task B reports attribution only; it makes no memory-reduction claim.

## PB-6 - Implement Task A: Process memory and phase snapshots

**Status:** Complete
**Completed:** 2026-10-23
**Implementation commits:** `e3f702c`, `0703fa4`

- Added versioned platform-aware process memory snapshots and all required
  ordered phase boundaries from process start through export.
- Added bounded training/report storage, stable JSON schema names, legacy report
  compatibility, and coarse Tracy plots for current working set and private bytes.
- Kept solver capacities, artifact validation, checkpoint bytes, and training
  behavior unchanged.

### Files

- `include/solver/multiway/engine/multiway_memory.hpp`
- `src/solver/multiway/engine/multiway_memory.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`

### Validation

- Focused Debug build passed for the four Task A targets.
- Focused CTest passed 4/4.
- Serial full Debug CMake build passed.
- Repository `compact_ctest.py` passed all 110 tests.
- `git diff --check` passed.
- Independent C++ review found no actionable findings.

### Limitations

- Runtime memory conclusions and Tracy captures remain out of scope for Task A.
- The prescribed `full_build.py` stalled before producing output; the equivalent
  serial full CMake build and complete compact CTest run passed.

## PB-6 - Add multiway memory task tracker

**Status:** Complete
**Completed:** 2026-10-23

- Added a one-page A-K tracker with current status, one-sentence problem or
  result, dependencies, task links, evidence state, and next action.
- Defined consistent update rules for task start, completion, failure, and
  blocking conditions.

### Files

- `docs/memory_tasks/TRACKER.md`
- `docs/memory_tasks/README.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed tracker dependencies and links against all A-K task files.
- No build, test, benchmark, solver run, or Tracy capture was performed because
  this task changed documentation only.

### Limitations

- All implementation tasks remain unstarted; Task A is the first ready task.

## PB-6 - Split multiway memory work into executable agent tasks

**Status:** Complete
**Completed:** 2026-10-23

- Split the memory plan into required foundation, evidence, accounting,
  evidence-gated optimization, and final qualification tasks A through K.
- Added standalone scope, dependencies, files, implementation instructions,
  commands, tests, acceptance criteria, completion contracts, and stop rules.
- Kept optimization tasks F through J conditional on the measured Task D
  candidate decision.

### Files

- `docs/memory_tasks/README.md`
- `docs/memory_tasks/A_process_memory_phase_snapshots.md`
- `docs/memory_tasks/B_retained_subsystem_capacity.md`
- `docs/memory_tasks/C_tracy_allocation_lifetimes.md`
- `docs/memory_tasks/D_bounded_attribution_matrix.md`
- `docs/memory_tasks/E_preflight_accounting.md`
- `docs/memory_tasks/F_bucket_validation_residency.md`
- `docs/memory_tasks/G_worker_stream_rightsizing.md`
- `docs/memory_tasks/H_compact_row_metadata_paging.md`
- `docs/memory_tasks/I_admission_scratch_retention.md`
- `docs/memory_tasks/J_checkpoint_export_peak.md`
- `docs/memory_tasks/K_final_memory_qualification.md`
- `docs/multiway_memory_measurement_and_reduction_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed task dependencies, source/test ownership, commands, acceptance
  gates, and stop conditions against the current repository.
- No build, test, benchmark, solver run, or Tracy capture was performed because
  this task changed documentation only.

### Limitations

- Tasks F through J remain intentionally unselected until Task D produces the
  bounded attribution evidence.

## PB-6 - Plan multiway memory measurement and reduction

**Status:** Complete
**Completed:** 2026-10-23

- Documented the existing bounded RAM evidence and current measurement gaps.
- Defined process, phase, subsystem-capacity, and Tracy allocation telemetry.
- Ranked memory hypotheses and specified a measurement-first implementation,
  qualification, and rollback sequence for future agents.

### Files

- `docs/multiway_memory_measurement_and_reduction_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the document against the current memory, bucket artifact, worker
  scratch, compact storage, Tracy build, and report source paths.
- No build, test, benchmark, solver run, or Tracy capture was performed because
  this task changed documentation only.

### Limitations

- All proposed root causes remain hypotheses until the bounded attribution
  matrix is run.
- No production memory behavior changed.

## F1 - Bounded execution plan and checkpoint equivalence

**Status:** Complete, bounded evidence only
**Completed:** 2026-10-23

- Restored and executed the ten-step F1 human execution plan.
- Froze development capacities from the bounded pinned sizing pilot.
- Preserved compact integer regret and strategy-mass values exactly across
  checkpoint save and restore, including canonical page-order remapping.
- Fixed training resume output so a separate `--checkpoint-dir` never overwrites
  the input checkpoint.
- Continuous and split/resumed 2,000-trajectory runs produced byte-identical
  334,233,264-byte blueprints with zero discards and merge fingerprint
  `13248175190465110669`.
- The 50-million-trajectory workload and large matrices were not run.

### Files

- `docs/f1_human_execution_implementation_plan.md`
- `configs/multiway/f1_dev_v1.cfg`
- `tmp/f1/final-evidence-bundle.md`
- `tmp/f1/resume-equivalence-evidence.md`

### Validation

- Focused build passed for training, compact storage, blueprint training,
  checkpoint equivalence, and F1 acceptance targets.
- Five focused tests passed, including reverse-page exact-value checkpoint
  recovery.


## TH1-TH2 - Bounded 20-process variance probe

**Status:** Complete, diagnostic only
**Completed:** 2026-10-23

- Ran the bounded four-case probe with 20 short processes.
- Dynamic throughput variance was low: CV `0.48%` pinned and `0.31%` unpinned.
- Fixed scheduling was noisier when pinned at `1.09%` CV; fixed unpinned was `0.20%`.
- The analyzer selected `diagnostic_only_no_solver_change`.

### Files

- `docs/PLURIBUS_LOG.md`

### Validation

- `run_multiway_th1_th2_dynamic_variance_probe.ps1` completed all 20 phases.
- `analyze_multiway_th1_th2_dynamic_variance_probe.ps1` completed and wrote `tmp/th1-th2-dynamic-variance-probe/variance_probe_summary.json`.

### Limitations

- Host context telemetry was incomplete, so this probe does not establish a cause.
- Five samples per scheduler/pinning case are insufficient for solver qualification.

## AD7 - Pending menu lookup timing qualification

**Status:** Complete
**Completed:** 2026-10-23

- Reused the pending-menu equality completion timestamp for lookup telemetry.
- Avoided timing-clock reads when the corresponding diagnostics are disabled.
- Preserved collision-safe equality checks, menu handles, and catalog behavior.

### Validation

- Focused catalog, builder, and qualification-report tests passed.
- Four bounded 1-worker cold captures passed for fixed and dynamic scheduling.
- Menu lookup measured `7.183-7.428 ms`, catalog commit `21.270-22.118 ms`,
  and coordinator admission `239.499-245.952 ms`.
- All captures reported `failed=false`, zero discards, fingerprint
  `9806774443936985566`, and checkpoint size `137380835` bytes.
- No large matrix was run. Tracy reports were valid; no `.tracy` files were
  emitted by the capture wrapper.

### Files

- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `docs/PLURIBUS_LOG.md`

## TH1-TH2 - Dynamic 2-worker variance review

**Status:** Complete, diagnostic only
**Completed:** 2026-10-23

- Added a replay-only analyzer for the formal TH1-TH2 artifacts.
- Compared 10 dynamic 2-worker samples with 10 fixed 2-worker control samples.
- Derived active imbalance, barrier, condition-variable wait, wake latency,
  scheduler-claim, tail-idle, and process-CPU shares.
- Host boundary telemetry did not explain the variance: clock delta was zero,
  with load deltas of `2%` pinned and `0%` unpinned.
- Scheduler-claim and condition-variable wait shares were the strongest
  measured associations, but no solver change is selected from this small
  sample.

### Files

- `scripts/analyze_multiway_th1_th2_dynamic_variance.ps1`
- `scripts/run_multiway_th1_th2_dynamic_variance_probe.ps1`
- `scripts/analyze_multiway_th1_th2_dynamic_variance_probe.ps1`
- `docs/plans/2026-10-15/14_th1_th2_dynamic_2worker_variance_review.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Replay analyzer completed successfully against
  `tmp/th1-th2-formal-qualification`.
- Wrote the target summary and target/control CSV artifacts.
- Added, but did not run, a four-case per-repeat probe for future evidence.

### Limitations

- No new benchmark was run.
- The correlations are diagnostic associations, not causal proof.
- Per-repeat host snapshots and scheduler event counters remain future work.
- The new probe is bounded to 20 short processes and is not a solver qualification.

## TH1-TH2 - Formal steady-state qualification closure

**Status:** Complete with variance review note
**Completed:** 2026-10-23

- Ran the formal worker set `1,2,4` for fixed/dynamic scheduling and pinned/unpinned placement.
- Used one warmup batch, four timed batches, five repeats, and batch size `1,000`.
- The dedicated analyzer verified `12` worker summaries and `60` samples.
- Checkpoint and blueprint output were disabled; host context and affinity metadata were complete.
- All groups shared fingerprint `5060116991684512043`.
- TH2 capacity accounting had already passed the bounded smoke with valid allocation probes and zero capacity exhaustion.
- The formal TH1-TH2 qualification is complete.

### Files

- `scripts/run_multiway_steady_state_matrix.ps1`
- `scripts/analyze_multiway_steady_state_matrix.ps1`
- `scripts/analyze_multiway_th1_th2_qualification.ps1`
- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- The formal runner completed all four scheduler/placement groups.
- The generic steady-state analyzer passed all `12` summaries.
- The dedicated TH1-TH2 analyzer reported `PASS_WITH_VARIANCE_REVIEW`.

### Limitations

- Dynamic 2-worker pinned and unpinned groups exceeded the diagnostic repeat-range threshold.
- No solver optimization is selected from that variance.
- Worker `8` and memory qualification were intentionally excluded.

## TH1-TH2 - Bounded pinned versus unpinned comparison

**Status:** Bounded verification complete
**Completed:** 2026-10-23

- Ran the pinned three-repeat counterpart for fixed and dynamic scheduling with workers `1`, `2`, and `4`.
- All six groups passed analysis with complete host context, stable variance classification, and fingerprint `5060116991684512043`.
- Pinned throughput versus the unpinned run changed by `+0.5%`, `+11.7%`, and `+2.8%` for dynamic workers `1`, `2`, and `4`; and `+0.3%`, `+3.6%`, and `-1.7%` for fixed workers `1`, `2`, and `4`.
- Host frequency stayed at `4,300 MHz`; pinned groups reported `pinned_logical_processor` placement on `32` logical processors.
- No solver optimization is selected from the comparison.

### Files

- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `scripts/run_multiway_steady_state_matrix.ps1` completed both pinned scheduler phases.
- `scripts/analyze_multiway_steady_state_matrix.ps1` passed all six groups and eighteen samples.

### Limitations

- This was a bounded three-repeat comparison, not the formal five-repeat qualification.
- No 8-worker or memory qualification was run.

## TH1 - Add steady-state host context and variance analysis

**Status:** Complete
**Completed:** 2026-10-23

- Added per-group CPU frequency, processor load, total CPU, logical processor, and benchmark affinity snapshots to the steady-state manifest.
- Added repeat minimum/maximum throughput, standard deviation, coefficient of variation, relative range, host deltas, and variance interpretation to the analyzer.
- Added host-context fallback from per-processor load to total CPU load.
- Reran the six-case, three-repeat unpinned benchmark with complete host context.
- All six groups classified as stable; boundary frequency was `4,300 MHz`, boundary load delta was at most one percentage point, and affinity was unrestricted on `32` logical processors.

### Files

- `scripts/run_multiway_steady_state_matrix.ps1`
- `scripts/analyze_multiway_steady_state_matrix.ps1`
- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Both PowerShell scripts passed syntax checks.
- The instrumented runner completed both scheduler phases.
- The analyzer passed all six groups and eighteen samples.
- All groups had complete host context and stable variance classifications.

### Limitations

- Host snapshots are group-boundary measurements, not per-repeat samples.
- Only unpinned execution was run.
- The full pinned/unpinned qualification remains pending.

## TH1 - Three-repeat unpinned steady-state qualification

**Status:** Bounded verification complete
**Completed:** 2026-10-23

- Repeated the six unpinned fixed/dynamic cases for workers `1`, `2`, and `4` three times.
- The analyzer passed all six groups with three samples per worker count.
- All samples shared fingerprint `5060116991684512043`.
- Median throughput was fixed `8,426.7`, `13,235.4`, and `19,648.0`; dynamic `8,406.5`, `15,316.8`, and `22,473.5` trajectories per second for workers `1`, `2`, and `4`.
- The repeat spread at 2 workers and fixed 4-worker tail-idle variation do not identify a stable code bottleneck.

### Files

- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `scripts/run_multiway_steady_state_matrix.ps1` completed both scheduler phases.
- `scripts/analyze_multiway_steady_state_matrix.ps1` passed all six groups and eighteen samples.

### Limitations

- Only unpinned execution was run.
- Repeat variance prevents selecting a new optimization target from this run.
- The full five-repeat pinned/unpinned qualification remains pending.

## TH1 - Bounded steady-state smoke

**Status:** Bounded verification complete
**Completed:** 2026-10-23

- Ran unpinned fixed and dynamic scheduling for workers `1`, `2`, and `4`.
- Used one warmup batch, four timed batches, one repeat, and batch size `1,000`.
- Checkpoint and blueprint output were disabled by the benchmark binary.
- The six worker summaries passed analysis with one common non-zero fingerprint: `5060116991684512043`.
- Throughput ranged from `8,384.8` to `27,968.3` trajectories per second.

### Files

- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `scripts/run_multiway_steady_state_matrix.ps1` completed both scheduler phases.
- `scripts/analyze_multiway_steady_state_matrix.ps1` passed all six worker summaries.

### Limitations

- Only one repeat and unpinned execution were run.
- The full five-repeat pinned/unpinned qualification remains pending.

## TH1-TH2 - Bounded capacity and accounting smoke

**Status:** Bounded verification complete
**Completed:** 2026-10-23

- Ran six unpinned cases: fixed and dynamic scheduling with workers `1`, `2`, and `4`.
- Used one batch of `100` trajectories per case.
- All cases accepted all trajectories with zero discards, failures, and capacity exhaustion.
- Allocation probing was valid in every report, with per-worker allocation counts and bytes recorded.
- Configured capacities were `1,000,000` worker-delta entries per worker and `4,000,000` discovery entries.
- Deterministic counts, checkpoint bytes `17,082,301`, and fingerprint `10519539386654739548` matched across all cases.

### Files

- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `scripts/run_multiway_worker_matrix.ps1` completed all `6/6` phases.
- Report gates passed for every case.
- Independent report comparison found one unique value for every deterministic and capacity gate.

### Limitations

- This was a bounded smoke, not the full five-repeat pinned/unpinned TH1 matrix.
- Checkpoint and blueprint output were not disabled by this runner, so this does not constitute final steady-state throughput qualification.

## AD7F.10 - Close Candidate A validation

**Status:** Complete
**Completed:** 2026-10-23

- Ran the dedicated one-stream fast-path equivalence test successfully.
- Ran short fixed and dynamic one-worker Tracy captures.
- Both captures accepted `1,000` trajectories with zero discards and failures.
- Fast-path use was `1` per capture, with zero duplicate candidates.
- Checkpoint bytes remained `137380835` and the deterministic fingerprint remained `9806774443936985566`.
- Admission measured `241.505 ms` fixed and `249.417 ms` dynamic.

### Files

- `docs/plans/2026-10-18/02_ad7_admission_variance_execution_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `test_multiway_solver multiway_solver_discovery_one_stream_fast_path_matches_generic_merge` passed.
- Two short Tracy captures passed the 1,000-trajectory report gates.

### Limitations

- The eight-case cold/steady matrix remains deferred.
- Allocation telemetry remains unavailable because `allocation_probe_valid=false`.

## AD7F.9 - Compare admission child timers and select candidate

**Status:** Complete
**Completed:** 2026-10-23

- Compared all four fresh AD7F.8 reports using the complete capture set.
- Confirmed that public-state merge accounts for nearly all discovery-merge time: `65.596-67.202 ms` of `65.703-67.276 ms`.
- Confirmed one single-stream batch per capture, zero duplicate candidates, and identical source counts of `69,194` public states and `31,070` rows.
- Selected Candidate A. No parent-transition edge kind dominated, so Candidate B was not selected.
- Recorded identical descriptor-copy volumes and zero output-capacity reuse.

### Files

- `docs/plans/2026-10-18/02_ad7_admission_variance_execution_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- PowerShell comparison of all four saved `report.json` files completed.
- No build, test, benchmark, or new solver capture was run.

### Limitations

- Allocation counts and bytes remain unavailable because `allocation_probe_valid=false`.
- The eight-case cold/steady matrix remains deferred.

## AD7F.8 - Controlled cold qualification tooling

**Status:** Complete
**Completed:** 2026-10-23
**Implementation commit:** `ec05509 [PB-5] Add AD7F.8 cold qualification tooling`

- Added a dedicated four-capture runner for fixed/dynamic one-worker cold runs.
- Recorded host CPU load/frequency, Tracy artifacts, processor placement, and per-capture provenance.
- Added an analyzer that requires every capture to pass phase coverage, byte accounting, fingerprint, checkpoint, deterministic-count, Tracy, and placement gates.
- CMake Tracy targets and the focused five-test CTest set passed.
- Four fresh one-worker cold captures passed with Tracy 0.14.1.
- All captures reported `acceptance_pass=true`, phase coverage from `0.985648` to `0.987272`, checkpoint bytes `137380835`, fingerprint `9806774443936985566`, zero discarded trajectories, and equal deterministic counts.
- Host snapshots recorded approximately 4% CPU load and 4.3 GHz current frequency; resolved processors were recorded per capture.

### Files

- `scripts/run_multiway_ad7f8_cold_qualification.ps1`
- `scripts/analyze_multiway_ad7f8_cold_qualification.ps1`
- `scripts/run_multiway_tracy_training_capture.ps1`

### Execution note

- Native Tracy capture required elevated local execution because the default sandbox blocked the localhost socket connection.
- No steady-state or large matrix was run.

## AD7F.5 - Reduce catalog commit copy and menu lookup cost

**Status:** Complete
**Completed:** 2026-10-23
**Implementation commit:** `d876cd4 [PB-5] Multiway: Reuse pending admission menus`

- Resolve matching pending batch menus before repeating the existing catalog menu-index search.
- Preserve collision checks, capacity preflight, deterministic menu handles, and strong commit semantics.

### Files

- `src/solver/multiway/engine/multiway_public_catalog.cpp`

### Validation

- CMake Debug focused build passed.
- CTest passed the catalog, solver, qualification-report, parallel-training, and terminal-adapter suites.

### Limitations

- No Tracy captures or large matrices were run.

## AD7F.4 - Reuse validated state metadata during parent and row checks

**Status:** Complete
**Completed:** 2026-10-23
**Implementation commit:** `7632c5f [PB-5] Multiway: Reuse admission betting metadata`

- Added allocation-free fixed betting-state scratch for the current admission batch.
- Reused validated parent and row metadata instead of rebuilding fixed states for prepared parents and batch rows.
- Preserved catalog-only row fallback and direct-admission behavior.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`

### Validation

- CMake Debug focused build passed in an isolated build directory.
- CTest passed the catalog, solver, qualification-report, parallel-training, and terminal-adapter suites.

### Limitations

- No Tracy captures or large matrices were run.

## AD7F.3 - Classify the public-state batch with one sorted merge

**Status:** Complete
**Completed:** 2026-10-23
**Implementation commit:** `66a7c1f [PB-5] Multiway: Reuse sorted admission classification`

- Reused the sorted catalog classification pass to retain non-owning existing-node views.
- Removed the second per-existing-state catalog lookup during prepared-batch preflight.
- Added malformed index protection and mixed existing/new admission coverage.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_solver.cpp`

### Validation

- CMake Debug build passed for the focused targets.
- CTest passed `test_multiway_solver`, `test_multiway_public_catalog`, `test_multiway_qualification_reports`, `test_multiway_parallel_training`, and `test_multiway_terminal_adapter`.

### Limitations

- No Tracy captures or large matrices were run.

## AD7F.2 - Packed existing-state compatibility

**Status:** Implementation complete
**Completed:** 2026-10-23

- Added a non-owning packed-catalog compatibility check for prepared existing public states.
- Removed descriptor reconstruction from the existing-state admission path.
- Added compatibility-check telemetry and report serialization.
- Added regression coverage for packed existing-state admission.

### Validation

- CMake Debug build passed.
- CTest passed the focused catalog, qualification-report, solver, parallel-training, and terminal-adapter suites.
- No Tracy capture or large matrix was run.

## TH4-TH5 - Traversal profiling and topology-aware placement

**Status:** Implementation complete
**Completed:** 2026-10-23

- Added nested Tracy zones for bucket lookup, action-menu buffers, public descriptor construction, street transitions, terminal resolution, and continuation/model evaluation.
- Added Linux physical-core placement from the allowed CPU set and preserved Windows processor-group placement.
- Excluded the coordinator processor when enough worker processors remain.
- Added a Windows active-processor-group fallback when topology enumeration returns no logical placements, and normalized group-qualified processor IDs.
- Added requested and resolved processor telemetry with affinity success and unpinned fallback reporting.
- Added focused affinity and traversal-profile coverage.

### Files

- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `scripts/run_multiway_ram_1000.ps1`
- `tests/test_multiway_parallel_training.cpp`

### Validation

- CMake Debug focused build passed.
- CTest passed `test_multiway_parallel_training`, `test_multiway_qualification_reports`, and `test_multiway_solver`.
- CMake RelWithDebInfo Tracy target passed.
- The short diagnostic binary ran successfully.
- Bounded pinned and unpinned qualification passed all 8 runs; pinned affinity resolved successfully for all selected workers and fingerprints matched.
- Two-repeat 4-worker comparison passed all 8 runs; pinned median throughput was approximately 13-14% below unpinned for both schedulers.

### Limitations

- The external Tracy capture tool did not connect within 15 seconds, so no new capture-zone ranking is claimed.
- The full repeat pinned-versus-unpinned operator comparison remains deferred; current evidence supports diagnostic-only pinning and unpinned default execution.
- The Linux affinity path was not runtime-tested on this Windows host.

## TH3 - Dynamic scheduler chunk qualification tooling

**Status:** Implementation complete
**Completed:** 2026-10-23

- Covered dynamic chunk sizes 1, 2, 4, 8, and 16 in scheduler and parallel-training tests.
- Added worker, scheduler, and pinning filters to the steady-state runner.
- Added a TH3 analyzer for chunk coverage, scheduler metrics, fingerprints, and fixed/dynamic equivalence.
- Fixed the steady-state sample-metric parser so scheduler claim, tail-idle, and CPU fields are captured.

### Validation

- CMake Debug focused build passed for `test_multiway_scheduler` and `test_multiway_parallel_training`.
- CTest passed both focused tests.
- CMake RelWithDebInfo benchmark target passed.
- Bounded TH3 smoke passed for pinned workers 1 and 4, fixed and dynamic modes, and chunks 1/2/4/8/16.

### Limitation

- The full multi-repeat operator qualification matrix was not run.
- No production chunk-size selection is claimed from the bounded smoke.

## Tooling - Make compact CTest use controlled temporary storage

**Status:** Complete
**Completed:** 2026-10-22

- Updated `scripts/compact_ctest.py` to set `TEMP`, `TMP`, and `TMPDIR` to `tmp/ctest_temp` for child tests.
- This keeps Windows atomic-publication tests independent of the default temporary directory.

### Validation

- Bundled Python `scripts/compact_ctest.py --parallel 1` completed with all 110 tests passing.

## Tracy - Split one-stream discovery copy zones

**Status:** Complete
**Completed:** 2026-10-22

- Added coordinator Tracy subzones for one-stream public-state capacity checks, telemetry accounting, owned descriptor copying, and cursor setup.
- Added matching row-copy and cursor subzones.

### Validation

- CMake Debug target `test_multiway_solver` built successfully.
- CMake RelWithDebInfo Tracy target `texas_solver_multiway_training_scaling_tracy` built successfully.
- CTest `test_multiway_solver` passed.
- Full CMake Debug build passed; full CTest passed all 110 tests with workspace-local `TEMP` and `TMP`.

## Tooling - Report empty Tracy captures

**Status:** Complete
**Completed:** 2026-10-22

- Added `-AllowEmpty` to `scripts/analyze_multiway_tracy_zones.ps1`.
- Header-only or prefix-free captures now produce a machine-readable `analysis_status=empty` report with an explicit reason instead of failing the analysis run.

### Validation

- Analyzed the existing header-only short smoke capture with `empty_reason=no_data_rows`.

## Verification - Current admission-copy telemetry smoke

**Status:** Complete
**Completed:** 2026-10-22

- Ran one short current-binary Tracy smoke capture with one worker, one batch, and 100 trajectories.
- The report emitted complete allocation and copy telemetry: 81,292 coordinator allocations, 14,750,031 coordinator bytes, 3,304,944 discovery descriptor-copy bytes, 3,701,028 discovery nested-copy bytes, 3,304,944 catalog descriptor-copy bytes, and 967,924 catalog nested-copy bytes.
- The report passed its 100-trajectory execution gates with zero discarded trajectories and `failed=false`.

### Validation

- `scripts/analyze_multiway_admission_copy_telemetry.ps1` reported `telemetry_complete=true`.

### Limitations

- The 3-second capture ended during artifact loading, so its Tracy CSV contained no zone rows. This was not a qualification capture or worker matrix.

## Tooling - Analyze admission copy telemetry

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tooling) Analyze admission copy telemetry`

- Added a report-only analyzer for coordinator allocations, public-state and nested descriptor copies, output-capacity reuse, and timer residuals.
- Legacy reports remain readable and are explicitly marked incomplete when the new fields are absent.

### Files

- `scripts/analyze_multiway_admission_copy_telemetry.ps1`

### Validation

- PowerShell parser check passed.
- Existing four admission-split reports were analyzed without rerunning workloads.
- The analyzer correctly reported `allocation_probe_valid=false` and missing copy/allocation fields for those legacy reports.

### Limitations

- Fresh captures from the current telemetry-enabled binary are required to populate the missing fields.

## Verification - Full CMake build and CTest

**Status:** Complete
**Completed:** 2026-10-22

- Ran the documented `scripts/full_build.py` workflow through `codex_powershell.ps1`.
- The complete Debug build passed and the full CTest suite passed.
- No qualification capture or worker matrix was started.

### Validation

- `tmp/build_debug.log`: build PASS.
- `tmp/ctest_debug.log`: all tests passed.

## Tooling - Analyze Tracy zone totals

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tooling) Analyze Tracy zone totals`

- Added a bounded PowerShell analyzer that ranks all `mw.*` Tracy CSV zones by total time.
- Reports total milliseconds, event counts, source files, and source lines, with optional JSON output.

### Files

- `scripts/analyze_multiway_tracy_zones.ps1`

### Validation

- PowerShell parser check passed.
- Existing Tracy CSV smoke analysis passed and produced a ranked JSON report.

### Limitations

- It analyzes completed CSV captures only; it does not start Tracy or run workloads.

## Tracy - Trace catalog reconstruction

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tracy) Trace catalog reconstruction`

- Split packed public-state reconstruction into scalar, vector, betting, and history Tracy regions.
- Added an export-output preparation region while preserving descriptor ownership and reconstruction behavior.

### Files

- `src/solver/multiway/engine/multiway_public_catalog.cpp`

### Validation

- CMake Debug build passed with errors-only output.
- Tracy RelWithDebInfo targets passed: `texas_solver_multiway_training_scaling_tracy` and `texas_multiway_train_tracy`.
- Focused tests passed: `test_multiway_public_catalog`, `test_multiway_qualification_reports`, and `test_multiway_solver`.

### Limitations

- No long qualification capture or worker matrix was run.

## Tracy - Trace parent transition edges

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tracy) Trace parent transition edges`

- Added typed Tracy regions for betting-action, board-chance, and street-transition validation.
- Kept the existing validation contracts, edge timers, counters, and deterministic behavior unchanged.

### Files

- `src/solver/multiway/engine/multiway_solver.cpp`

### Validation

- CMake Debug build passed with errors-only output.
- Tracy RelWithDebInfo targets passed: `texas_solver_multiway_training_scaling_tracy` and `texas_multiway_train_tracy`.
- Focused tests passed: `test_multiway_solver` and `test_multiway_parallel_training`.

### Limitations

- No long qualification capture or worker matrix was run.

## Tracy - Trace catalog admission phases

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tracy) Trace catalog admission phases`

- Added Tracy regions for pending preparation, menu resolution, reserve, index merge, and append/packing phases.
- Kept existing catalog counters, ownership, ordering, and error behavior unchanged.

### Files

- `src/solver/multiway/engine/multiway_public_catalog.cpp`

### Validation

- CMake Debug build passed with errors-only output.
- Tracy RelWithDebInfo targets passed: `texas_solver_multiway_training_scaling_tracy` and `texas_multiway_train_tracy`.
- Focused tests passed: `test_multiway_public_catalog`, `test_multiway_qualification_reports`, and `test_multiway_solver`.

### Limitations

- No long qualification capture or worker matrix was run.

## Tracy - Split discovery merge regions and harden topology parsing

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tracy) Add discovery merge subzones`

- Added Tracy regions for single-stream assignment, output preparation, and multi-stream public-state and row merges.
- Added bounds validation before walking variable-size Windows processor-topology records.
- Preserved merge ordering, diagnostics, and scheduler behavior.

### Files

- `src/solver/multiway/engine/multiway_traversal.cpp`

### Validation

- CMake Debug build passed with errors-only output.
- Tracy RelWithDebInfo targets passed: `texas_solver_multiway_training_scaling_tracy` and `texas_multiway_train_tracy`.
- Focused tests passed: `test_multiway_solver` and `test_multiway_parallel_training`.

### Limitations

- No long qualification capture or worker matrix was run.

## Verification - Current scheduler telemetry tree

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (verification) Verify current telemetry tree`

- Full CMake Debug build passed with errors-only output.
- Focused suites passed: public builder, public catalog, qualification reports, solver, and terminal adapter.
- No long matrix or qualification capture was run.

## Tooling - Report scheduler claim telemetry in qualification summaries

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tooling) Report scheduler claim telemetry`

- Added aggregate and per-worker scheduler-claim fields to cold qualification and cold/steady matrix summaries.
- Existing eight-capture matrix analysis passed.

## TH3 - Test scheduler claim accounting

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tests) Verify scheduler claim accounting`

- Added dynamic-skew coverage for per-worker scheduler-claim vector size and aggregate equality.
- `test_multiway_parallel_training` passed.

## TH3 - Preserve legacy scheduler sample parsing

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tooling) Preserve legacy scheduler sample parsing`

- Made the scheduler-claim sample field optional in the analyzer.
- Older sample lines now parse with a zero claim value; summary-only logs remain unchanged.
- PowerShell parsing and existing matrix re-analysis passed.

## TH3 - Parse scheduler claim samples

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (benchmark) Parse scheduler claim samples`

- Added `worker_scheduler_claim_ns` parsing to the steady-state sample analyzer.
- Added grouped median scheduler-claim output to JSON, CSV, and console summaries.

## TH3 - Expose scheduler claim metrics in scripts

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tooling) Expose scheduler claim metrics`

- Added scheduler-claim timing and share fields to the worker-matrix output.
- Added per-worker scheduler-claim output to the RAM-1000 runner.
- PowerShell parser validation passed for the updated scripts.

## TH3 - Add dynamic scheduler claim telemetry

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (scheduler) Add dynamic scheduler claim telemetry`

- Added per-worker and aggregate timing for dynamic scheduler chunk claims.
- Propagated the metric through training status and JSON reports.
- Added report serialization and vector-shape validation coverage.

### Validation

- Debug CMake targets built successfully.
- `test_multiway_parallel_training` and `test_multiway_qualification_reports` passed.
- Tracy RelWithDebInfo targets built successfully.
- A 1-worker, 100-trajectory smoke run emitted the new metric.

## TH3 - Isolate timed worker bounds

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (benchmark) Isolate timed worker bounds`

- Changed benchmark worker active-time and trajectory min/max metrics to use per-worker deltas from the timed interval.
- Prevented warmup batches from contaminating scheduler balance measurements.

### Validation

- CMake Debug target `texas_solver_multiway_training_scaling` built successfully.

## TH3 - Analyze scheduler balance samples

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (benchmark) Analyze scheduler balance samples`

- Extended the steady-state analyzer to parse per-sample barrier, wake, CV-wait, tail-idle, worker-active, and process-CPU metrics.
- Emits raw sample and grouped median JSON/CSV files when new sample lines are present.
- Keeps legacy summary-only logs valid.

### Validation

- PowerShell parser validation passed.
- Existing 16-summary steady-state output re-analyzed successfully.

## Tracy - Add admission and scheduler subzones

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (tracy) Add admission and scheduler subzones`

- Added Tracy subzones for discovery sorting and deduplication, fixed and dynamic scheduler execution, and per-worker result collection.
- Added per-state, parent-transition, row, catalog validation, and menu-resolution zones for admission analysis.

### Validation

- Debug CMake focused targets built successfully.
- Tracy RelWithDebInfo targets built successfully.
- `test_multiway_scheduler` and `test_multiway_parallel_training` passed.

## TH3 - Report scheduler balance metrics

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `[PB-4] (benchmark) Report scheduler balance metrics`

- Added dynamic chunk size and timed scheduler telemetry to benchmark sample output.
- Reports barrier wait, worker wake and condition-variable wait, tail idle, worker active bounds, and process CPU time.

### Validation

- CMake Debug target `texas_solver_multiway_training_scaling` passed with errors-only output.
- Benchmark `--help` passed and lists `--dynamic-chunk-sizes`.
- No long matrix or qualification run was performed.

## TH1 - Add steady-state matrix analyzer

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commit:** `8af97a9 [PB-4] (benchmark) Add steady-state matrix analyzer`

- Added a bounded parser for the steady-state matrix manifest and stdout logs.
- Validates every configured worker summary, non-zero fingerprint, and chunk-size group.
- Supports legacy logs that omit the chunk-size field by applying the default size 4.

### Files

- `scripts/analyze_multiway_steady_state_matrix.ps1`

### Validation

- Existing `tmp/multiway-steady-state` output passed with 16 summaries and one non-zero fingerprint.

### Limitations

- No new steady-state matrix was run.

## TH3/TH5 - Configurable scheduling and topology-aware pinning

**Status:** Complete
**Completed:** 2026-10-22
**Implementation commits:** `6840cc3`, `4720574`, `0c3b204`, `f291723`

- Added runtime dynamic scheduler chunk sizes with the existing size 4 default.
- Added benchmark and steady-state matrix controls for chunk-size sweeps.
- Versioned the scheduler policy in run metadata and schedule fingerprints.
- Replaced Windows logical-index pinning with physical-core-first processor-group affinity and an unpinned fallback when topology discovery is unavailable.

### Files

- `include/solver/multiway/engine/multiway_scheduler.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_scheduler.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `tests/test_multiway_scheduler.cpp`
- `examples/benchmarks/multiway_training_scaling_main.cpp`
- `scripts/run_multiway_steady_state_matrix.ps1`

### Validation

- CMake Debug targets built successfully.
- `test_multiway_scheduler` passed.
- `test_multiway_parallel_training` passed.
- Tracy RelWithDebInfo training targets built successfully.
- Benchmark `--help` exposed the chunk-size sweep option.

### Limitations

- No long scheduler or pinning matrix was started.
- Runtime placement results still require operator captures for qualification.

## Tooling - Build Tracy targets and analyze existing matrix reports

**Status:** Complete
**Completed:** 2026-10-22

- Added a CMake wrapper that configures Tracy diagnostics when needed and builds
  both training executables expected by the capture scripts.
- Added a bounded analyzer for the eight existing cold/steady matrix reports.
- The analyzer verifies acceptance, fingerprints, checkpoint bytes, failed and
  discarded counts, trajectories, deterministic counts, checkpoint section
  bytes, scheduler equivalence, and allocation-probe provenance.

### Files

- `scripts/build_multiway_tracy.ps1`
- `scripts/analyze_multiway_cold_steady_matrix.ps1`
- `docs/plans/2026-10-18/02_ad7_admission_variance_execution_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Both PowerShell scripts parsed successfully.
- CMake wrapper built both Tracy targets successfully.
- Analyzer passed all eight existing matrix reports.

### Limitations

- Historical matrix reports have `allocation_probe_valid=false`; no new matrix
  run was started.

## AD7 - Candidate A cold qualification

**Status:** Complete
**Completed:** 2026-10-22

- Validated the four existing controlled one-worker cold reports for fixed and
  dynamic schedulers.
- All four passed `acceptance_pass`; coordinator admission was `232.048` to
  `237.390 ms`, identity validation was `54.337` to `54.848 ms`, public-state
  validation was `94.495` to `95.688 ms`, and catalog commit was `22.520` to
  `23.221 ms`.
- Fingerprint `9806774443936985566`, checkpoint bytes `137380835`, phase
  coverage, byte accounting, discarded count, and deterministic counts passed.
- Candidate A is qualified. The full cold/steady worker matrix remains
  deferred because it is an hour-scale run.

### Files

- `scripts/analyze_multiway_cold_qualification.ps1`
- `docs/plans/2026-10-18/02_ad7_admission_variance_execution_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Existing four capture reports and Tracy CSV files were analyzed successfully.

### Limitations

- No new 60-second captures or large worker matrix was started in this turn.

## AD7 - Coordinator copy, allocation telemetry, and Tracy zones

**Status:** Implemented; verification pending
**Completed:** 2026-10-22

- Added coordinator allocation count and byte telemetry to batch results and
  training memory reports.
- Added discovery and catalog output-capacity reuse counters.
- Added public descriptor and nested vector copy counters and byte volumes for
  discovery merge and catalog packing.
- Added accumulation and JSON serialization regression coverage.
- Added scoped Tracy zones for coordinator admission, discovery merge, catalog
  packing, worker discovery, traversal, and checkpoint phases.
- Focused CMake builds completed for all five required targets, and the
  matching Debug CTest expression passed all five tests. The full-build helper
  was inspected but not used because its MSBuild step produced no output
  within the bounded run. A CMake-wide Debug build passed, and all 110 Debug
  CTest tests passed after redirecting `TEMP`/`TMP` to the workspace. The two
  atomic-publish tests report `Access is denied` under the default Windows
  temp directory.

## AD7 - Fast-path and identity verification coverage

**Status:** Implemented; qualification pending
**Completed:** 2026-10-22

- Strengthened one-stream discovery equivalence to compare every public
  descriptor field, nested vector, row field, cursor postcondition, and source
  rewind result.
- Confirmed the existing schema-v2 identity out-parameter and unrolled
  dual-lane encoder remain in the implementation.
- Added public-builder identity and menu Tracy zones in commit `827f129`.
- Focused CMake builds and CTest passed for the five required suites.
- An isolated MSVC RelWithDebInfo assembly compile confirmed the identity
  history encoder has explicit eight-byte steps, no `append_u64` or
  `append_u64_bytes` call in its code range, and no runtime eight-byte loop.
- Four cold qualification captures remain pending.

## TH1-TH2 - Steady-state benchmark and per-worker capacity

**Status:** Implemented; verification pending
**Completed:** 2026-10-22
**Implementation commits:** `2f90a96`, `81f80e0`, `5799d8c`, `e35d302`

- Changed worker delta capacity to allocate the configured amount per worker.
- Added explicit discovery capacity through solver limits, workflow configs, identity, preflight, and reports.
- Added worker allocation count/byte accounting to the memory ledger.
- Added TH1 benchmark controls for fixed/dynamic scheduling and pinned/unpinned groups.
- Added dynamic-skew and allocation-accounting coverage plus a four-group matrix launcher.
- No build, test, benchmark, or capture was run in this session.

### Files

- `examples/benchmarks/multiway_training_scaling_main.cpp`
- `scripts/run_multiway_steady_state_matrix.ps1`
- `configs/multiway/f1_dev_v1.cfg`
- `configs/multiway/f1_sizing_v1.cfg`

## Tooling - Add cold Tracy qualification runner

**Status:** Complete
**Completed:** 2026-10-22

- Added PowerShell wrappers for the four required cold one-worker captures and their telemetry analysis.
- Preserved fixed and dynamic scheduler IDs, 1,000 trajectories, and separate output directories.
- Added optional worker pinning, configurable capture timeouts, per-capture summaries, and aggregate gate reporting.

### Files

- `scripts/run_multiway_cold_qualification.ps1`
- `scripts/analyze_multiway_cold_qualification.ps1`
- `docs/PLURIBUS_LOG.md`

### Validation

- PowerShell parser checks passed.
- Dedicated analysis passed for all four existing captures with `acceptance_pass=true`.
- No build, test, or new capture was run.

### Limitations

- The wrapper does not analyze reports. Run `analyze_multiway_checkpoint_telemetry.ps1` separately for each output directory.

## AD7 - Candidate A one-stream discovery merge

**Status:** Implemented; verification pending
**Completed:** 2026-10-18
**Implementation commit:** `8b890c0 PB-3 Multiway: Add one-stream discovery fast path`
**Test commit:** `9098465 PB-3 Tests: Cover discovery fast path`

- Selected Candidate A from four qualified fixed/dynamic one-worker captures.
- Added prepared one-stream public-state and row copies with post-copy telemetry.
- Preserved generic zero-stream and multi-stream merge behavior and final cursor postconditions.
- Added equivalence, empty-range, cursor, and source-reuse coverage.
- No build or test was run in this session; static diff review and `git diff --check` passed.

## AD7 - Admission variance telemetry split

**Status:** Complete
**Completed:** 2026-10-18
**Implementation commit:** `944b82e PB-3 Multiway: Split admission variance telemetry`
**Test commit:** `0ff2373 PB-3 Tests: Cover admission variance diagnostics`

- Added exclusive discovery-merge and parent-transition diagnostics with transactional failure accounting.
- Preserved aggregate timers, direct-admission behavior, report schema version, and existing validation messages.
- Added focused accumulation, merge, edge-kind, rejection, direct-admission, and JSON emission coverage.
- Corrected the street-transition fixture to admit the complete turn board before transition validation.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static diff review completed with no actionable findings.
- `git diff --check` passed for all changed source and test files.
- No build or test was run, per session rules.

### Qualification

- Four telemetry-only captures passed the deterministic gates and exposed all Phase 1 fields.
- Candidate A qualified: one single-stream batch per capture, zero duplicates, and public-state merge dominated discovery merge time.
- Candidate B was not selected because no parent-transition edge kind dominated the measured cost.
- Post-optimization build, focused tests, and matched captures remain pending.

## AD7 - Admission variance follow-up plan

**Status:** Complete
**Completed:** 2026-10-18

- Recorded capture evidence showing the schema-v2 identity gate is stable while discovery merge and parent-transition work vary.
- Defined additive telemetry and a one-candidate-at-a-time decision path for the next AD7 optimization.

### Files

- `docs/plans/2026-10-18/01_ad7_admission_variance_followup.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed all inline-capture manifests and reports from `a6728c8`.
- No source, build, test, or executable was run while creating the plan.

### Limitations

- The selected implementation candidate depends on the planned exclusive telemetry captures.

## AD7 - State identity validation fixture correction

**Status:** Complete
**Completed:** 2026-10-17
**Implementation commit:** `4d88453 PB-3 Tests: Fix identity validation boundary fixture`

- Normalized the baseline history action index before malformed-history mutations.
- Restored meaningful coverage for the valid upper actor boundary and each isolated malformed field.

### Files

- `tests/test_multiway_public_builder.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the focused diff and `git diff --cached --check` passed.
- No build or test was run, per session rules.

### Limitations

- The corrected test requires operator execution for runtime confirmation.

## AD7 - State identity validation redesign

**Status:** Complete for implementation; operator qualification pending
**Completed:** 2026-10-17
**Implementation commits:** `0b59e99 PB-3 Multiway: Add transactional identity inspection API`, `60e93f4 PB-3 Multiway: Unroll dual-lane identity encoding`

- Added a compatibility-preserving transactional out-parameter identity API for admission.
- Validated every schema-v2 history entry before hashing and replaced the runtime byte loop with an explicitly unrolled scalar dual-lane FNV encoder.
- Preserved independent reference hash paths, admission error ordering, telemetry, and catalog/storage boundaries.

### Files

- `include/solver/multiway/abstraction/multiway_public_builder.hpp`
- `src/solver/multiway/abstraction/multiway_public_builder.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_public_builder.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Checked branch `PB-3` and reviewed focused staged diffs.
- `git diff --cached --check` passed before each implementation commit.
- No build, test, benchmark, executable, or solver script was run, per session rules.

### Limitations

- Operator qualification remains required: focused tests, MSVC disassembly confirming inlining, and matched Tracy captures against the AD7 acceptance gates.

## 2026-10-16 - AD7 state preflight identity validation optimization

**Status:** Complete for implementation; operator qualification pending
**Completed:** 2026-10-16
**Implementation commit:** `23c64c4 PB-3 Multiway: Optimize state identity validation`

- Added an allocation-free schema-v2 identity inspector that validates history
  shape while computing history and public-state hashes in lockstep.
- Replaced the new-state admission verifier's three history traversals with
  the dual-hash inspector while preserving rejection ordering and identity
  bytes.
- Added focused identity-equivalence, malformed-history, and independent ID
  rejection coverage.

### Files

- `include/solver/multiway/abstraction/multiway_public_builder.hpp`
- `src/solver/multiway/abstraction/multiway_public_builder.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_public_builder.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Checked branch `PB-3`.
- Reviewed the focused diff and staged patch.
- `git diff --cached --check` passed.
- No build, test, benchmark, executable, or solver script was run by Codex,
  per session rules.

### Limitations

- Operator must build, run focused tests, and execute the planned Tracy
  qualification captures before accepting the performance gates.

## AD7G - Catalog menu index and cold-admission follow-up

**Status:** Complete for implementation; operator qualification pending
**Completed:** 2026-10-15
**Implementation commits:** `57c2243 PB-3 Multiway: Index canonical catalog menus`, `dd019f7 PB-3 Tests: Cover indexed catalog admission`, `16e507d PB-3 Tests: Verify catalog telemetry reporting`, `1bae7ed PB-3 Multiway: Measure validated menu key preparation`, `5da74f3 PB-3 Tests: Prove catalog menu reuse counters`

- Replaced linear catalog menu lookup with a sorted canonical-ID index and
  full descriptor-vector equality checks for collision safety.
- Added reusable batch menu resolution, first-occurrence handle assignment,
  merged menu/state indexes, direct packed-node append, and pre-mutation
  capacity/reserve preparation.
- Reused validated public-state metadata during row preflight and added
  additive admission and validation-phase telemetry to training reports.
- Added catalog, coordinator, capacity, collision, clear/rebuild, telemetry,
  and report-serialization regression coverage.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_public_catalog.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Checked branch `PB-3`.
- Reviewed focused diffs and commit boundaries.
- No build, test, benchmark, executable, or solver script was run by Codex,
  per session rules.

### Limitations

- Operator must build, run focused tests, and execute the eight Tracy 0.14.1
  qualification captures before AD7G performance thresholds can be accepted.

## 2026-10-15 - Add AD7 catalog optimization planning prompt

**Status:** Complete
**Completed:** 2026-10-15

- Added a handoff prompt for the next AI agent to prepare the follow-up AD7
  catalog menu-lookup optimization plan.
- Recorded the latest performance evidence, correctness invariants, candidate
  solutions, required telemetry, and session constraints.

### Files

- `docs/prompts/ad7_catalog_menu_lookup_followup_planning_prompt.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the new prompt and latest `tmp/tracy/ad7-post-*` reports.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- The follow-up implementation plan must be prepared by the next agent.

## 2026-10-15 - Fix AD7 fixed-state transition compile error

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `b6296ba PB-3 Multiway: Fix fixed-state transition check`

- Replaced the remaining dynamic-only street-transition helper call with the
  equivalent `MultiwayFixedState::next_node_kind()` check.

### Files

- `src/solver/multiway/engine/multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Reviewed the focused diff and checked it with `git diff --check`.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- Operator must rerun the build and focused tests to confirm the compile fix.

## 2026-10-15 - Implement AD7 state preflight and catalog optimization

**Status:** Implementation complete; operator validation pending
**Completed:** 2026-10-15
**Implementation commits:**

- `afbf666 PB-3 Multiway: Optimize prepared admission preflight`
- `e27f204 PB-3 Tests: Preserve admission state on capacity failure`

- Replaced dynamic betting-state reconstruction in public-state and row
  admission validation with the allocation-free fixed-state representation.
- Reused coordinator admission scratch buffers across prepared batches.
- Classified sorted prepared public states with one catalog-index merge while
  preserving compatibility reconstruction and parent-first validation.
- Added additive admission counters and phase timings for classification,
  validation, parent resolution, row resolution, menu lookup, and index merge.
- Added regression assertions that capacity failures leave checkpoint catalog
  and row state unchanged.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Reviewed the focused diffs and checked staged patches with `git diff --check`.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- Operator must build, run focused tests, and repeat the Tracy 0.14.1 cold and
  steady capture matrix to verify performance, fingerprints, checkpoint bytes,
  and the new admission telemetry.

## 2026-10-15 - Fix AD7 admission compile error

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `9156d73 PB-3 Multiway: Fix admission index redeclaration`

- Reset the existing sparse-row merge scan index instead of redeclaring it in
  the same function scope.

### Files

- `src/solver/multiway/engine/multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Reviewed the focused diff and ran `git diff --check`.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- Operator must rerun the requested build and focused test to confirm the
  compiler error is resolved.

## 2026-10-15 - Implement AD7 admission hot-path optimization

**Status:** Implementation complete; operator validation pending
**Completed:** 2026-10-15
**Implementation commits:**

- `3879077 PB-3 Multiway: Add admission phase telemetry`
- `8aa38cb PB-3 Multiway: Reuse prepared parent indexes`
- `a6c1fd1 PB-3 Multiway: Batch public catalog admission`
- `eb97d7a PB-3 Multiway: Batch sparse-row admission`
- `d8ce8a5 PB-3 Tests: Cover batch admission storage`
- `840f246 PB-3 Multiway: Guard batch admission duplicates`
- `2b327bf PB-3 Multiway: Reuse row candidate indexes`

- Added admission phase telemetry for discovery preparation, merge, parent
  ordering, preflight, catalog commit, and storage commit.
- Replaced per-entry public-catalog index insertion with one sorted batch
  merge while preserving single-state admission.
- Added float and compact sparse-row batch commits with capacity preflight,
  stable offsets, sorted metadata, duplicate rejection, and compact pages.
- Reused prepared parent and row candidate indexes during admission.
- Added focused catalog, float-row, compact-row, and ordering regression
  coverage.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Reviewed the staged diffs and ran `git diff --check`.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- Operator must build, run focused tests, and repeat the RelWithDebInfo
  Tracy matrix to verify the below-250-ms cold admission target and output
  equivalence.

## 2026-10-15 - Plan AD7 admission hot-path optimization

**Status:** Plan complete; implementation pending
**Completed:** 2026-10-15

- Analyzed the completed fixed/dynamic RelWithDebInfo Tracy captures for
  AD5-AD6.
- Confirmed deterministic and checkpoint equivalence across all eight runs,
  with zero failures and zero discarded trajectories.
- Identified the remaining gate: cold coordinator admission measured
  1.338-1.487 seconds versus the required 250 ms maximum.
- Planned batched public-catalog and sparse-row commits, candidate-index
  reuse, phase telemetry, regression coverage, and operator qualification.

### Files

- `docs/plans/2026-10-15/10_ad7_admission_hot_path_optimization.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Inspected all eight AD5-AD6 Tracy manifests and reports.
- Reviewed the public-catalog, sparse-storage, prepared-admission, and
  parent-order implementation paths.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- The AD7 performance target remains unverified until implementation and
  operator-run qualification captures are complete.

## 2026-10-15 - Implement AD5-AD6 bulk admission and epoch filtering

**Status:** Implementation complete; operator validation pending
**Completed:** 2026-10-15
**Implementation commits:**

- `ac12103 PB-3 Multiway: Add bulk epoch admission`
- `4028346 PB-3 Tests: Cover bulk epoch admission`

- Added a prepared discovery-batch admission API with one coordinator lock.
- Preflighted public-state, row, compact-page, and value capacities before
  mutation.
- Preserved descriptor, parent-transition, row-shape, and duplicate-conflict
  validation while skipping epoch-known states and rows.
- Added lock-free immutable-epoch public-state membership queries and routed
  parent resolution through them.
- Added focused tests for one-lock admission, epoch filtering, and rejection
  without partial mutation.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Focused diff reviewed.
- Static symbol and path checks completed.
- No build, test, benchmark, executable, or solver script was run by Codex.
- Operator must run rejection, equivalence, capacity, and fixed/dynamic worker
  validation, then provide admission timing and checkpoint results.

### Limitations

- Runtime acceptance, checkpoint equivalence, and the below-250-ms cold-batch
  target remain unverified until operator validation.

## 2026-10-15 - Implement AD2-AD4 discovery preparation and parent-first merge

**Status:** Implementation complete; operator validation pending
**Completed:** 2026-10-15
**Implementation commits:**

- `beb507f PB-3 Multiway: Implement AD2-AD4 discovery merge`
- `3ceee18 PB-3 Multiway: Avoid redundant admission reconstruction`
- `b03caa2 PB-3 Tests: Cover shuffled discovery candidates`
- `8bfb2dd PB-3 Tests: Cover discovery merge and parent graphs`
- `d169820 PB-3 Tests: Stabilize discovery graph fixtures`

- Added worker-local discovery preparation that sorts deterministically,
  deduplicates candidates in place, and rejects conflicting public-state or
  row descriptors.
- Replaced coordinator discovery concatenation and global sorting with direct
  deterministic k-way merging of prepared worker streams.
- Added binary-search parent resolution with memoized candidate depths,
  cycle/unresolved-parent rejection before mutation, and `(depth, state_id)`
  admission ordering.
- Reused coordinator merge/order buffers and removed repeated parent descriptor
  reconstruction from the admission path.
- Added focused discovery-stream tests for shuffled input, compatible duplicate
  collapse, cross-worker conflicts, valid parent ordering, cycles, and
  unresolved parents.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Branch checked: `PB-3`.
- Focused source diff reviewed.
- `git diff --check` passed.
- No build, test, benchmark, executable, or solver script was run by Codex.
- Operator must run the correctness suite and identical 1-worker/4-worker
  admission measurements, then provide fingerprints, admitted-set comparisons,
  and timing results.

### Limitations

- Runtime acceptance and the four-worker admission slowdown threshold remain
  unverified until operator validation.

## 2026-10-15 - Accept formatted bucket checkpoint setup

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `721baa3 PB-3 Tests: Accept formatted checkpoint setup`

- Made the bucket workflow boundary check validate the checkpoint guard,
  directory creation call, and writer setup order independently of C++ line
  formatting.
- Preserved the requirement that the checkpoint directory is created before
  `MultiwayBucketArtifactWriter` setup.

### Files

- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused source diff reviewed.
- `git diff --check` passed.
- No build or test was run by Codex.
- Operator reported build and boundary test passed.

### Limitations

- None identified.

## 2026-10-15 - Fix AD1 report validation compile error

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `0959bd4 PB-3 Multiway: Fix admission report validation`

- Corrected `MultiwayTrainingReport::validate()` to access its own
  `admission_diagnostics` member instead of an undeclared local `report`.

### Files

- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Focused source diff reviewed.
- `git diff --check` passed.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- None identified.

## 2026-10-15 - Apply project C++ formatting

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `99157c7 PB-3 Format: Apply project C++ formatting`

- Applied the repository formatter to the tracked C++ and multiway test files.
- No runtime behavior or solver logic was intentionally changed.

### Files

- `examples/multiway_workflow_main.cpp`
- `include/util/tracy.hpp`
- `src/core/atomic_publish.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/engine/multiway_allocation_probe.cpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/engine/multiway_scheduler.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_phase4.cpp`
- `tests/test_multiway_public_catalog.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --cached --check` passed before commit.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- None identified.

## 2026-10-15 - AD1 coordinator admission diagnostics

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `ddf7827 PB-3 Multiway: Add admission diagnostics`

- Added cold-path admission counters for raw, worker-local unique, globally
  unique, already-admitted, and newly admitted state and row candidates.
- Added parent-resolution pass/probe counts, descriptor reconstruction counts,
  lock acquisitions, catalog index insertion time, and row admission time.
- Propagated global and per-worker candidate diagnostics into training report
  JSON without changing strategy identity or merge behavior.

### Files

- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Source-only C++ review completed with no actionable findings.
- `git diff --cached --check` passed before commit.
- No build, test, benchmark, executable, or solver script was run by Codex.
- Operator build and test passed.
- Operator completed matching Tracy 0.14.1 one-worker and four-worker captures
  with one batch and 1,000 trajectories using the fixed scheduler.
- Both reports had `failed=false`, 1,000 accepted trajectories, and merge
  fingerprint `9806774443936985566`.
- Both reports reconciled to 85,996 raw public-state candidates, 47,173 raw
  row candidates, 69,194 globally unique public states, and 31,070 globally
  unique rows; already-admitted plus newly-admitted counts matched those
  totals.
- Parent-resolution, reconstruction, lock, and admission timing diagnostics
  were present and nonzero. Per-worker arrays matched their worker counts.
- Semantic model identity components matched; the runtime identity differed
  only because the configured worker count is part of that runtime identity.

### Limitations

- None identified.

## 2026-10-15 - Correct checkpoint payload hash ordering

**Status:** Complete
**Completed:** 2026-10-15
**Implementation commit:** `ba3b7c0 PB-3 Multiway: Preserve checkpoint payload hash order`

- Corrected the incremental checkpoint writer hash sequence to match the
  existing `payload_hash()` contract.
- Preserved the schema-2 serialized field order and bytes by deferring hashing
  of the street-visit arrays until their historical hash order is reached.

### Files

- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --cached --check` passed before commit.
- Operator reported the targeted round-trip and tampering test passed.
- Operator reported `acceptance_pass: true` for
  `tmp/tracy/cp3-1w-1x1000-300s-rerun/report.json` after matching the
  one-batch workload and expected deterministic merge fingerprint.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- None identified.

## 2026-10-15 - Implement CP3-CP5 checkpoint write path

**Status:** Complete
**Completed:** 2026-10-15

- Added a sealed checkpoint writer for the trusted training workflow while
  retaining validation in the public `save_atomic()` API.
- Replaced per-byte file output with a 64 KiB buffered stream and computed the
  payload hash during serialization, including block-wise double-array output.
- Added checked Windows replacement publication, restricted copy fallback to
  destinations that do not exist, and reported the publication mode in training
  telemetry.
- Preserved the schema-2 serialization contract in source; runtime byte and
  resume compatibility still require operator verification.

### Files

- `include/core/atomic_publish.hpp`
- `src/core/atomic_publish.cpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_artifact.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the changed C++ with the TexasSolver C++ review rules.
- `git diff --check` passed for the staged changes.
- Commits: `9b32e42`, `b533176`, `15b64ac`.
- Operator reported checkpoint save/load and tampering validation passed.
- Operator reported CP3-CP5 Tracy acceptance passed with complete phase
  coverage, byte accounting, zero discarded trajectories, and the expected
  deterministic merge fingerprint.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- None identified.

## 2026-10-15 - Implement CP2 linear compact checkpoint export

**Status:** Implemented, operator validation pending
**Completed:** 2026-10-15

- Replaced per-cell compact checkpoint page lookup with a sequential page walk.
- Precomputed row offsets and the total logical checkpoint cell count once.
- Allocated and zero-initialized the final regret and strategy arrays before
  copying active page ranges into their action-major destinations.
- Kept compact row and page indexes in public-state-major, then seat-major
  checkpoint order so the coordinator no longer creates a second compact
  canonical storage copy.
- Preserved the Float64 reference export and canonicalization path unchanged.
- Added compact action-major sparse-page coverage and compact-versus-Float64
  checkpoint equivalence coverage, including multiple canonical rows.

### Files

- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the changed C++ diff with the TexasSolver C++ review rules.
- `git diff --check --ignore-submodules=all` passed for the changed files.
- Committed as `bd4cceb` (`PB-3 Multiway: Linearize compact checkpoint export`).
- Added the multirow equivalence case in commit `e0c9e37`.
- No build, test, benchmark, executable, or solver script was run by Codex.

### Limitations

- Operator build and compact/Float64 round-trip tests remain required.
- The large sparse-page measurement and CP1 timing comparison remain pending.

## 2026-10-15 - Implement CP1 checkpoint phase telemetry

**Status:** Implemented and operator-validated
**Completed:** 2026-10-15

- Added independent Tracy zones and report telemetry for checkpoint snapshot
  locking, catalog export, compact storage export, canonicalization, both
  validations, payload hashing, binary encoding, stream close/flush, and atomic
  publication.
- Added serialized byte counts for public states, row shapes, regrets, strategy
  sums, and the late-window baseline without changing checkpoint payload bytes or
  payload hashing.
- Kept payload-hash timing separate from binary encoding timing so phase totals
  do not double-count the nested hash operation.
- Bumped the training report schema to 7 and updated the worker-matrix schema
  gate.
- Propagated the nested checkpoint telemetry object into the QA wrapper's
  `run_metrics.json` artifact.
- Added a non-executing report analyzer for schema, phase coverage, byte
  accounting, discarded trajectories, and merge-fingerprint validation.
- Extended the analyzer to require every CP1 Tracy zone in the exported CSV.
- Preserved exact decimal comparison for the 64-bit deterministic merge
  fingerprint in Windows PowerShell.
- Added a checkpoint round-trip assertion that runtime telemetry is not
  serialized into the checkpoint artifact.
- Corrected PowerShell analyzer comparisons to use PowerShell numeric literal
  syntax instead of C++ unsigned suffixes.

### Files

- `docs/plans/2026-10-15/01_cp1_checkpoint_telemetry.md`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `include/solver/multiway/workflow/multiway_evidence.hpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `examples/multiway_workflow_main.cpp`
- `scripts/run_multiway_ram_1000.ps1`
- `scripts/analyze_multiway_checkpoint_telemetry.ps1`
- `scripts/run_multiway_worker_matrix.ps1`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the changed C++ diff with the TexasSolver C++ review rules.
- `git diff --check` passed for tracked changes.
- Operator capture `tmp/tracy/cp1-1w-1x1000` produced a schema-7 report and
  Tracy 0.14.1 manifest for the current CP1 instrumentation.
- The CP1 analyzer passed: all CSV zones present, phase total
  `161722238900 ns` versus checkpoint total `161727571400 ns` (`99.9967%`),
  payload sections `137380459` bytes versus checkpoint size `137380835` bytes,
  zero discarded trajectories, and matching merge fingerprint
  `9806774443936985566`.
- No build, test, benchmark, executable, or solver script was run by Codex;
  the operator supplied the runtime capture and analyzer result.

### Limitations

- The evidence is a bounded one-worker, one-batch, 1,000-trajectory capture;
  it validates CP1 instrumentation and acceptance gates, not throughput scaling.

## 2026-10-15 - Split Tracy scaling remediation into operator-ready tasks

**Status:** Complete
**Completed:** 2026-10-15

- Split the Tracy scaling remediation plan into nine independently scoped tasks.
- Marked source-only work that Codex can prepare and runtime validation that an
  operator must execute because it requires builds, tests, captures, or solver
  workflows.
- Preserved the remediation dependencies, determinism gates, and performance
  acceptance criteria.

### Files

- `docs/plans/2026-10-15/01_cp1_checkpoint_telemetry.md`
- `docs/plans/2026-10-15/02_cp2_linear_compact_snapshot_export.md`
- `docs/plans/2026-10-15/03_cp3_cp5_checkpoint_write_path.md`
- `docs/plans/2026-10-15/04_ad1_admission_diagnostics.md`
- `docs/plans/2026-10-15/05_ad2_ad4_discovery_merge.md`
- `docs/plans/2026-10-15/06_ad5_ad6_bulk_admission_epoch_filtering.md`
- `docs/plans/2026-10-15/07_th1_th2_steady_state_capacity.md`
- `docs/plans/2026-10-15/08_th3_dynamic_scheduler_qualification.md`
- `docs/plans/2026-10-15/09_th4_th5_traversal_pinning.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the generated Markdown structure and task dependencies.
- No build, test, benchmark, executable, or solver script was run.

### Limitations

- The documents are execution plans. Runtime gates remain unverified until an
  operator runs the required workflows.

## 2026-10-14 - Document application and script I/O contracts

**Status:** Complete
**Completed:** 2026-10-14

- Documented C++ executable targets, generated test executables, Python utilities,
  PowerShell measurement workflows, Tracy tools, command-line inputs, stream
  outputs, generated files, and the multiway artifact pipeline.
- Added explicit artifact and stage relationships for bucket generation,
  inspection, training, lookup qualification, finalization, and Tracy capture.

### Files

- `docs/application_and_script_io.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Cross-checked target names and build gates against `CMakeLists.txt`.
- Cross-checked CLI options and artifact paths against executable entry points
  and scripts.
- Ran targeted `git diff --check` on the documentation log successfully and
  checked the new document for trailing whitespace.
- No build, test, benchmark, script, or solver run was performed by request.

### Limitations

- The document is source-derived. Runtime-only behavior and future schema changes
  are not independently exercised here.

## 2026-10-14 - Align Tracy integration with 0.14.1

**Status:** Complete
**Completed:** 2026-10-14

- Updated Tracy provenance, capture-tool version checks, manifests, and report
  fixtures from 0.14.2 to the vendored 0.14.1 release.
- Updated current Tracy operator and integration documentation while retaining
  historical 0.14.2 capture records.

### Files

- `include/util/tracy.hpp`
- `tests/test_multiway_qualification_reports.cpp`
- `scripts/run_multiway_tracy_capture.ps1`
- `scripts/run_multiway_tracy_training_capture.ps1`
- `docs/tracy_multiway_blueprint_training.md`
- `docs/tracy_multiway_blueprint_training_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Confirmed `external/tracy/public/common/TracyVersion.hpp` reports 0.14.1.
- Ran `git diff --check` on the changed files successfully.
- No build, test, benchmark, script, or solver run was performed by request.

### Limitations

- The Tracy 0.14.1 compatibility changes were not compiled or executed.

## 2026-10-14 - Plan Tracy multiway scaling remediation

**Status:** Complete
**Completed:** 2026-10-14

- Analyzed checkpoint construction and serialization, deterministic discovery
  admission, worker scheduling, and capacity contracts against the Tracy
  scaling QA evidence.
- Defined scoped implementation, validation, and performance gates for faster
  checkpoint output, bulk coordinator admission, and multi-worker scaling.

### Files

- `docs/plans/tracy_multiway_scaling_remediation_plan_2026-10-14.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Cross-checked the plan against the captured JSON metrics and the responsible
  checkpoint, public-catalog, admission, scheduling, and memory-preflight code.
- Reviewed the documentation diff.
- No build, test, benchmark, or solver run was performed because this was an
  analysis and planning task.

### Limitations

- Performance targets require future implementation and measurement.

## 2026-10-14 - Document Tracy multiway scaling QA findings

**Status:** Complete
**Completed:** 2026-10-14

- Added a developer-facing QA report comparing the one-worker and four-worker
  real-workflow Tracy captures.
- Documented the weak 12.3% batch speedup, coordinator admission regression,
  worker imbalance, and 198-240 second checkpoint-write cost.
- Separated batch timing evidence from lifetime worker waits and end-to-end
  artifact output timing.

### Files

- `docs/reports/tracy_multiway_training_scaling_qa_2026-10-14.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Compared both Tracy CSV exports with their matching `report.json` files.
- Cross-checked accepted trajectories, failure status, and deterministic merge
  fingerprints for both worker counts.
- No build, test, benchmark, or solver run was required for this documentation-only task.

### Limitations

- Source captures contain one batch and one sample per worker count.
- Checkpoint sub-operation timing is not yet instrumented.

## 2026-10-14 - Add complete CLI Tracy training capture workflow

**Status:** Complete
**Completed:** 2026-10-14

- Added a no-GUI wrapper that starts `tracy-capture`, runs the real multiway
  training workflow, preserves trainer JSON and logs, exports Tracy phase CSV,
  writes a wrapper lifecycle log, and writes a manifest containing workload,
  tool hashes, paths, and trace metadata.
- Fixed the Windows line-ending match and idempotent-value handling in the
  existing bounded training wrapper so its run-local `trajectories_per_batch`
  override works on CRLF configs and when the requested value is already 1000.
- Fixed the `_tracy` workflow executable dispatch so it enters the training
  path instead of falling through to evaluation.
- Documented the bounded one-worker invocation and artifact layout.

### Files

- `scripts/run_multiway_tracy_training_capture.ps1`
- `scripts/run_multiway_ram_1000.ps1`
- `examples/multiway_workflow_main.cpp`
- `docs/tracy_multiway_blueprint_training.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- PowerShell parser validation passed.
- Rebuilt `texas_multiway_train_tracy` successfully.
- `texas_multiway_train_tracy.exe --help` now reports the train workflow.

### Limitations

- A new training capture was not launched automatically; the wrapper is ready
  for the operator-selected workload and output directory.

## 2026-10-14 - Implement Tracy diagnostics for multiway blueprint training

**Status:** Complete
**Completed:** 2026-10-14
**Implementation commits:** `4628ebd`, `f29bc5d`, `9cac476`, `700dcc4`

- Vendored Tracy 0.14.2 under `external/tracy` and isolated it behind the
  diagnostic-only `texas_tracy` target and `_tracy` executables.
- Added compile-time no-op wrappers, multiway worker/coordinator/trainer phase
  zones, batch frames, plots, and schema 6 provenance fields.
- Added the bounded capture wrapper, matching Tracy capture/profiler/CSV tools,
  operator documentation, and the normal-target dependency boundary test.

### Files

- `CMakeLists.txt`
- `include/util/tracy.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `docs/tracy_multiway_blueprint_training.md`
- `docs/tracy_multiway_blueprint_training_plan.md`
- `scripts/run_multiway_tracy_capture.ps1`
- `tests/cmake/test_tracy_boundary.cmake`

### Validation

- Normal Debug target and Tracy RelWithDebInfo diagnostic targets built.
- Full CTest passed: 110/110 tests.
- Focused qualification and Tracy boundary tests passed: 2/2.
- Bounded loopback capture produced a non-empty Tracy 0.14.2 trace and a
  provenance manifest. Matching CSV export completed successfully.

### Limitations

- The Tracy profiler GUI remains manual and was not opened or connected by the
  agent. `_tracy` timings are diagnostic only and are not throughput
  qualification data.

## 2026-10-14 - Plan Tracy diagnostics for multiway training scaling

**Status:** Complete
**Completed:** 2026-10-14

- Verified the current multiway build, profiling, worker, trainer, report, and measurement boundaries.
- Documented proven production-like scaling regressions and stale evidence limits.
- Defined an offline, target-isolated Tracy integration with exact zones, controls, validation, capture protocol, and commit-sized tickets.

### Files

- `docs/tracy_multiway_blueprint_training_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Source, CMake, scripts, retained JSON evidence, and upstream Tracy 0.14.1 documentation were reviewed.
- Builds, tests, benchmarks, installs, and solver jobs were not run because the task explicitly requested a plan only.

### Limitations

- The retained production-like reports use older schemas and unknown binary provenance. A fresh unprofiled baseline and Tracy captures are still required to attribute the scaling loss.

## 2026-10-13 - Record multiway worker-matrix telemetry

**Status:** Complete
**Completed:** 2026-10-13

- Recorded the 40-run unpinned worker and scheduler matrix results.
- Documented throughput, correctness gates, memory usage, and scaling observations.

### Files

- `docs/multiway_worker_matrix_telemetry_report.md`

### Validation

- User-run worker matrix reported `PASS`: 40 runs completed with zero discards, bounded RSS, and one merge fingerprint.

### Limitations

- The 160-trajectory pilot is insufficient for a stable scaling conclusion.

## 2026-10-13 - Repair multiway worker-matrix sampling and failure telemetry

**Status:** Complete
**Completed:** 2026-10-13
**Implementation commits:** `414c95c`, `9e6b8fe`, `316fb20`, `a789e51`

- Replaced one-shot private-card proposals with deterministic blocker-conditioned sampling and conditional proposal probabilities.
- Preserved the zero-discard worker-matrix gate and added failed-batch telemetry without committing failed training progress.
- Added CSV worker-list parsing and documented the failure analysis.

### Files

- `include/games/multiway_private.hpp`
- `src/games/multiway_private.cpp`
- `src/solver/multiway/abstraction/multiway_terminal_adapter.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_evidence.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `scripts/run_multiway_ram_1000.ps1`
- `scripts/run_multiway_worker_matrix.ps1`
- `tests/test_multiway_private.cpp`
- `tests/test_multiway_cfr.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/multiway_worker_matrix_failure_analysis.md`

### Validation

- Not run. Builds, tests, benchmarks, and executables were explicitly excluded.

### Limitations

- Runtime verification remains required before using the worker matrix for measurements.

## 2026-10-13 - Add multiway batch throughput telemetry

**Status:** Complete
**Completed:** 2026-10-13
**Implementation commit:** `7adf47c pb-1 Multiway: Add batch telemetry`

- Added batch-boundary worker wake, completion, tail-idle, sorting, delta, discovery, and per-worker profile telemetry.
- Propagated telemetry through blueprint training status and JSON reports without changing traversal, scheduling, merge order, or deterministic identity.

### Files

- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`

### Validation

- Source diff review completed.
- Builds, tests, benchmarks, and scripts were not run per user instruction.

### Limitations

- Allocation probing, benchmark CLI extensions, and baseline matrix execution remain deferred.

## 2026-10-13 - Implement deterministic fixed and dynamic worker scheduling

**Status:** Complete
**Completed:** 2026-10-13
**Implementation commit:** `9f8f7a5 pb-1 Multiway: Add deterministic dynamic worker scheduling`

- Added fixed and four-trajectory dynamic worker scheduling with deterministic
  per-trajectory seeds, sorted worker streams, and existing k-way merges.
- Extended workflow and bounded matrix scripts with scheduler selection,
  run-local batch sizing, process CPU telemetry, correctness gates, and repeat
  throughput statistics.
- Added chunk coverage and fixed-versus-dynamic equivalence contracts for 1,
  4, and 8 workers.

### Files

- `include/solver/multiway/engine/multiway_scheduler.hpp`
- `src/solver/multiway/engine/multiway_scheduler.cpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `examples/multiway_workflow_main.cpp`
- `scripts/run_multiway_ram_1000.ps1`
- `scripts/run_multiway_worker_matrix.ps1`
- `tests/test_multiway_scheduler.cpp`
- `tests/test_multiway_recursive_traversal.cpp`
- `tests/test_multiway_parallel_training.cpp`

### Validation

- PowerShell parser: pass.
- Git staged diff checks: pass.
- Multiway traversal and workflow boundary checks: pass.
- Builds, tests, and the worker matrix were not run per user instruction.

### Limitations

- Runtime equivalence and scaling results remain for the human-bounded matrix
  run specified by the plan.

## 2026-10-12 - Add worker timing and scaling matrix telemetry

**Status:** Implemented; runtime validation pending rebuild
**Completed:** 2026-10-12

- Added minimum and maximum worker active-time telemetry.
- Added per-worker trajectory counts and active-time vectors to the training
  report.
- Added derived barrier-wait telemetry separate from coordinator wait.
- Added a PowerShell matrix runner for workers 1, 2, 4, 8, and 16 with JSON
  and CSV summaries.

### Files

- `include/solver/multiway/engine/multiway_traversal.hpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `scripts/run_multiway_ram_1000.ps1`
- `scripts/run_multiway_worker_matrix.ps1`
- `docs/multiway_memory_scaling_execution_plan.md`

### Validation

- Static diff review completed.
- Build, CTest, and the worker matrix were not run because the user asked not
  to build or rerun during this change.

### Limitations

- The existing executable will not emit the new fields until rebuilt.

## 2026-10-12 - Add pinned versus unpinned worker comparison

**Status:** Implemented; runtime validation pending rebuild
**Completed:** 2026-10-12

- Added `--pin-workers true|false` to the production training executable.
- Kept unpinned execution as the default and excluded pinning from strategy
  identity.
- Extended the existing worker matrix runner with `-Pinning both` and larger
  batch support.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `scripts/run_multiway_ram_1000.ps1`
- `scripts/run_multiway_worker_matrix.ps1`
- `docs/multiway_memory_scaling_execution_plan.md`

### Validation

- Static C++ diff review and PowerShell parser validation passed.
- Build and runtime matrix execution were not run because the active task
  forbids building.

## 2026-10-12 - Add bounded batch-count override to RAM runner

**Status:** Complete
**Completed:** 2026-10-12

- Added `-Batches` to the RAM runner for the next bounded scaling pilot.
- Capped the option at 10 batches, preserving a maximum 10,000-trajectory
  run until its memory result is reviewed.
- Derived the expected trajectory count and default output directory from the
  selected batch count.

### Files

- `scripts/run_multiway_ram_1000.ps1`
- `docs/PLURIBUS_LOG.md`

### Validation

- PowerShell syntax and diff checks passed.
- No training run was started for this option change.

### Limitations

- The runner remains intentionally capped at 10 batches.

## 2026-10-12 - Validate 1,000-trajectory RAM pilot

**Status:** Complete
**Completed:** 2026-10-12

- Completed the bounded one-batch pilot with 1,000 accepted trajectories and
  zero discarded trajectories.
- Recorded internal peak RSS of 19,756,826,624 bytes and external sampled
  working-set peak of 20,650,893,312 bytes.
- Recorded a memory preflight estimate of 56,088,838,330 bytes, 22,995 public
  states, 10,902 sparse rows, and 450,261 sparse values.

### Files

- `scripts/run_multiway_ram_1000.ps1`
- `docs/PLURIBUS_LOG.md`

### Validation

- User-provided runner output passed the configuration, execution, and
  1,000-trajectory report gates.
- The process exit code remained unavailable, so the validated training report
  gate was used as the success criterion.

### Limitations

- This is bounded sizing evidence only. It does not qualify the 50-million
  trajectory F1 run.

## 2026-10-12 - Fix RAM runner exit-code reporting

**Status:** Complete
**Completed:** 2026-10-12

- Refreshed the PowerShell process object before reading `ExitCode`, avoiding a
  false failure after a successful training report.
- Added a report-gate fallback when PowerShell cannot expose the completed
  child-process exit code.
- The reported run completed 1,000 accepted trajectories with zero discarded
  trajectories and recorded 19,754,254,336 bytes peak RSS.

### Files

- `scripts/run_multiway_ram_1000.ps1`
- `docs/PLURIBUS_LOG.md`

### Validation

- User-provided run output confirmed the training report passed its bounded
  pilot metrics.
- The runner was not rerun after this follow-up fix.

### Limitations

- Exit-code fallback handling was not runtime-verified after the change.

## 2026-10-12 - Add bounded 1,000-trajectory RAM runner

**Status:** Complete
**Completed:** 2026-10-12

- Added a PowerShell runner for the bounded one-batch, 1,000-trajectory RAM
  measurement using the expanded pilot configuration.
- Reports internal RSS fields and samples the external Windows working set.
- Logs validated inputs, effective run settings, process progress, report
  metrics, output paths, and the final bounded-pilot gate.
- Uses fresh output and checkpoint paths and preserves the eight-worker
  reference measurement settings.

### Files

- `scripts/run_multiway_ram_1000.ps1`
- `docs/PLURIBUS_LOG.md`

### Validation

- Script contents and repository diff inspected.
- The runner was not executed during this documentation/tooling change.

### Limitations

- Requires a successful Debug build, CTest run, bucket artifact, and expanded
  pilot configuration before execution.

## 2026-10-11 - Multiway deterministic scaling smoke capacity

**Status:** Complete
**Completed:** 2026-10-11
**Implementation commit:** `0949b61 fix(multiway): stabilize scaling smoke capacity`

- Fixed the benchmark fixture's under-sized per-worker delta reserve, which
  discarded a trajectory only under the one-worker partition.
- Added immutable compact worker-partition regression coverage and retained
  fingerprint diagnostics with expected and actual values.
- Kept throughput monotonicity validation for production-sized batches; tiny
  smoke batches skip that noisy timing gate but still require matching merges.

### Files

- `examples/benchmarks/multiway_training_scaling_main.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `docs/multiway_memory_scaling_completion_report.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Complete Debug `ALL_BUILD` succeeded.
- Build scan reported no failures.
- CTest passed all 109 tests; the CTest scan reported no failures.
- Workers 1 and 2 smoke passed with seed 119, batch size 8, and matching
  fingerprint `10576998949177294300`.

### Limitations

- The large production-shaped trajectory and 1/2/4/8/16-worker RSS matrix
  remain intentionally unrun.

## 2026-10-11 - Multiway memory scaling recovery implementation

**Status:** Complete
**Completed:** 2026-10-11

- Implemented the M1-M6 recovery slices: memory ledger, mapped-artifact
  accounting, paged compact storage, packed public catalog, immutable batch
  epochs, and direct compact export.
- Added focused unit coverage for page occupancy, packed-state reconstruction,
  mapped bucket memory, epoch traversal, memory preflight, checkpoint
  equivalence, and report serialization.
- Added the execution checklist and detailed RAM-cost completion report. The
  prohibited 10,000+ trajectory qualification remains intentionally unrun.

### Files

- `docs/multiway_memory_scaling_execution_plan.md`
- `docs/multiway_memory_scaling_completion_report.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Complete Debug build succeeded and the build scanner reported no failures.
- Scripted CTest passed all 109 registered tests.
- CTest scanner reported no failures.

### Limitations

- New production RSS measurements and the full 1/2/4/8/16-worker large-run
  matrix were not executed because the request explicitly prohibited the large
  trajectory qualification.

## 2026-10-11 - Multiway F1 memory and scaling recovery plan

**Status:** Complete
**Completed:** 2026-10-11

- Reviewed the measured F1 sizing failures against the current multiway
  training, compact-storage, memory-admission, checkpoint, and traversal paths.
- Recorded five actionable memory/performance findings and a gated recovery
  sequence covering memory telemetry, mapped buckets, paged CFR storage, packed
  public states, immutable batch epochs, streaming artifacts, and production
  qualification.
- Kept the 50-million-trajectory F1 run blocked until the bounded memory,
  determinism, checkpoint, and worker-scaling gates pass.

### Files

- `docs/multiway_memory_scaling_recovery_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the documentation patch and Markdown whitespace with
  `git diff --check`.
- No build, test, benchmark, or solver run was performed because this task
  changed documentation only and the code-review workflow forbids those runs
  unless explicitly requested.

### Limitations

- The plan uses the existing bounded pilot and Release fixture evidence. Its
  proposed memory savings and production scaling gates remain to be measured
  during implementation.

## 2026-10-11 - Multiway F1 sizing pilot findings

**Status:** Complete
**Completed:** 2026-10-11

- Documented bucket identity verification and the failed low-capacity sizing
  runs.
- Recorded successful 1,000- and 10,000-trajectory pilots, including memory,
  state, row, value, throughput, and checkpoint measurements.
- Established that the current shared traversal storage must be bounded or
  redesigned before F1 qualification.

### Files

- `docs/multiway_sizing_pilot_findings.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Documentation reviewed against the supplied inspection and training reports.
- No build, tests, or solver executable was run while preparing the report.

### Limitations

- The sizing conclusion is based on bounded pilots and an explicitly labeled
  linear projection; no 50-million-trajectory qualification is claimed.

## 2026-10-05 - Add C++ formatting script

**Status:** Complete
**Completed:** 2026-10-05

- Added `scripts/format_cpp.py` with file, recursive folder, and project modes.
- Formats `.h`, `.hpp`, and `.cpp` files through `clang-format`; project mode
  skips external, generated, and dependency directories.

### Files

- `scripts/format_cpp.py`
- `docs/PLURIBUS_LOG.md`

### Validation

- Python bytecode compilation and CLI help passed.
- Fixture checks passed for file, folder, and project file selection, plus the
  missing-formatter error path.

### Limitations

- `clang-format` is not installed on this host, so actual formatting was not
  verified.

## 2026-10-05 - Bucket inspection progress for all hash passes

**Status:** Complete
**Completed:** 2026-10-05

- Added phase-labeled progress output for parallel and legacy bucket hash passes.
- The serial fallback and second legacy pass now emit progress callbacks,
  including a phase reset marker before legacy scanning begins.
- Added a regression test for progress reporting on the serial fallback path.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- No build, tests, or solver executable was run, per user instruction.

### Limitations

- Progress changes take effect after rebuilding the inspector executable.

## 2026-10-05 - Configurable F1 workflow bucket counts

**Status:** Complete
**Completed:** 2026-10-05

- Removed the fixed 96/128/192 bucket-count restriction from F1 workflow
  validation. Positive bucket counts remain validated by the model config and
  remain part of workflow and model identities.
- Updated F1 config fixtures and added coverage for both 12/12/12 and
  96/128/192, including rejection of a zero bucket count.

### Files

- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_sizing_report.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- No build, tests, or solver executable was run, per user instruction.

### Limitations

- The existing bucket artifact was not reinspected after this source change.

## 2026-10-05 - Current multiway next-actions document

**Status:** Complete
**Completed:** 2026-10-05

- Compared the current C++ training and artifact paths with the scaling audit,
  project state, technical report, local artifact evidence, and F1 configs.
- Recorded the next sequence: reconcile bucket identity, size and freeze F1,
  qualify training and lookup, then measure production scaling and strength.

### Files

- `docs/multiway_next_actions.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the document against current source, configs, and local artifact
  inspection; `git diff --check` passed. No build or solver run was needed for
  this documentation task.

### Limitations

- The existing bucket artifact's provenance and compatibility with F1 remain
  unverified; no production training or acceptance is claimed.

## 2026-09-01 - Deep multiway scaling fixture verification

**Status:** Verified implementation step

- Configured the scaling benchmark for a three-player flop root, depth 3,
  turn and river transitions, 1024 buckets on every postflop street, and
  Blueprint continuation selection.
- Added complete canonical turn/river bucket registry coverage and equalized
  per-worker delta capacity so trajectories are not discarded at 16 workers.
- Preserved legal action-kind coverage when sizing templates clip to the
  all-in boundary and accepted equivalent chance-transition transpositions.
- Release benchmark: 7,694.7, 9,471.5, 18,365.5, 30,880.6, and 38,054.5
  trajectories/s for 1, 2, 4, 8, and 16 workers, with identical fingerprints.
- Full Debug build and all 108 tests passed.

## Post-activation multiway training scaling audit

**Status:** Complete
**Completed:** 2026-09-01

- Recorded 30 evidence-backed findings across the active 1-through-16-worker
  traversal, coordinator, compact storage, continuation, scheduling, merge,
  allocation, telemetry, checkpoint, and benchmark paths.
- Defined an ordered measurement and immutable-batch-epoch plan to remove shared
  traversal writes and qualify monotonic 1/2/4/8/16-worker scaling.

### Files

- `docs/multiway_blueprint_training_scaling_benchmark.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static source review only. Builds, tests, solver runs, and benchmarks were not
  run because the requested code-review workflow forbids them without explicit
  authorization.

### Limitations

- Bottleneck priorities are supported by code paths and the recorded 4-versus-8
  worker regression. Phase 0 instrumentation is required to quantify each share.

## Multiway blueprint multithreaded training activation and qualification

**Status:** Complete
**Completed:** 2026-09-01

- Activated schema 2 training worker counts, CLI overrides, pre-allocation memory admission, deterministic worker telemetry, and true per-worker delta high-water accounting.
- Qualified four workers on the checked-in benchmark fixture with a measured 2.20x median gain over one worker. Eight and sixteen workers were slower on this host.

### Files

- `docs/multiway_blueprint_multithreaded_training_done.md`
- Existing implementation and focused test files are recorded in commit `40a6738`.

### Validation

- `python scripts/full_build.py`: PASS; Debug build and all registered tests passed.
- Release scaling benchmark commands and results are recorded in `docs/multiway_blueprint_multithreaded_training_done.md`.

### Limitations

- Four workers are qualified only for the documented fixture and host. Production F1 performance and memory qualification still require the human execution run.

## Sixteen-worker blueprint training scaling audit plan

**Status:** Complete
**Completed:** 2026-08-31

- Audited the shared coordinator, traversal pool, compact storage, and existing
  scaling evidence to identify the global traversal mutex and locked admission
  work as the primary sixteen-worker bottleneck.
- Defined a frozen batch-epoch architecture with immutable coordinator views,
  worker-local updates and diagnostics, deterministic staged admission, and a
  fixed-order reduction.
- Added measurable 1/2/4/8/16 speedup gates, contention instrumentation,
  controlled experiments, implementation phases, validation requirements, and
  completion criteria.

### Files

- `docs/multiway_blueprint_training_scaling_benchmark.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check -- docs/PLURIBUS_LOG.md`: passed.
- The new audit-plan section passed a focused trailing-whitespace check.
- No build or tests were run because this task changes documentation only.

### Limitations

- The proposed speedups remain targets until the planned code changes and the
  full Release qualification matrix are completed on the target hardware.

## Multiway blueprint training scaling benchmark

**Status:** Complete
**Completed:** 2026-08-31

- Added a configurable Release benchmark for deterministic multiway blueprint
  training worker and batch-size scaling.
- The benchmark reports per-sample and median throughput, coordinator wait,
  aggregate worker-active time, sort and merge time, trajectory balance,
  per-worker delta occupancy, and deterministic merge fingerprints.
- Measured 4/8/16 workers at 1,000, 4,000, and 16,000 trajectories per batch
  with one warm-up and three independent samples per point.
- Published the command, all raw samples, median comparison, bottleneck
  analysis, and limitations in a dedicated benchmark report.

### Files

- `CMakeLists.txt`
- `examples/benchmarks/multiway_training_scaling_main.cpp`
- `docs/multiway_blueprint_training_scaling_benchmark.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `python scripts/full_build.py`: Debug build and all 108 CTest tests passed.
- Release benchmark target built successfully and its `--help` smoke run passed.
- Median trajectories/s for 4/8/16 workers were 56.8k/51.9k/47.5k at batch
  1,000, 56.8k/50.5k/48.2k at batch 4,000, and 55.3k/41.2k/40.6k at batch
  16,000.
- Every worker count produced the same deterministic merge fingerprint for
  each fixed batch size.
- `git diff --check` passed and the focused C++ review found no actionable
  findings.

### Limitations

- The benchmark is a bounded three-player river, depth-one training fixture.
  It isolates scheduler and coordinator scaling but does not replace a full
  six-player preflop-to-river production qualification.

## Multiway blueprint multithreaded training

**Status:** Complete
**Completed:** 2026-08-31

- Activated deterministic schema-2 blueprint training with validated 1 to 16
  worker configuration and a `--threads` override.
- Added mandatory process-memory admission, requested/effective worker and
  stage timing telemetry, true per-worker delta high-water reporting,
  canonical checkpoint/export ordering, and allocation-free compact regret
  matching in the serialized lookup path.
- Qualified exact 1/2/4/8/16-worker state and policy equivalence, fixed-worker
  checkpoint resume, cancellation/error/capacity boundaries, and selected four
  workers for the sizing profile from the Release scaling matrix.

### Files

- `configs/multiway/f1_sizing_v1.cfg`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/engine/multiway_compact_storage.hpp`
- `include/solver/multiway/engine/multiway_traversal.hpp`
- `include/solver/multiway/workflow/multiway_evidence.hpp`
- `include/solver/multiway/workflow/multiway_training_report.hpp`
- `include/solver/multiway/workflow/multiway_workflow_config.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/engine/multiway_compact_storage.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `src/solver/multiway/workflow/multiway_workflow_config.cpp`
- `tests/test_multiway_compact_storage.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tests/test_multiway_workflow_config.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `python scripts/full_build.py`: Debug build and all 108 CTest tests passed.
- Dedicated Release parallel-training executable: all seven tests passed.
- Release matrix after warm-up, three samples each: median throughput was
  19.8k, 35.5k, 52.6k, 47.1k, and 44.1k trajectories/s for 1, 2, 4, 8, and
  16 workers. Four workers delivered 2.65x median speedup; 16 delivered 2.23x.
- `git diff --check` passed.
- Independent C++ review: no remaining actionable findings.

### Limitations

- The acceptance profile remains schema 1 with one worker until its full
  50-million-trajectory production qualification is completed.
- Sixteen workers are supported and deterministic but were not fastest on the
  bounded local Release fixture.

## Multiway blueprint multithreaded training audit and plan

**Status:** Complete
**Completed:** 2026-08-31

- Audited the F1 blueprint training path and confirmed that its deterministic
  persistent worker pool is implemented but disabled by the one-worker workflow
  contract and train CLI plumbing.
- Recorded two actionable findings and a staged activation, memory admission,
  telemetry, correctness, performance, and qualification plan for up to 16
  training workers.

### Files

- `docs/multiway_blueprint_multithreaded_training_plan.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Reviewed the documentation diff and targeted source locations.
- No build, test, benchmark, or solver job was run because this was a read-only
  C++ audit and documentation task.

### Limitations

- The plan does not activate training workers or claim measured training
  speedup. Implementation and Release qualification remain deferred.

## Multiway artifact inspection multithreading

**Status:** Complete
**Completed:** 2026-08-31
**Implementation commits:** `e743294`, `ce44b1f`

- Added a bounded deterministic worker pool for complete schema-4 artifacts,
  with fixed 4,096-table leaves, independent buffered readers, validation,
  worker-local results, ordered reduction, and coordinator-only progress.
- Added explicit parallel, legacy, and both hash modes while preserving the
  historical stream FNV payload hash.
- Wired inspection thread and hash options through the CLI and added a focused
  deterministic inspection test.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- `python scripts/full_build.py` passed: Debug build and all 107 CTest tests.
- `git diff --check` passed.
- Release production-artifact inspection reached the workflow evidence gate
  successfully: 31.055 seconds legacy buffered inspection versus 12.077
  seconds with four parallel workers on the local 14.3 GiB artifact.
- Release CLI qualification with 16 workers completed successfully on the
  production artifact, emitted progress during inspection, and reported
  2,891,785 tables and 3,140,843,160 live assignments.

### Limitations

- CLI provenance fields default to the explicit value `unknown` when the local
  environment does not provide them.

## Multiway bucket inspection progress reporting

**Status:** Complete
**Completed:** 2026-08-31
**Implementation commit:** `0103c31 feat(inspect): report artifact inspection progress`

- Added console progress reporting to `texas_multiway_inspect`.
- Progress is enabled by default and configurable with `--progress true|false`
  and `--progress-every N`.
- Preserved the library API through optional callback and interval parameters.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_artifact.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_inspector.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Full Debug build and CTest passed.
- Focused bucket-generation suite passed all 18 tests.
- CLI help confirmed the new options and defaults.

### Limitations

- Progress output is emitted by the executable callback, not by default from library callers.

## F1 bucket generation - single-worker performance implementation

**Status:** Complete
**Completed:** 2026-08-31
**Implementation commits:** `317d6c2 perf(buckets): optimize serialized worker kernel`,
`510b9f4 fix(buckets): preserve empty serialized ranges`

- Added overflow-checked serialized-generation memory estimation and wired it
  into artifact preflight.
- Optimized the direct worker kernel with cached combo metadata, board masks and
  counts, reusable FNV board prefixes, and exact-size cursor encoding.
- Added worker-active timing telemetry while preserving byte-identical artifacts.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `full_build.py`: Debug build and full CTest passed.
- `test_multiway_bucket_generation.exe`: 18 tests passed.
- Release end-to-end qualification matrix: five repetitions across global
  starts `0`, `22100`, and `292825`, 65,536 tables, and 1/2/16 workers.
  Checksums were identical for every repeated range.
- Release median rates for starts `0`/`22100`/`292825` were respectively
  `37561`/`74287`/`102307`, `38146`/`75474`/`101953`, and
  `39699`/`78556`/`102531` tables/s at 1/2/16 workers.

### Limitations

- The ranges are global catalog ranges, so the first range crosses the flop to
  turn boundary. A same-binary before/after run was not available; comparison
  is against the frozen review baselines.

## F1 bucket generation - single-worker performance review

**Status:** Complete
**Completed:** 2026-08-31

- Froze the final 1/2/16-worker Release qualification medians and reviewed the
  direct serialized worker kernel, benchmark attribution, and memory contract.
- Identified five actionable single-worker performance costs and one memory
  preflight defect, with an ordered fix-and-measure plan that preserves exact
  artifact bytes and checksums.

### Files

- `docs/multiway_bucket_single_worker_performance_review.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static review only. No build, test, benchmark, profiler, or solver run was
  performed because the review skill forbids execution unless requested.

### Limitations

- Hotspot rankings use code-path operation counts. Kernel-active timing and a
  CPU profile are required before implementation priority is treated as final.

## F1 bucket generation - final five-run qualification matrix

**Status:** Complete
**Completed:** 2026-08-31

- Completed five Release end-to-end repetitions at 65,536 tables for flop,
  turn, and river using 1, 2, and 16 workers on the final direct-encoding and
  reusable-slot pipeline.
- Median rates for 1/2/16 workers were flop `8859`/`19381`/`102839`, turn
  `8838`/`18874`/`102299`, and river `8241`/`17903`/`102826` tables/s.
- All runs produced 256 chunks and identical per-street checksums. A 32-worker
  comparison was slower than 16 workers on all streets.

### Files

- `examples/multiway_workflow_main.cpp`

### Validation

- Final Release qualification matrix passed: 45 benchmark runs.
- Two-worker end-to-end throughput exceeded the 1.25x gate on every street.
- Sixteen-worker throughput was the best tested safe policy; 32 logical workers
  were slower and are excluded from the default.

### Limitations

- Full 2,891,785-table production generation remains intentionally unrun per
  the user instruction. OS-level allocator, disk, and context-switch counters
  were not collected.

## F1 bucket generation - final worker policy qualification

**Status:** Complete
**Completed:** 2026-08-31

- Raised the measured worker cap to 16 and kept the CLI default topology-aware
  as `min(16, physical_cores)`.
- Release 65,536-table end-to-end measurements showed 16 workers at
  `103567`/`102801`/`100819` tables/s for flop/turn/river, while 32 logical
  workers fell to `99315`/`98162`/`97414` and had much higher worker wait time.
- The policy therefore avoids SMT oversubscription by default while allowing
  explicit qualification overrides.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `examples/multiway_workflow_main.cpp`

### Validation

- Release 65,536-table matrix passed at 16 and 32 workers for all streets.
- Checksums matched across worker counts.
- Full Debug build and all tests passed after the policy change.

### Limitations

- The selected cap is measured on the qualification host; other machines
  should use the explicit override and benchmark mode.

## F1 bucket generation - reusable serialized ring slots

**Status:** Complete
**Completed:** 2026-08-31

- Reworked serialized chunk publication to use bounded reusable payload slots.
- Payload vectors retain capacity across worker chunks and return to the free
  slot pool only after ordered publication, bounding in-flight payload memory.
- Release 4,096-table smoke rates at 1/2/4/8 workers were `10262`/`20283`/
  `36451`/`50774` tables/s with matching checksums.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- Full Debug build and all tests passed.
- Release build passed.
- Release end-to-end smoke benchmark passed for 1, 2, 4, and 8 workers.

### Limitations

- The conservative default remains four workers until a longer repeated matrix
  validates whether the 8-worker result persists.

## F1 bucket generation - direct worker serialization

**Status:** Complete
**Completed:** 2026-08-31

- Added direct artifact-record encoding from fixed catalog boards and compact
  hole combinations, avoiding temporary table objects and assignment vectors
  on the production serialized path.
- Added differential coverage against the checked/table-object artifact path.
- Updated Release smoke rates for 1/2/4 workers to `10060`/`18542`/`29489`
  tables/s on 1,024 flop tables; checksums matched.

### Files

- `include/solver/multiway/abstraction/multiway_bucket_generation.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_bucket_generation.cpp`

### Validation

- Full Debug build and all tests passed.
- Release build passed.
- Release direct-serialization smoke benchmark passed at 1, 2, and 4 workers.

### Limitations

- Payload vectors still allocate once per worker chunk; reusable ring-slot
  buffers and external allocator/CPU/disk profiling remain future work.

## F1 bucket generation - serialized scheduler wait telemetry

**Status:** Complete
**Completed:** 2026-08-31

- Added worker and ordered-publisher wait timers to serialized chunk
  generation, making scheduler diagnostics comparable with the table-object
  path.

### Files

- `src/solver/multiway/abstraction/multiway_bucket_generation.cpp`

### Validation

- Full Debug build and all tests passed.

### Limitations

- Hardware performance counters and allocator profiling remain external to the
  benchmark executable.

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
