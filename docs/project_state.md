# Pluribus project state

Updated: 2026-08-27

## Current position

TexasSolver now contains the main software architecture described by the
[Pluribus solver technical report](pluribus_solver_technical_report.md):

```text
offline sampled blueprint training
    -> verified abstract strategy artifacts
    -> public-state range tracking
    -> bounded nested postflop search
    -> continuation evaluation
    -> mixed policy export for the actual hand
```

The project is past architecture construction. Roadmap phases P0 through P9
are recorded complete in [PLURIBUS_LOG.md](PLURIBUS_LOG.md), followed by five
audit rounds, contract repairs, and a completed solver-layout migration.

The current maturity is best described as implementation-complete but not yet
strategy-quality proven. Core mechanisms and deterministic contracts exist.
A production-scale blueprint, calibrated abstractions, target-hardware
performance, and external playing-strength evaluation are not demonstrated by
the repository evidence.

## Project direction

The primary direction is a six-player sampled Pluribus-style solver, not a
larger exact HUNL tree.

- Keep `HUNLFlatDCFR` deterministic and behavior-compatible.
- Put scalable HUNL work in sampled or lazy modules.
- Use a broad offline blueprint as the prior for bounded runtime search.
- Search public states over full private-hand beliefs, not only the actual hand.
- Prefer sparse, preallocated, integer-indexed hot paths.
- Treat artifacts, model identity, memory admission, and deterministic replay as
  release contracts.

The exact HUNL and generic small-game solvers remain useful reference and
validation paths. They are not the intended route to a full six-player exact
solution.

## Pluribus requirement coverage

| Published design requirement | Current repository state | Main evidence |
|---|---|---|
| Six-player no-limit rules and terminal utility | Implemented | `games/multiway_*`, replay and terminal suites |
| Canonical private hands and ranges | Implemented | 1,326-combination IDs, fixed range beliefs, blocker filtering, Bayes updates |
| Offline sampled CFR blueprint | Implemented as a training framework | `multiway_blueprint_trainer`, traversal, sparse rows, checkpoints, artifacts |
| Action abstraction | Implemented | Contextual menus, pseudo-harmonic translation, local off-tree expansion |
| Information abstraction | Implemented as artifact infrastructure | Exact current-street keys and versioned future-street bucket artifacts |
| Full blueprint prior | Implemented | Verified immutable blueprint store and non-traverser policy provider |
| Public-state nested search | Implemented | Root external-sampling traversal and resolver-managed search sessions |
| Range-wide hero policy | Implemented | Exact canonical-hand rows, actual-hand export, within-round freeze |
| Street and action rerooting | Implemented | Posterior transfer, same-street and next-street session replacement |
| Continuation policies | Implemented | Normal, fold-heavy, call-heavy, and raise-heavy transforms and learned selection |
| Bounded runtime operation | Implemented | Deadlines, clean batches, cancellation, memory preflight, fallback chain |
| Deterministic parallel execution | Implemented | Fixed worker partition, seed derivation, merge order, run fingerprints |
| Evaluation boundary | Partially implemented | Differential candidates and protected AIVAT-compatible records |
| Production policy quality | Not established | Requires trained artifacts, calibration, cross-play, and statistical evaluation |

## Current runtime path

`MultiwayResolverSearchMode::ReleaseDefault` is the default resolver mode.
It runs deterministic root external-sampling search only when the request has:

- verified root and full-blueprint artifacts;
- matching model and bucket identities;
- complete live ranges;
- a supported postflop root;
- typed terminal and continuation dependencies;
- bounded search and memory limits.

If admission fails, the resolver returns the established stable-root,
blueprint, or static-legal fallback. The old synthetic perturbation path has
been removed. `LegacyStatic`, `SearchShadow`, and explicit `SearchActive` remain
as controlled compatibility and evaluation modes.

The runtime is library code only. The deployment host owns artifact loading,
request construction, deadlines, protected evaluation storage, and integration
with its game environment. Poker-client automation is outside project scope.

## Implemented subsystem map

Solver code now uses mirrored ownership directories under `include/solver` and
`src/solver`: `generic`, `hunl/{bucket,flat,sampled}`, and
`multiway/{abstraction,blueprint,continuation,engine,evaluation,resolver,session}`.

### Game and state

- Multiway rules, legal action state, replay, private deal sampling, public
  chance, rake, fold settlement, showdown, side pots, and odd-chip handling.
- One compact `[0, 51]` card encoding and canonical two-card IDs.

### Beliefs and public roots

- Six fixed 1,326-entry range rows with legal masks and provenance.
- Transactional initialization and Bayes action-observation updates.
- Lossless root-street public keys and posterior-preserving reroots.

