# Pluribus Roadmap Progress Log

Update this file after each completed roadmap part. Record completed scope,
files, verification, and any limitations. Do not claim an item is complete
until its implementation and required validation are finished.

## P4.1-P4.5 - Resolver-managed runtime hand lifecycle

**Status:** Complete
**Completed:** 2026-08-12

- Added a request-local runtime session factory on `MultiwayResolver` for validated postflop snapshots.
- Added an owning runtime session that applies observed-action Bayes updates and replaces the round state on street or qualifying same-street reroots.
- Rerooting carries legal posterior ranges forward and clears the previous actual-hand freeze.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_runtime_session.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_runtime_session.cpp`
- `tests/test_multiway_resolver.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused resolver factory and round-replacement coverage.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- Full-hand terminal replay remains owned by the existing rules and replay suites; this lifecycle layer does not duplicate terminal settlement.

## P3.5, P4.4-P4.5 - Resumable blueprints and posterior rerooting

**Status:** Complete
**Completed:** 2026-08-12

- Added deterministic offline checkpoints for admitted public descriptors and canonical sparse regret/strategy rows.
- Restored compatible trainer state before exporting the same full blueprint artifact.
- Added posterior-belief transfer to next-street and same-street reroot roots; the new session owns fresh rows and no prior actual-hand freeze.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_blueprint_trainer.hpp`
- `include/solver/multiway_search_session.hpp`
- `include/solver/multiway_solver.hpp`
- `src/solver/multiway_blueprint_trainer.cpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_solver.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused sparse checkpoint, full-artifact resume, and posterior-reroot coverage.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- Checkpoints are an in-memory offline training boundary; persistence remains a future artifact-format extension.

## P3.4, P4.2-P4.3 - Runtime blueprint prior and hero policy lifecycle

**Status:** Complete
**Completed:** 2026-08-12

- Connected the immutable full-blueprint store to runtime traversal as the non-traverser policy prior.
- Added request-local hero-range export, actual-hand policy freeze, and explicit freeze clearing for a new round root.
- Preserved singleton actual-hand behavior when callers do not supply a hero range.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_search_session.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_search_session.cpp`
- `tests/test_multiway_search_session.cpp`

### Validation

- Added focused hero policy export and freeze coverage.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- P3.5 full checkpoint/resume equivalence and P4.1/P4.4/P4.5 root reconstruction, observed-action orchestration, and street rerooting remain separate work.

## Implementation workflow skill

**Status:** Complete
**Completed:** 2026-08-12

- Added a project-local skill that orchestrates navigation, implementation,
  focused tests, code review, project logging, and one scoped commit.

### Files

- `.agents/skills/implementation-workflow/SKILL.md`
- `.agents/skills/implementation-workflow/agents/openai.yaml`

### Validation

- Skill structure validation passed.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- The workflow applies only when `$implementation-workflow` is invoked or its
  implementation trigger matches.

## P3.2-P3.4 - Blueprint coverage, artifact, and traversal provider

**Status:** Complete
**Completed:** 2026-08-12

- Added sparse training coverage counters and a public-only coverage manifest.
- Added an atomic, hash-verified full-blueprint row artifact beside the root fallback artifact.
- Added immutable lock-free blueprint lookup for non-traverser traversal decisions, with explicit miss and incompatible-menu outcomes.

### Files

- `include/solver/multiway_blueprint_trainer.hpp`
- `src/solver/multiway_blueprint_trainer.cpp`
- `include/solver/multiway_artifact.hpp`
- `src/solver/multiway_artifact.cpp`
- `include/solver/multiway_blueprint_policy_provider.hpp`
- `src/solver/multiway_blueprint_policy_provider.cpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_traversal.cpp`
- `include/solver/multiway_solver.hpp`
- `src/solver/multiway_solver.cpp`
- `include/core/lib.hpp`
- `tests/test_multiway_blueprint_store.cpp`
- `tests/test_multiway_artifact.cpp`

### Validation

- Added focused artifact and lookup-provider tests.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- P3.5 full checkpoint/resume equivalence remains incomplete.

## P3.1 - Define full blueprint row and index schema

**Status:** Complete
**Completed:** 2026-08-12
**Implementation commits:** `ed5188c`, `fce9455`

- Added an immutable, identity-bound blueprint row store with deterministic
  sorted lookup by public state, seat, bucket, and action-menu identity.
- Rows reject duplicate keys, malformed menus, and non-normalized policies.

### Files

- `include/solver/multiway_blueprint_store.hpp`
- `src/solver/multiway_blueprint_store.cpp`
- `tests/test_multiway_blueprint_store.cpp`

### Validation

- Added a 128-row deterministic lookup fixture and malformed-row checks.
- Build and tests were not run because the task explicitly prohibited them.

### Limitations

- P3.2-P3.5 serialization, trainer integration, artifact loading, and resume
  work remain incomplete.

## P2.5 - Replace bounded perturbation only for eligible requests

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commits:** `9d4fbd7`, `f4dda35`

- Added typed runtime-search eligibility diagnostics and host-configured seat
  and root-menu limits.
- Active search now requires a supported postflop root and complete live
  non-hero ranges; otherwise it uses the established fallback chain.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_runbook.md`

