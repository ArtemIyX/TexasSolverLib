# PB-12 External policy export investigation and fix plan

Date: 2026-10-27

Status: Planning only. The actual bottleneck and External/Compact equivalence remain unverified. No implementation, build, test, profiling run, export retry, or commit was performed while preparing this plan.

## 1. Scope and completion criteria

Continue on branch `PB-12`, starting from `4f0874ee`. Do not merge into main. First measure where export time goes; implement only a fix supported by those measurements.

Every production-checkpoint retry must use `--batches 0 --resume` with:

`tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/external/checkpoints/latest.bin`

Use a fresh GUID-based output directory for every invocation, including diagnostics. Preserve all existing checkpoints, store generations, logs, and partial outputs. Never resume writing the old `.tmp` policy.

A successful final External report must match all these Compact values exactly:

| Field | Required value |
| --- | ---: |
| Policy content hash | 11500758613148677101 |
| Accepted trajectories | 1,000 |
| Discarded trajectories | 0 |
| Export rows | 2,434,250 |
| Export actions | 6,403,142 |
| Serialized bytes | 189,818,882 |

Reference: `tmp/pb12_catalog_chunk_1k_d75ed43eeea34a4093df37e2271bce82/compact/report.json`.

Do not require identical full payload hashes across backend identities. Policy content identity and full artifact payload identity are separate contracts. Validate each artifact's own payload integrity.

## 2. Evidence already available

1. The preserved status report records 28,180,480 bytes written over 4,439 seconds, approximately 6,348 bytes/s and 14.85% of the expected final size. The partial file still has that length.
2. Observed working set peaked near 21.4 GiB and later settled near 14.4 GiB. This does not identify CPU, storage, or paging costs.
3. The Compact report records `export_wall_nanoseconds=3856066500` and `export_duration_nanoseconds=3570386400`. These are separate existing timer scopes, approximately 3.856 s and 3.570 s. A partial External observation is not directly comparable to completed Compact throughput.
4. `save_full_policy_atomic` already pins each External page once and visits its buckets in ascending order. The page-reuse change has a full/partial-page average comparison test. The status report says the prior focused test and full build passed, with 119 CTest tests. These results were not rerun for this plan.
5. The preserved attempt log stops at checkpoint loading and contains no export-phase timing breakdown. The partial policy proves writing progressed, but the log cannot attribute elapsed time.
6. Its binary provenance reports source commit `fe1f55679931820ff7da6c2ec5250c38add29151`, dirty state, Debug configuration, and unknown run checkout identity. This does not establish that the binary was freshly built from `4f0874ee`; resolve this before a new comparison.
7. Read-only branch and revision queries returned `PB-12` and `4f0874ee`. Git status failed in this environment, so a clean working tree was not independently confirmed. Do not change Git configuration to bypass that problem; diagnose repository/worktree access first.

Evidence files:

- `tmp/pb12_external_export_pagepin_d3b266eb3a03424d874123196f78ff11/export_status_report.md`
- `tmp/pb12_external_export_pagepin_d3b266eb3a03424d874123196f78ff11/report.json.log`
- `tmp/pb12_external_export_pagepin_d3b266eb3a03424d874123196f78ff11/blueprint.bin.tmp`

## 3. Source inspection findings, not measured root causes

| Component | Current behavior | Measurement needed |
| --- | --- | --- |
| Trainer export | Ordered row bindings, menu lookup once per shape, pin once per page, average/quantize/write once per bucket | Shape/page/row counts and phase times |
| Compact page cache | `MultiwayExternalCompactPages::find` linearly scans page slots; pin and unpin call it | Slot probes and exclusive search time |
| Record lookup | Cache misses visit runs newest-first; `lookup_run` opens the index, seeks during binary search, then opens data for a match | Runs probed, opens, seeks, reads, bytes, elapsed time |
| Missing page | A missing stored page is synthesized as zero cells by `pin` | Absent versus present pages and cost of negative searches |
| Catalog menu | `lookup_menu` fetches a record, checks menu checksum, and decodes actions | Lookup I/O versus validation/decode time |
| Ordered row traversal | `visit_ordered_records` opens bounded run cursors and merges keys in order | Exclusive cursor/merge time, excluding visitor work |
| Average | Integer mass total and the existing scaled double arithmetic | Exclusive average time and bitwise comparison |
| Writer | Validates row/order, updates payload and policy hashes, and encodes through a bounded buffer | Validation, hashes, encoding, actual flush/write, final publication |

Primary source locations:

- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`: `save_full_policy_atomic` near line 1109; External visitor near line 1355.
- `src/solver/multiway/engine/multiway_external_storage.cpp`: `lookup_unlocked` near 2149; `lookup_run` near 2177; `lookup_menu` near 2823; ordered rows near 3098; page `find` near 3257; `pin` near 3381; `pin_bucket` near 3646; pinned average near 3703.
- `src/solver/multiway/blueprint/multiway_artifact.cpp`: `BufferedEncoder` near 409; writer implementation near 824; `write_row` near 934; `finish` near 963.
- `include/solver/multiway/engine/multiway_external_storage.hpp`: existing storage statistics and bounded cache APIs.

The existing `disk_reads` counter increments once per run probe, not once per physical disk operation. `read_nanoseconds` covers the miss path, not all lookup costs. Neither alone proves physical storage is the bottleneck. Sparse zero pages, cache scans, repeated file opens, checksum work, and Debug overhead are hypotheses until measured.

## 4. Step-by-step execution

### Step 1: Establish a trustworthy baseline and preserve inputs

1. Read `AGENTS.md`, `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`, and the requested project skills. Use `apply_patch` for all edits. Run Python only through `scripts/codex_powershell.ps1`.
2. Resolve the read-only Git status failure. Confirm branch, HEAD, staged diff, and unstaged diff without resetting, cleaning, or rewriting anything. Preserve unrelated changes.
3. Inventory source checkpoint, referenced store manifests/generations, config, bucket artifact, reference report, and preserved attempt. Record lengths, timestamps, identities, and existing hashes. Do not duplicate the entire store or rehash large artifacts casually; retain mandatory integrity verification at restore.
4. Establish the restore path's mutation boundaries. It must read the original checkpoint/generation and place new checkpoint/output files only in the fresh attempt directory. Record source pointers before and after to detect unexpected mutation.
5. Recover the exact prior invocation and effective configuration from preserved logs and workflow code. The existing `external.cfg` contains a 64 GiB process limit, 256 MiB row-page cache, fan-in 4, synchronous durability, and 1.5 GiB disk reserve. Do not change these to improve the measurement.
6. Confirm all effective gates, including the 48 GiB working-set limit, system reserve, private-commit/process checks, disk requirements, and provenance. Record exact executable path, configuration, compiler, source revision, and dirty state. Rebuild through the prescribed workflow before treating a stale binary as current.

Deliverable: a small baseline/provenance manifest in a new diagnostic directory, with no training performed.

### Step 2: Add bounded observability without changing export results

1. Separate bucket loading, session construction, checkpoint verification/restore, checkpoint output, export planning/gates, streaming, finish/publication, and report publication. Record wall-clock and process CPU time where available.
2. Add opt-in aggregate profiling at export, shape, page, and buffered-write boundaries. No text formatting, logging, allocations, hash-map lookups, or per-action clocks inside numeric loops.
3. Snapshot storage counters separately for catalog, row index, and page store before and after streaming. Avoid combining restore statistics with export statistics.
4. Record menu lookup/validation; cache search; page location; lock wait if relevant; store lookup; file open/search/read; page checksum/decode; zero-page synthesis; average; quantization; writer validation; payload hashing; policy hashing; encoding; flush; close; atomic publication.
5. Use nested timing scopes or explicit exclusive durations so page I/O is not counted again as independent pin cost. Check that measured exclusive time plus residual explains streaming wall time.
6. Start with a short sampling trace of the actual exporter if supported. Use targeted diagnostic instrumentation only where sampling cannot distinguish the categories. If fine bucket-level timing is needed, sample deterministically and record sample counts and estimated overhead. Do not present sampled estimates as complete exclusive totals.
7. Emit compact periodic snapshots outside the hot loops so evidence survives interruption before `report.json`. Report elapsed streaming time, rows, actions, logical bytes, physically flushed bytes, pages, memory, and counter deltas. State timer boundaries and buffer lag explicitly.
8. Measure profiler overhead on small deterministic export fixtures with profiling enabled and disabled. Keep telemetry scratch bounded and charged to the memory ledger; preserve report/schema compatibility.

Deliverable: a phase/counter report capable of distinguishing CPU work from I/O waits, without changing row bytes or hashes.

### Step 3: Establish a credible improvement before another full retry

1. Construct bounded synthetic export fixtures, with no training batches, covering the suspected dominant operation: present/missing pages, cache hits/misses, multiple runs, varied bucket counts, and out-of-page-ID export order.
2. Profile the current path and a candidate fix on identical fixtures and build configuration. Use operation counts and measured elapsed/CPU time; do not infer improvement from reduced pin calls alone.
3. Require repeatable reduction in the dominant measured cost, identical output, and bounded memory/handles before a production-checkpoint retry. Record cold/warm cache conditions honestly; do not forcibly evict system caches or disable protection software.
4. If fixtures do not explain the real slowdown, use one bounded diagnostic checkpoint invocation with `--batches 0 --resume`, a unique output directory, existing gates, and an explicit duration/stop condition. Keep it distinct from a full equivalence attempt. Preserve logs and any partial output on interruption. A diagnostic invocation must not be represented as an improved or completed export.
5. Write a short root-cause statement naming the operation, call count, time share, and reason it scales poorly. If a large unexplained residual remains, continue measuring rather than selecting a speculative fix.

Deliverable: before/after evidence supporting the chosen change, or a documented unresolved bottleneck.

### Step 4: Implement only the fix justified by Step 3

Choose the applicable branch below; do not implement every option.

1. If repeated open/search/read dominates, add a bounded read session that holds verified run handles and reuses scratch under a stable generation/export lease. Keep random seeks safe under existing locking. Bound handles by effective run count and budget all buffers. Invalidate/rebind on generation changes; never reuse stale handles after publication, rollback, compaction, or restore.
2. If negative page lookups dominate, consider a verified bounded presence/index view or run-range rejection that avoids unnecessary probes. Missing pages may become zero only after a valid not-found result under current semantics. Truncation, checksum failure, and identity errors must still fail closed. Negative caches require generation-aware invalidation.
3. If page-cache scans dominate, use a preallocated numeric page-ID-to-slot structure or an export-only bounded read path. Budget metadata within the existing cache limit. Preserve pin counts, dirty-page eviction rules, victim selection validity, rollback, and synchronization. Do not introduce an allocating hash map in per-bucket work.
4. If catalog lookup dominates, consider bounded reusable record buffers or a verified catalog/row merge only after proving compatible ordering. Public-state export order does not imply monotonic page-ID order. Do not reorder policy output to simplify reads.
5. If checksum/decode dominates, avoid redundant work only when verification is tied to immutable bytes in the current verified generation. Do not remove mandatory validation or trust merely matching identifiers.
6. If averaging/quantization dominates, remove measured overhead while preserving the exact existing floating-point operation order, integer totals, uniform fallback, action order, and rounding. Algebraically equivalent arithmetic is insufficient for bitwise identity.
7. If writer hashing/serialization/write dominates, optimize the measured stage with bounded buffers and identical byte/hash order. Keep both hash contracts, strict row ordering, count validation, write failure handling, close checks, and atomic publication. Do not solve I/O latency by bypassing durability or resource checks.
8. Keep page ownership exception-safe on every success, failure, and early exit. Review whether the existing try/catch unpin structure is sufficient; do not assume the movable pinned object automatically unpins on destruction.

Expected change surfaces: storage header/implementation for a measured storage fix; trainer for export integration and telemetry; artifact writer only if measured; workflow/report definitions only if new diagnostic fields require them. Avoid unrelated training changes.

### Step 5: Add deterministic regression coverage

1. Extend `tests/test_multiway_external_storage.cpp` for the chosen optimized API. Retain `multiway_external_pinned_page_average_matches_bucket_order` and cover full/partial pages, multiple actions, nonuniform and zero masses, absent pages, cache collisions/eviction, and repeated reads.
2. Exercise multiple run generations and duplicate keys so newest committed values win. Cover reopen/restore and generation changes for any new reader/cache. Cover corruption/truncation and wrong identity with rejection rather than uniform fallback.
3. Test injected failures after a successful pin and during writing. Prove pins and locks are released, state remains usable under its documented contract, and no incomplete final artifact is published.
4. Extend `tests/test_multiway_blueprint_training.cpp` with tiny equivalent Compact and External export fixtures, explicitly reopening/restoring External storage. Compare averages bitwise where required, quantized row contents, strict row order, policy content hash, row/action counts, serialized size, and repeatability. Use byte equality only when identity metadata is intentionally identical.
5. Preserve existing checkpoint/resume, gate rejection, streaming, and sharded-output coverage. Add a narrow test for any newly affected contract. Test memory/handle bounds with deterministic counters, not wall-clock thresholds.
6. Verify profiling enabled/disabled yields identical policy results. Do not put long checkpoint exports or performance thresholds into unit tests.

Deliverable: small regression cases that fail on the measured defect and prove optimized-path equivalence.

### Step 6: Review and validate before the full checkpoint retry

1. Review the scoped diff using `cpp-code-review`. Check arithmetic, lifetimes, exception paths, locking/reentrancy, immutable generation assumptions, resource accounting, deterministic ordering, and unchanged failure gates.
2. Build and run focused storage and blueprint tests using repository helpers and the actual configured build directory/configuration. Include checkpoint equivalence tests if restore/reader contracts changed.
3. Run the required full validation exactly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/python/full_build.py
```