### Offline blueprint

- External-sampling traversal, regret matching, linear averaging, sparse row
  admission, coverage manifests, checkpoints, and resume support.
- Identity-bound full blueprint storage and hash-verified artifact loading.
- Deterministic future-bucket feature generation and clustering machinery.

### Runtime search

- Request-local search sessions with worker-local deltas and fixed-order merge.
- Exact private-hand decision rows on the current street.
- Blueprint policy lookup for non-traversers.
- Off-tree translation and bounded local expansion.
- Clean-batch export, deadline reserve, cancellation, and fallback diagnostics.

### Continuation and evaluation

- Four continuation policy families with information-set keyed selection.
- Typed rollout leaves, request-local cache, repeated-seed variance diagnostics,
  and model-bound cache identities.
- Differential resolver adapters and integrity-sealed AIVAT-compatible records.
  The AIVAT estimator itself remains host-owned.

### Performance and operations

- Staged memory admission for artifacts, ranges, exact rows, buckets, workers,
  merge buffers, continuation scratch, caches, and export.
- Preallocated hot-path storage and shared scalar action-major CFR kernels.
- Optional profiling checkpoints and deterministic worker scheduling.
- AVX2 row math exists where dispatched, with scalar reference paths retained.

## Validation state

The latest recorded full validation is the 2026-08-27 solver-layout migration
run, after audit round five and the contract repairs:

- Debug build passed.
- All registered tests passed.
- The working tree was clean before this document was created.

Earlier log entries often record static review or newly added tests without
execution. The later full run is the strongest repository-wide validation
record, but it proves implementation contracts rather than poker strength.
No build or test command was run while creating this document.

## Remaining gaps

### 1. Produce production artifacts

The repository implements training and artifact formats, but does not contain
evidence of a production-scale six-player blueprint or calibrated future-bucket
artifact. The next major deliverable should be a reproducible offline training
run with versioned configuration, checkpoints, coverage, and final hashes.

### 2. Calibrate abstraction and continuation quality

Action profiles, translation thresholds, future bucket features, cluster
counts, continuation transforms, and search limits need empirical tuning.
Contract tests establish determinism and validity, not strategic quality.

### 3. Measure end-to-end strength

The evaluation adapter and AIVAT record boundary exist. The project still needs
a host-side estimator and repeatable experiments covering:

- blueprint-only versus searched play;
- search-disabled versus `ReleaseDefault`;
- cross-play against independent policies;
- off-tree action stress cases;
- confidence intervals and variance reduction;
- regression gates for policy quality.

NashConv is not a sufficient primary target for six-player poker and is not
implemented as an end-to-end repository metric.

### 4. Validate production budgets

Run authorized profiles on target hardware to choose decision deadlines,
worker counts, row limits, memory caps, cache sizes, and continuation sample
counts. Existing defaults and synthetic admission tests are safety mechanisms,
not throughput or latency claims.

### 5. Complete and reconcile deployment documentation

Only the project state, progress log, and solver technical report remain under
`docs/`. The README still links to the removed earlier technical report and
solver-layout migration plan, while historical progress entries reference an
absent runtime runbook. A release candidate needs corrected documentation
links plus a checked-in runtime contract, artifact generation procedure,
release configuration, compatibility matrix, and paired rollback procedure.

## Recommended next sequence

1. Freeze a versioned experimental configuration and artifact identity.
2. Generate and verify a small end-to-end blueprint as a pipeline qualification.
3. Run blueprint-only and searched self-play through the evaluation boundary.
4. Calibrate buckets, continuation selection, and off-tree thresholds.
5. Scale training while tracking coverage, checkpoint equivalence, memory, and
   deterministic replay.
6. Profile `ReleaseDefault` on target CPU and memory limits.
7. Promote only after statistical policy-quality and rollback gates pass.

## Scope boundaries

- This is a Pluribus-style reconstruction based on public material. It is not
  the original Pluribus source or a claim of algorithmic identity.
- The published action menus, bucket counts, thresholds, and hardware figures
  are reference inputs, not validated defaults for this implementation.
- Passing unit and contract tests does not establish convergence, low
  exploitability, or professional-level play.
- The project must not add poker-client automation, screen scraping, clicking,
  stealth, evasion, or account/session code.

## Evidence used

- [Pluribus solver technical report](pluribus_solver_technical_report.md)
- [Pluribus roadmap progress log](PLURIBUS_LOG.md)
- Current `include/games/multiway_*`, `include/solver/{generic,hunl,multiway}`,
  matching implementations, tests, `CMakeLists.txt`, and recent Git history