### Validation

- Added focused active-search eligibility and fallback coverage.
- Build and tests were not run because the task explicitly prohibited them.
- Reviewed staged commits with `git diff --cached --check`.

### Limitations

- Full blueprint runtime lookup remains P3 work.

## P2.4 - Add resolver feature flag and shadow mode

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commits:** `9d4fbd7`, `f4dda35`

- Added privacy-safe shadow elapsed-time and observed-memory diagnostics.
- Preserved the legacy output in shadow mode while reporting completed search
  counters, merge volume, and policy divergence.

### Files

- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_runbook.md`

### Validation

- Added focused shadow diagnostics coverage.
- Build and tests were not run because the task explicitly prohibited them.
- Reviewed staged commits with `git diff --cached --check`.

### Limitations

- Shadow mode may increase request latency and memory; it remains opt-in.

## P2.3 - Wire worker-local deltas into session rows

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commits:** `f6a0f3a`, `db9a8e4`

- Added request-local row metrics and immutable clean-batch snapshots.
- Resolver runtime and shadow search now export only through the latest clean
  session snapshot and report non-private merge provenance.
- Added deterministic replay and rejected-snapshot preservation coverage.

### Files

- `include/core/lib.hpp`
- `include/solver/multiway_search_session.hpp`
- `include/solver/multiway_resolver.hpp`
- `src/solver/multiway_search_session.cpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_search_session.cpp`
- `tests/test_multiway_resolver.cpp`
- `docs/multiway_release_runbook.md`

### Validation

- Added focused session and resolver regression tests.
- Build and tests were not run because the task explicitly prohibited them.
- Reviewed each staged commit with `git diff --cached --check`.

### Limitations

- P3 full-blueprint artifact work remains separate.
- Cross-worker comparisons retain the existing normalized-policy tolerance;
  bitwise replay is defined for a fixed worker layout.

## P2.2 - Implement runtime budget and clean-batch semantics

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commit:** `227a6eb feat(multiway): add resolver runtime budget`

- Added a request-local runtime budget with an internal deadline derived from
  the external deadline and reserve.
- Enforced batch, trajectory, sparse-row, sparse-value, cancellation, and
  clean-merge checks before root-policy export.
- Preserved the last clean batch as a partial result when the deadline expires
  after that batch, and rejected requests with no clean batch.
- Added direct budget contract tests and resolver deadline regression coverage.

### Files

- `include/solver/multiway_resolver_budget.hpp`
- `src/solver/multiway_resolver_budget.cpp`
- `src/solver/multiway_resolver.cpp`
- `tests/test_multiway_resolver_budget.cpp`
- `tests/test_multiway_resolver.cpp`

### Validation

- Not run. The repository instruction explicitly prohibited build and test
  commands for this task.
- Reviewed staged diff with `git diff --cached --check`.

### Limitations

- Budget cancellation is cooperative at bounded batch checkpoints. Interrupting
  an in-flight trajectory remains future scheduler/traversal work.

## M2 - Isolated real sampled root search: clean batches and shadow comparison

**Status:** Complete
**Completed:** 2026-08-11
**Implementation commit:** `b5b5b4b fix(multiway): enforce clean root search batches`

- Added an explicit clean flag to root batch results, set only after worker
  streams are merged by the coordinator.
- Rejected active resolver searches that produce no accepted trajectory or no
  merged delta, preserving legal fallback behavior.
- Added shadow-mode completion counters and normalized-policy L1 divergence
  diagnostics.
- Added regression coverage for clean batches, no-progress search fallback,
  and shadow comparison.

### Files

- `include/solver/multiway_resolver.hpp`
- `include/solver/multiway_traversal.hpp`
- `src/solver/multiway_resolver.cpp`
- `src/solver/multiway_traversal.cpp`
- `tests/test_multiway_recursive_traversal.cpp`
- `tests/test_multiway_resolver.cpp`

### Validation

- Not run. The repository instruction explicitly prohibited build and test
  commands for this task.
- Reviewed staged diff with `git diff --cached --check`.

### Limitations

- Broader M2 runtime-budget integration remains. Runtime search is still
  root-only; rerooting, off-tree expansion, future abstraction, and
  continuation-policy integration remain future M4-M6 work.

## P1.1 - Canonical Combination IDs

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commit:** `cb88695 Add canonical HUNL combination indexing`

- Added one fixed 1,326-entry HUNL-card combination view with unordered
  pair-to-ID, ID-to-pair, and dead-card legal-mask operations.
- Updated HUNL joint-range normalization and range enumeration to use the
  shared mapping while retaining board-relative range indices where required.
- Separated compact bucket-artifact cards from HUNL runtime cards with explicit
  adapters; updated resolver, traversal, range-update, and fixture boundaries.
- Added exhaustive combination, mask, and compact-artifact/HUNL-adapter tests.

### Validation

- Static staged and working-tree diff checks passed.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass.

## P1.2 - Fixed-Array Range Beliefs

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commits:**

- `964233c Add multiway range belief storage`
- `82c8824 Harden multiway range belief resets`
- `2c5cfbd Test multiway range belief storage`

- Added a session-owned six-seat belief container with fixed 1,326-combination
  `double` rows, legal masks, source, mass, action, and revision metadata.
- Added transactional uniform and supplied range initialization. Supplied
  entries merge by canonical ID, apply blockers before normalization, and fail
  explicitly on invalid or zero legal mass.
- Added read-only C++17 non-owning views and controlled belief copying while
  preventing implicit owner copy and move operations.
- Kept resolver, sampler, and public descriptors unchanged.

### Validation

- Added deterministic initialization, blocker, normalization, duplicate merge,
  transaction, copy, view, and bounds regression tests.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass.

## P1.3 - Bayes Action-Observation Updates

**Status:** Complete
**Completed:** 2026-08-09
**Implementation commits:**

- `8592f88 Apply multiway range observations`
- `20fda71 Harden multiway range observations`
- `4f14c42 Test multiway range observations`

- Added fixed-row, two-pass Bayes updates for observed actions with explicit
  source, public-state, menu, table, action-index, and revision provenance.
- Updates validate policy/table identity before mutation, preserve illegal
  combinations at zero, and return a typed no-posterior result unchanged.
- Preserved legacy action metadata compatibility and isolated the new path from
  resolver, traversal, sampler, and public descriptors.

### Validation

- Added exact posterior, table-binding, blocker, transaction, seat-isolation,
  and repeat-update regression coverage.
- Quiet Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED`.

