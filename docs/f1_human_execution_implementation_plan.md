# F1 qualification harness and human execution plan

Status: planned

Created: 2026-08-31

Primary profile: `F1-DEV-12-v1`

Target: first accepted six-player preflop-to-river blueprint pipeline

## 1. Objective

Move TexasSolver from implemented multiway subsystems to the first measured,
reproducible, accepted global blueprint pipeline.

F1 is complete only when checked-in machine-readable evidence proves:

```text
complete verified 12/12/12 bucket artifact
    -> frozen 64 GiB-safe training capacities
    -> 50,000,000 accepted preflop-root trajectories
    -> nonzero rows on every street and terminal visits
    -> zero discarded trajectories
    -> bit-identical continuous and disk-resumed outputs
    -> zero required blueprint lookup failures
    -> reproducible hashes, commands, machine data, and reports
```

This phase qualifies architecture and reproducibility. It does not establish
strategy strength or superhuman play.

## 2. Hard execution boundary

Only a human may build or execute compiled programs.

### LLM agents may

- inspect text files, source, Git history, configuration, and existing reports;
- create or edit C++17 code, CMake, tests, scripts, configuration templates,
  schemas, and documentation;
- review diffs and reason about evidence supplied by a human;
- prepare exact human-run commands;
- analyze logs and machine-readable output after a human run;
- update code and documentation in response to measured failures.

### LLM agents must not

- invoke CMake builds, compilers, linkers, CTest, or test executables;
- run any `texas_*.exe`, benchmark, test binary, bucket generator, trainer,
  inspector, evaluator, or qualification executable;
- start, stop, resume, or monitor a compiled TexasSolver process;
- infer a successful run from code inspection or an existing file name;
- invent capacity values, hashes, timings, memory measurements, or pass results;
- modify generated artifacts to make a gate pass;
- commit generated multi-gigabyte artifacts.

### Human responsibilities

- choose the machine and available RAM/disk envelope;
- build the requested configuration;
- execute every compiled program and test;
- preserve command lines, exit codes, stdout, stderr, timestamps, and artifacts;
- stop runs when system safety limits are approached;
- provide reports and logs back to the LLM for analysis.

This boundary applies even when a task would normally authorize an agent to run
tests. A human must perform every compiled-program execution in this project.

## 3. Current starting point

Implemented:

- bounded preflop-to-river root external-sampling traversal;
- Pluribus-style pruning schedule and exemptions;
- compact integer sparse training storage;
- deterministic physical-board catalog;
- streaming/resumable bucket writer and streaming inspection;
- durable full-training checkpoints;
- full-blueprint artifact export and verified load;
- required-lookup auditing and repeated replay fingerprints;
- `train`, `buckets`, `inspect`, and `evaluate` workflow executables.

Still missing for acceptance:

- resolved values for all eight capacity fields in
  `configs/multiway/f1_dev_v1.cfg`;
- an explicit sizing-pilot workflow and report;
- peak RSS, elapsed time, throughput, capacity high-water marks, and artifact
  identity in the training report;
- an automatic continuous-versus-resumed comparison report;
- a final evidence aggregator that refuses publication on an unmet predicate;
- complete bucket, training, checkpoint-equivalence, and lookup evidence from
  human-executed production runs.

## 4. Required repository outputs

The implementation phase should add or update these text/source artifacts:

```text
configs/multiway/
  f1_sizing_v1.cfg
  f1_dev_v1.cfg

include/solver/multiway/workflow/
  multiway_sizing_report.hpp
  multiway_checkpoint_equivalence.hpp
  multiway_f1_acceptance.hpp

src/solver/multiway/workflow/
  multiway_sizing_report.cpp
  multiway_checkpoint_equivalence.cpp
  multiway_f1_acceptance.cpp

tests/
  test_multiway_sizing_report.cpp
  test_multiway_checkpoint_equivalence.cpp
  test_multiway_f1_acceptance.cpp

docs/qualification/f1/
  README.md
  acceptance_report.template.json
  machine_report.template.json
```

