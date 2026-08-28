# F1 acceptance blocker execution plan

Status: planned

Owner: unassigned

Last updated: 2026-08-28

## Objective

Produce the missing evidence required to accept F1:

1. A complete, versioned postflop bucket artifact for every board accepted by
   the current multiway canonical-board contract.
2. A deterministic, production-scale global-training workload starting from
   the six-player preflop root.
3. A qualification report proving all-street row creation, terminal reach,
   disk checkpoint equivalence, and complete required blueprint lookups.

F1 is complete only when the artifact, workload configuration, and
qualification report exist and pass every gate in this document.

## Current repository facts

- `MultiwayRootExternalSamplingTraversal` supports preflop through river.
- Preflop traversal uses the lossless 169-class representation.
- `MultiwayBlueprintTrainingConfig` defaults to all three public chance
  boundaries and the supported decision-depth cap.
- Bucket builders accept caller-provided board lists.
- `MultiwayBlueprintTrainingCheckpoint` exists only as an in-memory value.
- `texas_multiway_buckets`, `texas_multiway_train`, and
  `texas_multiway_inspect` parse production options but do not execute them.
- The training session has no external blueprint provider. Its
  `missing_lookup_requests` value is therefore not a valid lookup-completeness
  gate by itself.

## Canonical-board scope

The current multiway contract treats a canonical board as valid distinct
compact cards sorted by card ID. It does not perform suit-isomorphic board
reduction.

| Street | Required boards | Live hole assignments per board |
|---|---:|---:|
| Flop | 22,100 | 1,176 |
| Turn | 270,725 | 1,128 |
| River | 2,598,960 | 1,081 |
| Total | 2,891,785 | n/a |

Each table currently contains 1,326 `uint32_t` assignments, including blocked
holes marked with `MULTIWAY_INVALID_BUCKET`. The raw assignment payload is
approximately 15.3 GB. Full production generation must not retain all board
requests, tables, and serialized bytes simultaneously.

Do not introduce suit-isomorphic reduction as part of F1. That changes the
model identity and belongs to the production abstraction work in F5.

## Scope boundaries

In scope:

- Deterministic physical-board enumeration.
- Bounded-memory, resumable bucket generation.
- Full artifact inspection and hashing.
- Versioned workflow configuration.
- Persistent full-training checkpoints.
- Real bucket, train, and inspect executable behavior.
- F1 coverage, determinism, and lookup qualification.
- External publication of large binary artifacts.

Out of scope:

- Policy-strength claims.
- Bucket-quality calibration.
- Suit-isomorphic or EMD abstraction replacement.
- Mapped runtime blueprint storage.
- Full self-play, AIVAT, LBR, or restricted-BR release gates.
- Changes to exact `HUNLFlatDCFR` behavior.

## Execution rules for agents

1. Execute work packages in dependency order.
2. Preserve unrelated user changes.
3. Keep exact HUNL code unchanged.
4. Use C++17 and mirror public headers under `include/` with implementations
   under `src/`.
5. Add new source and installed public-header entries to `CMakeLists.txt`
   explicitly.
6. Keep artifact generation outside traversal hot paths.
7. Do not run builds, tests, benchmarks, bucket generation, or solver workloads
   unless the user explicitly authorizes them.
8. Never commit generated multi-gigabyte artifacts to Git.
9. Record only validation that was actually executed.
10. Stop a work package if its artifact schema or compatibility contract cannot
    be determined from the repository.

## Target qualification profile

Use a versioned `F1-DEV-12-v1` profile for F1 acceptance. This is an
architecture qualification, not a strength profile.

Freeze these semantic values:

```text
players                     = 6
initial_stack_chips         = 10000
small_blind_chips           = 50
big_blind_chips             = 100
ante_chips                  = 0
rake                        = explicit zero
preflop_classes             = 169
flop_buckets                = 12
turn_buckets                = 12
river_buckets               = 12
storage_backend             = CompactInt32
max_decision_depth          = 64
max_public_chance_depth     = 3
deterministic_seed          = 1
reference_worker_count      = 1
target_trajectories         = 50000000
```

