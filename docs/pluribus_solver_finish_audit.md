# Pluribus solver finish audit

Updated: 2026-08-27

## Scope

Target: a reproducible, CPU-oriented, six-player Pluribus-style solver that can
train a global blueprint, load it, resolve postflop decisions, and measure policy
quality. Original-Pluribus identity and superhuman strength are not assumed.

Sources:

- [Technical requirements](pluribus_solver_technical_report.md)
- [Declared project state](project_state.md)
- Static inspection of the current multiway implementation

## Verdict

The repository has strong game, range, artifact, traversal, resolver, and
contract infrastructure. It is not finished as a Pluribus solver.

The main missing deliverable is a trained, globally usable policy. Several
current mechanisms are scaffolds or differ materially from the technical
report. Production workflow, memory scaling, calibration, and strength evidence
are also missing.

## Required implementation

| ID | Priority | Problem | Required change | Acceptance condition |
|---|---|---|---|---|
| F1 | P0 | Offline training is rooted at one postflop state. `MultiwayRootExternalSamplingTraversal` rejects preflop roots. | Add sampled traversal from the six-max initial state through all streets. Add lossless 169-class preflop infosets and export one global sparse blueprint. Keep the exact HUNL path unchanged. | Training from the initial deal produces preflop and postflop rows, reaches terminals, resumes bit-identically, and has no missing required blueprint lookups in a qualification run. |
| F2 | P0 | `prune_negative_regrets()` clips every negative regret to zero. This is not Pluribus negative-regret pruning. | Implement traversal-time action skipping after warmup: prune only below a configured threshold, explore all actions on recovery iterations, never prune river or immediate-terminal actions, and retain a regret floor. | Deterministic tests cover threshold, 95/5 scheduling, recovery, terminal exemption, river exemption, and floor behavior. |
| F3 | P0 | Persistent blueprint storage uses `double` regrets and `double` strategy sums for every cell. This does not satisfy the 64 GB design. | Add a production storage backend with compact integer regrets, saturation/flooring, sparse lazy rows, and no full postflop running-average table. Retain the scalar Float64 backend as the reference oracle. | A measured DEV artifact stays within its declared memory budget and matches the reference backend within defined policy tolerances. |
| F4 | P0 | Runtime search exports `average_strategy()` for the hero action. The target semantics require current/final policy for action selection and weighted-average policy for Bayes updates. | Export both policies explicitly. Use current regret-matched policy for action sampling and averaged range-wide rows for belief updates. Bind both to the same clean search snapshot. | Tests prove action and belief policies are distinct, normalized, identity-bound, and derived from one completed batch boundary. |
| F5 | P0 | Current postflop buckets are baseline structural features or 10-value Euclidean k-means. They are not potential-aware equity-distribution/EMD buckets. | Implement offline equity-distribution features, future-potential representation, suit-isomorphic canonicalization, deterministic clustering, and EMD or an experimentally justified equivalent. Version the feature and clustering models. | Held-out policy loss and within-bucket variance pass frozen calibration limits. Training and runtime return identical bucket IDs. |
| F6 | P0 | `texas_multiway_train`, `buckets`, `inspect`, and `evaluate` only parse options. Only `--tiny` performs work. | Wire real versioned configuration loading and each workflow to the existing library APIs. Implement checkpoint/resume, atomic artifact publication, artifact inspection, and nonzero failure exits. | A small config runs buckets -> train -> inspect -> evaluate without custom test code and produces verified hashes and metrics. |
| F7 | P1 | Blueprint artifacts are loaded and copied into heap vectors. There is no mmap-backed production store or packed pure-row encoding. | Add a stable indexed binary format, read-only mapped lookup, bounds/hash validation, and optional deterministic-row packing. | Runtime opens a production-size artifact without duplicating its full payload and returns identical policies to the in-memory reader. |
| F8 | P1 | Search depth is a generic decision/chance count, not the published street- and player-structured routing policy. | Add a versioned search router for preflop bypass, multiway flop limits, later-street limits, and heads-up late-street routing. Keep configurable safety caps. | Route tests cover every street, player count, all-in state, and unsupported/fallback case. |
| F9 | P1 | No specialized exact/vector heads-up river solver is connected to runtime routing. | Add range-vs-range late-street CFR with exact blockers and terminal evaluation. Add turn support only after river validation. | River results match the scalar reference and beat or equal sampled search at the same latency budget. |
| F10 | P1 | Calibration APIs exist, but no frozen production profiles or empirical artifacts exist. | Calibrate action menus, off-tree thresholds, bucket counts, continuation bias, rollout samples, search batches, and cache limits. Publish DEV and BALANCED profiles with identities. | Each selected profile has held-out results, memory/latency measurements, and a reproducible configuration artifact. |
| F11 | P1 | Evaluation components exist, including `estimate_multiway_aivat`, but there is no end-to-end match host or release gate. | Implement duplicate-deal self-play/cross-play, blueprint-only versus search ablation, LBR/restricted-BR callbacks, AIVAT record persistence, confidence intervals, and regression thresholds. | A single evaluation command emits machine-readable paired results and fails when a frozen quality gate regresses. |
| F12 | P1 | Coverage manifest fields `terminal_visits`, `leaf_visits`, and `missing_lookup_requests` are declared but not populated. | Collect and merge these counters from worker traversal into training status and artifacts. | Nontrivial runs report nonzero terminal/leaf counts as applicable; missing lookups are measured and release-gated. |
| F13 | P1 | Target-hardware budgets are unmeasured. Current defaults are safety values, not production values. | Add compact RSS, throughput, latency, fallback-rate, row-growth, and worker-scaling profiles. Run authorized DEV then BALANCED qualification. | A checked-in result records peak RSS, iterations/s, p50/p95/p99 decision time, fallback rate, and deterministic replay fingerprint. |
| F14 | P2 | Deployment documentation is incomplete and README links are stale. | Fix document links. Add runtime contract, artifact procedure, compatibility matrix, release configuration, and paired rollback procedure. | Every release artifact and runtime request can be validated from checked-in documentation. |

