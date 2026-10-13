# Multiway memory scaling execution tickets

This checklist decomposes `multiway_memory_scaling_recovery_plan.md` into
small, reviewable implementation slices. The 50-million-trajectory run stays
disabled until the final qualification gates pass.

## M1 - Memory contract and ledger

- [x] Include observed post-load RSS and mapped/owned bucket bytes in preflight.
- [x] Separate live, reserved, committed, current RSS, and peak RSS fields.
- [x] Account for compact backend element sizes and checkpoint/export peaks.
- [x] Add focused boundary and overflow tests.

## M2 - Read-only bucket registry view

- [x] Add verified artifact lookup without a second owned assignment copy.
- [x] Keep owned registry as the reference/test implementation.
- [x] Add mapped-versus-owned differential and rejection tests.

## M3 - Paged compact CFR storage

- [x] Keep sorted row headers and lazily admit fixed-width action-major pages.
- [x] Emit/admit page identities in stable order at a batch boundary.
- [x] Preserve scalar/reference policy and checkpoint behavior.
- [x] Add page occupancy and active-cell telemetry.

## M4 - Packed public catalog

- [x] Retain fixed-size parent-linked nodes and deduplicated menu handles.
- [x] Keep descriptor reconstruction at cold compatibility boundaries.
- [x] Add identity, replay, and retained-bytes tests.

## M5 - Immutable batch epochs

- [x] Publish immutable admitted state/row/page views to workers.
- [x] Move discovery, admission, counters, and merge to coordinator barriers.
- [x] Add fixed-seed worker-count determinism and no-lock traversal coverage.

## M6 - Streaming checkpoint/export

- [x] Serialize canonical frozen spans with bounded scratch.
- [x] Stream compact rows without a full Float64 checkpoint conversion.
- [x] Preserve atomic previous checkpoints and schema identity.

## M7 - Qualification and reporting

- [x] Run bounded 1/10/100/1,000-batch smoke qualification only.
- [x] Run the requested build and CTest workflows through repository scripts.
- [x] Record measured RAM changes and remaining gates in the completion report.

The user-requested 10,000+ trajectory production qualification was not run.

## M8 - Worker timing and scaling telemetry

- [x] Record minimum and maximum worker active time per training run.
- [x] Record per-worker attempted/completed trajectory counts and active time.
- [x] Separate derived barrier wait from coordinator wait in the report.
- [x] Add a PowerShell matrix runner for workers 1, 2, 4, 8, and 16 using
  the same seed, input, and batch size.
- [ ] Rebuild and run the matrix after the telemetry change.

The implementation is committed separately from the runtime qualification.

## M9 - Pinned versus unpinned worker diagnosis

- [x] Add an explicit production `--pin-workers true|false` option.
- [x] Reuse the worker matrix runner for both pinning modes.
- [x] Allow larger batch counts to amortize one-batch setup noise.
- [ ] Rebuild and run the pinned/unpinned matrix.
