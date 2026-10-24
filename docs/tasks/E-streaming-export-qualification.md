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