Names may be adjusted to match nearby ownership, but public headers and source
files must remain mirrored and CMake source lists explicit.

Generated binaries, checkpoints, bucket tables, and blueprints stay under
`artifacts/` or external storage and must not enter Git.

## 5. Evidence directory contract

Human runs should populate one immutable run directory per configuration hash:

```text
artifacts/f1_dev_v1/<config-fingerprint>/
  provenance/
    machine.json
    build.txt
    git.txt
    commands.txt
  buckets/
    buckets.bin
    buckets.manifest.json
    inspection.json
    generation.stdout.log
    generation.stderr.log
  sizing/
    sizing_report.json
    sizing.stdout.log
    sizing.stderr.log
  continuous/
    checkpoints/
    blueprint.bin
    blueprint.manifest.json
    training_report.json
    stdout.log
    stderr.log
  resumed/
    checkpoints/
    blueprint.bin
    blueprint.manifest.json
    training_report.json
    stdout.log
    stderr.log
  qualification/
    checkpoint_equivalence.json
    lookup_report.first.json
    lookup_report.second.json
    final_acceptance.json
```

The implementation must refuse to combine evidence with different model
identities, configuration fingerprints, producer commits, schema versions, or
seeds.

## 6. Work package sequence

Each work package follows this loop:

```text
LLM edits code/tests/docs
    -> LLM reviews diff without executing compiled programs
    -> human builds and runs requested validation
    -> human returns logs/reports
    -> LLM audits evidence and fixes code if needed
    -> repeat until the package exit gate passes
```

Do not start production workloads while any earlier package is unresolved.

## WP0. Freeze schemas and responsibility boundaries

### LLM task

1. Add schema-versioned structures for sizing, training, checkpoint comparison,
   bucket inspection, lookup qualification, and final acceptance.
2. Document required and optional fields, units, and zero-value semantics.
3. Add a producer identity containing:
   - Git commit;
   - build configuration;
   - compiler identity supplied by the human/build system;
   - model identity;
   - workflow configuration fingerprint;
   - artifact schema versions.
4. Make every report validator reject missing required identity fields,
   non-finite values, impossible counts, and mismatched units.
5. Add the human-only execution rule to the F1 qualification README.

### Tests for the LLM to write

- valid report round-trip;
- missing and mismatched identity rejection;
- unknown schema rejection;
- impossible counter rejection;
- atomic publication behavior;
- deterministic serialization ordering.

### Human validation

The human builds and runs the new report tests.

### Exit gate

All later evidence has a typed schema and cannot be published with incomplete
identity or invalid values.

## WP1. Add sizing and training telemetry

### LLM task

Extend cold-path training status/reporting with these measurements:

```text
configured limits
peak admitted public states
peak sparse rows
peak sparse values
peak worker delta entries
compact storage bytes
checkpoint bytes and write duration
blueprint bytes
current and peak process RSS, with availability flags
elapsed wall time
accepted trajectories
discarded trajectories
trajectories per second
new rows per batch or sampled row-growth series
per-street rows and visits
terminal and depth-limited leaf visits
merge fingerprint
capacity-exhaustion stage, if any
```

Implementation rules:

- reuse existing process-memory helpers in the evaluation subsystem or move
  them to a shared cold-path utility;
- do not add OS queries, clocks, formatting, strings, or allocations inside
  traversal, row-update, terminal, or merge inner loops;
- update high-water counters at batch boundaries or existing cold checkpoints;
- use integer nanoseconds and bytes in serialized reports;
- include explicit `*_available` fields where a platform may not expose a
  measurement;
- increment the training-report schema version.

### Tests for the LLM to write

- monotonic high-water marks;
- exact unit and overflow behavior;
- unavailable RSS handling;
- throughput calculation for zero and nonzero elapsed time;
- capacity failure preserves a valid failure report;
- deterministic fields remain equal while environmental fields may differ.

### Human validation