4. Inspect the compact summary and failures rather than dumping all output. Record actual command, build provenance, test totals, exit code, and log location. Resolve relevant failures before proceeding.
5. Repeat the bounded before/after export measurement after the final fix. Confirm the improvement persists without diagnostic overhead and record its limits. Do not switch Debug/Release between baseline and candidate and attribute the difference solely to code.

### Step 7: Run the checkpoint-only External export safely

1. Create a new `tmp/pb12_external_export_verified_<GUID>/` directory. Resolve all inputs and outputs to absolute paths and verify none aliases preserved artifacts.
2. Use the freshly verified workflow executable and the unchanged source config/bucket artifact. The invocation must include `--batches 0`, `--resume` pointing to the original `latest.bin`, and fresh `--output`, `--report`, and `--checkpoint-dir` paths. Recover executable name and remaining original flags from the actual workflow/build; do not guess a runnable command.
3. Preserve 64 GiB process, 48 GiB working-set, system reserve, private-commit, disk, provenance, and fail-closed guards. Include all new reader buffers, cache metadata, handles, and any output overlap in preflight accounting.
4. Record invocation, source/binary/config identities, start/end UTC, monotonic durations, CPU time, peak memory, I/O metrics, and exit status. Distinguish total restore-to-report duration from streaming and final publication duration.
5. Poll progress no more noisily than needed. Keep the existing resource watchdog and stop safely on its rejection. If throughput remains near the failed baseline or evidence invalidates the chosen cause, stop under the documented diagnostic bound and preserve the attempt instead of leaving an unexplained multi-hour retry.
6. Record real final bytes/streaming seconds and bytes/total invocation seconds separately. Compare against the prior partial rate with that limitation stated. Do not extrapolate to 50M.

