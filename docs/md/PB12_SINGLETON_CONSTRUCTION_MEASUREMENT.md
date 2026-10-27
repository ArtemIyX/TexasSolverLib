# PB-12 singleton-run filter construction measurement

Date: 2026-10-27

## Result

A bounded synthetic fixture reproduced the checkpoint's page-store shape: 8,321 singleton runs, 8,321 records, 7,016 distinct keys, and 1,584-byte payloads. The keys and payloads were synthetic; no production store or checkpoint was opened by the benchmark. The store loader validated the generated manifest and run checksums before filter construction.

The Debug measurement produced:

| Phase or metric | Result |
|---|---:|
| Generate fixture files and manifest | 3.736 s |
| Reopen store | 86.321 s |
| Construct 1 MiB filter | 0.778 s wall, 0.750 s CPU |
| Whole process, including 10,000 absent probes | 90.884 s wall; last sampled CPU 90.422 s |
| Synthetic data / indexes | 13,247,032 / 66,568 B |
| Sampled peak working set / private bytes | 14,528,512 / 9,994,240 B |
| Absent probes rejected by filter | 10,000 / 10,000 |

The 10,000 absent probes used synthetic IDs outside the stored-key range, so their false-positive rate is not representative of the allocated production page IDs. The separate read-only checkpoint simulation evaluated all allocated absent IDs and found zero positives for the inspected keys. The filter measurement includes checksum validation of each run, index scanning and allocation; stream opens are not physical I/O counts. Files were generated immediately before reopening and may have been cached. Memory peaks are sampled whole-process values, not filter-only peaks. The run had 1,021 samples and completed before the automatic 120-second process-tree timeout. The minimum sampled free physical memory and free disk space were 53,157,621,760 and 422,580,523,008 bytes; the 1.5 GiB reserve gates remained satisfied.

An earlier full-size run, without separate setup timers, took 90.753 s overall and 0.779 s for filter construction. Smaller singleton fixtures completed in 1.911 s overall / 0.089 s filter construction for 1,000 runs, and 12.936 s overall / 0.293 s filter construction for 3,000 runs. All used Debug builds and the same resource gates, but the full-size timed-breakdown run was rebuilt to add phase counters.

## Implication

Filter construction itself is small in this warm synthetic workload. Reopening dominates the observed process time. The existing store constructor's reopen phase includes manifest and checksum validation plus orphan cleanup; inspection shows that cleanup scans the active run list for each run file, producing quadratic comparisons as the number of runs grows. This measurement does not separately time those substeps or prove which one accounts for the 86.321 seconds.

The actual checkpoint has the same record and run counts, but different contents, file layout history and cache conditions. This is not a production runtime or throughput estimate. Production export remains incomplete and requires separate authorization; any future attempt must re-audit an isolated snapshot and retain the existing preflight and watchdog gates. The full policy hash, counts and size remain unverified.

## Reproduction and verification

- Built the standalone `pb12_filter_construction` Debug target against the current uncommitted filter implementation. Binary SHA-256: `AF6349F9C157AD27440E7A53EDDFD7B151D389F3B1B2C53C832835AD264391DE`.
- Ran `scripts/run/measure_pb12_filter_construction.ps1` with `-Records 8321 -Singleton -MaximumSeconds 120` and the Debug executable. A unique synthetic directory holds the generated files and diagnostics at `tmp/pb12_export_fixture_c23960721cd342a4abfa10d3a28b55ea`.
- A separate earlier 8,321-run observation is at `tmp/pb12_export_fixture_7d213f8c99344fcdbb7de9f5ece82dba`; 1,000- and 3,000-run observations are at `tmp/pb12_export_fixture_6dd7a8a8f8c9453c8803eee87b768ae8` and `tmp/pb12_export_fixture_1fc3379da07345589a325452cba7d085`.
- The standalone build had a 900-second process-tree timeout. All synthetic measurements had 120-second timeouts and completed; no timeout or production solver run occurred.