The human builds and runs focused telemetry tests, then the full registered test
suite.

### Exit gate

A bounded human-run training job emits enough data to select capacities without
external guesswork.

## WP2. Implement an explicit sizing-pilot profile

### LLM task

1. Separate sizing and acceptance intent in the workflow schema.
2. Add `configs/multiway/f1_sizing_v1.cfg` with:
   - the same game, abstraction, storage backend, and deterministic seed as F1;
   - a smaller human-approved trajectory target;
   - finite safety capacities;
   - an explicit process memory ceiling;
   - `profile_kind=sizing`, so its output cannot be mistaken for acceptance.
3. Keep `F1-DEV-12-v1` fixed at 50,000,000 trajectories.
4. Make acceptance code reject sizing artifacts.
5. Make the pilot stop cleanly and publish a failure report before unsafe
   growth when a declared capacity or memory boundary is reached.
6. Do not auto-increase capacities within one run.

The LLM must not choose final acceptance capacities. Initial pilot limits are
entered only after the human supplies the available RAM and disk envelope.

### Tests for the LLM to write

- sizing versus acceptance identity separation;
- unresolved/partial capacity rejection;
- process-memory limit validation;
- sizing output rejected by final acceptance;
- deterministic pilot configuration fingerprint.

### Human validation

The human builds and runs the configuration and workflow tests.

### Exit gate

The repository has a bounded pilot that can measure real growth without
changing or qualifying the final profile.

## WP3. Harden artifact preflight and publication

### LLM task

Before any long human run, add cold-path preflight checks for:

- output and checkpoint directories;
- required free disk space;
- refusal to overwrite a verified final artifact;
- temporary artifact and sidecar consistency;
- model identity and configuration fingerprint;
- expected table count and estimated final bytes;
- available process memory where supported;
- atomic publication support on the target filesystem.

Bucket completion must publish a manifest only after reopening and fully
inspecting the final file. The manifest must include:

```text
schema version
model identity
config fingerprint
producer commit
byte length
flop/turn/river table counts
live assignments
payload hash
inspection timestamp
```

### Tests for the LLM to write

- insufficient disk rejection;
- mismatched resume sidecar rejection;
- incomplete artifact never receives a final manifest;
- verified final artifact is not overwritten;
- manifest fields match inspector output.

### Human validation

The human builds and runs artifact tests. No full bucket generation occurs yet.

### Exit gate

A failed or interrupted human generation cannot publish or overwrite a final
verified artifact.

## WP4. Implement checkpoint-equivalence qualification

### LLM task

Add a cold-path comparator that accepts continuous-run and resumed-run evidence
and verifies:

```text
model identity equality
config fingerprint equality
seed and schedule equality
ordered blueprint byte equality
blueprint payload hash equality
coverage fingerprint equality
per-street row equality
terminal/leaf/discard counter equality
merge fingerprint equality
completed trajectory equality
```

The comparator must stream large artifacts instead of loading two complete
blueprints into RAM. It must publish `checkpoint_equivalence.json` atomically
only after every equality passes.

### Tests for the LLM to write

- identical reduced fixtures pass;
- one-byte payload difference fails;
- report-only mismatch fails;
- identity, seed, schedule, and schema mismatch fail;
- truncated inputs fail without a final report;
- comparison memory remains bounded by a fixed buffer.

### Human validation

The human builds and runs focused comparator tests and the full test suite.

### Exit gate

A human can prove disk resume equivalence with one deterministic comparator
command and a machine-readable result.

## WP5. Implement final F1 evidence aggregation

### LLM task

Add a finalizer that reads existing reports only. It must not rerun training or
silently repair evidence.

The finalizer validates:

