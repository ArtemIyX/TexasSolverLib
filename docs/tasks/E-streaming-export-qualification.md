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

### Limitation

The default checkpoint-enabled path still materializes the existing full checkpoint snapshot and previously measured above 20 GiB during checkpointing. Tasks A-E preserve that behavior; reducing that separate peak requires a bounded checkpoint-writer task.
