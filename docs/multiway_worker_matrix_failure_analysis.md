# Multiway worker-matrix failure analysis and repair plan

Updated: 2026-10-13

## Scope

This analysis covers the failed `run_multiway_worker_matrix.ps1` run for the
`PB-2` branch and the current working tree. No build, test, benchmark, training
executable, or solver executable was run while preparing it.

## Failure summary

The process launched and completed initialization. The first training batch
then attempted 32 trajectories, accepted 6, discarded 26, merged 87 retained
delta entries, and reported no discovery-capacity exhaustion. The blueprint
trainer rejected that batch because `MultiwayRootBatchResult::clean` requires:

```text
discarded == 0
attempted == accepted
nonempty merged deltas imply a nonzero merge fingerprint
```

The PowerShell failures are downstream effects:

1. `MultiwayBlueprintTrainer::run_batches` throws on the unclean batch.
2. The executable writes a failure report.
3. `run_multiway_ram_1000.ps1` rejects the report at its bounded-pilot gate.
4. `run_multiway_worker_matrix.ps1` stops the first matrix cell because the
   child PowerShell process returned exit code 1.

This is not a process-launch, scheduler, worker-pinning, RAM-preflight, or
discovery-capacity failure.

## Root cause

### The private sampler and the training gate have incompatible contracts

`MultiwayCompiledPrivateRanges::try_sample_into` independently samples one
two-card hand from each seat's marginal range. It makes exactly one proposal.
If any later seat overlaps the board or an earlier seat, it returns `false` and
marks the trajectory discarded.

The workflow creates six identical uniform full-deck ranges. The probability
that six independent two-card draws do not collide is:

```text
product(i=0..5, C(52 - 2i, 2) / C(52, 2)) = 0.2841595168
```

Only about 9.1 of 32 proposals are expected to be accepted. The observed 6 of
32 is plausible. The probability that all 32 proposals are collision-free is
about `3.27e-18`. Therefore the zero-discard batch gate cannot reliably pass
with the current six-player uniform-range sampler.

This conflicts with two explicit project contracts:

- `docs/pluribus_solver_technical_report.md` states that a correct multiway
  range sampler must condition on blockers and describes sequential sampling
  from compatible candidates.
- F1 qualification and the worker matrix require accepted trajectories with
  zero discarded trajectories.

### Why the report shows zero work

`MultiwayBlueprintTrainer::run_batches` checks `batch_result.clean` before it
copies batch counters, timing, worker vectors, and coordinator diagnostics into
`status_`. When it throws, `publish_training_failure` serializes the previous
status. This is why `report.json` contains zero trajectories, zero RSS, and
empty worker telemetry even though the exception reports 32 attempts and 87
deltas.

The failure report is therefore internally valid but diagnostically stale. It
does not mean the worker never ran.

## Branch-change assessment

The commits after `main` add batch telemetry, build/test script options, and a
scheduler type qualification. The telemetry commit reads counters after a
batch and does not alter private-card selection or the clean-batch predicate.
The scheduler qualification is compile-only. These committed changes do not
cause the 26 discards.

The current working tree also contains three uncommitted changes:

