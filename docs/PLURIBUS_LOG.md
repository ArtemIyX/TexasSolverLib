# Pluribus Roadmap Progress Log

## CMake build structure refactor

**Status:** Complete
**Completed:** 2026-10-29

- Kept project setup, options, and source/header classification in the root; moved target and package rules to `cmake/TexasSolverTargets.cmake`, examples to `examples/CMakeLists.txt`, and test declarations to `tests/CMakeLists.txt`.
- Preserved target names, C++17 and CMake 3.20 requirements, dependency visibility, platform source selection, test gates, and independent optional components.
- Corrected the parent install rule to install the vendored evaluator's public headers from their actual source paths without editing vendored files.
- Added a CMake structure contract and updated boundary checks for the new ownership files.

### Files

- `CMakeLists.txt`
- `cmake/TexasSolverTargets.cmake`
- `examples/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `tests/cmake/test_cmake_structure.cmake`
- `tests/cmake/test_example_boundary.cmake`
- `tests/cmake/test_legacy_vector_boundary.cmake`
- `tests/cmake/test_multiway_workflow_boundary.cmake`
- `tests/cmake/test_tracy_boundary.cmake`
- `docs/PLURIBUS_LOG.md`

### Validation

- Configured six independent Windows/MSVC variants: defaults, research examples, legacy research, fixed HUNL research, logging, and Tracy diagnostics.
- `scripts/python/full_build.py` passed the full build and test suite before the final install-path adjustment. After that adjustment, `compact_build.py --target texas`, all CTest tests, and seven focused CMake boundary tests passed.
- The first post-adjustment parallel CTest run hit an MSVC program-database mismatch in `test_public_header_boundary`; that test passed alone and the subsequent full CTest run passed.
- Installed the package, then configured and linked a separate `TexasSolver::texas` consumer before and after relocating the install prefix.
- `git diff --check` passed for the task changes.

### Limitations

- Verification used Windows/MSVC with CMake 4.4.0-rc2; exact CMake 3.20, Linux, and macOS runs were unavailable.

## Project skill - CMake maintenance

**Status:** Complete
**Completed:** 2026-10-29

- Added a project-local CMake skill with implicit invocation enabled and an AGENTS.md requirement to read it before every CMakeLists.txt edit or related CMake change.
- Added five focused guides covering targets/modules, dependencies/packages, tests/verification, platforms/presets, and authoritative CMake plus LLVM/VTK/curl references. Included C++17, package-config, test-registration, and CMake 3.20 preset examples.
- Preserved execution authorization boundaries. No CMake files, build behavior, or solver code changed.

### Files

- `AGENTS.md`
- `.agents/skills/cmake-maintenance/SKILL.md`
- `.agents/skills/cmake-maintenance/agents/openai.yaml`
- `.agents/skills/cmake-maintenance/references/targets-and-modules.md`
- `.agents/skills/cmake-maintenance/references/dependencies-and-packages.md`
- `.agents/skills/cmake-maintenance/references/testing-and-verification.md`
- `.agents/skills/cmake-maintenance/references/platforms-and-presets.md`
- `.agents/skills/cmake-maintenance/references/sources-and-projects.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Static checks passed for required frontmatter, all five local reference links, the JSON preset example, implicit invocation metadata, and AGENTS.md routing.
- `git diff --check` passed. No project builds or tests ran for this instruction-only change.
- Attempted the bundled skill-creator `quick_validate.py` through the project Python wrapper using both available runtime paths; both lacked the required PyYAML module.

### Limitations

- Full skill-creator validation remains unavailable without PyYAML. Automatic use is instructed through skill metadata and AGENTS.md; this is not a filesystem hook or proof of future agent behavior.

## PB-11 CI follow-up: Force page-sub-batch rollback coverage

**Status:** Test fixture corrected; CI rerun pending
**Completed:** 2026-10-29

