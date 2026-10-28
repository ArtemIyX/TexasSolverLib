# PB-12 orphan-check lookup optimization

Date: 2026-10-27

## Change

External record-store orphan cleanup now binary-searches the already generation-sorted active runs instead of scanning all runs for each segment file. It still compares the exact expected path and retains the existing checkpoint-pin fallback and temporary-file rules. Manifest loading rejects unordered or duplicate generations; appends and compaction publish increasing generations. The change allocates no additional metadata and does not change export arithmetic or storage formats.

## Bounded synthetic measurement

The same Debug singleton fixture generated 8,321 runs with 7,016 distinct synthetic page keys and 1,584-byte payloads. The fixture, instrumentation and resource limits were identical; binaries differ only because of the candidate implementation. Each measurement was bounded to 120 seconds. Files were generated immediately before reopening, so they may have been cached.

| Metric | Before | After |
|---|---:|---:|
| Fixture file creation | 3.736 s | 3.696 s |
| Store reopen, including validation and cleanup | 86.321 s | 0.911 s |
| Filter construction | 0.778 s | 0.742 s |
| Whole process | 90.884 s | 5.394 s |
| Last sampled process CPU | 90.422 s | 5.172 s |
| Sampled peak working set | 14,528,512 B | 14,540,800 B |
| Sampled peak private memory | 9,994,240 B | 10,108,928 B |
| Samples | 1,021 | 55 |

Both runs completed without timeout, retained 8,321 records and rejected all 10,000 synthetic absent probes. The fixture manifest and first and last run files had identical SHA-256 hashes across the two runs. Stream operations are not physical I/O. Measurements include fixture creation and are not production throughput or completion-time estimates; sampled peaks may miss short-lived allocations.

Before: `tmp/pb12_export_fixture_c23960721cd342a4abfa10d3a28b55ea`. After: `tmp/pb12_export_fixture_5a01a4ddf7bb407c87476c9c4c251b59`. The prior filter implementation remains uncommitted; the fixture is linked against that working-tree implementation.

## Verification and limits

- Debug build of the affected test targets passed. Focused CTest passed 3/3, including checks for active runs, numeric filename aliases, stale runs, temporary files, and checkpoint-pinned files.
- The synthetic process and builds used 120-second and 900-second process-tree watchdogs respectively; no timeout occurred.
- Full serial Debug build passed. Full CTest initially passed 118/119: an artifact test could not open a temporary shard manifest under the long temporary path. That test passed with a shorter temporary path; a full retry with a shorter path passed 119/119. The precise cause of the first failure was not established.
- Static review found no actionable findings. Existing page checksums, disk reserves and snapshot handling were not relaxed.
- No production solver or checkpoint export was launched. Production snapshot integrity, completed export throughput and policy equivalence remain unverified. A future production attempt requires separate authorization and the existing preflight and watchdog gates.
