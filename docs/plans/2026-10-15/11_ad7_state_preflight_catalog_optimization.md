Task: Reduce multiway admission state-preflight and catalog-commit time; Codex can implement the source and tests, but an operator must build, test, and run the Tracy qualification captures.

# AD7 follow-up: state preflight and catalog commit optimization

## Purpose

Reduce the remaining multiway prepared-batch admission cost after the first
AD7 optimization pass. The first pass made correctness-preserving progress,
but the measured cold admission time is still above the required 250 ms gate.

This plan is limited to the coordinator admission path. It does not change
traversal semantics, worker scheduling, sampling, merge order, checkpoint
format, public-state identity, sparse-row identity, or policy computation.

## Current evidence

The operator completed the eight AD7 verification captures under
`tmp/tracy/ad7-verify-*` using RelWithDebInfo and Tracy 0.14.1.

### Cold admission

| Capture | Admission | State preflight | Catalog commit | Result |
|---|---:|---:|---:|---|
| dynamic, 1 worker | 601.717 ms | 409.295 ms | 71.961 ms | Correct |
| dynamic, 4 workers | 609.712 ms | 408.152 ms | 75.216 ms | Correct |
| fixed, 1 worker | 710.699 ms | 500.491 ms | 75.222 ms | Correct |
| fixed, 4 workers | 621.413 ms | 416.838 ms | 74.730 ms | Correct |

The best cold run is still about 2.4 times slower than the 250 ms target.
State preflight is the dominant measured phase. Discovery merge is about
81-90 ms, row preflight is about 22-32 ms, and storage commit is about 4 ms.

### Steady admission

| Capture | Admission total | State preflight | Catalog commit | Result |
|---|---:|---:|---:|---|
| fixed, 1 worker | 3310.680 ms | 1487.246 ms | 1283.756 ms | Correct |
| fixed, 4 workers | 3416.172 ms | 1555.246 ms | 1280.758 ms | Correct |
| dynamic, 1 worker | 3368.981 ms | 1541.666 ms | 1276.531 ms | Correct |
| dynamic, 4 workers | 3403.432 ms | 1538.143 ms | 1294.232 ms | Correct |

The steady captures also passed correctness, but their larger admitted batch
makes both state preflight and catalog commit important scaling targets.

All eight captures reported:

- `failed=false`;
- zero discarded trajectories;
- no capacity exhaustion;
- stable cold fingerprint `9806774443936985566`;
- stable steady fingerprint `3496596242701454594`;
- identical checkpoint bytes within each workload group.

## Code path under investigation

The primary implementation path is:

- `src/solver/multiway/engine/multiway_traversal.cpp`
  - prepares and merges worker discovery streams;
  - builds parent-first order and parent indexes;
  - calls `MultiwaySolverCoordinator::admit_prepared_batch`.
- `src/solver/multiway/engine/multiway_solver.cpp`
  - performs state preflight;
  - reconstructs existing catalog descriptors;
  - validates parent transitions;
  - performs row preflight and state resolution;
  - calls the catalog and storage batch commits.
- `src/solver/multiway/engine/multiway_public_catalog.cpp`
  - classifies, packs, and merges public catalog entries;
  - reconstructs packed descriptors for existing-state checks.
- `include/solver/multiway/engine/multiway_solver.hpp`
  - owns `MultiwayPublicCatalog`, sparse storage, compact storage, and
    admission diagnostics declarations.
- `src/solver/multiway/workflow/multiway_training_report.cpp`
  - serializes admission phase telemetry.
- `tests/test_multiway_solver.cpp`
  - contains the existing batch admission, ordering, conflict, and capacity
    regression coverage.

The current state preflight holds the coordinator write lock while it loops
over the parent-first batch. For each candidate it checks catalog membership,
may reconstruct an existing descriptor, validates the candidate, resolves a
parent, and validates the child transition. Existing catalog lookup and
descriptor reconstruction are currently separate operations. The current
catalog batch commit already avoids per-entry index insertion, but it still
packs menus and nodes and creates a merged index buffer for each batch.

## Goals

1. Reduce cold `coordinator_admission_nanoseconds` below 250 ms for the
   required one-worker and four-worker fixed and dynamic captures.
2. Reduce steady-state admission growth without weakening existing-state
   filtering or capacity checks.
3. Preserve exact deterministic fingerprints, checkpoint bytes, admitted
   descriptors, row metadata, and policy results.
