# Pluribus project state

Updated: 2026-08-31  
Branch: `pluribus/pt2`  
Evidence commit: `292f777`  

## Verdict

TexasSolver contains a broad C++17 implementation of the intended
Pluribus-style architecture, including multiway rules, sampled global training,
compact CFR storage, artifacts, beliefs, nested runtime search, continuation
policies, off-tree handling, and evaluation APIs.

The project is not yet a finished or qualified Pluribus-Lite solver. The most
accurate maturity label is:

> subsystem implementation substantially complete; production artifact,
> runtime fidelity, memory, and strategy-quality qualification incomplete.

The immediate blocker is F1 acceptance. The repository can enumerate and write
the full bucket catalog, train from the six-player preflop root, checkpoint,
export, inspect, and replay lookups. It does not contain the completed measured
F1 evidence set: a frozen-capacity configuration, verified full bucket and
blueprint manifests, a 50-million-trajectory training report, checkpoint
equivalence, and zero-miss deterministic lookup qualification.

Unit and contract success must not be interpreted as convergence, low
exploitability, or superhuman play.

## Target architecture

The current implementation follows the main pipeline in the
[technical report](pluribus_solver_technical_report.md):

```text
six-player rules and public state
    -> sampled preflop-to-river blueprint training
    -> verified abstract strategy artifact
    -> 1,326-combo public-state beliefs
    -> bounded nested postflop search
    -> continuation evaluation and off-tree repair
    -> mixed policy for the actual private hand
```

The target is a reproducible, CPU-oriented six-player 100bb NLHE system that
fits a 64 GiB workstation by combining a coarse global blueprint with precise
local online search. Exact HUNL remains a deterministic reference path, not the
scaling strategy for six-player poker.

## Repository snapshot

The inspected worktree was clean before this report was created.

| Evidence | Current value |
|---|---:|
| Repository files in compact scan | 346 |
| C++ implementation files | 203 |
| Public/internal headers | 113 |
| Test files reported by repository scan | 113 |
| Main library | `TexasSolver::texas` |
| Multiway workflow executables | `train`, `buckets`, `inspect`, `evaluate`, `finalize` |
| F1 profile | `F1-DEV-12-v1` |
| F1 target trajectories | 50,000,000 |
| F1 storage backend | `CompactInt32` |
| F1 postflop buckets | 12 / 12 / 12 |

The local `artifacts/f1_dev_v1/` directory is empty and no TexasSolver workflow
process was running during inspection. A prior log entry records that production
bucket generation resumed at 292,825 verified tables, but there is no completed
artifact or acceptance report in the current workspace. External artifacts were
not available for verification.

## Implemented capability map