Derive these capacity values from an authorized sizing pilot, then freeze them
before the acceptance run:

- maximum public states;
- maximum sparse rows;
- maximum sparse values;
- worker delta capacity;
- trajectories per batch;
- checkpoint interval;
- disk-space requirement;
- process memory limit.

No adaptive capacity increase is allowed during the acceptance run. A capacity
change creates a new configuration identity and requires a new run.

## Work package 0: freeze the acceptance contract

Dependencies: none.

Files:

- Add `docs/qualification/f1_acceptance_v1.md`.
- Add a configuration template under `configs/multiway/`.

Steps:

1. Copy the semantic profile above into a machine-readable configuration.
2. Assign a configuration schema version.
3. Define which fields contribute to `MultiwayModelIdentity`.
4. Define external integrity hashes for buckets, checkpoints, blueprints, and
   reports.
5. Define the exact output directory layout and atomic publication rules.
6. Define every acceptance predicate listed in the final gate below.
7. Mark pilot-derived capacity fields as unresolved until work package 7.

Output:

- A reviewable contract that leaves no acceptance threshold implicit.

Acceptance:

- Two agents given the same config would generate the same board order, seeds,
  batch boundaries, and artifact identities.

## Work package 1: deterministic board catalog

Dependencies: work package 0.

Suggested files:

- `include/solver/multiway/abstraction/multiway_bucket_catalog.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_catalog.cpp`
- `tests/test_multiway_bucket_catalog.cpp`

Steps:

1. Implement lexicographic combination enumeration for board sizes 3, 4, and
   5 over compact cards `[0, 51]`.
2. Emit `MultiwayBucketBoardRequest` values one at a time.
3. Expose total and per-street board counts without enumeration.
4. Support deterministic `[begin_index, end_index)` shards.
5. Support restoring the next global board index.
6. Reject invalid street and range inputs.
7. Keep iteration allocation bounded and independent of catalog size.

Acceptance:

- Exact counts are 22,100, 270,725, and 2,598,960.
- Every emitted board passes `is_multiway_canonical_board()`.
- No board is duplicated or skipped across adjacent shards.
- One complete enumeration has a stable catalog fingerprint.

## Work package 2: streaming and resumable bucket writer

Dependencies: work package 1.

Suggested files:

- `include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp`
- `tests/test_multiway_bucket_artifact_writer.cpp`

Steps:

1. Retain the current in-memory builder as the small reference path.
2. Add a writer that creates the final artifact header in a temporary file.
3. Enumerate one board, build one table, append it, and release its scratch.
4. Update an incremental payload hash after each table.
5. Persist a sidecar generation checkpoint at a configurable interval.
6. Store identity, profile, next board index, table counts, temporary-file
   length, and rolling hash state in the sidecar.
7. On resume, verify the sidecar against the temporary file before appending.
8. Validate final counts and length before atomic rename.
9. Leave the temporary file and sidecar intact after recoverable interruption.
10. Never replace a verified output with an incomplete artifact.

Acceptance:

- Peak generator memory is bounded independently of total board count.
- Reduced-catalog streaming bytes match the reference serializer exactly.
- Interrupted and resumed generation matches uninterrupted generation.
- Corrupt or mismatched sidecars are rejected before output mutation.

## Work package 3: streaming reader and full inspector

Dependencies: work package 2.

Suggested files:

- Extend the bucket artifact reader under `solver/multiway/abstraction`.
- Extend `examples/multiway_workflow_main.cpp` for the `inspect` workflow.
- Add focused reader and inspector tests.

Steps:

1. Read tables directly from the input stream instead of first loading the
   entire file into a serialized byte vector.
2. Validate schema, identity, profile, table order, table count, and EOF.
3. Validate every bucket ID against its street bucket count.
4. Require `MULTIWAY_INVALID_BUCKET` exactly for board-blocked holes.
5. Count tables and live assignments by street.
6. Recompute the payload hash.
7. Emit a machine-readable inspection report.
8. Return nonzero for any structural, identity, coverage, or hash failure.

Acceptance:

- A complete artifact proves all 2,891,785 board tables are present exactly
  once.
