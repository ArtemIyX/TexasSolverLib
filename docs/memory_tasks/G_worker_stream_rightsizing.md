# Task G: Right-size worker stream capacities

## Trigger

`Fully implement G.`

## Objective

Replace shared per-worker capacity ceilings with explicit capacities for
regret deltas, continuation deltas, discovery public states, and discovery
rows. Preserve preallocation and allocation-free traversal.

## Evidence gate

Task D must select G. Worker-pool construction private bytes and Task B stream
capacity totals must dominate the one-to-four-worker slope.

If D does not select G, do not implement this task.

## Required design

Introduce explicit limits:

```text
max_regret_delta_entries_per_worker
max_continuation_delta_entries_per_worker
max_discovery_public_states_per_worker
max_discovery_rows_per_worker
```

Compatibility:

- old `worker_delta_capacity` initializes both delta capacities when new keys
  are absent;
- old `discovery_capacity` initializes both discovery capacities when new keys
  are absent;
- serialization and profile identity must distinguish explicit new values;
- old configs remain accepted with unchanged behavior.

Select new F1 development values from Task D observed peaks plus a documented
safety margin. Do not guess values from current live counts alone. Include
failed/discarded attempts and all captured batches.

### F1 development sizing

Task D `baseline-final` covered every captured batch in the five bounded cases,
including failed/discarded-attempt telemetry. The maxima were 28,390 regret
delta entries, 0 continuation delta entries, 99,262 discovery public states,
and 47,173 discovery rows per worker. No failed batch or discarded trajectory
was present in the evidence.

The development capacities are therefore 64,000 regret entries, 1,024
continuation entries, 200,000 discovery public states, and 100,000 discovery
rows per worker. These are approximately 2.25x, a nonzero continuation floor,
2.01x, and 2.12x the observed maxima respectively. The continuation floor
also protects the separate stream from an unobserved nonzero batch while
remaining much smaller than the legacy shared delta reservation. The values
are based on all `report.json` files and the A3/A4 Tracy captures recorded in
`tmp/memory-attribution/baseline-final/evidence.md`, not on live counts alone.

## Required changes

1. Extend workflow config parsing, validation, and serialization.
2. Extend `MultiwaySolverLimits` and identity/versioning as required.
3. Construct each stream with its specific capacity.
4. Split capacity exhaustion telemetry by stream type while retaining a
   compatibility aggregate if existing readers require it.
5. Update preflight estimates and Task B capacity attribution.
6. Update F1 development config only after bounded evidence establishes safe
   values.
7. Preserve fixed-size preallocation before worker execution.

Never use unreserved `push_back` in hot paths.

## Tests

Add tests for:

- legacy config fallback;
- explicit config parsing and round-trip;
- zero/overflow/invalid limits;
- independent exhaustion of each stream;
- transactional failed batch behavior;
- no allocations during traversal after construction;
- 1/2/4-worker deterministic merge equivalence;
- fixed/dynamic equivalence;
- preflight and Task B totals use the four distinct capacities;
- schedule/profile identity changes when an explicit capacity changes if that
  value participates in compatibility identity.

Primary tests:

- `tests/test_multiway_workflow_config.cpp`
- `tests/test_multiway_parallel_training.cpp`
- `tests/test_multiway_solver.cpp`
- `tests/test_multiway_memory.cpp`
- `tests/test_multiway_qualification_reports.cpp`

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\compact_build.py `
  --target test_multiway_workflow_config `
  --target test_multiway_parallel_training `
  --target test_multiway_solver `
  --target test_multiway_memory `
  --target test_multiway_qualification_reports `
  -o tmp\memory-task-g-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_workflow_config|test_multiway_parallel_training|test_multiway_solver|test_multiway_memory|test_multiway_qualification_reports)$"

.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
```

Rerun all five Task D cases for three normal repeats. Capture A3 and A4 with
Tracy because worker construction and steady retention are the targets.

## Acceptance

- Old configs retain old capacity behavior.
- New capacities are explicit, validated, and reported.
- Traversal remains allocation-free after construction.
- No bounded run exhausts capacity or discards trajectories.
- Fingerprints and fixed/dynamic outputs match baseline.
- Four-worker private bytes and worker capacity bytes decrease beyond noise.
- One-worker memory does not regress materially.
- Throughput does not materially regress.
- Full build/CTest and review pass.
- Project log records old/new capacities and measured memory changes.

## Completion statement

Task G is complete only after config compatibility, deterministic tests, and
the full bounded Task D matrix pass with measured private-memory reduction.

## Stop conditions

Stop if evidence does not support separate limits, if any bounded case
exhausts capacity, if hot-path allocations appear, or if identity compatibility
cannot be resolved safely.