- `scripts/run_multiway_worker_matrix.ps1` parses comma-separated worker lists.
- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp` expands the
  unclean-batch exception with counters.
- `src/solver/multiway/engine/multiway_traversal.cpp` bypasses continuation
  selection when a leaf does not have a valid continuation key.

The expanded exception exposed the useful `32/6/26` evidence. The worker-list
parsing does not affect traversal. The continuation-key change can allow the
run to reach this later sampler defect, but it does not generate private-card
collisions.

The earlier sizing documents reporting 1,000 and 10,000 accepted trajectories
with zero discards are inconsistent with the current one-shot sampler for six
uniform ranges. Those results should not be used as evidence for the current
executable until their producer commit/build identity is known. The current
report records those producer fields as `unknown`.

## Required solution

Implement blocker-conditioned private sampling. Do not weaken the worker
matrix or F1 zero-discard gate and do not merely retry the existing independent
proposal while keeping its old probability fields.

A retry-only patch would hide the symptom but silently change the accepted
sample distribution. The current fields claim that `chance_reach`,
`conditional_deal_probability`, and `proposal_reach` are the same independent
range-product probability. That claim is false after rejection conditioning.

### Sampling contract

For each seat in the defined sampling order:

1. Scan its canonical compiled range and sum weights of hands that do not
   intersect the public board or already selected private cards.
2. Fail only if the compatible mass is zero. This is unreachable for the
   current six-player uniform root after fewer than 52 cards are used. For
   arbitrary narrow ranges, global feasibility alone does not prove that every
   greedy prefix can be completed, so the worker API must retain a bounded
   failure path or use completion-aware candidate filtering.
3. Sample from the compatible mass using the existing deterministic RNG.
4. Record the selected hand and mark its cards used.
5. Multiply proposal reach by
   `selected_weight / compatible_weight_sum`.

The probability contract must be made explicit before implementation:

- Recommended project contract: follow the technical report's sequential
  blocker-conditioned proposal. Set `proposal_reach` to the product of the
  per-seat conditional probabilities. Set `conditional_deal_probability` to
  that same proposal probability. Define `chance_reach` consistently with the
  target distribution used by MCCFR, and retain a nontrivial importance ratio
  only if the target differs from the proposal.
- Do not keep `chance_reach` equal to the unconditioned product unless the
  traversal intentionally uses importance weighting from that target and the
  target's normalization is fully specified and tested.

For the current training root, this produces one valid six-seat deal per
requested trajectory without heap allocation in the worker hot path. The
compiled vectors already own the range data; the worker can scan them using
fixed scratch state.

## Files to change

### Core sampler

- `include/games/multiway_private.hpp`
  - Replace the one-shot/discard comments with the blocker-conditioned
    contract.
  - Clarify each probability field and whether `try_sample_into(false)` is
    possible after successful feasibility preflight.
  - Add fixed worker scratch only if needed for compatible-mass selection.
- `src/games/multiway_private.cpp`
  - Replace independent cumulative draws with compatible-mass scans and
    deterministic conditional draws.
  - Preserve canonicalized ranges, fixed seat count, no allocation in
    `try_sample_into`, and deterministic seed behavior.
  - Keep construction-time feasibility validation.

### Traversal and accounting

- `src/solver/multiway/abstraction/multiway_terminal_adapter.cpp`
  - Propagate the revised probability contract unchanged.
  - Treat a zero-compatible-mass result as a real sampler/configuration error,
    not a normal six-player trajectory outcome.
- `src/solver/multiway/engine/multiway_traversal.cpp`
  - Preserve the distinction between sampler failure and delta/discovery
    capacity rejection.
  - Keep requested trajectory IDs and deterministic seeds stable across fixed
    and dynamic scheduling.
- `src/solver/multiway/engine/multiway_cfr.cpp`
  - Update validation only if the relationship among chance, conditional, and
    proposal reach changes.
  - Verify that importance weighting uses the intended target/proposal ratio.

### Failure telemetry

- `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
  - Copy the completed batch result and coordinator diagnostics into training
    status before throwing on `!clean`, or construct the failure report from a
    captured failed-batch snapshot.
  - Do not advance committed batch count, trajectory count, checkpoint state,
    or schedule state for a failed batch.
