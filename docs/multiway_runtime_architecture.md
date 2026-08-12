# Multiway Runtime Architecture

This document describes the supported six-max postflop runtime boundary. It is
for deployment hosts and maintainers, not a poker-client integration guide.

## Request flow

1. The host derives `MultiwayModelIdentity` from its frozen release profile.
2. The host verifies and loads the full blueprint, compact root fallback, and
   future-bucket artifact against that identity.
3. The host constructs `MultiwayResolverConfig` with the bucket registry, full
   blueprint, verified compact artifact, typed leaf evaluator, deterministic
   `MultiwaySolverLimits`, and `MultiwayMemoryBudget`.
4. The host supplies exact public state, hero cards, ranges, deadline, and a
   protected per-decision seed through `MultiwayResolverRequest`.
5. `MultiwayResolver::resolve` reconstructs the legal menu, checks admission,
   runs search from clean batches when eligible, and returns a normalized legal
   policy plus diagnostics.

`DefaultSearch` is the production default. It activates runtime search only
when the release profile is complete. `SearchActive` requires the same explicit
search configuration. `SearchShadow` preserves legacy delivered policy while
recording comparison diagnostics. `LegacyStatic` is retained for rollback and
differential tests. `ForcedFallback` skips search.

## Ownership map

| Concern | Owner | Boundary |
| --- | --- | --- |
| Rules, state transitions, and action legality | `games/multiway_*` | `MultiwayState`, `MultiwayActionAbstraction` |
| Canonical public state and menu identity | `MultiwayPublicBuilder` | `MultiwayPublicStateDescriptor` |
| Private-range compilation and blockers | `games/multiway_private.hpp` | `MultiwayPrivateConfig` |
| Terminal settlement | `games/multiway_terminal.hpp` | `settle_multiway_terminal` |
| Sparse rows and deterministic merge | `MultiwaySolverCoordinator` | worker-local delta streams |
| Runtime traversal and clean snapshots | `MultiwayRootExternalSamplingTraversal`, `MultiwaySearchSession` | root-only export |
| Resolver policy normalization | `multiway_resolver_policy.*` | scalar reference kernel |
| Legacy deterministic adjustment | `multiway_legacy_resolver.*` | explicit legacy/shadow modes only |
| Artifact verification and public audit | `multiway_artifact.*` | verified artifact and decision-log types |

Do not merge HUNL helpers into these paths solely because their names are
similar. HUNL and multiway utility, legality, and range contracts differ.

## Model identity

Every persisted or runtime artifact must use an exact `MultiwayModelIdentity`.
The fields are stable FNV-1a compatibility fingerprints, not security hashes.

| Field | Binds |
| --- | --- |
| `rules_hash`, `rules_schema_hash` | game rules and their schema |
| `action_abstraction_hash` | legal-menu abstraction |
| `bucket_model_hash` | root bucket model |
| `terminal_model_hash` | terminal utility and leaf model |
| `resolver_schema_hash`, `code_schema_hash` | resolver and code contracts |
| `range_semantics_hash` | range interpretation and blocker semantics |
| `future_bucket_model_hash` | continuation bucket artifact |
| `off_tree_policy_hash` | off-tree policy behavior |
| `continuation_policy_hash` | continuation-leaf policy |
| `runtime_search_schema_hash` | runtime-search contract |
| `combined_hash` | complete identity tuple |

Any mismatch rejects the artifact or search path. The host must rebuild and
reevaluate the full and compact artifacts after changing a bound input.

## Result interpretation

`MultiwayResolverStatus` reports request outcome:

| Status | Meaning |
| --- | --- |
| `Solved` | Runtime or explicit legacy mode produced a full policy. |
| `Partial` | Runtime search exported the latest clean snapshot at deadline. |
| `DeadlineFallback` | Deadline reserve was reached; fallback policy was returned. |
| `InvalidRequest` | Invalid input; no action is sampled. |
| `ArtifactMismatch` | Identity/artifact mismatch; valid requests use fallback. |
| `BucketUnavailable` | No compatible postflop bucket; valid requests use fallback. |
| `ResourceExhausted` | Search could not produce a clean result; fallback was used. |
| `RejectedByBudget` | Admission, limits, or forced fallback rejected search. |

`policy_provenance` identifies the actual source: runtime search, legacy
adjustment, stable-root fallback, compact-blueprint fallback, or static legal
fallback. Never infer provenance from status alone.

## Operations

- Use the limits and identity versions from
  `docs/multiway_release_config.json`; the host maps JSON to C++ types.
- Enforce the 48 GiB warning, 56 GiB operating cap, 60 GiB rejection cap,
  15-second internal deadline, and 100 ms reserve from the release profile.
- Export only the latest clean root snapshot. Workers own bounded local deltas;
  the coordinator owns all row mutation and fixed-order merge.
- Preserve the full blueprint and compact root artifact as one identity-matched
  promotion and rollback set. Static legal policy remains the last fallback for
  valid requests.
- Keep cards, ranges, raw deltas, and protected seeds out of public logs.
  Use `MultiwayPublicDecisionLog` for public audit and protected replay records
  only in protected storage.
- For bitwise replay, keep request, seed, worker count, limits, and batch
  partition identical. Different worker counts require policy-tolerance
  comparison rather than a bitwise claim.

The detailed startup, failure handling, promotion, and rollback procedure is
in [multiway_release_runbook.md](multiway_release_runbook.md).