```text
bucket.identity_matches                    = true
bucket.payload_hash_matches                = true
bucket.flop_tables                         = 22100
bucket.turn_tables                         = 270725
bucket.river_tables                        = 2598960

training.started_at_preflop                = true
training.preflop_rows                      > 0
training.flop_rows                         > 0
training.turn_rows                         > 0
training.river_rows                        > 0
training.terminal_visits                   > 0
training.accepted_trajectories             = 50000000
training.discarded_trajectories            = 0
training.peak_rss_bytes                    <= frozen process limit

resume.blueprint_payload_equal             = true
resume.coverage_equal                      = true
resume.merge_fingerprint_equal             = true

lookup.missing_infosets                    = 0
lookup.missing_buckets                     = 0
lookup.action_menu_mismatches              = 0
lookup.first_replay_fingerprint            = lookup.second_replay_fingerprint
```

The finalizer also requires human-supplied machine, command, compiler, commit,
start/end time, and exit-code records. It publishes `final_acceptance.json`
only when every gate passes. Failed runs may publish a clearly named diagnostic
report, never an acceptance report.

### Tests for the LLM to write

- one positive reduced fixture;
- one failure fixture per predicate family;
- mismatched provenance rejection;
- missing report rejection;
- unknown fields/schema policy;
- atomic final publication.

### Human validation

The human builds and runs focused finalizer tests and the full registered suite.

### Exit gate

F1 status can be derived from one strict final report rather than prose or
manual interpretation.

## WP6. Human build and test gate

This package is human-only execution.

### LLM preparation

- provide the exact expected Git commit and changed-file list;
- provide build and test commands without executing them;
- list expected test names and report schemas;
- identify where the human should save stdout/stderr and exit codes.

### Human-only command template

The human chooses the actual generator/configuration and runs commands similar
to:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 `
  cmake -S . -B build `
  -DTEXASSOLVER_BUILD_TESTS=ON `
  -DTEXASSOLVER_BUILD_EXAMPLES=ON `
  -DTEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON

powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 `
  cmake --build build --config Release

powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 `
  ctest --test-dir build -C Release --output-on-failure
```

These commands are documentation for the human. An LLM agent must not execute
them.

### Human handoff

Return:

- exact commands;
- Git commit;
- compiler/CMake versions;
- exit codes;
- complete failed-test output, if any;
- saved build and test logs.

### LLM audit

The LLM verifies that the tested commit matches the intended source and edits
code for real failures. It must not record a pass without the human evidence.

### Exit gate

All focused and registered tests pass on the exact candidate commit.

## WP7. Human complete bucket generation

This package is human-only execution.

### LLM preparation

- inspect the candidate config and expected model identity;
- calculate expected table counts from repository constants;
- prepare generation, resume, and inspection commands;
- verify output paths do not target Git-tracked locations;
- provide required free-space and stop-condition checklist.

### Human-only command template

```powershell
.\build\Release\texas_multiway_buckets.exe `
  --config configs\multiway\f1_dev_v1.cfg `
  --output artifacts\f1_dev_v1\buckets\buckets.bin `
  --checkpoint-dir artifacts\f1_dev_v1\buckets\checkpoints

.\build\Release\texas_multiway_inspect.exe `
  --config configs\multiway\f1_dev_v1.cfg `
  --input artifacts\f1_dev_v1\buckets\buckets.bin `
  --report artifacts\f1_dev_v1\buckets\inspection.json