- User-reported Linux CTest failure: `multiway_solver_external_bounded_page_merge_rolls_back_flushed_subbatches` observed no page publication before the intentionally invalid delta.
- A 2,048-byte attempt was rejected because its remaining page-cache budget could not hold one committable page. The initial 3,000-byte fixture update still failed on Linux: `MultiwayWorkerDelta` aggregate fields are bucket then action, but the attempted updates varied action while keeping bucket zero, so only the root and one child page were dirtied. The fixture now keeps the valid 3,000-byte cache and updates child buckets 0 through 3 at action 0 before the invalid action 255. This dirties five distinct pages, exceeding the two-page cache capacity and forcing a subbatch publication. Production code is unchanged.

### Files

- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- User-provided CI results confirm the original no-publication failure, the 2,048-byte attempt failed with `external compact page cache cannot hold a committable page`, and the first 3,000-byte attempt still had no page publication because its aggregate delta initializers addressed one child bucket.
- `git diff --check` passed after the change. Builds and tests were not run by the assistant, per repository instructions.

### Limitations

- CI rerun is required to confirm the test fixture passes on Linux.

## Stage 9.4 post-stop: Correct the first bounded append admission

**Status:** Complete, bounded diagnostic verified; P99 inconclusive
**Completed:** 2026-10-29

- The prepared PB-12 run failed on its first catalog append with `external storage disk amplification budget cannot admit a record`; its two scans recorded 397 live bytes and 581 stored bytes, below the 3x hard stop. The run produced no batch/checkpoint and is not a completed-run result.
- Synchronous bounded aggregate training now relies on its existing durable transaction WAL and manifest backups for append rollback/recovery, avoiding a duplicate component payload WAL. Append preflight counts only the component's segment/index and manifest staging; aggregate-manifest staging is checked at commit against the updated live-byte denominator and disk reserve. Generic stores retain component WAL behavior.
- The monitor now preserves trainer failure as the primary completion reason instead of overwriting it with missing-observation status. Added first-record admission, outer-journal rollback/recovery, commit-peak rejection, and monitor reason-precedence regressions.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `tests/test_multiway_external_storage.cpp`
- `scripts/run/pb12_diagnostic_process.ps1`
- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `docs/PLURIBUS_LOG.md`
- `tmp/pb12_diagnostic_20261029_aggregatewalfix/training.conf`
- `tmp/pb12_diagnostic_20261029_aggregatewalfix/plan.json`
- `tmp/pb12_diagnostic_20261029_aggregatewalfix/preparation_evidence.json`
- `tmp/pb12_diagnostic_20261029_verifiedprep/training.conf`
- `tmp/pb12_diagnostic_20261029_verifiedprep/plan.json`
- `tmp/pb12_diagnostic_20261029_verifiedprep/preparation_evidence.json`
- `tmp/pb12_diagnostic_20261029_verifiedprep/monitor.jsonl`
- `tmp/pb12_diagnostic_20261029_verifiedprep/training.log`
- `tmp/pb12_diagnostic_20261029_memretry/training.conf`
- `tmp/pb12_diagnostic_20261029_memretry/plan.json`
- `tmp/pb12_diagnostic_20261029_memretry/preparation_evidence.json`
- `tmp/pb12_diagnostic_20261029_memretry/monitor.jsonl`
- `tmp/pb12_diagnostic_20261029_memretry/training.log`
- `tmp/pb12_diagnostic_20261029_memretry/training.log.diagnostics.17932886830030567.jsonl`
- `tmp/pb12_diagnostic_20261029_memretry/report.json`
- `tmp/pb12_diagnostic_20261029_memretry/policy.mwfb`

### Validation