- `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
  - Add explicit last-failed-batch telemetry if updating committed status would
    blur committed versus attempted work.
- `include/solver/multiway/workflow/multiway_training_report.hpp` and
  `src/solver/multiway/workflow/multiway_training_report.cpp`
  - Serialize failed-batch attempted, accepted, discarded, delta, capacity,
    timing, and worker counters without presenting them as committed training.
  - Bump the report schema only if new serialized fields are added.
- `scripts/run_multiway_ram_1000.ps1`
  - Print failed-batch fields when present. Keep the hard failure gate.

The worker-matrix script does not need a correctness relaxation. Its current
zero-discard gate should remain.

## Tests to add or update

### `tests/test_multiway_private.cpp`

- Six uniform full-deck ranges produce collision-free deals for a deterministic
  seed set.
- Every selected hand avoids the board and all earlier selected hands.
- A small weighted fixture proves the exact per-seat conditional probability
  and total proposal reach with hand-computed values.
- Repeated equal seeds produce identical deals and probability fields.
- Zero-weight, duplicate/canonicalized, board-blocked, narrow, and six-seat
  ranges preserve their existing validation behavior.
- An impossible joint range is rejected at construction/preflight.
- Replace tests that deliberately require one-shot collision discards; they
  freeze the incorrect workflow contract.

### `tests/test_multiway_cfr.cpp`

- Freeze the revised `chance_reach / proposal_reach` semantics in the external
  sampling request.
- Reject inconsistent or non-finite probability tuples.

### `tests/test_multiway_recursive_traversal.cpp`

- A small colliding marginal-range fixture completes the requested accepted
  trajectory count with zero discards.
- Delta capacity exhaustion still rejects and rewinds a trajectory.
- Invalid continuation keys use the direct blueprint leaf path only where that
  behavior is part of the intended local fix.

### `tests/test_multiway_parallel_training.cpp`

- Fixed and dynamic schedulers, at one and multiple workers, produce identical
  accepted counts, zero discards, deterministic merge fingerprints, and policy
  state for the same accepted trajectory IDs.
- A failed batch exposes attempted/accepted/discarded worker telemetry while
  leaving committed batch/checkpoint counters unchanged.

### `tests/test_multiway_qualification_reports.cpp`

- Failure reports retain last-failed-batch metrics.
- Successful reports preserve the existing schema and zero-discard gate.

## Implementation order

1. Define and document the probability semantics for blocker-conditioned
   sampling.
2. Add deterministic sampler regression tests that fail under the one-shot
   implementation.
3. Implement allocation-free compatible-mass sampling.
4. Update CFR probability validation and traversal accounting as required.
5. Add failed-batch telemetry without committing failed training progress.
6. Review the continuation-key working-tree change independently. It addresses
   a separate leaf-boundary issue and should not be folded into the sampler
   fix without its own regression test.
7. After implementation, run the focused private, CFR, traversal, parallel
   training, and report tests.
8. Run the compact full build and compact CTest only after focused tests pass.
9. Re-run a single bounded 32-trajectory, one-worker cell before the full
   worker matrix. Require 32 accepted, zero discarded, nonzero fingerprint,
   and populated telemetry.
10. Run the complete fixed/dynamic, pinned/unpinned matrix only after the
    bounded cell passes.

## Acceptance criteria

- Every requested six-player uniform-range trajectory receives a valid,
  blocker-free private deal.
- `accepted_trajectories == requested trajectories` and
  `discarded_trajectories == 0` for the bounded pilot.
- Fixed and dynamic scheduling remain deterministic for the existing seed and
  trajectory-ID contract.
- The sampler hot path performs no heap allocation, logging, formatting,
  virtual dispatch, or hash lookup.
- Probability fields are mathematically consistent and directly covered by
  hand-computed tests.
- A failed batch report contains the actual failed-batch metrics and does not
  claim that failed work was committed.
- Memory-preflight and worker-delta capacity contracts remain unchanged.

## Non-solutions

- Increasing worker delta, public-state, row, or value capacities. The failure
  reports `discovery_capacity_exhausted=false` and retains only 87 deltas.
- Changing pinning, worker count, scheduler, or chunk size. The first
  one-worker fixed-scheduler cell already demonstrates the sampler defect.
- Allowing discards in the matrix gate. That would violate the current F1
  acceptance contract and measure different accepted workloads per cell.
- Retrying the current sampler without correcting probability semantics.
- Treating the zero-valued JSON report as proof that the executable did not
  run.