```

The human should deliberately exercise one clean stop/resume cycle only if the
workflow documents a recoverable interruption procedure. Do not terminate by
corrupting or deleting files.

### Human handoff

- bucket manifest and inspection JSON;
- stdout/stderr logs and exit codes;
- file length and cryptographic/content hash recorded by the workflow;
- peak RSS, elapsed time, disk usage, and resume evidence.

### LLM audit

The LLM reads the reports and verifies exact counts and matching identities. It
does not open or copy the full 15+ GiB payload unless targeted diagnosis requires
a bounded read.

### Exit gate

The complete bucket artifact is verified, immutable, externally backed up, and
represented by a checked-in small manifest/retrieval record.

## WP8. Human sizing pilot and final config freeze

This package alternates human execution and LLM analysis.

### Human action

Run the sizing profile for the agreed bounded workload. Preserve the report even
when a capacity is reached.

### LLM analysis

For each pilot, calculate from measured data:

- peak-to-limit ratio per capacity;
- new rows per million trajectories and whether growth is stabilizing;
- bytes per sparse row and value;
- worker delta headroom;
- checkpoint and blueprint staging headroom;
- peak RSS plus OS/page-cache reserve;
- required disk for temporary artifact, checkpoints, blueprint, and reports;
- safe trajectories per batch and checkpoint interval.

The LLM proposes values but does not claim they are safe until the human accepts
the machine envelope. After approval, the LLM edits every capacity field in
`f1_dev_v1.cfg` in one change. Partial resolution is forbidden.

Recommended acceptance headroom:

- process peak remains below the human-approved limit and below the 64 GiB
  machine budget with at least 8-12 GiB reserved for OS/page cache;
- table and worker capacities retain documented headroom over measured peaks;
- disk allocation includes temporary files and at least one recoverable
  checkpoint generation;
- no capacity field is changed during the acceptance run.

### Human verification

The human reruns configuration tests and a bounded frozen-config smoke workload.

### Exit gate

`f1_dev_v1.cfg` contains no `UNRESOLVED` values, has a stable fingerprint, and
has an attached sizing report and human-approved rationale.

## WP9. Human checkpoint-equivalence runs

This package is human-only execution.

### Continuous run

Run a bounded deterministic qualification workload from zero to `N` batches and
save its blueprint, report, and final checkpoint.

### Split/resumed run

1. Run the same profile from zero to `N/2` batches.
2. Exit normally after an atomic checkpoint.
3. Start a new process from that checkpoint.
4. Run the remaining `N/2` batches.
5. Export to a separate immutable output directory.

### Comparison

The human runs the new checkpoint-equivalence executable against the two output
directories.

### LLM audit

The LLM verifies the comparator report and confirms the compared batch count,
trajectory count, seed, configuration fingerprint, and producer commit.

### Exit gate

The machine-readable comparator proves byte, coverage, and merge equivalence.

## WP10. Human 50-million-trajectory acceptance run

This package is human-only execution.

### Preconditions

- exact candidate commit passed WP6;
- bucket artifact passed WP7;
- frozen configuration passed WP8;
- checkpoint equivalence passed WP9;
- sufficient disk and RAM are confirmed immediately before launch;
- no configuration or binary changes occur after provenance capture.

### Human action

Run `texas_multiway_train.exe` with the frozen profile until exactly 50,000,000
accepted trajectories complete. Resume only from verified atomic checkpoints.

The human monitors system health and owns pause/stop decisions. The LLM does not
start, stop, resume, or poll the process.

### Human handoff

- final blueprint and manifest;
- training report;
- final checkpoint manifest;
- commands, exit codes, logs, timestamps, and machine report;
- peak RSS, throughput, disk use, and checkpoint durations.

### LLM audit

The LLM checks:

- exact trajectory and zero-discard counts;
- nonzero rows on all streets and terminal visits;
- no capacity exhaustion;
- configuration and artifact identity consistency;
- peak RSS within the frozen limit;
- artifact publication occurred only after validation.

### Exit gate

The global blueprint training portion of F1 passes every machine-readable gate.

## WP11. Human required-lookup qualification

This package is human-only execution.

### Human action

Run the required-lookup workflow twice with identical config, artifacts, seed,
and trajectory count. Save each report separately before final comparison.

### LLM audit

Verify:

- lookup hits are nonzero;
- missing infosets are zero;
- missing buckets are zero;
- action-menu mismatches are zero;
- both replay fingerprints match;
- both reports use the accepted bucket and blueprint identities.

Any miss is a code/model coverage failure. The LLM diagnoses and edits code, and
the human repeats validation. Do not weaken the zero-miss gate.

### Exit gate

Two independent process executions produce identical zero-miss qualification.

## WP12. Human finalization and LLM evidence audit

### Human action

Run the final evidence aggregator against the immutable evidence directory.

### LLM audit

1. Read `final_acceptance.json` and all referenced small reports.
2. Confirm every referenced path, hash, identity, commit, and command record.
3. Compare the final report against every predicate in this document.
4. Update:
   - `docs/project_state.md`;
   - `docs/PLURIBUS_LOG.md`;
   - `docs/qualification/f1/README.md`;
   - README status and corrected documentation links.
5. Check in only configurations, manifests, reports, retrieval metadata, and
   documentation. Never check in large binary artifacts.

### Exit gate

F1 is marked accepted only when the current repository contains a complete,
internally consistent evidence chain to immutable external artifacts.

## 7. Failure handling

| Failure | Required response |
|---|---|
| Build or test failure | Human supplies logs; LLM fixes code; human reruns |
| Capacity reached | Preserve failure report; revise pilot only; do not alter frozen acceptance config mid-run |
| Memory ceiling approached | Human stops safely; LLM analyzes telemetry and proposes a bounded change |
| Bucket resume mismatch | Refuse mutation; diagnose sidecar/artifact identity; never truncate without verified prefix logic |
| Checkpoint mismatch | Treat as deterministic correctness bug; reduce to bounded fixture before another long run |
| Missing blueprint lookup | Treat as coverage/model mismatch; do not translate it into a pass |
| Artifact hash mismatch | Quarantine outputs; never publish or aggregate them |
| Human evidence incomplete | Keep status unqualified; request the exact missing report or command record |
| Producer commit changed | Rebuild and rerun affected gates; never mix commits in one acceptance set |

## 8. LLM task packet template

Use this template for every implementation subtask:

```text
Goal:
  One concrete F1 capability.