- Reader peak memory does not include a second full serialized copy.
- The inspector reports the three exact street counts.

## Work package 4: versioned config and real bucket workflow

Dependencies: work packages 0 through 3.

Suggested files:

- Add `include/solver/multiway/workflow/multiway_workflow_config.hpp`.
- Add `src/solver/multiway/workflow/multiway_workflow_config.cpp`.
- Update `examples/multiway_workflow_main.cpp`.
- Add parser and command-boundary tests.

Steps:

1. Implement a strict versioned config parser with no ignored unknown fields.
2. Validate semantic fields before opening output files.
3. Bind all model-affecting fields to the existing model identity.
4. Wire `texas_multiway_buckets` to the catalog and streaming writer.
5. Support new generation, verified resume, and refusal to overwrite an
   incompatible completed artifact.
6. Write an artifact manifest after successful inspection.

Required command behavior:

```text
texas_multiway_buckets
  --config configs/multiway/f1_dev_v1.cfg
  --output artifacts/f1_dev_v1/buckets.bin
  --checkpoint-dir artifacts/f1_dev_v1/bucket-checkpoints
```

Acceptance:

- The command either publishes a verified artifact and manifest or exits
  nonzero without publishing a final file.

## Work package 5: persistent full-training checkpoint

Dependencies: work package 4.

Suggested files:

- `include/solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp`
- `src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp`
- `tests/test_multiway_training_checkpoint_artifact.cpp`

Persist:

- schema version and complete model identity;
- training schedule hash and deterministic seed;
- completed batches and trajectories;
- public-state descriptors;
- sparse row shapes;
- regrets and strategy sums;
- coverage counters;
- late-window baseline;
- storage backend identity;
- payload hash.

Steps:

1. Add strict serialization and deserialization.
2. Validate all sizes before allocation.
3. Reject incompatible identity, schedule, seed, or backend.
4. Save through a temporary file and atomic replacement.
5. Restore into a newly constructed training session.
6. Preserve compact-storage values without introducing resume drift.

Acceptance:

- A checkpoint survives process destruction and reload.
- A bounded continuous run and split disk-resumed run export identical ordered
  blueprint bytes and coverage values.

## Work package 6: real training workflow and telemetry

Dependencies: work packages 4 and 5.

Suggested files:

- Update `examples/multiway_workflow_main.cpp` for `train`.
- Add workflow helpers under `solver/multiway/workflow` if main becomes large.
- Extend qualification telemetry without adding hot-path strings or logging.

Steps:

1. Load the frozen config and verified bucket artifact.
2. Build uniform ranges containing all 1,326 hole combinations for all seats.
3. Create the six-player initial preflop root.
4. Construct `MultiwayBlueprintTrainingSession` with compact storage.
5. Resume the latest verified full checkpoint when requested.
6. Run exactly the configured batch count.
7. Save full checkpoints at fixed batch boundaries.
8. Export and atomically save the global sparse blueprint.
9. Report accepted and discarded trajectories.
10. Report rows and visits separately for preflop, flop, turn, and river.
11. Report terminal visits, leaf visits, peak RSS, elapsed time, throughput,
    row growth, and deterministic merge fingerprint.
12. Exit nonzero on capacity exhaustion, discarded work, incomplete street
    coverage, artifact mismatch, or publication failure.

Acceptance:

- The resulting report proves nonzero row coverage on all four streets and
  nonzero terminal visits from a preflop root.

## Work package 7: sizing pilot and config freeze

Dependencies: work package 6.

Execution condition: requires explicit authorization to run a solver workload.

Steps:

1. Run a bounded pilot with the final semantic profile and seed.
2. Record peak public states, rows, values, worker deltas, RSS, disk use, and
   trajectories per second.
3. Set fixed capacity limits with documented headroom.
4. Select fixed trajectories per batch and checkpoint interval.
5. Confirm sufficient disk for the bucket artifact, temporary copy,
   checkpoints, blueprint, and reports.
6. Freeze the final `F1-DEV-12-v1` config and its hash.
7. Do not alter it during later acceptance execution.