- An earlier post-fix Debug build attempt failed in `test_multiway_phase4`'s `vcpkg z-applocal` step (exit 32); the subsequent user-reported Debug build passed. The trainer hash is `6520FB95D63B013CDF54527EA902DCB09D29ECD83E6800B82CF716F9E2262A61`.
- User-reported compact CTest passed all 128 tests (`tmp/ctest_debug.log`). `LastTest.log` confirms all 61 storage cases passed, including first-record admission, aggregate-journal rollback recovery, and commit-peak rejection.
- User-reported focused PowerShell monitor checks passed.
- `verifiedprep` stopped when available physical memory reached 3,000,545,280 bytes, below the 3 GiB monitor floor. Its training log had reached batch 3/3, but the process ended with exit -1 and no final report; this attempt remains incomplete.
- The fresh `memretry` run completed with exit 0: 3/3 batches, 3,000 accepted trajectories, zero discarded, `failed=false`, and successful policy export. The monitor recorded 3 batches and checkpoints, no stop reason, and a minimum sampled available-memory value of 21.82 GiB.
- The monitor made 178 disk scans; the largest reported amplification was 1.6458x, with no disk stop. Each scan spans sequential filesystem reads, so its live/store/temporary component values are not an instantaneous breakdown. Compaction read/write amplification was 1.20128x/1.17755x.
- The journal recorded catalog run counts 3, 4, then 2; row run counts 2, 3, then 4; and page run counts 1, 2, then 3. This three-batch observation does not prove run-count growth is bounded.
- The fresh preparation evidence matches the trainer binary and Stage 9.4 bucket size/timestamp; `bucket_hash_measured=false`. The earlier `postfix`, `aggregatewalfix`, and `verifiedprep` artifacts remain preserved.
- `git diff --check` and PowerShell syntax parsing passed. No build or test was run by the assistant.

### Limitations

- The bucket content hash remains unmeasured. The P99 gate is inconclusive because three batches provide only two checkpoint cycles, short of the required three two-cycle cohorts. This diagnostic does not qualify Stage 9.4 or Stage 9.5.

## Stage 9.4 post-stop: Bound PB-12 external-store publication amplification

**Status:** Complete
**Completed:** 2026-10-29

- Added an opt-in fixed 3x bound for the PB-12 diagnostic. Append publications include WAL, segment/index temporaries, component-manifest publication, the aggregate-manifest temporary reserve, and all existing store files.
- Added exact index-only output counting and pre-publication/post-publication amplification checks for compaction. Batch suffix runs are consolidated within configured fan-in; rollback inputs remain until aggregate commit and cleanup.
- Closed compaction source streams before deleting intermediate suffix runs. Bounded cleanup errors now fail closed instead of silently leaving old run files in the disk-accounting root. The aggregate transaction still retains its rollback inputs.
- Kept the monitor's hard stop and generic external-storage defaults unchanged. Added regression cases for cap-driven chunking, rollback recovery, run-count limits, and reopen integrity.

### Files

- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `examples/multiway_workflow_main.cpp`
- `scripts/run/run_pb12_diagnostic.ps1`
- `tests/test_multiway_external_storage.cpp`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed.
- User-reported Debug build passed after the source and test edits.
- User-reported compact CTest passed all 128 tests (`tmp/ctest_debug.log`); `LastTest.log` records the staged-byte recovery regression as passed.
- User reported the focused monitor checks passed. Prepared `tmp/pb12_diagnostic_20261029_postfix`; its evidence SHA-256 matches the rebuilt trainer binary and the preserved bucket size/timestamp baseline.
- No solver diagnostic job was started.

### Limitations

- The bucket content hash was not measured. The first bounded run later stopped before a completed batch; its scans do not establish workload memory or disk amplification. The exact compaction preflight adds an index-read pass; any diagnostic launch needs separate authorization.

## Stage 9.4 post-stop: Correct diagnostic startup guard

**Status:** Complete, implementation only
**Completed:** 2026-10-29

- Corrected the diagnostic monitor to allow an empty store directory during bucket-artifact loading, while retaining fail-closed checks for storage files without a training manifest.
- Preserved the stopped diagnostic and its monitor evidence.

### Files

- `scripts/run/pb12_diagnostic_process.ps1`
- `scripts/run/run_pb12_diagnostic.ps1`
- `scripts/run/test_pb12_diagnostic_monitor.ps1`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Inspected the stopped run's training and monitor logs; they show bucket loading when the guard fired.
- PowerShell syntax parsing and `git diff --check` passed. Tests were not run.