4. Keep all validation and mutation failure semantics explicit.
5. Produce enough phase telemetry to identify the remaining cost if the target
   is still missed.

## Non-goals

- Do not change the traversal algorithm or trajectory sampling.
- Do not change fixed or dynamic scheduler semantics.
- Do not remove public-state, parent-transition, row-shape, or capacity
  validation.
- Do not replace deterministic sorted arrays with an unordered container in
  the admission path without a measured and reviewed reason.
- Do not remove the coordinator lock based only on the timing result.
- Do not change checkpoint schema or binary layout as part of this plan.
- Do not optimize the worker traversal inner loops in this task.

## Work breakdown

### AD7F.1 - Refine admission telemetry

Split the existing state-preflight and catalog-commit measurements into
actionable subphases. Keep the counters outside worker traversal loops.

Measure at minimum:

- catalog existing-state classification;
- existing descriptor compatibility without mutation;
- new descriptor validation;
- parent transition validation;
- row public-state resolution;
- row shape and owner validation;
- catalog menu lookup and node packing;
- catalog index merge and append.

Add counts for:

- existing candidates;
- new candidates;
- existing descriptor reconstructions;
- direct packed-node compatibility checks;
- parent descriptors loaded from the existing catalog;
- rows resolved from the current batch versus the existing catalog.

The report must retain the current fields and remain readable by existing
report consumers. Add fields without changing the meaning of the current
nanosecond totals unless a schema change is explicitly justified.

### AD7F.2 - Add a non-allocating catalog lookup view

Add an internal catalog operation that returns a non-owning packed-node view
or equivalent stable reference for an admitted state. The operation must:

- use the existing sorted catalog index;
- distinguish missing, present, and malformed entries;
- avoid constructing temporary vectors for boards, actions, histories, or
  per-seat betting arrays;
- remain valid only while the coordinator admission lock prevents catalog
  mutation;
- preserve the current public catalog ownership model.

Add a compatibility check that compares a candidate descriptor to the packed
catalog representation directly. It must cover the same identity, payload,
menu, incoming-edge, betting, and chance-transition rules currently enforced
by `public_state_candidates_compatible`.

Keep `reconstruct` for checkpoint export, diagnostics, and callers that need
an owning descriptor. Do not make the view part of the installed public API.

### AD7F.3 - Classify the public-state batch with one sorted merge

Replace repeated per-state catalog membership checks in
`admit_prepared_batch` with a sorted merge between the prepared state IDs and
the existing catalog index, or with the catalog view from AD7F.2.

The classification pass must:

- preserve the sorted and unique input checks;
- mark existing and new states in stable input order;
- validate compatibility for existing IDs;
- validate capacity before any mutation;
- retain the parent-first order and prepared parent indexes;
- avoid a binary search plus descriptor reconstruction for every candidate;
- reuse the classification results during row preflight and catalog commit.

Do not infer that a state is new solely because it was not found in a worker
local discovery stream. The coordinator catalog remains authoritative.

### AD7F.4 - Reuse validated state metadata during parent and row checks

Introduce preallocated admission scratch for metadata that is derived once per
batch. Candidate metadata may include:

- existing/new classification;
- parent source, either prepared-batch index or catalog view;
- validated street and next-node kind;
- current player and legal-action count;
- compact menu handle when available;
- row state index for rows referencing the prepared batch.

Use the scratch only for the current admission epoch. Clear or resize it at
the epoch boundary. Do not add a long-lived cache whose invalidation depends
on mutable solver state.

For existing parents, validate the child transition against the non-owning
catalog view where possible. For prepared parents, continue to use the
parent-first descriptor and index relationship. Preserve the current cycle,
missing-parent, and conflicting-parent rejection behavior.

### AD7F.5 - Reduce catalog commit copy and menu lookup cost

Profile the remaining catalog commit before choosing an implementation. Then
evaluate these narrow options in order:

1. Reuse precomputed menu handles for candidates with identical legal menus.
2. Reserve all node, menu, and index capacity before mutation.
3. Merge the sorted index with one preallocated scratch buffer, or use a
   proven backward in-place merge when it preserves strong failure semantics.
4. Append packed nodes only after all operations that can reject have finished.

The chosen implementation must guarantee that an exception or capacity
failure cannot leave nodes, menus, or index entries partially committed.
Single-state `admit` behavior must remain unchanged for restore and direct
API callers.

