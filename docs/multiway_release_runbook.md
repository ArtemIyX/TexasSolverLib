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
2. Load the primary checkpoint with
   `MultiwayBlueprintArtifacts::load_with_fallback(primary, known_good, identity)`.
   Both checkpoint/manifest pairs must validate and have the expected identity
   and snapshot hash. If primary fails, use only the validated known-good pair.
   If both fail, do not construct the resolver or accept play.
3. Pass the returned artifact through `MultiwayResolverConfig::verified_blueprint`.
   Do not also set the legacy `blueprint` pointer. Require the bucket registry
   identity to equal the verified artifact identity.
4. Package the known-good checkpoint, its `.manifest`, the bucket registry,
   this profile, and the static legal policy implementation together. Rotate a
   newly evaluated artifact into primary only after retaining the previous
   verified primary as known-good.

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

## Static verification inventory

Existing focused sources cover malformed manifests and identity mismatches
(`test_multiway_artifact.cpp`), missing buckets, malformed requests, deadline
fallback, and legal policy output (`test_multiway_resolver.cpp`), and injected
worker-failure propagation without merge (`test_multiway_recursive_traversal.cpp`).
No additional failure-injection source is needed for this documentation-only
release boundary. Do not run these tests as part of this release-document step.