Allowed:
  Inspect and edit source, tests, config, CMake, and docs.

Forbidden:
  Build, run CTest, or execute any compiled program.

Relevant files:
  Exact mirrored header/source/test paths.

Required behavior:
  Explicit schema, identity, failure, atomicity, and memory contracts.

Tests to author:
  Positive, negative, corruption, mismatch, and deterministic cases.

Static review:
  Inspect diff, CMake classification, hot-path rules, and generated-file scope.

Human handoff:
  Exact commands for the human, expected outputs, and log paths.

Completion evidence:
  Human-provided build/test reports plus reviewed diff.
```

An LLM must not mark an implementation task complete merely because tests were
written. Completion requires human-executed validation evidence unless the task
is documentation-only.

## 9. Human run record template

For every compiled execution, record:

```yaml
run_id: unique immutable id
operator: human identifier
start_utc: ISO-8601
end_utc: ISO-8601
git_commit: full hash
working_tree_clean: true/false
config_path: path
config_fingerprint: integer/hash
model_identity: integer/hash
binary_path: absolute or repository-relative path
binary_hash: hash
command: exact command line
working_directory: absolute path
exit_code: integer
stdout_log: path
stderr_log: path
machine:
  os: value
  cpu: value
  logical_cores: value
  physical_memory_bytes: value
  storage: value
result_reports:
  - path
artifacts:
  - path, byte length, payload hash
notes: factual only
```

## 10. Definition of F1 complete

F1 is complete when all of the following are true:

- code and schemas for sizing, reporting, equivalence, and finalization exist;
- a human built the exact acceptance commit and ran all tests successfully;
- all canonical bucket tables exist and pass full inspection;
- the final F1 configuration has no unresolved capacity fields;
- continuous and disk-resumed training are deterministically equivalent;
- the 50-million-trajectory run satisfies coverage, discard, RAM, and identity
  gates;
- repeated lookup qualification has zero misses and matching fingerprints;
- final acceptance aggregation passes without manual report edits;
- small evidence files and external artifact retrieval metadata are checked in;
- `docs/project_state.md` and README accurately state F1 acceptance without
  making a strategy-strength claim.

After F1, the next implementation phase is dual online policy semantics:
current/final strategy for action selection and weighted-average strategy for
belief updates from the same clean solve boundary.
