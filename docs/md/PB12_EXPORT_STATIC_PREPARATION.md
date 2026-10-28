# PB-12 export static preparation

Status: isolated restore path prepared and verified. A production checkpoint export was run against the isolated store and stopped before completion. The partial profile identifies record-store lookup as the bottleneck; production throughput and target equivalence remain unverified.

## Trace and measurement boundaries

The export plan precedes streaming. External streaming visits ordered row records and invokes a visitor for each shape. Each visitor looks up and validates its menu, pins each page once, averages and quantizes buckets in order, and passes each row to the writer. The writer validates ordering and counts, updates both hashes, encodes to a bounded buffer, and flushes it. Finish checks counts and size, flushes and closes, then publishes atomically. The ordered-record visit includes the visitor; subtract visitor time before interpreting traversal time.

Page pin includes the page-cache scan, victim selection, record lookup, optional checksum and decode or zero synthesis. Record lookup checks generations newest first and skips a run when the requested key is outside its sorted endpoint range. Each remaining run probe opens its index and performs binary-search seeks, then reads data for a matching key. Unpin performs another page-cache scan. Existing `disk_reads` counts candidate run probes, not physical reads; `read_nanoseconds` covers the miss path. Report counter deltas around streaming rather than restore totals.

The existing export duration includes planning and gates, so it is not a pure streaming timer. With profiling enabled, the workflow separately reports External checkpoint restore and session resume durations. Session construction occurs between those scopes and is not included in either. Successful timings are also logged before streaming; failed restores may not have a report. Separate inclusive pin, menu, ordered visit, and writer time from their children; do not add inclusive values together. Sampling is needed to split bucket averaging, quantization, validation, hashes, and encoding without timing each action. Record sample frequency, counts, overhead and residual. Count rows, actions, shapes, pages, cache probes, run probes, bytes encoded and bytes flushed separately.

`--profile-external-export` enables a fixed-size aggregate, charged with 4 KiB of additional stack allowance to export headroom. It reports inclusive ordered visit, shape visitor, menu lookup, pin, unpin and finish, plus counts. Separate catalog, row-index and page-store counters are snapshotted immediately around ordered streaming, excluding restore and planning. Their run-probe counts are not physical I/O counts; ordered row iteration does not increment record lookup counters. Every 256th row is sampled for average, quantization and inclusive writer time; sample values are not full totals. The writer separately samples row validation, payload hashing, policy hashing and encoding, restarting sampling at each shard. Sharded wrapper validation and its additional policy hash are outside these component samples. The encoder counts completed buffer flushes, including finish, but does not time them. Ordered visit minus visitor approximates traversal overhead, including timer and progress-report overhead; pin includes lookup and decode. Sampled encoding includes any flush on that particular row, so extrapolation can be biased. No clocks run inside action loops. Every 4,096 unpinned pages the CLI flushes a progress record to stderr with elapsed streaming time, pages, rows, actions, encoded bytes, bytes handed to the output stream and inclusive pin/unpin times. Stream-write bytes are not a physical disk or durability measure. The final streaming snapshot precedes the single-file checksum trailer; sharded snapshots include completed shard trailers but exclude the manifest. Checksum versus decode and flush duration remain combined or unmeasured. The optional Tracy client does not establish a bound or memory-ledger charge for its event buffers.

The page profile separates bucket location, page-cache search, victim selection, store lookup, checksum/decode with identity validation, and missing-page zero synthesis. These non-overlapping child scopes are included in pin time; the residual includes locking and other pin work. Decoded and synthesized counts cover cache misses only. Store lookup is inclusive and its existing miss timer is nested within it. For page-store run probes only, nested scopes separately time index open, index seek/read search, data open and data seek/read, with operation counts and requested byte totals. Requested bytes and C++ stream operations are not physical disk I/O. The catalog lookup is not broken down this way. The page breakdown and sampled writer/row aggregates are copied to progress records as well as the final report. Timer overhead is included in the enclosing scopes.

The restore path is not read-only: `restore_multiway_external_checkpoint` creates backups and publishes component manifests and generation pointers in the configured storage directory. Checkpoint capture also writes retention pins there. Its identity and checkpoint pins bind the storage path. Changing only the output directory does not protect the original store.

