# Multiway Release Runbook

`multiway_release_config.json` is a deployment profile. The library does not
parse it. The host maps it to `MultiwayGameRules`, `MultiwayBlueprintConfig`,
`MultiwayMemoryBudget`, and `MultiwayResolverConfig` before startup.

## Release profile

- Use `MultiwayGameRules::standard_6max()`: six seats, 10,000-chip stacks,
  50/100 blinds, no ante, straddle, rake, or rebuys.
- Use the 96/128/192 flop/turn/river bucket profile and the version values in
  the tracked configuration. Build the expected `MultiwayModelIdentity` from
  that configuration. Do not copy or invent its hashes.
- Keep preflight warning at 48 GiB. Do not admit a solve whose estimated
  residency can exceed the 56 GiB operating cap. 60 GiB is the host hard cap:
  stop new work before it is reached and never raise the release profile above
  it. `MultiwayMemoryBudget` supports the 48/60 GiB warning/reject guard;
  the host must enforce the stricter 56 GiB resident cap.
- The external turn deadline is 20,000 ms. The host passes a resolver deadline
  15,000 ms after request receipt. Set `MultiwayResolverConfig::deadline_reserve`
  to 100 ms. The remaining five seconds belong to input validation, fallback,
  logging, and caller transport.

## Startup and artifact recovery

1. Validate rules, blueprint configuration, solver limits, memory preflight,
   bucket registry, and expected model identity before accepting requests.
2. Derive the expected identity from the frozen release profile. Verify the
   future-bucket model before either blueprint artifact; it must carry that
   exact identity.
3. Load the full blueprint primary with
   `MultiwayFullBlueprintArtifacts::load_verified`. On failure, load only its
   validated known-good full-blueprint counterpart. Verify identity and payload
   hash before retaining either result. If both fail, disable runtime search.
4. Load the compact root fallback with
   `MultiwayBlueprintArtifacts::load_with_fallback(primary, known_good, identity)`.
   Both checkpoint/manifest pairs must validate and have the expected identity
   and snapshot hash. If both fail, disable blueprint and root fallback lookup,
   but retain static legal fallback for valid requests.
5. Build a `MultiwayBlueprintStore` from the verified full artifact and pass it
   through `MultiwayResolverConfig::full_blueprint`; pass the compact artifact
   through `verified_blueprint`. Do not set the legacy `blueprint` pointer.
   Require the bucket registry, full blueprint, and root fallback identities to
   match.
6. Package both primary and known-good variants of the full blueprint and root
   fallback, root manifests, future-bucket model, this profile, and the static
   legal policy implementation together. Promote a candidate only after it
   passes identity/hash verification and evaluation; retain the prior verified
   pair as known-good for rollback.

## Request, fallback, and failure handling

- Submit only exact public state, hero cards, legal-action history, generic
  opponent ranges, an absolute internal deadline, and a protected per-decision
  seed through `MultiwayResolverRequest`.
- For valid requests, fallback priority is: compatible latest stable root,
  compatible blueprint policy, then the static legal policy. Bucket absence,
  identity mismatch, or deadline expiry must return a normalized action from
  that chain and set the resolver diagnostics accordingly.
- `InvalidRequest` has no sampled action. Reject malformed cards, ranges,
  state, menu, or deadline before action delivery. Do not substitute an action
  outside the resolver's reconstructed legal menu.
- A failed artifact load, missing bucket registry at a postflop root, worker
  exception, exhausted solve limit, non-finite policy, or resident-cap breach
  stops new solve work. Preserve the last valid result only when its identity,
  public-state id, and action menu match; otherwise use the static legal policy
  for a valid resolver request. Record the failure and remove the bad artifact
  from promotion candidates.
- Do not retain hero cards or opponent ranges in public logs. Emit
  `MultiwayPublicDecisionLog` for every delivered decision. Create
  `MultiwayProtectedReplayRecord` only in protected storage; it binds the
  model identity, public history, and per-decision seeds with an integrity hash.
- Create `MultiwayAivatEvaluationRecord` only in the same protected boundary.
  It binds public history, sampled actions, policies, action-value estimates,
  raw chip outcomes, model identity, and deterministic seeds with an integrity
  hash for an external estimator. Do not emit it through decision logs or pass
  it into resolver traversal; this release does not claim estimator correctness.
- Runtime search rows belong to one `MultiwaySearchSession`. Workers write only
  bounded local delta streams; coordinator merge is the sole row-mutation path.
  Export a runtime policy only from the latest clean session snapshot after a
  complete merge. Snapshot diagnostics may include row/value counts, root
  revision, trajectory interval, worker count, and merged-entry count, but not
  cards, ranges, raw deltas, or seeds.
- Reproduce a runtime-search fixture with the same request, seed, worker count,
  limits, and batch partition. This is the bitwise replay contract. Comparisons
  across different worker counts use normalized-policy tolerance and must not be
  claimed bitwise equivalent by default.
- Use `ReleaseDefault` for a validated release configuration. It delivers
  active search only when verified root and full-blueprint artifacts, buckets,
  and complete runtime-search configuration are present; otherwise it uses the
  normal safe fallback chain. `FallbackOnly` delivers that same fallback chain
  without runtime search and is retained for rollback and differential
  comparison. Use `SearchShadow` to retain the fallback-delivered policy while
  recording only policy L1 divergence, completed search counters,
  elapsed time, and observed process memory. Shadow diagnostics never contain
  cards, ranges, raw deltas, or seeds.
- `ReleaseDefault` and `SearchActive` require explicit seat and root-menu limits through
  `active_search_min_seats`, `active_search_max_seats`, and
  `active_search_max_menu_actions`. Active search requires a supported
  postflop root, complete live ranges for every non-hero seat, and a clean
  batch. Ineligible or unsuccessful requests use the normal fallback chain.
- Treat `off_tree_mode`, `continuation_mode`, the search-budget fields, and
  deterministic-mode fields as compatibility inputs. A change to any of them
  invalidates the expected model identity and requires rebuilding and
  reevaluating both full and root artifacts.

## Stabilization and freeze

- Freeze rules, model identity/version inputs, bucket tables, action menus,
  continuation policy, training seed, decision-seed derivation, and replay
  schema before release evaluation.
- During stabilization, accept only fixes with a deterministic replay fixture.
  Any change to a frozen semantic input invalidates the artifact identity and
  requires rebuilding artifacts and repeating evaluation before promotion.
- Quarantine any release candidate that emits an illegal action, misses the
  internal deadline, exceeds the operating cap, produces non-normalized or
  non-finite policy, fails artifact verification, or cannot replay exactly.

## Promotion and rollback

1. Stage a full blueprint, root fallback, root manifest, future-bucket model,
   and release profile as one candidate set. Verify every identity and hash
   before changing the active set.
2. Run the host dry-run for missing, corrupt, and identity-mismatched full and
   root artifacts. Confirm that a valid request still receives static legal
   fallback when both non-static artifacts are unavailable.
3. Promote the verified candidate set atomically. Keep the previous full and
   root artifact set as known-good.
4. Roll back the full blueprint and root fallback together. Never combine a
   root snapshot, future-bucket model, or full blueprint from different model
   identities.

## Static verification inventory

Existing focused sources cover malformed manifests and identity mismatches
(`test_multiway_artifact.cpp`), missing buckets, malformed requests, deadline
fallback, and legal policy output (`test_multiway_resolver.cpp`), injected
worker-failure propagation without merge (`test_multiway_recursive_traversal.cpp`),
and sealed AIVAT record integrity plus protected sink boundaries
(`test_multiway_aivat_record.cpp`). Do not run these tests as part of this
release-document step.
