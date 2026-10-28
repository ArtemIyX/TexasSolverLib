# PB-12 checkpoint page-store scale assessment

Date: 2026-10-27

## Scope and result

Inspected the named checkpoint and original page-store metadata read-only. No production solver or policy export was launched. The checkpoint SHA-256 was `098f37cb91d202c46ae6a969fe54515fff3fdff211c3cfff718f4f95569a5524`, matching the prior handoff. The checkpoint and page-component manifest FNV checksums were verified. The original page `CURRENT` manifest exactly matched the checkpoint's page generation, component metadata and complete active run references.

| Checkpoint page-store metric | Value |
|---|---:|
| Completed trajectories | 1,000 |
| Allocated page IDs | 162,370 |
| Active page runs | 8,321 |
| Records across active runs | 8,321 |
| Distinct page IDs found in original indexes | 7,016 |
| Duplicate records across generations | 1,305 |
| Page payload bytes | 1,584 |
| Active data bytes | 13,247,032 |
| Active index bytes | 66,568 |

All 8,321 runs contain one record: the manifest has 8,321 nonempty runs and 8,321 records. The original store had no missing or wrong-sized referenced page files. Its index keys ranged from 1 to 162,352. The code defines allocated IDs as 1 through the checkpoint's page count, so 155,354 allocated IDs have no record in the inspected indexes.

A separate read-only simulation applied the current 1 MiB, two-bit filter to those 7,016 distinct index keys, then tested every allocated ID absent from that set. It found **zero false positives among 155,354 absent IDs**. This simulation did not execute the exporter or measure filter construction. It assumes the index contents inspected are correct; the combined segment checksums were not verified against their data and index files.

The old snapshot's page `CURRENT` had the same SHA-256 as the original page manifest, matched the checkpoint references, and its referenced page files had expected lengths. Its index contents and segment checksums were not inspected. These observations do not verify its catalog, row store, transaction state, retention pins, or suitability for a retry.

## Comparison and next step

The previous synthetic construction test covered 5,000 records in five runs and 1,000,000 records in 1,000 runs. This checkpoint has only 8,321 stored records, but **8,321 individual runs**, over eight times the largest measured run count. Its approximately 13.3 MB of active data plus indexes is close in byte volume to the smaller synthetic cases, but their file-open patterns are not representative.

From the current filter-construction code, processing these 8,321 runs would request 24,963 stream opens: each run's index and data for checksum validation, followed by a second index open to build the filter. It would read approximately 13,380,168 bytes across those streams. These are code-derived logical operations and byte counts, not observed physical I/O or measured production construction time. The checkpoint metadata suggests low filter saturation, but says nothing about actual operating-system cache, open latency, checksum cost, or complete export speed.

Before requesting a separately authorized production export, measure a bounded synthetic construction fixture with roughly 8,321 singleton runs and similar payload width. Then re-evaluate the existing watchdog and production preflight. Re-audit or create an isolated snapshot as required; do not treat the old snapshot as fully verified. The prior production attempt remains incomplete, and production policy equivalence and completion time remain unknown.

## Reproduction and limits

`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex_powershell.ps1 python scripts/analyze/read_pb12_page_metadata.py <checkpoint> --store <original-store> --inspect-keys --simulate-filter`

The script verifies manifest checksums and bounds index-key inspection to 100,000 records and simulation to 1,000,000 allocated IDs. It only reads manifests and page index keys and checks file lengths. It does not verify the combined index/data checksum of any run, rebuild a snapshot, or validate the policy. The snapshot page manifest and referenced file lengths were checked in a separate read-only invocation.