| Area | Current implementation | Qualification state |
|---|---|---|
| Multiway game engine | Six-player rules, legal actions, replay, public chance, private sampling, folds, all-ins, side pots, rake, showdown, and odd chips | Broad contract coverage exists; no production match qualification |
| Private information | Canonical 1,326-combo IDs, fixed range rows, blockers, normalization, Bayesian action updates, and conditional sampling | Implemented and tested at component level |
| Global traversal | Root external-sampling traversal supports bounded preflop through river and all public chance boundaries | Implemented; production coverage evidence missing |
| CFR | Regret matching, linear schedules, deterministic worker batches, sparse row admission, checkpoints, and export | Implemented; convergence and production scale unmeasured |
| Pluribus pruning | Threshold pruning after warmup, deterministic 5% recovery exploration, river and immediate-terminal exemptions, and regret floor | F2 implementation corrected and full test workflow recorded as passing |
| Compact training storage | Sparse lazy rows, compact integer regrets and strategy mass, saturation, flooring, reference Float64 path | F3 implementation present; 64 GiB measurement and reference tolerance report missing |
| Bucket artifacts | Deterministic physical-board catalog, bounded streaming writer, resume sidecar, streaming load, inspection, payload hashes, and atomic manifest publication | Workflow implemented; full verified artifact absent |
| Training artifacts | Versioned full-state checkpoint, atomic persistence, identity validation, resume, coverage telemetry, blueprint export, and checkpoint-equivalence comparator | Workflow implemented; production disk-resume evidence absent |
| Blueprint lookup | Immutable store, model-bound provider, hit/missing/menu-mismatch audit, deterministic replay fingerprints | Implemented; production zero-miss report absent |
| Action abstraction | Contextual menus, pseudo-harmonic translation, exact local insertion, calibration APIs, and coverage gates | Implemented; no promoted production profile |
| Future card abstraction | Versioned bucket artifacts, current-street exact rows, held-out calibration APIs, and profile selection | Infrastructure implemented; potential-aware EMD and suit-isomorphic production model not established |
| Runtime search | Request-local root external-sampling MCCFR, exact current-street hand rows, future buckets, fixed worker merge, memory admission, deadlines, cancellation, and fallback | Implemented; published structural depth routing and target latency unqualified |
| Rerooting | Same-street action reroot, next-street posterior transfer, hero-hand decision freezing, and stable-root fallback | Implemented with regression coverage |
| Continuations | Blueprint, fold-biased, call-biased, and raise-biased policies; typed leaf evaluation; cache and calibration contracts | Implemented; production calibration absent |
| Policy export | Range-wide policy and actual-hand row export | Final-action versus averaged-belief dual-policy semantics remain an explicit gap |
| Evaluation | Duplicate deals, seat rotation, cross-play APIs, reduced-game metrics, LBR callback boundary, off-tree gauntlets, AIVAT records and estimator, confidence intervals | Strong infrastructure; no single production evaluation run or frozen quality gate |
| Exact/vector late street | Exact HUNL reference solvers and vector/scalar utilities exist | No specialized multiway-runtime heads-up river route is qualified |
| Runtime artifact storage | Verified in-memory blueprint loading | No mmap-backed indexed production strategy store or deterministic-row packing |

## F1 acceptance state

Recent work made the F1 workflow executable rather than a command-line stub:

- `a779dcc` added global traversal, F2 pruning, and F3 compact-storage
  foundations.
- `0e29a02` added versioned F1 configuration, catalog/writer/inspector,
  checkpoint/report contracts, and workflow commands.
- `5006a43` hardened compact storage and traversal behavior.
- `97ce765` added durable checkpoint loading, streaming registry loading,
  deterministic lookup qualification, frozen-capacity validation, and verified
  bucket manifest publication.

The checked-in `configs/multiway/f1_dev_v1.cfg` still contains unresolved
capacity fields:

- maximum public states, sparse rows, and sparse values;
- worker delta capacity;
- trajectories per batch and checkpoint interval;
- disk-space requirement;
- process memory limit.

F1 remains unaccepted until measured evidence proves all of these conditions:

1. Exact bucket counts: 22,100 flop, 270,725 turn, and 2,598,960 river tables.
2. Matching artifact identity, byte length, and payload hash.
3. Preflop-root training with nonzero rows on every street and terminal visits.
4. Exactly 50,000,000 accepted trajectories and zero discarded trajectories.
5. Identical continuous and disk-resumed blueprint payload and coverage hashes.
6. Zero missing infosets, missing buckets, and action-menu mismatches.
7. Identical repeated lookup replay fingerprints.
8. Recorded peak RSS, disk use, throughput, machine, compiler, config hash, and
   producer commit.

## Fidelity gaps against the technical goal

The following are not closed by the current code and contract tests.

### 1. Dual online policy semantics

The target requires the final/current online strategy for action selection and
the weighted-average strategy for Bayesian belief updates, both from one clean
solve boundary. The current public result does not establish that complete
separation end to end.

### 2. Production card abstraction

The repository has deterministic features, clustering, bucket artifacts, and
calibration gates. It has not established the report's potential-aware
equity-distribution/EMD abstraction, suit-isomorphic production indexing, or a
held-out production dataset with frozen passing thresholds.