Acceptance:

- The checked-in config contains no unresolved or adaptive production values.

## Work package 8: disk checkpoint equivalence gate

Dependencies: work packages 5 through 7.

Execution condition: requires explicit authorization to run tests and a
bounded solver workload.

Procedure:

1. Run `N` batches continuously in process A.
2. Run `N/2` batches in process B.
3. Save the full checkpoint and terminate process B.
4. Start process C and load that checkpoint.
5. Run the remaining `N/2` batches.
6. Compare ordered full-blueprint bytes, payload hash, coverage counters,
   terminal/leaf counts, and merge fingerprint.

Acceptance:

- Every compared value is identical.

## Work package 9: required-lookup qualification

Dependencies: work packages 6 through 8.

The ordinary training counter is not sufficient because training does not use
an external blueprint provider.

Steps:

1. Load the exported full blueprint through
   `MultiwayBlueprintPolicyProvider`.
2. Start deterministic replay from the six-player preflop root.
3. Use fixed deal, trajectory, and action seeds from the config.
4. Route opponent policy queries through the provider.
5. Count hits, missing infosets, missing buckets, and action-menu mismatches
   separately.
6. Record bounded diagnostic identifiers for misses without storing private
   cards.
7. Exit nonzero if any required lookup misses.
8. Repeat the replay and require an identical fingerprint.

Acceptance:

- Missing infosets, missing buckets, and action-menu mismatches are all zero.

## Work package 10: production artifact and training runs

Dependencies: all prior work packages.

Execution condition: requires explicit authorization for long-running bucket
generation and solver workloads.

Bucket procedure:

1. Record the Git commit, compiler, architecture, config hash, and free-space
   preflight.
2. Generate all 2,891,785 tables.
3. Exercise generation resume at least once.
4. Run full inspection.
5. Publish the binary artifact to content-addressed external storage.
6. Check in only its manifest, hashes, size, and retrieval instructions.

Training procedure:

1. Verify the bucket artifact again before training.
2. Run the frozen 50-million-trajectory workload.
3. Save checkpoints at the configured fixed boundaries.
4. Export and inspect the global sparse blueprint.
5. Run disk checkpoint equivalence.
6. Run required-lookup qualification twice.
7. Publish the machine-readable qualification report.

Never add generated multi-gigabyte binaries to the repository.

## Final acceptance gate

F1 passes only when a checked-in report proves all of the following:

```text
bucket_artifact.identity_matches             = true
bucket_artifact.payload_hash_matches         = true
bucket_artifact.flop_tables                  = 22100
bucket_artifact.turn_tables                  = 270725
bucket_artifact.river_tables                 = 2598960

training.started_at_preflop                  = true
training.preflop_rows                        > 0
training.flop_rows                           > 0
training.turn_rows                           > 0
training.river_rows                          > 0
training.terminal_visits                     > 0
training.accepted_trajectories               = configured_trajectories
training.discarded_trajectories              = 0

resume.continuous_payload_hash               = resume.resumed_payload_hash
resume.continuous_coverage_fingerprint       = resume.resumed_coverage_fingerprint

lookup.missing_infosets                      = 0
lookup.missing_buckets                       = 0
lookup.action_menu_mismatches                = 0
lookup.first_replay_fingerprint              = lookup.second_replay_fingerprint
```

Required checked-in evidence:

- frozen configuration and hash;
- bucket artifact manifest and external location;
- bucket inspection report;
- checkpoint equivalence report;
- global training report;
- blueprint manifest;
- required-lookup report;
- exact commands executed;
- validation status and machine specifications.

## Completion updates

After every implemented work package:

1. Add a newest-first factual entry to `docs/PLURIBUS_LOG.md`.
2. List only files changed by that package.
3. Record only validation actually run.
4. Keep F1 marked blocked until work package 10 passes.

After final acceptance:

1. Update `docs/pluribus_solver_finish_audit.md` with the evidence paths.
2. Change the F1 status from implemented-but-unqualified to accepted.
3. Retain the artifact hashes and configuration identity permanently.
4. Do not make a policy-strength claim from the F1 result.