### Limitations

- The revised guard requires the focused monitor checks and integration validation.

## Stage 9.4 post-stop: Refresh diagnostic binary evidence

**Status:** Complete
**Completed:** 2026-10-29

- Preserved the original prepared diagnostic after its binary hash changed and prepared a separate directory with the current binary hash.
- Made the launcher identify a changed binary separately from a configuration mismatch.

### Files

- `scripts/run/run_pb12_diagnostic.ps1`
- `docs/pb12_poststop_diagnostic_procedure.md`
- `tmp/pb12_diagnostic_20261029_rebuilt/training.conf`
- `tmp/pb12_diagnostic_20261029_rebuilt/plan.json`
- `tmp/pb12_diagnostic_20261029_rebuilt/preparation_evidence.json`
- `docs/PLURIBUS_LOG.md`

### Validation

- Checked the new configuration and binary hashes, launcher syntax, and `git diff --check`.
- No build, test, benchmark, or solver job was run.

### Limitations

- The complete monitor and trainer integration remains unverified.

## Stage 9.4 post-stop: Classify diagnostic headers

**Status:** Complete
**Completed:** 2026-10-29

- Added the observer and journal headers to the public CMake header list so the classification check can find them.

### Files

- `CMakeLists.txt`
- `docs/PLURIBUS_LOG.md`

### Validation

- A read-only classification scan found every `include/*.hpp` in exactly one header list.
- CMake configuration, builds, and tests were not run; the user requested separate authorization.

### Limitations

- CMake configuration and linking still require verification.

## Stage 9.4 post-stop: Bounded page publication and diagnostics

**Status:** Complete, implementation only
**Completed:** 2026-10-29

- Grouped dirty page updates within cache, WAL, segment, and publication-buffer bounds; aggregate transactions remain the commit boundary.
- Added an exclusive, synchronously flushed diagnostic journal for component publications, clean batches, and checkpoints, with timings, counters, and memory samples.
- Documented the compaction and pilot-validation limits. The Stage 9.4 hard stops are unchanged.

### Files

- `CMakeLists.txt`
- `docs/pb12_50m_memory_scaling_plan.md`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/engine/multiway_external_observer.hpp`
- `include/solver/multiway/engine/multiway_external_storage.hpp`
- `include/solver/multiway/engine/multiway_solver.hpp`
- `include/solver/multiway/workflow/multiway_diagnostic_journal.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/engine/multiway_external_storage.cpp`
- `src/solver/multiway/engine/multiway_solver.cpp`
- `src/solver/multiway/workflow/multiway_diagnostic_journal.cpp`
- `tests/test_multiway_diagnostic_journal.cpp`
- `tests/test_multiway_external_storage.cpp`
- `tests/test_multiway_solver.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- `git diff --check` passed during implementation.
- Builds, tests, benchmarks, and solver jobs were not run, as requested.

### Limitations

- The patch and journal overhead require authorized build, test, and bounded-run validation.
- A compaction call removes at most fan-in minus one active runs; run growth and pilot gates are not proven bounded.
- Smaller page-cache configurations can now be rejected because publication buffers are included in the cache budget.

## Stage 9.1 and 9.2: Pre-pilot validation gates

**Status:** Complete
**Completed:** 2026-10-29

- Completed Stage 9.1 focused validation for estimator arithmetic, bucket schema, external catalog/storage, WAL recovery, checkpoint manifests, sharded export, and workflow contracts.
- The prescribed Stage 9.2 full-build workflow passed, as confirmed by the user. Its CTest output reports 116/116 tests passing.
- Stage 9.3 was not rerun. Stage 9.4 remains pending.

### Files

