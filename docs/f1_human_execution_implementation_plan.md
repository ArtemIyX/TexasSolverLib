# F1 bounded qualification execution plan

This document is the execution handoff for the F1-DEV-12-v1 workflow. It
keeps sizing, artifact identity, checkpoint equivalence, and final acceptance
as separate gates.

## Scope

- Use `configs/multiway/f1_sizing_v1.cfg` only for bounded sizing.
- Freeze numeric capacities in `configs/multiway/f1_dev_v1.cfg` only after
  reviewing bounded high-water evidence with headroom.
- Validate the bucket artifact against its manifest, inspection report, and
  workflow identity before training.
- Run split/resumed bounded training before any final acceptance workload.

## Execution sequence

1. Record the source commit, toolchain, host memory, free disk, configuration
   hashes, and artifact paths.
2. Build the F1 workflow and run focused configuration, sizing, artifact,
   preflight, and acceptance tests.
3. Inspect the existing sizing and bucket reports. Failed capacity reports are
   diagnostic only and cannot be used as acceptance evidence.
4. Run one bounded sizing pilot with an approved process-memory and disk
   budget. Do not use the 50-million-trajectory target.
5. Select each capacity from measured high-water marks plus documented
   headroom. Record the selection and the reason for every field.
6. Freeze the numeric sizing fields in `f1_dev_v1.cfg` and verify the workflow
   fingerprint and model identity.
7. Verify the complete bucket artifact with inspection, payload hash, manifest,
   and configuration identity. Regenerate only if identity or completeness
   fails.
8. Run bounded continuous and split/resumed training with identical inputs and
   compare checkpoint, blueprint, and deterministic merge identities.
9. Prepare the final F1 evidence bundle and required-lookup report. External
   publication requires an explicit destination and operator authorization.
10. Consider the 50-million-trajectory acceptance workload only after all
    previous gates pass. It is intentionally excluded from bounded execution.

## Current evidence and blockers

- Steps 1-7 are complete. Numeric capacities are frozen in
  `configs/multiway/f1_dev_v1.cfg` from the pinned 4-worker 5,000-trajectory
  pilot.
- The bucket artifact inspection and manifest pass identity and payload-hash
  checks without regeneration.
- Focused F1 workflow, training, compact-storage, checkpoint-equivalence, and
  acceptance tests pass.
- The bounded 2-batch continuous and 1+1-batch resumed runs both pass coverage
  and discard gates, but their final blueprints differ in five sparse rows.
- The 50-million-trajectory workload remains excluded until the split/resume
  gate passes.

## Required evidence

- Numeric frozen configuration and workflow fingerprint.
- Bucket inspection report, payload hash, and manifest.
- Bounded sizing report with zero discarded trajectories and no capacity
  exhaustion at the selected limits.
- Split/resume equivalence report with matching deterministic identities.
- Final machine and operator reports, when the acceptance workload is
  explicitly authorized.
