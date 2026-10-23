Task: Replace per-cell compact snapshot lookup with sequential export; Codex can implement the source change, but an operator must run round-trip and performance validation.

# CP2 - Linear compact snapshot export

## Goal

Make compact checkpoint snapshot export linear in rows, logical cells, and
active pages.

## Scope

1. Compute canonical row offsets and total logical cell count once.
2. Allocate final regret and strategy arrays once.
3. Zero inactive logical cells.
4. Walk compact pages sequentially.
5. Copy active page ranges into action-major destinations.
6. Guarantee canonical row order from compact export.
7. Remove the coordinator's second canonical storage copy for this backend.

Keep the Float64 reference path unchanged.

## Agent work

- Inspect compact page layout and canonical row contracts.
- Implement sequential export using preallocated buffers.
- Preserve sparse-page semantics and inactive-cell values.
- Add focused equivalence coverage against the scalar reference export.

## Operator work

- Build and run compact and Float64 checkpoint round-trip tests.
- Run a large sparse-page export measurement.
- Report peak allocation and phase timing from CP1.

## Acceptance

- Compact export matches scalar reference output byte-for-byte or by the
  documented value-equivalence rule.
- Canonical row order is stable.
- No additional peak allocation beyond the checkpoint budget.
- CP1 shows reduced compact-export and canonicalization time.

## Dependency

CP1 telemetry is recommended first. CP2 must preserve existing checkpoint
schema and loading behavior.