An isolated snapshot was created at `tmp/pb12_external_snapshot_13053527369d42cb8f38ca1bb7bc48f7/store`, with its hash manifest beside it. The copy contains 16,669 files and 47,203,419 bytes. Each copied file was compared with its source by length and SHA-256; the source training, catalog, row and page pointer hashes matched before and after copying. The snapshot CLI option redirects storage I/O to the copy while retaining the configured source path for checkpoint/model identity. It only accepts `--batches 0 --resume`, rejects source/runtime overlap and transaction, rollback, and temporary pin markers, and keeps restore/capture writes in the copy. The checkpoint resume regression also resumes from a copied test store and compares every source-store file before and after.

The isolated retry used the original checkpoint at `tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/external/checkpoints/latest.bin`, `--external-storage-snapshot-dir` with the verified copy, `--batches 0 --resume`, and a fresh unique output directory. It was stopped before completion at the user's request. Do not retry without further authorization. Any retry must preserve the memory, working-set, system reserve, disk, provenance and fail-closed gates and use a new unique output directory.

## Interrupted isolated production export

Run directory: `tmp/pb12_external_export_43c6672df6304e0c86f3fd87e8f6fe04`. The process started at 2026-10-27 09:50:06 UTC and ended at 12:00:43 UTC with exit code `-1` after the user requested a stop. The watchdog recorded `protected_hashes_unchanged: true`; its final report was empty. The partial `policy.mwfb.tmp` was 48,562,176 bytes and remains preserved.

The last flushed progress record, at 40,960 pages and about 2h06m37s of streaming, reported 613,611 rows, 1,618,613 actions, and 47,934,931 encoded bytes. It also recorded 334,554,606 page-store index opens, 627,401,739 index reads, and 7,594,407,430,400 ns in the inclusive page-store lookup timer out of 7,597,634,696,100 ns of streaming time. These are C++ stream-operation counters and nested timers, not physical I/O measurements. They identify repeated run-index lookup as the measured hot path in this incomplete attempt.

Retry integrity audit on 2026-10-27 rechecked all 16,669 source and snapshot files against the manifest: 47,203,419 bytes, zero missing files or hash/length mismatches. The original checkpoint is 200,160 bytes with SHA-256 `098F37CB91D202C46AE6A969FE54515FFF3FDFF211C3CFFF718F4F95569A5524`; `external.cfg` SHA-256 is `D23F10CB27287BE9BE110FEF4F24F8ED1BB8AE2709ECA793D991F9AAF02760B5`. No relevant solver process was active. The bucket input exists at `artifacts/f1_sizing_v1/buckets/buckets.bin` and is 15,369,522,466 bytes.

The first provenance query found that the configured build tree had cached an older source commit. It was reconfigured with vcpkg manifest installation disabled and `texas_multiway_train` rebuilt. Its current `--provenance` reports source commit `4f0874ee77384ab7c02e1ecb55adb85a05e79537`, matching `HEAD`, dirty state `dirty`, and Debug configuration. Use this rebuilt executable for any authorized retry.

## Static review

Reviewed the changed pin, record lookup, writer, telemetry, progress and snapshot-isolation paths against their immediate callers and failure handling. The profiled record lookup retains newest-first probes and throws on failed reads; page checksum and identity checks remain in place. The original unprofiled pin and writer paths remain separate. A failed writable pin releases its page, and the export loop still unpins on exceptions. Progress observers run after unpinning but while ordered traversal owns the row-store lock and must not re-enter storage. Review found missing snapshot rejection for rollback and temporary marker names; the guard now rejects the storage system's root and component transaction markers, including unfinished checkpoint-pin files. No remaining actionable findings.

The later record-store range-pruning change was reviewed separately. Append and compaction derive bounds from sorted records; manifest loading derives them from checksum-verified index endpoints. Newest-first lookup skips only runs whose bounds exclude the requested key. No actionable findings.

## Prepared regression and future validation

The new tiny test compares Compact and External policy content hashes, counts, size, serialized row order and quantized actions, and External repeatability. It compares profiled and unprofiled External policy hashes and counts, without comparing full payload bytes across different storage identities. A second fixture checkpoints and reopens the same External store, then compares profiled and unprofiled full-export bytes with identical identity. A writer-level fixture compares profiled and unprofiled bytes and covers injected write, close and publication failures. The existing External checkpoint resume fixture now enables profiling for its resumed and uninterrupted sharded exports and checks counters and hashes. Storage coverage includes full and partial page averages, including the pre-existing uncommitted zero-mass additions, plus new absent, cached and reopened page profile counts, newest-generation and cache lookup behavior, truncated index/data and corrupted page rejection, wrong cached identity rejection, and a failed writable-pin release check. Trainer-level export failure remains uncovered.