- `tmp/pb12_stage91_20261029T062952Z/bucket_artifact.log`
- `tmp/pb12_stage91_20261029T062952Z/checkpoint_equivalence.log`
- `tmp/pb12_stage91_20261029T062952Z/external_storage.log`
- `tmp/pb12_stage91_20261029T062952Z/focused_build.log`
- `tmp/pb12_stage91_20261029T062952Z/memory.log`
- `tmp/pb12_stage91_20261029T062952Z/qualification_reports.log`
- `tmp/pb12_stage91_20261029T062952Z/sharded_export.log`
- `tmp/pb12_stage91_20261029T062952Z/sharded_workflow_lookup.log`
- `tmp/pb12_stage91_20261029T062952Z/sizing_report.log`
- `tmp/pb12_stage91_20261029T062952Z/solver.log`
- `tmp/pb12_stage91_20261029T062952Z/workflow_config.log`
- `tmp/build_debug.log`
- `tmp/ctest_debug.log`
- `docs/PLURIBUS_LOG.md`

### Validation

- Stage 9.1 focused suites passed; outputs are recorded in the listed run logs.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py` passed, as confirmed by the user; CTest reports 116/116 passed.

### Limitations

- Stage 9.4 and larger workloads remain pending.

## Stage 9.3: 1K deterministic differential pilot

**Status:** Complete
**Completed:** 2026-10-29

- Enabled PB-12 targeted coverage for `CompactInt32` and verified the same eight exact state/menu/seat/bucket rows before training.
- Ran one batch of 1,000 trajectories on both backends with seed 1, one fixed-scheduler worker, and the same `sizing/f1_sizing_v1` model settings. Both accepted all trajectories with zero discards.
- Public-state records, sparse rows, logical action values, per-street row counts, merge fingerprint, and policy-content hash matched. Compact checkpoint-only resume with zero added batches produced a byte-identical policy.
- External and Compact training identity hashes differ because storage backend is bound into identity; their bucket-model hashes and policy-content hashes match.

### Files

- `examples/multiway_workflow_main.cpp`
- `src/solver/multiway/workflow/multiway_pb12_coverage.cpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_training_report.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `tmp/pb12_compact_reference_1k_20261029T081219/training.conf`
- `tmp/pb12_compact_reference_1k_20261029T081219/policy.mwfb`
- `tmp/pb12_compact_reference_1k_20261029T081219/report.json`
- `tmp/pb12_compact_reference_1k_20261029T081219/checkpoints/latest.bin`
- `tmp/pb12_compact_reference_1k_20261029T081219/policy_resume_verified.mwfb`
- `tmp/pb12_compact_reference_1k_20261029T081219/resume_report_verified.json`
- `tmp/pb12_compact_reference_1k_20261029T081219/comparison.md`
- `tmp/pb12_compact_reference_1k_20261029T081219/protected_before.json`
- `tmp/pb12_compact_reference_1k_20261029T081219/protected_after.json`

### Validation

- `cmake --build build-pb12 --target test_multiway_blueprint_training test_multiway_qualification_reports texas_multiway_train --parallel 2` passed.
- Focused Compact targeted-admission, External targeted-admission, and checkpoint-report tests each passed (1 test each).
- The Compact training command in `tmp/pb12_compact_reference_1k_20261029T081219/command.txt` exited 0; report has 1,000 accepted and zero discarded trajectories.
- The checkpoint-only resume command in `tmp/pb12_compact_reference_1k_20261029T081219/resume_command_verified.txt` exited 0 with zero added batches. The original and resumed Compact policies and checkpoints have identical SHA-256 hashes.
- The External manifest and Compact report show 69,204 public states, 31,078 rows, and 6,405,846 logical action values. Merge fingerprint `9806774443936985566` and policy-content hash `2488139010106883890` match.
- Pre/post SHA-256 and size snapshots matched for all 16,664 protected files, including the bucket artifact and the existing External run. `git diff --check --ignore-submodules=all` passed.

### Limitations

- The External report's `peak_public_states` is one below its committed manifest count, and its `peak_sparse_values` is unavailable as zero. The comparison uses the committed manifest for External counts and Compact `export_action_count` for logical-value parity. Backend-bound model identity and artifact payload hashes therefore are not equal; policy-content hashes are equal.
- Stage 9.4 and larger workloads were not run.

