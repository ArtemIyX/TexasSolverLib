Task: Measure where coordinator admission spends time and how many candidates are redundant; Codex can add telemetry, but an operator must run the diagnostic workload and provide the report.

# AD1 - Coordinator admission diagnostics

## Goal

Replace speculation about coordinator admission with counts and timings for
each source of duplicate, parent, lock, and catalog work.

## Scope

Record per worker and globally:

- raw state and row candidates;
- worker-local unique candidates;
- globally unique candidates;
- already-admitted candidates;
- newly admitted candidates;
- parent-resolution passes and probes;
- descriptor reconstructions;
- lock acquisitions;
- catalog index insertion time;
- row admission time.

## Agent work

- Add counters and phase timers outside traversal inner loops.
- Propagate values into the training report and JSON output.
- Preserve deterministic merge behavior and existing accounting.
- Avoid logging or allocation in hot traversal paths.

## Operator work

- Build the diagnostic target.
- Run one-worker and four-worker captures using the same workload and seed.
- Provide candidate counts, admission timings, and merge fingerprints.

## Acceptance

- Counters reconcile with admitted state and row totals.
- Worker and coordinator timings are internally consistent.
- No strategy identity or merge fingerprint changes.

## Dependency

None, but AD1 should precede AD2-AD6 because it provides the optimization
baseline.
