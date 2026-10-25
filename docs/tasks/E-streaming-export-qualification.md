# Task E: Streaming export qualification

## Objective

Prove the integrated exporter preserves artifact compatibility and meets the defined bounded-memory qualification target on the matched workload.

## Dependencies

Task D.

## Scope

- Execute the architecture's focused build/test sequence, full build, compact CTest, diff check, and final C++ review.
- Perform the required repeated matched 8-worker, 50-batch, 50,000-trajectory RelWithDebInfo exports only after fresh disk/time/memory authorization and gates.
- Record byte/hash equality against the deterministic reference, peak/after-export process values, private commit, minimum available physical memory, allocation evidence, and duration.
- Update `docs/PLURIBUS_LOG.md` with actual results and limitations.

## Acceptance

- All correctness and transaction tests pass.
- Same-state schema-2 output remains 3,921,243,000 bytes with the reference payload hash, unless a separately approved reference is established.
- Matched export working-set increase is no more than 1 GiB and total process peak is no more than 20 GiB.
- No full-artifact retained capacity or row-count-proportional allocation remains after lease/order setup.

## Stop conditions

Stop and report if bytes/hash differ, loader compatibility breaks, publication is non-transactional, any unbounded owner remains, capacity behavior changes, or any required qualification gate rejects the run.

## Qualification evidence

- Authorized run: 8 workers, 50 batches, 50,000 trajectories, RelWithDebInfo, fixed scheduler, normal binary.
- The qualification runner used `-DisableCheckpoint` so the measured peak covers the integrated streaming export path without the optional full checkpoint snapshot.
- Output: 3,921,243,000 bytes; payload hash `5946562739452589295`; SHA-256 `96F9FD460B1FFF68407A8D2163E0A28DA7D079767021E62680B92BF1D93DAFA2`.
- All 50,000 trajectories were accepted. Publication mode was `rename`; process and system export gates passed.
- External wrapper peak working set was 18,618,073,088 bytes. The report peak working set was 18,700,632,064 bytes.
- System guard telemetry reported 66,156,724,224 total physical bytes and 64,546,111,488 maximum used bytes, below the 1.5 GiB reserve threshold.
- Tracy evidence smoke completed with allocation events, CSV export, and a 222,261,090-byte trace. Free events are optional because capture may end with active allocations.

### Checkpoint-enabled follow-up

- Run directory: `tmp/task-e-qualification-checkpointed`.
- Checkpoint creation was enabled. The run accepted all 50,000 trajectories and wrote a 4,576,234,700-byte checkpoint.
- Checkpoint file SHA-256: `936BDD5AB1F5923B81FF60D0A8C54E214AAA6B911B4332657C621C72C9BACC50`.
- Checkpoint write duration: 63,375,287,700 ns. Publication completed without temporary-file residue.
- Observed peak working set was 18,706,137,088 bytes and peak RSS was 17,932,279,808 bytes.
- Process and system gates passed; the final policy remained 3,921,243,000 bytes with payload hash `5946562739452589295`.

### Limitation

The qualification used the existing RelWithDebInfo binary `B6E36BFAF97740C13BF0E5B68103BED083E9F1772FC485C2E3C72AD015A253CB`, built before the final checkpoint-report telemetry commit. Its runtime report contains checkpoint size and write duration, while focused report tests cover the newer planned-size, scratch-size, payload-hash, publication, and gate fields. Rebuild and repeat the large run only if those extended runtime fields are required.