## PB-12 targeted 1K training pilot

**Status:** Complete
**Completed:** 2026-10-29

- Ran the planned one batch of 1,000 trajectories with `ExternalCompactInt32` and `--pb12-targeted-coverage`.
- Regular targeted training now runs the existing PB-12 exported-policy verifier after export. The run admitted all eight exact store keys, verified them in the policy, and replayed the captured histories.
- Training completed with 1,000 accepted trajectories, zero discards, nonzero rows on every street, and `failed=false`.

### Files

- `examples/multiway_workflow_main.cpp`
- `tmp/pb12_targeted_training_1k_capacity100m/training.conf`
- `tmp/pb12_targeted_training_1k_capacity100m/store/`
- `tmp/pb12_targeted_training_1k_capacity100m/policy.mwfb`
- `tmp/pb12_targeted_training_1k_capacity100m/report.json`
- `tmp/pb12_targeted_training_1k_capacity100m/checkpoints/latest.bin`

### Validation

- `cmake --build build-pb12 --target test_multiway_blueprint_training texas_multiway_train --parallel 2` passed.
- The focused `multiway_pb12_captured_preflop_misses_are_admitted_exported_and_replayed` test passed (1 test).
- The one-batch training command completed with exit code 0 and policy-content hash `2488139010106883890`.
- `git diff --check --ignore-submodules=all` passed.
- The bucket artifact was used as input only and remained 15,369,522,466 bytes with its existing 2026-08-31 timestamp.

### Limitations

- This is the planned 1K pilot, not a 50M qualification run.

## PB-12 targeted training admission and replay

**Status:** Complete
**Completed:** 2026-10-28

- Recovered the captured public histories from the PB-12 lookup report backup; no histories were inferred from IDs.
- Added a targeted admission API that rebuilds both captured branches, uses the normal coordinator path to add eight exact rows, and verifies each public state, seat, bucket range, and action-menu ID in External storage before export.
- Added `--pb12-targeted-coverage` for targeted rows before regular training and `--pb12-coverage-only` for zero-batch admission, export, and replay verification. Coverage-only requires a fresh External store.
- The exported policy is replay-checked against both captured branches. All eight exact provider lookups must hit and return normalized strategies.

### Files

- `CMakeLists.txt`
- `configs/multiway/pb12_targeted_coverage.conf`
- `examples/multiway_workflow_main.cpp`
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
- `include/solver/multiway/workflow/multiway_pb12_coverage.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
- `src/solver/multiway/workflow/multiway_pb12_coverage.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `docs/PLURIBUS_LOG.md`

### Validation

- Configure: `cmake -S . -B build-pb12 -G Ninja -DCMAKE_MAKE_PROGRAM="C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_TOOLCHAIN_FILE= -DTEXASSOLVER_BUILD_TESTS=ON -DTEXASSOLVER_BUILD_EXAMPLES=ON -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON -DTEXASSOLVER_ENABLE_LOGGING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` passed.
- `cmake --build build-pb12 --target test_multiway_blueprint_training texas_multiway_train --parallel 2` passed.
- With `TEMP` and `TMP` set to `build-pb12/test-temp`, `test_multiway_blueprint_training.exe multiway_pb12_captured_preflop_misses_are_admitted_exported_and_replayed` passed (1 test).
- `texas_multiway_train.exe --config configs/multiway/pb12_targeted_coverage.conf --pb12-coverage-only` passed against the preserved bucket artifact. It reported `exact_keys=8`, `batches=0`, and produced a policy with 1,352 rows (89,416 bytes); the report records zero trajectories and `failed=false`.
- `git diff --check --ignore-submodules=all` passed.
- `artifacts/f1_sizing_v1/buckets/buckets.bin` remains present at 15,369,522,466 bytes and was read-only during the run.

### Limitations

- The default Visual Studio build in `build` was blocked by compiler error C1902; focused targets were built and verified in `build-pb12` with Clang/Ninja.

## PB-12 missing-key admission provenance