### Step 8: Prove equivalence from completed artifacts

1. Require successful process exit, atomically published final policy and `report.json`, and `export_succeeded=true`. A growing `.tmp` or matching partial prefix is insufficient.
2. Parse hashes and counts as exact integers. In PowerShell use `ConvertFrom-Json` and explicit integer types; avoid a comparison path that converts 64-bit hashes through floating point.
3. Compare every value in Section 1, and confirm trajectory state did not advance during the zero-batch invocation. Record observed and expected values in a comparison table with individual pass/fail results.
4. Validate the final artifact with the existing bounded reader/inspection path, including payload integrity, ordering, and counts. Do not materialize the entire policy merely for verification if a bounded reader suffices. If an independent streamed policy-content recomputation is available, record that result too.
5. Confirm the original checkpoint, generation pointers, and preserved partial output remain unchanged. Record any permitted new attempt files separately.
6. If any field is missing or differs, leave equivalence unverified, preserve all evidence, and investigate the first mismatch. Do not change config or expected values to make the comparison pass.

### Step 9: Document and commit the verified result

1. Write a concise attempt report with the measured root cause, phase/counter evidence, before/after timings, real throughput, peak resources, provenance, final hash/count table, and limitations.
2. After implementation and validation are complete, update `docs/PLURIBUS_LOG.md` using `update-project-log`. Separate new measured results from historical interrupted attempts. Do not record this plan as a completed export fix.
3. Review staged and unstaged diffs using `git-commit-hygiene`; stage only scoped code, tests, and documentation. Do not stage policy artifacts, temporary files, source checkpoints, or unrelated work.
4. Commit on `PB-12` with an accurate subject such as `PB-12 Multiway: Fix <measured export bottleneck>`. Record the actual commit hash and validation. Do not merge or rewrite history.
5. If a safe completed export is not achieved, document the evidence and explicitly state that equivalence remains unverified. Never describe an uncompleted retry or an unmeasured optimization as the verified fix.

## 5. Current limitations

- This document is based on read-only code, metadata, and existing-report inspection. No profiler measurements were collected during planning.
- The source shows plausible expensive operations, but their contribution to the 4,439-second attempt has not been measured.
- Existing binary provenance and unavailable Git status need resolution before reliable performance attribution.
- No External final hash or counts exist for the preserved attempt. Equivalence remains unverified.
