Task: Prepare worker discoveries locally, merge them deterministically, and compute parent-first admission order; Codex can implement the source changes, but an operator must run correctness and admission benchmarks.

# AD2-AD4 - Discovery preparation and parent-first merge

## Goal

Remove concatenation, global fifth-vector sorting, repeated parent scans, and
repeated descriptor reconstruction from coordinator admission.

## Scope

### AD2 - Worker preparation

- Sort states by stable state ID and deterministic tie-break.
- Sort rows by infoset ID.
- Deduplicate local candidates.
- Reject conflicting descriptors or row shapes.
- Reuse worker-local buffers.

### AD3 - Deterministic k-way merge

- Merge sorted worker streams directly.
- Collapse cross-worker duplicates.
- Verify duplicate compatibility.
- Emit one canonical state and row stream.

### AD4 - Parent-first order

- Resolve candidate parents by binary search.
- Treat existing catalog parents as depth zero.
- Memoize candidate depth.
- Sort by `(depth, state_id)`.
- Reject cycles and unresolved parents before mutation.

## Agent work

- Keep tie-breaking and stream order deterministic.
- Use flat/preallocated records where possible.
- Add tests for shuffled input, duplicates, conflicts, cycles, and unresolved
  parents.

## Operator work

- Run the admission correctness suite.
- Run one-worker and four-worker admission measurements on identical candidate
  sets.
- Provide fingerprints and admitted-set comparisons.

## Acceptance

- Same admitted states, rows, merge fingerprint, checkpoint hash, and policy as
  the current implementation.
- No repeated `while (remaining)` parent scans.
- Four-worker admission is no more than 10% slower than one-worker admission
  for the same candidate set.

## Dependency

AD1 should be complete. AD5 bulk mutation should follow this work.