**Status:** Complete
**Completed:** 2026-10-28

- Compared all eight corrected replay keys with the external training row index, public catalog, and exported policy rows.
- All eight states are absent from both training stores and the policy. The evidence indicates a training coverage gap, not export filtering or identity loss.
- Confirmed the attempt policy and preserved blueprint have the same SHA-256; the parsed policy row count and payload hash match the export report.

### Files

- `scripts/analyze/compare_pb12_policy_admissions.py`
- `docs/PLURIBUS_LOG.md`

### Validation

- The comparison tool returned `training_coverage_gap` for all eight keys.
- The snapshot audit passed for the source and disposable copy: 16,669 files, no missing, extra, or mismatched files.
- C++ builds and tests were not run because no C++ sources changed.

### Limitations

- None identified.

## PB-12 External policy lookup replay diagnostics

**Status:** Complete
**Completed:** 2026-10-28

- Corrected lookup qualification to query the verified provider on first local visits when auditing. That result is diagnostic only; the existing uniform first-visit strategy remains unchanged.
- Added opt-in samples selected by deterministic hash priority across distinct missing policy keys, capped at 16 entries and 64 public history actions per entry. Samples include the exact policy key and the public inputs used to build its state ID, plus a reconstructed ID. Private cards are excluded.
- Training row admission and policy export use the same public-state/seat/bucket/action-menu key. The earlier 25,149 `Missing` statuses included first local visits where provider lookup was skipped, so they do not establish policy misses.
- Added `--diagnostic-limit`; zero keeps the existing report shape. Lookup fingerprints now include actual provider results on first local visits, so their values may differ from the old audit while remaining deterministic.

### Files

- `include/solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp`
- `src/solver/multiway/blueprint/multiway_blueprint_policy_provider.cpp`
- `src/solver/multiway/engine/multiway_traversal.cpp`
- `include/solver/multiway/workflow/multiway_lookup_qualification.hpp`
- `src/solver/multiway/workflow/multiway_lookup_qualification.cpp`
- `examples/multiway_workflow_main.cpp`
- `tests/test_multiway_blueprint_training.cpp`
- `tests/test_multiway_qualification_reports.cpp`
- `docs/application_and_script_io.md`
- `docs/PLURIBUS_LOG.md`

### Validation

- Read the preserved `tmp/pb12_lookup_replay_20261027/lookup_report.json` and confirmed its aggregate counts, policy hash, and equal replay fingerprints match the logged results. The old miss count is not an accurate provider-coverage measurement.
- Reviewed the training admission, export, and provider lookup key paths and the changed C++ diff.
- C++ review found no actionable issues. `git diff --check --ignore-submodules=all` passed.
- Targeted Debug build succeeded; both focused CTest targets passed (2/2). The repository `full_build.py` workflow also passed the full Debug build and all 116 CTest tests.
- Corrected replay against the preserved policy reported 39,742 hits, 8 actual missing infosets, zero missing buckets, and zero action-menu mismatches. Policy hash remained `11500758613148677101`; replay fingerprints both matched at `9162681264995464848`. The report captured all 8 distinct missing keys from trajectory 529 (lookup ordinals 21,492–21,499); every captured state ID reconstructed successfully and no private cards were included. Qualification correctly failed the zero-miss gate.
- The original adjacent `compact.cfg` was rejected because its discovery capacity exceeds the current 1,000,000-entry resource limit. Replay used the matching model-profile config at `tmp/pb12_phase9_3_1k_7e662d3c0c6f40029b3ee4cc9ec13ac0/compact.cfg`, whose capacity is valid.
- Final report: `tmp/pb12_lookup_diagnostics_20261028/lookup_report.json` (identical to `lookup_report_final.json`).

### Limitations

- The preserved replay package contains the policy and bucket artifacts but no original evaluator config or training checkpoint. The replay used a separate compact 1,000-trajectory config with the same model profile. No training checkpoint or admission catalog was found beside it, so true misses cannot be classified as unadmitted branches versus training-to-export identity problems.
