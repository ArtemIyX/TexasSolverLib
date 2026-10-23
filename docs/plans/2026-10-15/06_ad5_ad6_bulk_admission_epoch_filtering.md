Task: Admit prepared discoveries in one coordinator transaction and skip states already known in the immutable epoch; Codex can implement the source changes, but an operator must run rejection, equivalence, and capacity tests.

# AD5-AD6 - Bulk admission and epoch filtering

## Goal

Reduce coordinator lock and sorted-index overhead while preserving strict
validation and deterministic state identity.

## Scope

### AD5 - Bulk admission

Add a prepared-batch API that:

- acquires one exclusive coordinator lock;
- preflights state, row, page, and value capacities;
- validates each new descriptor once;
- admits states parent-first;
- admits canonical rows;
- appends packed catalog nodes contiguously;
- merges old and new sorted index entries once;
- updates diagnostics before releasing the lock.

### AD6 - Epoch-known filtering

- Expose an immutable-epoch membership query.
- Avoid appending discoveries already present in the epoch catalog.
- Preserve state-ID conflict detection using a compact fingerprint or one
  structural validation per unique ID.
- Retain strict validation for direct coordinator admission.

## Agent work

- Ensure capacity rejection occurs before partial mutation.
- Define epoch lifetime and thread-safety contracts.
- Add tests for capacity failures, conflicts, duplicates, and fixed/dynamic
  worker equivalence.

## Operator work

- Run the admission and checkpoint-equivalence tests.
- Run fixed and dynamic worker-count captures.
- Provide admitted-set, merge-fingerprint, checkpoint-hash, and policy results.

## Acceptance

- No partial mutation after capacity rejection.
- Identical outputs to the existing deterministic path.
- Near-zero existing-state admission work in steady-state batches.
- Admission below 250 ms for the captured cold batch.

## Dependency

AD2-AD4 must provide prepared, canonical discovery streams.
