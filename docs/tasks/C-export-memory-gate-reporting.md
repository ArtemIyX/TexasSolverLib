# Task C: Export memory gate and reporting

## Objective

Add a fresh, fail-closed export-phase process gate, Windows system write-cache gate, and versioned export telemetry without conflating process and machine-wide memory.

## Dependencies

None.

## Scope

- Add exact export-plan, scratch, process-estimate, gate-result, and observed export-delta fields in `multiway_memory.*`.
- Capture a fresh `before_export` process snapshot after lease acquisition and plan construction.
- Estimate process export peak from current working set, order index, fixed scratch, encoder buffer, explicit exporter headroom, and explicit mapped-residency allowance.
- Add Windows `GlobalMemoryStatusEx` sampling and compare available physical memory with planned output bytes, exporter scratch, and configured system reserve.
- Serialize the separate process/system values, publication mode, result bytes/hash/duration, and schema version through training reports.

## Required invariants

- Required unavailable metrics fail closed when their corresponding gate is enabled.
- Do not count write-cache allowance as process private or working-set memory.
- Retain startup admission, checkpoint transient, export scratch, output, and observed values as separate ledger categories.
- No telemetry allocation, formatting, or sampling enters a traversal/merge inner loop.

## Tests

Update `tests/test_multiway_memory.cpp` for exact/saturating arithmetic, missing metrics, process-limit boundaries, system-allowance boundaries, and snapshots. Update `tests/test_multiway_qualification_reports.cpp` for report schema and validation.

## Validation

Run focused memory/report builds/tests, then full build, compact CTest, `git diff --check`, and C++ review.

## Acceptance

- Export admission uses a fresh snapshot and exact planned output bytes.
- Process and system-memory rejection reasons are distinct and serialized.
- Existing report readers remain compatible, or the schema is explicitly versioned with compatible readers.

## Stop conditions

Stop if an enabled gate must guess an unavailable required metric or if process/system categories are merged.