### 3. Mapped runtime blueprint

Blueprints are verified but loaded into in-memory structures. The planned
read-only mmap/indexed format, quantized runtime policy, and packed pure rows are
not implemented as a production lookup path.

### 4. Published structural search routing

Runtime search is bounded and configurable, but it is not qualified against the
published street/player routing rules: ordinary preflop bypass, multiway flop
depth limits, terminal later-street solves, and specialized heads-up late-street
routing.

### 5. Specialized exact/vector river solving

There is no production multiway-runtime route to an exact blocker-aware
range-versus-range river solver with scalar differential and latency evidence.

### 6. Production calibration and strength

Calibration APIs and evaluation primitives exist, but there are no promoted
DEV/BALANCED artifacts, target-hardware results, blueprint-versus-search
ablation, independent cross-play, restricted/LBR results, or statistical
release thresholds.

### 7. Release operations and documentation

The project lacks a checked-in runtime contract, artifact retrieval procedure,
compatibility matrix, release configuration, and paired rollback procedure.
The README also contains stale links to the removed solver-layout plan and uses
the wrong filename for the technical report.

## Validation evidence

The current implementation validation on 2026-08-31 passed the configured
Debug build, research workflow targets, and all 107 registered tests. This is
code validation only and does not qualify production artifacts or human-run F1
evidence.

Historical evidence in [PLURIBUS_LOG.md](PLURIBUS_LOG.md):

- Full Debug build and all then-registered tests passed repeatedly through F1.25.
- F1.13 explicitly records all 101 registered tests passing on 2026-08-28.
- F1.27 records targeted library/artifact builds and all six bucket-artifact
  tests passing.
- F1.28 records only a successful Debug build of `texas_multiway_inspect`.

Therefore the strongest recent repository-wide evidence is the 2026-08-28 full
workflow. Changes after that run have narrower validation and should receive a
new full build/test run before release.

## Recommended next sequence

The implementation and human-only execution contract is documented in the
[F1 qualification plan](f1_human_execution_implementation_plan.md).

1. Run the explicitly authorized F1 sizing pilot and freeze every unresolved
   `F1-DEV-12-v1` capacity field with headroom.
2. Generate, inspect, hash, and externally publish the complete bucket artifact;
   check in its manifest and retrieval metadata.
3. Run continuous and split/resumed bounded training and prove bit-identical
   checkpoint equivalence.
4. Run the 50-million-trajectory acceptance workload and deterministic
   required-lookup qualification twice; publish the complete F1 report.
5. Implement and verify final-action versus averaged-belief policy separation.
6. Implement and calibrate the production potential-aware card abstraction.
7. Add the published structural search router and exact/vector river route.
8. Add mmap-backed runtime blueprint storage and measure the full 64 GiB budget.
9. Freeze DEV and BALANCED profiles only after paired evaluation, LBR/restricted
   response, AIVAT, latency, memory, and fallback gates pass.
10. Complete release and rollback documentation, then correct README links.

## Scope boundaries

- This is a reconstruction from public research, not original Pluribus source.
- Full exact six-player NLHE solving is not the objective.
- Neural policy/value models should not replace the measurable tabular baseline.
- Exact `HUNLFlatDCFR` behavior remains a compatibility contract.
- Poker-client automation, screen scraping, clicking, stealth, account, and
  session integration are outside this repository.
- Generated multi-gigabyte artifacts must not be committed to Git.

## Evidence sources

- [README](../README.md)
- [Agent rules](../AGENTS.md)
- [Technical report](pluribus_solver_technical_report.md)
- [Progress log](PLURIBUS_LOG.md)
- `scripts/list_recent_changes.py`
- Current `CMakeLists.txt`, `include/`, `src/`, `tests/`, `examples/`, and
  `configs/multiway/f1_dev_v1.cfg`
- Git commits `a779dcc`, `0e29a02`, `5006a43`, `97ce765`, and `292f777`
