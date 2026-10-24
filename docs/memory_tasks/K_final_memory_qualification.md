# Task K: Final bounded memory qualification

## Trigger

`Fully implement K.`

## Objective

Run the final bounded qualification after Task E and every evidence-selected
optimization. Produce the authoritative before/after memory summary and decide
whether a larger F1 qualification may proceed.

Task K is evidence and closure work. Do not add a new optimization unless a
qualification defect requires a scoped fix.

## Dependencies

- Tasks A through E complete;
- at least one of F through J complete, unless D selected no optimization;
- clean matched RelWithDebInfo normal and Tracy builds;
- preserved Task D baseline bundle.

## Required matrix

Run the full Task D A1-A5 matrix with:

- five normal repeats per case;
- matching build, seed, artifact, trajectories, scheduler, and pinning;
- at least one Tracy capture for A1, A4, and any checkpoint-enabled case;
- checkpoint output disabled for normal-training cases unless J is under
  qualification;
- external working-set sampling enabled.

If multiple candidates were implemented, also compare each intermediate
commit or saved evidence so reductions are attributable.

## Required analyzer output

Write:

```text
tmp/memory-attribution/final/memory_summary.json
tmp/memory-attribution/final/memory_samples.csv
tmp/memory-attribution/final/evidence.md
```

The final evidence must include:

- baseline and final medians/ranges;
- current/peak working set;
- private committed bytes;
- mapped artifact virtual bytes and validation delta;
- subsystem capacities;
- unexplained private-memory remainder;
- one-to-four-worker slope;
- cold-to-steady retention;
- throughput and CPU deltas;
- deterministic fingerprints;
- discard/failure status;
- Tracy allocation evidence;
- preflight estimate categories;
- each candidate's attributable reduction.

## Execute

1. Run focused tests for every changed subsystem.
2. Run:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
```

3. Build matching RelWithDebInfo normal and Tracy binaries.
4. Execute the five-case, five-repeat matrix.
5. Execute required Tracy captures.
6. Run the memory analyzer against baseline and final bundles.
7. Run `git diff --check`.
8. Perform final C++ correctness and performance reviews.

Do not commit `tmp/` evidence unless the user explicitly requests a curated
evidence artifact under `docs/`.

## Acceptance

- Every run succeeds with zero discarded trajectories.
- Fingerprints match baseline and fixed/dynamic equivalence holds.
- All required process, phase, subsystem, and Tracy evidence is complete.
- Private and working-set reductions exceed measured noise.
- No memory is shifted into an unreported mapping, transient, or cache.
- Throughput change is reported and has no unexplained material regression.
- Preflight remains conservative and no valid bounded case is rejected.
- Full build and all CTest tests pass.
- Independent reviews have no unresolved actionable findings.
- Project log contains the final before/after table and limitations.

## Production decision

The analyzer must emit one of:

- `bounded_memory_qualification_pass`;
- `bounded_memory_qualification_pass_with_limitations`;
- `bounded_memory_qualification_fail`.

A pass authorizes planning a larger F1 run. It does not itself qualify the
50-million-trajectory workload. A larger run still requires explicit user
authorization and its own disk, time, and memory gates.

## Completion statement

Task K is complete when the final bundle, analyzer decision, full verification,
reviews, and project-log entry are complete.

## Stop conditions

Stop on any fingerprint mismatch, discard, missing private-memory metric,
missing Tracy memory events, build mismatch, incomplete matrix, unexplained
memory regression, or failed test.