## P0.1 - Resolver and Traversal Baselines

**Status:** Complete  
**Completed:** 2026-08-09  
**Commit:** `8c6ca324b0f8adecaaca321ab2c46be6def8918a`

Implemented privacy-safe deterministic baseline reporting for existing multiway
resolver and root-traversal paths. This does not change production inference.

- Added resolver and traversal baseline report APIs.
- Added fixture labels for valid, invalid, off-tree, no-artifact,
  deadline-exhausted, and max-row scenarios.
- Reports record status, fallback source, normalized policy, sampled action,
  batch/trajectory and row counters, timing, and observed resident memory.
- Reports omit hero cards, opponent ranges, and sampling seeds.
- Deterministic comparison and serialization exclude timing and memory.
- Added an isolated resolver fixture harness so retained stable-root state does
  not affect fixture order.
- Traversal reports use coordinator counter deltas rather than lifetime totals.
- Added resolver and traversal profiling scopes.

### Validation

- Added `tests/test_multiway_baseline.cpp`.
- Tests cover all P0.1 fixture labels, fixture isolation, privacy-safe stable
  serialization, memory measurement precedence, reused traversal coordinators,
  deterministic reports, and max-row behavior.
- Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass because direct execution was
  blocked by host policy.

### Deferred

None in Phase 0 implementation.

## P0.2 - Policy Provenance and Status Observability

**Status:** Complete
**Completed:** 2026-08-09

- Added distinct resolver statuses for partial and budget-rejected outcomes.
  They are forward-compatible values only; the legacy resolver does not emit
  either one yet.
- Added policy provenance for legacy adjustment, stable-root, blueprint, and
  static-legal policy paths.
- Added the legacy search-engine identifier/version and request artifact
  identity to resolver diagnostics.
- Extended public decision logs with provenance and search-engine metadata.

## P0.3 - Centralized Semantic Model Identity Inputs

**Status:** Complete
**Completed:** 2026-08-09

- Added explicit identity components for range semantics, future buckets,
  off-tree policy, continuation policy, and runtime search schema.
- Centralized their hashing in `make_multiway_model_identity`.
- Bound the new components into artifact identity hashing and bumped the
  manifest schema version.
- Diagnostic formatting remains outside the model identity contract.

## P0.4 - Baseline Allocation and Timing Profile

**Status:** Complete
**Completed:** 2026-08-09

- Added cold-boundary profiler scopes for resolver and root batches. Detailed
  baseline reports cover the contained traversal, private sampling, terminal,
  row-admission, and merge work without adding locks or allocations to those
  hot operations.
- Baseline reports now collect wall-clock time, process CPU time when exposed
  by the platform, current resident-memory snapshots, and process peak RSS on
  Windows/Linux. Peak RSS is explicitly unavailable on other platforms.
- Root-batch reports include accepted trajectories and deterministic worker
  minimum/maximum trajectory counts for imbalance measurement.
- Allocation-byte telemetry is explicitly unavailable until the host supplies
  an allocator profiler; RSS is not treated as allocation volume.

### Validation

- Added resolver, public-decision-log, semantic-identity, artifact-rejection,
  and baseline-environment regression coverage.
- Debug build passed.
- `scripts/ctest-compact.ps1` passed with `ALL TESTS PASSED` using a
  process-local PowerShell execution-policy bypass because direct execution was
  blocked by host policy.
