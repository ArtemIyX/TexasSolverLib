# PB-12 External Export Performance Handoff

Date: 2026-10-27

## Purpose

Investigate whether External policy export can be improved with concurrency and Tracy captures. Do not infer a speedup from the current tests or the tiny fixture. Do not restart the production checkpoint export without further user authorization.

## Repository state

- Worktree: `C:\Users\xeuse\RiderProjects\TexasSolver`
- Branch: `PB-12`
- HEAD: `4f0874ee77384ab7c02e1ecb55adb85a05e79537`
- CMake generator: Visual Studio 18 2026; multi-configuration build; tested configuration: Debug.
- There are pre-existing uncommitted changes across PB-12 profiling, snapshot isolation, reports, tests and docs. Preserve them. Nothing was staged or committed.
- The production export process was stopped at the user's request; no `texas_multiway_train.exe` process remained afterward.

## What happened

An isolated checkpoint-only export was launched with `--batches 0 --resume`, a new output directory, and `--external-storage-snapshot-dir`. An initial CLI attempt incorrectly used a `train` subcommand and failed before checkpoint access. The corrected invocation passed preflight and began streaming. The user stopped it after about 2h10m.

Run directory: `tmp/pb12_external_export_43c6672df6304e0c86f3fd87e8f6fe04`.

The corrected executable invocation was:

```powershell
build/Debug/texas_multiway_train.exe --config tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/external.cfg --input artifacts/f1_sizing_v1/buckets/buckets.bin --output tmp/pb12_external_export_43c6672df6304e0c86f3fd87e8f6fe04/policy.mwfb --report tmp/pb12_external_export_43c6672df6304e0c86f3fd87e8f6fe04/report.json --checkpoint-dir tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/external/checkpoints --resume tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/external/checkpoints/latest.bin --external-storage-snapshot-dir tmp/pb12_external_snapshot_13053527369d42cb8f38ca1bb7bc48f7/store --batches 0 --profile-external-export --progress true --progress-memory true --progress-speed true --no-log
```

- Process start: `2026-10-27T09:50:06.5029673Z`; end: `2026-10-27T12:00:43.1367368Z`; exit code `-1`.
- `run_summary.json` reports `protected_hashes_unchanged: true`; final report is `{}`.
- The preserved partial `policy.mwfb.tmp` is 48,562,176 bytes. Do not treat it as a valid policy or overwrite/delete it.
- Last flushed profile: 40,960 pages, 613,611 rows, 1,618,613 actions and 47,934,931 encoded bytes at about 2h06m37s streaming time.
- That profile recorded 334,554,606 page-store index opens, 627,401,739 index reads, and 1,839 data reads. Inclusive page-store lookup time was 7,594,407,430,400 ns of 7,597,634,696,100 ns elapsed streaming time. Index open and binary-search timing were 3,654,265,916,700 ns and 2,467,835,855,000 ns respectively. Nested timings overlap; stream operations are not physical I/O counts.
- The capture indicates repeated record-run lookup dominates this partial attempt. It does not establish completed production throughput or output equivalence.
- The target output is 189,818,882 serialized bytes. Required completion values are policy content hash `11500758613148677101`, 1,000 accepted trajectories, 0 discarded trajectories, 2,434,250 rows and 6,403,142 actions.

The source checkpoint is `tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/external/checkpoints/latest.bin` (expected SHA-256 `098F37CB91D202C46AE6A969FE54515FFF3FDFF211C3CFFF718F4F95569A5524`). The verified snapshot was `tmp/pb12_external_snapshot_13053527369d42cb8f38ca1bb7bc48f7/store`. Restore/capture code can write manifests and retention pins. Do not assume that snapshot remains pristine after the stopped run; re-audit or create a new verified snapshot before any future retry. Keep the original checkpoint, store generations, configs, logs and partial outputs unchanged.

## Current change and verification

The measured lookup path was changed to retain each sorted run's minimum/maximum key, skip runs whose range excludes the requested key, and preserve newest-generation lookup order. Appends and compaction assign bounds; manifest load derives them from checksum-verified index endpoints. Tests cover range skipping after reopen and lookup precedence. This change is validated for correctness but has not been measured on a complete production export; it may still scan all runs in memory, and overlapping ranges may yield few skips.

The latest bounded fixture runner passed in 614,747,800 ns, but its profiled export had one row, one page and zero page-store index opens. It does not exercise this bottleneck and is not comparable production throughput.

Commands and results:

```powershell
cmake --build build --config Debug --target test_multiway_external_storage test_multiway_blueprint_training test_multiway_qualification_reports test_multiway_artifact
ctest --test-dir build -C Debug --output-on-failure -R '^(test_multiway_external_storage|test_multiway_blueprint_training|test_multiway_qualification_reports|test_multiway_artifact)$'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/python/full_build.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run/measure_pb12_export_fixture.ps1 -Executable build/Debug/test_multiway_blueprint_training.exe -BuildConfiguration Debug -MaximumSeconds 120
```

The focused tests passed 4/4. The full Debug build and CTest passed 119/119. The fixture passed but did not exercise page-store reads. `git diff --check` passed with only LF/CRLF conversion warnings. Static review of the range-pruning change found no actionable findings. No production performance improvement or policy equivalence has been demonstrated.

## Concurrency constraints to investigate

- `MultiwayExternalRecordStore::lookup_profiled` and `lookup` hold the record-store mutex across cache handling and the full newest-to-oldest run scan, including index opens, binary-search reads and matching data reads (`src/solver/multiway/engine/multiway_external_storage.cpp`, around lines 2063-2240).
- `MultiwayExternalCompactPages::pin_profiled` holds the page-cache mutex while it calls the record store (`.../multiway_external_storage.cpp`, around lines 3610-3680). Simply adding page workers is likely to serialize on these locks.
- Ordered export visits rows in key order and feeds the writer/hash path serially. Any parallel read/decode design must retain deterministic row ordering, exception-safe unpinning, cache identity/checksum validation, bounded memory, and existing memory/disk reserve and fail-closed gates.
- Consider measurement-first options: quantify range skips and run fan-out; investigate immutable lookup metadata or read-ahead; then test worker-local readers/page buffers with an ordered writer commit. Do not parallelize shared mutable storage by removing locks without a clear ownership and merge design.

## Tracy state and next investigation

The configured `build` tree has `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=OFF`, `TEXASSOLVER_TRACY_ON_DEMAND=ON`, and Tracy sampling, call stacks, lock instrumentation and memory events all `OFF`. `CMakeLists.txt` defines a `texas_multiway_train_tracy` diagnostic target when Tracy diagnostics are enabled. Check existing local dependency availability before changing configuration; do not install dependencies.

The current `--profile-external-export` counters are separate from Tracy. Progress records are emitted every 4,096 unpinned pages. Tracy client event buffers are not currently established as bounded or charged to the solver memory ledger (`docs/md/PB12_EXPORT_STATIC_PREPARATION.md`). A capture must therefore keep the existing memory gates and quantify profiler overhead. Prefer on-demand capture around a bounded representative workload first; do not assume a tiny fixture will reproduce the production run fan-out.

Before any new production run:

1. Reconfirm exact binary/config/checkpoint provenance and source/checkpoint hashes.
2. Verify a fresh isolated snapshot or a genuinely non-mutating restore path, including pointer and pin-marker checks. Never point the run at the original store for writes.
3. Preserve the working-set, private-memory, system-memory, disk reserve and fail-closed watchdog limits.
4. Use `--batches 0 --resume` and a fresh unique output directory. Require explicit user authorization before launch.
5. Keep throughput claims conditional on a fully completed export matching every target hash, count and byte total above.