Verification completed on 2026-10-27:

- Focused Debug build passed for `test_multiway_external_storage`, `test_multiway_blueprint_training`, `test_multiway_qualification_reports` and `test_multiway_artifact`.
- Focused CTest passed 4/4.
- `texas_multiway_train` Debug build passed; `test_multiway_workflow_boundary` passed 1/1 after the snapshot guard update.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/python/full_build.py` passed; all 119 CTest tests passed.
- `git diff --check` passed; Git emitted only existing LF/CRLF conversion warnings.

The prepared validation commands were run against the configured Visual Studio 18 Debug tree:

```powershell
cmake --build build --config Debug --target test_multiway_external_storage test_multiway_blueprint_training test_multiway_qualification_reports test_multiway_artifact
ctest --test-dir build -C Debug --output-on-failure -R '^(test_multiway_external_storage|test_multiway_blueprint_training|test_multiway_qualification_reports|test_multiway_artifact)$'
cmake --build build --config Debug --target texas_multiway_train
ctest --test-dir build -C Debug --output-on-failure -R '^test_multiway_workflow_boundary$'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/python/full_build.py
cmake -S . -B build -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build --config Debug --target texas_multiway_train
build/Debug/texas_multiway_train.exe --provenance
```

An authorized production checkpoint invocation is now documented above, but did not complete. A future retry requires further authorization and must use the original checkpoint, the verified isolated store copy, `--batches 0 --resume`, and a fresh unique output directory. Preserve source generations, all gates, and prior outputs. Capture binary/config provenance, CPU and wall time, peak process memory and I/O, streaming counters, and completed throughput. Compare policy content hash `11500758613148677101`, 1,000 accepted and 0 discarded trajectories, 2,434,250 rows, 6,403,142 actions, and 189,818,882 bytes only after a successful complete export.

The bounded **fixture-only** runner targets `multiway_streaming_external_and_compact_policy_content_match`, which runs three small sessions with one four-trajectory batch each: Compact, External and profiled External. It passed previously in 666,469,100 ns. The latest run passed in 614,747,800 ns and again reported 1 row, 3 actions, 1 page, 261 encoded bytes, policy hash `4103060085492157644`, and zero page-store index opens. Both times include fixture setup and training. This tiny fixture did not exercise the production lookup bottleneck and provides no production throughput or improvement evidence. The first invocation selected a different no-training test; the selector was corrected for these runs. The command is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run/measure_pb12_export_fixture.ps1 -Executable build/Debug/test_multiway_blueprint_training.exe -BuildConfiguration Debug -MaximumSeconds 120
```

It creates a GUID-named directory under `tmp`, directs fixture temporary files there, records source revision, dirty state, binary SHA-256, sampled resource use, wall/CPU time, phase counters and the fixture hash. It enforces a timeout, 48 GiB working-set limit, 64 GiB private limit, and 1.5 GiB system-memory and disk reserves. Timeout enforcement has polling and telemetry-call slack. It does not use a checkpoint or the production store. The fixture creates three tiny deterministic sessions, each with one four-trajectory training batch, so its total process time is not export-only. Its sampled encoded-byte rate includes the header while the ordered-visit timer starts later; treat it as diagnostic, not a production throughput or before/after claim. The declared build configuration and binary/source correspondence must be independently verified. Short runs may finish before a process sample; I/O bytes are unavailable. Profiling and the progress observer perturb this tiny fixture.

The read-only postprocessor below is prepared but unrun. It summarizes a completed report and/or the final flushed progress record, retains the policy hash as exact decimal text, and includes available provenance and process metrics. It does not run an export, capture a watchdog, validate payload integrity, or establish policy equivalence. Missing metrics remain null. The streaming rate uses encoded bytes and the progress interval, not physical disk throughput.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/analyze/summarize_pb12_export.ps1 -ReportPath <fresh-report.json> -ProgressLogPath <fresh-stderr.log> -ProvenanceLogPath <fresh-report.json.log>
```
