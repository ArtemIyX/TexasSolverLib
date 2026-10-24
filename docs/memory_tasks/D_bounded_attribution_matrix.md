# Task D: Bounded memory attribution matrix and analyzer

## Trigger

`Fully implement D.`

## Objective

Create reproducible bounded memory tooling, execute the baseline matrix, and
produce an evidence bundle that selects the next memory optimization. This
task must not change solver memory behavior.

## Dependencies

Tasks A, B, and C must be complete.

## Required inputs

- F1 sizing configuration and bucket artifact used by
  `scripts/run/run_multiway_ram_1000.ps1`;
- matching RelWithDebInfo normal and Tracy binaries;
- functioning Tracy `0.14.1` capture/export tools;
- Task A process snapshots;
- Task B subsystem capacities;
- Task C allocation events.

If a matching normal RelWithDebInfo executable does not exist, add a bounded
build path for it. Do not compare Debug normal memory against RelWithDebInfo
Tracy memory as optimization evidence.

## Required tooling

Add a runner or extend existing runners to execute:

| Case | Workers | Batches | Scheduler | Pinning | Purpose |
| --- | ---: | ---: | --- | --- | --- |
| A1 | 1 | 1 | fixed | off | Startup and cold admission |
| A2 | 1 | 5 | fixed | off | One-worker retained steady state |
| A3 | 4 | 1 | fixed | off | Worker construction slope |
| A4 | 4 | 5 | fixed | off | Four-worker retained steady state |
| A5 | 4 | 5 | dynamic | off | Scheduler-equivalent memory |

Run each normal case at least three times. Run one successful Tracy capture per
distinct worker/batch shape after the normal runs. Keep trajectories per batch
at `1,000` unless the existing artifact requires a lower safe bound.

## Analyzer output

Add `scripts/analyze/analyze_multiway_memory_matrix.ps1` or an equivalent focused
analyzer. It must produce JSON and a concise table containing:

- commit and build identity;
- workload identity and deterministic fingerprint;
- current/peak working set;
- current private committed bytes;
- phase-to-phase deltas;
- subsystem live and capacity bytes;
- unexplained private-byte remainder;
- mapped artifact virtual size;
- startup validation working-set delta;
- one-to-four-worker private-memory slope;
- cold-to-steady retained-memory delta;
- Tracy allocation count, allocated bytes, freed bytes, and live bytes when
  export tooling supports them;
- acceptance status and failure reasons.

Never subtract mapped virtual size from RSS. Never compare cumulative
allocation bytes with live private bytes.

## Candidate selection rules

The analyzer must rank F through J using evidence:

- Select F when bucket validation causes the dominant working-set jump while
  private bytes remain comparatively flat.
- Select G when worker construction private bytes and worker stream capacities
  dominate the one-to-four-worker slope.
- Select H when compact row metadata capacity is a material construction cost.
- Select I when cold admission scratch remains a dominant steady-state retained
  capacity.
- Select J when checkpoint/export is the dominant peak after normal training
  memory is accounted.

The analyzer may return `no_candidate_selected` when measurements are
incomplete or no component exceeds baseline noise.

## Required files

Likely changes:

- `scripts/run/run_multiway_ram_1000.ps1`
- `scripts/run/run_multiway_tracy_training_capture.ps1`
- `scripts/run/run_multiway_worker_matrix.ps1` or a new bounded memory runner;
- new `scripts/analyze/analyze_multiway_memory_matrix.ps1`;
- `tests/test_multiway_qualification_reports.cpp` for schema fixtures;
- script-level tests if the repository has an established location.

Do not change solver/container behavior.

## Tests

Test the analyzer with synthetic fixtures for:

- complete passing matrix;
- missing phase snapshot;
- unavailable private bytes;
- mismatched fingerprints;
- mismatched build configuration;
- normal/Tracy workload mismatch;
- missing Tracy allocation events;
- negative or impossible derived deltas;
- each candidate selection;
- no-candidate result.

## Execute

Build matching binaries and required tests. Then run the matrix using the new
bounded runner. Exact runner arguments may follow the implemented interface,
but every case must write to a unique directory under:

```text
tmp/memory-attribution/baseline/<case>/<repeat>
```

Run the analyzer and write:

```text
tmp/memory-attribution/baseline/memory_summary.json
tmp/memory-attribution/baseline/memory_samples.csv
tmp/memory-attribution/baseline/evidence.md
```

Finally run:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
git diff --check
```

Do not commit `tmp/` artifacts.

## Acceptance

- All five normal cases complete three repeats.
- Required Tracy captures contain memory events.
- Every case has complete Task A/B telemetry.
- All runs have zero discards and matching expected fingerprints.
- Fixed/dynamic equivalent cases match deterministic output.
- Analyzer output is deterministic and identifies one candidate or explicitly
  selects none.
- Build/tests pass and review finds no misleading memory arithmetic.
- The project log records medians, ranges, candidate decision, and limitations.

## Completion statement

Task D is complete only when the baseline evidence bundle exists and the
analyzer has made a defensible candidate decision. Tooling without executed
evidence is incomplete.

## Stop conditions

Stop if required artifacts are missing, Tracy does not connect, build types do
not match, private bytes are unavailable, fingerprints differ, or any run
discards trajectories.