### AD7F.6 - Preserve storage and row behavior

Keep the existing float and compact batch storage APIs unless telemetry shows
a material storage-side regression. Verify that the new state classification
does not change:

- row offsets;
- action-major layout;
- compact page width and page offsets;
- row shape conflict handling;
- existing-row filtering;
- configured row and value capacity checks.

Only optimize row preflight if its new telemetry shows that the remaining
cost is material after state preflight changes.

### AD7F.7 - Add focused regression coverage

Extend `tests/test_multiway_solver.cpp` with tests for:

- direct packed-node compatibility against an equivalent descriptor;
- rejection of a conflicting existing descriptor without mutation;
- sorted batch classification with mixed existing and new states;
- parent validation using a prepared parent and an existing catalog parent;
- missing parent and cycle rejection;
- stable row state-index reuse;
- catalog capacity rejection with unchanged nodes, menus, and index;
- deterministic catalog ordering and descriptor export after optimization;
- unchanged checkpoint bytes and fingerprints for the existing fixture paths.

Keep tests small and deterministic. Do not depend on Tracy output for unit
test correctness.

### AD7F.8 - Operator qualification

After the implementation and focused tests are complete, the operator must:

1. Build the Debug test target and the RelWithDebInfo Tracy target.
2. Run the focused multiway tests.
3. Run the four cold captures and four steady captures with Tracy 0.14.1.
4. Inspect all admission phase fields and report schema fields.
5. Compare fingerprints, checkpoint bytes, discarded trajectories, capacity
   status, and fixed/dynamic equivalence.

Codex must not build, test, run executables, or run solver-launching scripts.

## Suggested implementation order and commits

Keep each logical change isolated:

1. `PB-3 Multiway: Refine admission phase telemetry`
2. `PB-3 Multiway: Add packed catalog admission views`
3. `PB-3 Multiway: Classify prepared states with sorted merge`
4. `PB-3 Multiway: Reuse validated admission metadata`
5. `PB-3 Multiway: Reduce catalog commit copying`
6. `PB-3 Tests: Cover optimized admission validation`
7. `PB-3 Docs: Record AD7 follow-up qualification`

Do not stage or commit the pre-existing untracked plan files from the same
date directory.

## Acceptance criteria

### Performance

- Cold admission is below 250 ms for fixed and dynamic scheduling with one
  and four workers.
- State preflight and catalog commit phase totals are reported and explain
  the measured coordinator admission total.
- Steady admission does not regress materially from the current baseline,
  and its phase scaling is documented.

### Correctness

- All four cold runs report `failed=false`, zero discarded trajectories, and
  no capacity exhaustion.
- All four steady runs report the same conditions.
- Fixed and dynamic cold runs produce fingerprint
  `9806774443936985566` and checkpoint bytes `137380835` for this workload.
- Fixed and dynamic steady runs produce fingerprint
  `3496596242701454594` and checkpoint bytes `387676624` for this workload.
- Public catalog descriptors, sparse rows, compact pages, and policy outputs
  remain equivalent to the pre-optimization path.

### Safety and maintainability

- No partial catalog or storage mutation after validation or capacity failure.
- No new heap allocation, string operation, logging, hash lookup, or lock
  acquisition is added to worker traversal or merge inner loops.
- The coordinator lock contract is documented and unchanged unless a separate
  concurrency review proves a safe epoch-based alternative.
- Existing single-state admission and checkpoint restore paths remain intact.
- Focused tests cover the new views, classification, conflicts, capacity,
  ordering, and deterministic output.

## Operator report locations

Each capture writes its raw report under:

`tmp/tracy/<capture-id>/report.json`

The expected phase fields are under `admission_diagnostics`. The operator
should preserve the reports and manifests used for the final comparison.

## Risks and fallback

- If direct packed-node comparison is difficult to prove equivalent, keep
  `reconstruct` as a correctness fallback and optimize only the sorted batch
  classification and parent-index reuse.
- If the 250 ms target remains unreachable after state classification, use the
  phase data to split the next task between descriptor validation and catalog
  commit instead of broadening the optimization scope.
- If any fingerprint, checkpoint byte, admitted-set, or policy mismatch
  appears, stop performance work, preserve the failing report, and return to
  the smallest commit that introduced the mismatch.
