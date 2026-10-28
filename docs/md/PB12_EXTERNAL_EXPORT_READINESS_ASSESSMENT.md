# PB-12 external export readiness assessment

Date: 2026-10-27

**Status: not ready for a production attempt.** A bounded filter has been implemented, but production-sized construction cost and filter saturation remain unverified. No production solver or checkpoint export was run.

## Implementation

The export session builds a 1 MiB, two-probe membership filter from active page-run indexes after validating each run's lengths and checksum and its index ordering. Negative results synthesize missing pages; positive results retain the existing newest-first lookup, data read, page checksum and identity checks. The filter is released by an export-scope guard and record-store mutation is rejected while it exists. Export preflight charges 1 MiB plus 128 KiB construction headroom before allocation. It adds no workers or reorder queue. The ordered writer and arithmetic remain unchanged. Existing storage and disk gates remain in force. This design assumes the active run files remain immutable throughout the export lease, consistent with the existing storage contract.

## Measurements

The Debug page-store fixture creates 24 overlapping three-page runs, reopens storage and checks oldest and newest generation hits. Both measurements use the same binary, instrumentation and 50,002 deterministic nonmonotonic page requests. Setup and filter construction are included in whole-process elapsed time. Limits were 120 seconds per measurement and resource gates; no timeout occurred.

| Metric | Filter off | Filter on |
|---|---:|---:|
| Wall time | 17.231 s | 0.235 s |
| Last sampled CPU time | 17.078 s | 0.172 s |
| Sampled peak working set | 5,996,544 B | 7,016,448 B |
| Sampled peak private bytes | 1,126,400 B | 2,125,824 B |
| Samples | 190 | 2 |
| Index opens / reads | 922,459 / 2,767,377 | 2,923 / 8,769 |
| Range skips | 272,548 | 2,572 |
| Filter negatives | 0 | 49,563 |
| Data reads / zero pages | 439 / 49,563 | 439 / 49,563 |
| Filter construction | none | 2.52 ms |

Stream operations are not physical I/O. CPU is the last sampled value, not necessarily the process total; memory peaks are sampled and the candidate has only two samples. This is a deliberately sparse page-store fixture, not a production throughput estimate. Full run checksum validation rereads the page data during filter construction; its cost on large stores is unknown. The fixed filter can saturate, leaving positive probes and little benefit.

Saved measurements: `tmp/pb12_export_fixture_138ea6a4ad23415fbefe72562f9ac2e4/summary.json` and `tmp/pb12_export_fixture_fb56db33034d498b817b08b4d53c8a29/summary.json`.

## End-to-end synthetic export

The separate Debug export fixture builds twelve overlapping two-page update batches, reopens the storage, and exports 1,538 rows across 98 pages with nonmonotonic page IDs. Two separate processes used the same binary and instrumentation. Whole-process time includes fixture setup, checkpoint creation and reopening, not only export. The artifacts have identical identity and were compared byte by byte.

| Metric | Filter off | Filter on |
|---|---:|---:|
| Whole-process wall time | 1.221 s | 1.162 s |
| Export wall time, including filter construction | 4.141 ms | 3.425 ms |
| Last sampled CPU time | 0.172 s | 0.281 s |
| Sampled peak working set | 9,658,368 B | 9,703,424 B |
| Sampled peak private bytes | 2,412,544 B | 2,420,736 B |
| Samples | 12 | 12 |
| Index opens / reads | 65 / 317 | 13 / 57 |
| Range skips / filter negatives | 226 / 0 | 23 / 85 |
| Data reads / zero pages | 13 / 85 | 13 / 85 |

Both outputs had 4,614 actions, policy content hash 13259330168370456785 and 130,914 serialized bytes. Byte equality covers row content, ordering and quantized actions for this identical-identity fixture. The filter's short allocation may fall between memory samples; do not treat those peaks as proof of its maximum. The same-identity artifacts and summaries are in `tmp/pb12_export_fixture_ba53e59a4fc7410794b3e75fb0d65c5a` and `tmp/pb12_export_fixture_9827714d946145d1b60b7ad720447f4e`. This fixture is much smaller than production and cannot predict its speedup or completion time.

## Verification and remaining uncertainty

- Serial Debug full build via `compact_build.py --parallel 1` passed. The earlier eight-job attempt failed with Windows error 32 during vcpkg DLL deployment; serial retry passed. Logs were written to unique temporary directories to preserve existing artifacts.
- Full Debug CTest: 119/119 passed. The focused suites also passed 3/3. Build and test commands had 900-second process-tree watchdogs; no timeout occurred.
- The fixtures cover sparse and overlapping ranges, oldest/newest hits, reopening, nonmonotonic requests, missing-page synthesis and same-identity byte equality. An active-run corruption rejects export before publication; after repair, export succeeds with identical bytes. Mutation is rejected during a filter session.
- Static review using the project C++ review skill found no actionable findings. `git diff --check` passed with line-ending warnings. Tracy was not used.
- At production scale the active run count, page-data validation cost, filter saturation, process peak memory and achievable throughput remain unknown. An interrupted historical profile is not proof of a completed export or policy equivalence. The 1 MiB filter is bounded, but its usefulness may decline as distinct stored keys increase. Do not infer production speedup from the synthetic fixtures.

## Conditional production watchdog

Any later attempt requires separate authorization. First verify binary/config/checkpoint provenance and a fresh isolated snapshot, preserve the existing preflight and 100 GiB configured disk gate, and use a fresh output directory. A proposed initial watchdog envelope is a 30-minute hard wall limit, 48 GiB working set, 64 GiB private commit, 1.5 GiB minimum available physical memory and disk reserve, process-tree termination and failure on unavailable telemetry. The wall limit is an assessment bound, not a completion prediction; stricter configured gates remain in force. Require a complete export matching policy content hash 11500758613148677101, accepted/discarded 1000/0, 2434250 rows, 6403142 actions and 189818882 bytes before claiming equivalence.
