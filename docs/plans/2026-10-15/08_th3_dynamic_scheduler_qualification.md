Task: Select a dynamic scheduling chunk size that reduces tail idle time without adding material worker CPU overhead; Codex can prepare the scheduler changes, but an operator must run the qualification matrix.

# TH3 - Dynamic scheduler qualification

## Goal

Measure and select the smallest dynamic chunk policy that improves worker
balance while preserving deterministic trajectory assignment and merge order.

## Scope

After TH2 capacity repair:

- sweep chunk sizes 1, 2, 4, 8, and 16;
- measure queue overhead;
- measure worker critical path;
- measure tail idle time;
- measure aggregate worker CPU;
- include scheduler policy in identity or versioning;
- preserve global trajectory IDs and fixed-order stream merging.

## Agent work

- Verify scheduler state and identity contracts.
- Add or refine chunk-size configuration.
- Preserve deterministic per-trajectory seeds.
- Add coverage for chunk assignment and fixed/dynamic equivalence.

## Operator work

- Run the chunk-size sweep with multiple batches and repeats.
- Compare fixed and dynamic scheduling at 1, 2, 4, and 8 workers.
- Provide tail idle, queue overhead, CPU, throughput, and fingerprints.

## Acceptance

- Maximum tail idle approaches one worst-case chunk.
- Aggregate worker CPU does not materially increase.
- Deterministic merge identity is unchanged.
- The selected chunk policy is documented and versioned.

## Dependency

TH1-TH2 must establish valid steady-state measurements and safe capacity
semantics.

## Implementation status

Completed 2026-10-23:

- Added focused scheduler coverage for chunk sizes 1, 2, 4, 8, and 16.
- Added fixed-versus-dynamic merge equivalence coverage for every chunk size.
- Added bounded runner filters for workers, scheduler modes, and pinning mode.
- Added `scripts/analyze_multiway_th3.ps1` to validate chunk coverage, metrics,
  fingerprints, and fixed/dynamic equivalence.
- Fixed sample-metric parsing in `scripts/analyze_multiway_steady_state_matrix.ps1`.

The full multi-repeat operator qualification remains a separate measurement
step. A bounded smoke run is not used to select the production chunk policy.
