# PB-12 filter construction measurement

Date: 2026-10-27

## Scope

Measured the current uncommitted export-filter implementation on synthetic record stores. The standalone Windows harness depends on the existing working-tree `begin_export_filter` API and Debug libraries; that prerequisite is not part of this measurement commit. The harness does not load a production checkpoint or run a solver.

Each store contains fixed-width 1,584-byte payloads, matching the size of a 16-bucket, eight-action page record. It writes 1,000 records per sorted run, closes and reopens the store, then times one `begin_export_filter()` call. Construction allocates the 1 MiB filter, checks active run lengths and checksums over indexes and data, and scans indexes into the filter. It then checks two present keys and 10,000 deterministic absent queries. At 5,000 records those absent queries repeat; the one-million-record case has 10,000 distinct queries. The page-shaped payloads are synthetic bytes, not valid encoded pages.

Debug measurements used the same executable (SHA-256 `F5C755B6DB8C6F8D8EAFCAB89B3F8AD6D153C30F402447A4000D0C64E6E4B8B8`), same instrumentation and 120-second process-tree watchdog per run. Construction CPU comes from Windows process kernel plus user time; wall time comes from `steady_clock`. CPU clock granularity is about 15.6 ms. Peak process memory was sampled over the whole process, including fixture creation and reopening, not just filter construction.

| Records / runs | Data + index bytes | Build wall | Build CPU | False-positive absent queries | Sampled peak working set / private |
|---|---:|---:|---:|---:|---:|
| 5,000 / 5 | 8,000,000 | 19.264 ms | 15.625 ms | 0 / 10,000 | 7,442,432 / 2,998,272 B (1 sample) |
| 25,000 / 25 | 40,000,000 | 97.139 ms | 109.375 ms | 0 / 10,000 | 7,495,680 / 3,272,704 B (6 samples) |
| 100,000 / 100 | 160,000,000 | 390.615 ms | 390.625 ms | 4 / 10,000 | 7,962,624 / 3,837,952 B (24 samples) |
| 1,000,000 / 1,000 | 1,600,000,000 | 3.957 s | 3.953 s | 494 / 10,000 | 10,641,408 / 6,111,232 B (270 samples) |

The one-million-key run recorded 494 index opens and 5,422 index reads for those absent queries after filter construction. These are stream operations, not physical disk I/O. The filter returned no false negatives for the two present keys checked in each run. The observed 4.94% false-positive rate is for this deterministic key distribution and is not a general rate guarantee.

Whole-process wall times were 0.148, 0.623, 2.356 and 25.919 seconds, respectively. The files were written and read in the same process shortly before the timed construction, so filesystem cache likely influenced the result. No cold-cache measurement was attempted. One observation per size does not establish variance; do not extrapolate these numbers to production storage, run overlap, I/O load, or completion time. Sampled peaks can miss short-lived allocations. No timeout occurred. Minimum sampled available memory and disk remained above the configured 1.5 GiB reserves.

The preserved summaries and synthetic data are under:

- `tmp/pb12_export_fixture_717bf3e122c046e7a4f3ea34eed5d963`
- `tmp/pb12_export_fixture_4e144065784c4c04977d967ed856a27d`
- `tmp/pb12_export_fixture_c4892bc6e96d4d10b7b69aa55bdca746`
- `tmp/pb12_export_fixture_4e8e813b0f354ebdaf28f02a89d57c4a`

## Reproduction

The standalone CMake project links the current Debug `texas.lib` and `pheval.lib`; build the working-tree filter implementation first. Configure and build `scripts/benchmark/pb12_filter_construction` in a fresh directory. Invoke `scripts/run/measure_pb12_filter_construction.ps1` with the resulting executable, `-BuildConfiguration Debug`, `-MaximumSeconds 120`, and a record count divisible by 1,000 up to 1,000,000. The runner creates a unique output directory, preserves data and diagnostics, and enforces memory and disk reserve gates. Existing production inputs remain untouched.

The measurement confirms construction has a real cost and that false positives increase with key count. It does not establish that a production export will finish within a particular time or satisfy the production policy targets. A production attempt still requires separate authorization and the existing preflight and watchdog gates.
