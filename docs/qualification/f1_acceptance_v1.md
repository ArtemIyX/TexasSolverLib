# F1 acceptance contract

Status: implemented-but-unqualified

Configuration: `configs/multiway/f1_dev_v1.cfg`

Evidence output starts from `f1_acceptance_report.template.json` and may be
published as `f1_acceptance_report.json` only after every required predicate
has a measured value.

The profile freezes the six-player rules, 169 lossless preflop classes, 12
postflop buckets per street, compact int32 storage, deterministic seed 1, one
reference worker, and 50,000,000 target trajectories. Capacity fields remain
unresolved until an explicitly authorized sizing pilot.

## Identity and artifacts

All rule, bucket, terminal, range, continuation, resolver, runtime, and code
schema versions contribute to `MultiwayModelIdentity`. Diagnostic formats do
not contribute to identity. Binary artifacts are written to a temporary path,
verified, and atomically renamed. Each manifest records the configuration
hash, model identity, byte length, payload hash, producer commit, and source
machine.

Expected output layout:

```text
artifacts/f1_dev_v1/
  buckets.bin
  buckets.manifest
  bucket-checkpoints/
  checkpoints/
  blueprint.bin
  blueprint.manifest
  training_report.json
  checkpoint_equivalence_report.json
  lookup_report.json
```

## Acceptance predicates

The final report must prove exact flop, turn, and river table counts of
22,100, 270,725, and 2,598,960; nonzero rows on every street; nonzero terminal
visits; zero discarded trajectories; and complete preflop-root training.
Continuous and disk-resumed runs must have identical ordered blueprint bytes,
payload hashes, coverage counters, and merge fingerprints. Two deterministic
provider replays must have zero missing infosets, missing buckets, and action
menu mismatches, with identical replay fingerprints.

The contract remains unaccepted until the unresolved pilot capacities are
frozen and all required evidence is checked in. Generated multi-gigabyte
artifacts are published externally and are never committed.