## Important corrections to `project_state.md`

- "Implementation-complete" currently means subsystem/API construction, not a
  complete Pluribus training and playing pipeline.
- The multiway trainer is not yet a global preflop-to-river blueprint trainer.
- Negative-regret clipping is not the published negative-regret pruning rule.
- The AIVAT estimator is implemented in `multiway_evaluation.cpp`; protected
  storage and end-to-end evaluation hosting remain external/missing.
- Future-bucket infrastructure exists, but production-quality potential-aware
  abstraction is not established.

## Implementation order

```text
F1 global traversal
  -> F2 pruning fidelity
  -> F3 compact training storage
  -> F5 production card abstraction
  -> F6 executable artifact pipeline
  -> F4 dual online policy semantics
  -> F8 structural search routing
  -> F9 exact river path
  -> F10 calibration
  -> F7 mapped runtime artifact
  -> F11 quality evaluation
  -> F12/F13 release telemetry
  -> F14 release documentation
```

F12 should be added while changing traversal, even though its release gate is
later.

The execution plan for the remaining F1 data-production and qualification
blocker is [F1 acceptance blocker execution plan](pluribus_f1_acceptance_blocker_plan.md).

## Definition of finished

- One versioned command generates buckets, trains/resumes a six-player global
  blueprint, verifies it, and publishes it atomically.
- The runtime loads the artifact within the 64 GB target and returns bounded
  postflop decisions from complete public-state beliefs.
- Pluribus-specific pruning, continuation, off-tree, reroot, and dual-policy
  semantics have deterministic reference tests.
- Blueprint-only and searched policies pass frozen cross-play, LBR/restricted
  BR, AIVAT, latency, memory, and fallback-rate gates.
- No strength claim is made beyond the measured confidence interval.

## Out of scope

- Poker-client automation, screen scraping, clicking, stealth, or account code.
- Replacing the exact `HUNLFlatDCFR` behavior.
- Neural value/policy models before the tabular baseline is measured.
- Full exact six-player NLHE solving.

Audit method: static inspection only. No build, test, benchmark, or solver run
was executed.
