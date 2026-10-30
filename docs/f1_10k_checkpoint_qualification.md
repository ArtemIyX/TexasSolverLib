# F1 10K checkpoint qualification

This is the human-run gate before scaling the F1 blueprint toward 50M trajectories. It uses the six-player F1 model, 12/12/12 buckets, one fixed worker, 10 batches of 1,000 trajectories, and an ExternalCompactInt32 store. It checkpoints every batch so the run produces nine measurable checkpoint cycles and at least three two-cycle P99 cohorts.

## Qualification blocker

The stopped Stage 9.4 and later 3K diagnostic configs requested 96/128/192 buckets. The preserved bucket artifact at `artifacts/f1_sizing_v1/buckets/buckets.bin` is schema 4 and its first table for each street has 12 buckets. The old training loader accepted a bucket artifact without matching its identity or per-street counts to the workflow config. The new startup check rejects identity or table-count mismatches, and bucket inspection reports the per-street counts. The previous 10K and 3K timings therefore do not qualify this F1 profile.

The checkpoint journal records write time separately from elapsed time between checkpoint publications. Treat the latter as a batch-to-checkpoint cycle, which includes traversal, storage work, and checkpoint writing. Report both values; the cycle P99 is the scalability gate.

The `pb12_diagnostic_f1_10k_20261030` attempt exited during batch 7 with `external training manifest publish failed: Access is denied.` Its report records six completed batches and 6,000 accepted trajectories; the journal records batch 7 component publications but no aggregate commit. The monitor recorded six checkpoints and five cycles, so the P99 qualification is incomplete. No monitor hard stop was recorded. The Windows monitor opened live manifests without delete sharing, which can block atomic replacement; the logs do not identify the actual handle owner. Live telemetry reads now permit replacement while retaining the stability and disk checks. Preserve that failed directory.

The `pb12_diagnostic_f1_10k_20261030_retry1` monitor was interrupted after seven checkpoints. The `retry2` monitor stopped after checkpoint 8 under the P99 rule. Its cycles 1–7 were 61735.8170, 69640.4928, 28253.2422, 65828.6434, 75226.4279, 77450.4109, and 81176.3630 ms; their endpoint live-byte samples were 95219697, 143844948, 161110818, 206790582, 251849651, 302007021, and 345943612. The sliding cohorts beginning at cycles 3 and 5 had P99 values 65828.6434 and 77450.4109 ms and maxima 206790582 and 302007021 bytes; cycle 7 already establishes a strictly higher lower bound for both measures. The earlier three complete cohorts do not increase, but the rule also examines subsequent overlapping sequences of cohorts. This was a valid stop, not a qualification pass.

The journal records checkpoint writes of 43.4582–257.1528 ms across eight checkpoints, much shorter than the cycles. Batch 8 records 49.403 s worker activity, 24.312 s admission, and 2.548 s merge; storage read counters total 27.446 s and can overlap those phases. These observations implicate batch work and external storage reads, but do not establish which operation or configuration caused the trend. Compaction and sampling can contribute; no controlled comparison proves a performance root cause. Stable samples hash manifests around the scan but are not explicitly bound to the checkpoint generation, so their exact association with a checkpoint is not guaranteed if publication advances during observation. The current evidence does not show such a mismatch for retry2. Preserve both retry directories and investigate any performance change separately before claiming a fix.

Previously the monitor stopped before logging `checkpoint_observed` for the triggering checkpoint; its timestamp can be recovered from the preserved `latest.bin`, and the stop event contains its duration and live bytes. The monitor now records the observation and each cycle before evaluating the stop. This does not change the gate.

## Human execution

Build and run the focused regression first:

```powershell
cmake --build build --config Debug --target test_multiway_bucket_artifact texas_multiway_train --parallel 2
ctest --test-dir build -C Debug --output-on-failure -R '^test_multiway_bucket_artifact$'
powershell.exe -NoProfile -File .\scripts\run\test_pb12_diagnostic_monitor.ps1
```

Prepare a fresh run directory, run the monitored 10K qualification, then run the isolated checkpoint-only resume check:

```powershell
$runName = 'pb12_diagnostic_f1_10k_20261030_retry3'
& .\scripts\run\prepare_f1_10k_checkpoint_qualification.ps1 -RunName $runName
& .\scripts\run\run_pb12_diagnostic.ps1 -RunName $runName
& .\scripts\run\resume_f1_10k_checkpoint_qualification.ps1 -RunName $runName
```

Proceed to preparation only if the build, CTest, and monitor test succeed. Run resume only after the monitored training run exits successfully and its gates pass. If the retry directory already exists, choose another fresh name. The preparation step hashes the trainer and bucket artifact and writes a run-specific config under `tmp/<runName>`. It does not start a compiled program. The qualification runner enforces fresh output paths, validates those hashes before and after training, checks the 12/12/12 config, launches the trainer, samples process memory and store disk use, records a stable live-byte sample when observing each checkpoint, and stops on the configured hard limits. Training startup validates the artifact identity and every table's bucket count before traversal.

The resume step copies the completed External store to an isolated snapshot, restores `checkpoints/latest.bin` with zero added batches, exports a second policy, and compares its SHA-256 and model identity with the original.

The first retry3 resume wrote matching policy hashes and report identity and trajectory counts, but the wrapper reported a blank exit code and did not persist resource measurements. That attempt does not qualify. The focused monitor test passed, and a separate named checkpoint-only resume against the same completed training run passed on 2026-10-30:

```powershell
$runName = 'pb12_diagnostic_f1_10k_20261030_retry3'
& .\scripts\run\resume_f1_10k_checkpoint_qualification.ps1 -RunName $runName -ResumeName 'retry1'
```

The named attempt writes under `tmp/<runName>/resume_attempt_<resumeName>`; choose a fresh resume name if that directory exists. For retry3, the monitor observed 10 checkpoints and 9 cycles with P99 pass. Training peaks were 22.40 GiB working set and 8.28 GiB private commit; minimum available memory was 21.46 GiB, and maximum disk amplification was 1.83x. The `resume_attempt_retry1` resume exited 0, reported 10,000 accepted and zero discarded trajectories, matched model identity `14334205690234538347`, and produced a policy with the same SHA-256 as the original (`D6421B352A1181F10B96C781396327266C84527F8EB3CD51A08C29B6036B7A6A`). Resume peaks were 16.30 GiB working set and 2.04 GiB private commit, with minimum available memory of 27.64 GiB. Evidence is under `tmp/pb12_diagnostic_f1_10k_20261030_retry3/resume_attempt_retry1/`. The previous root-level resume files remain preserved as failed evidence.

## Acceptance criteria

- The prepared plan and `training.conf` identify `F1-QUAL-10K-12-v1`, six players, 12/12/12 buckets, seed 1, one fixed worker, 1,000 trajectories per batch, and checkpoint interval 1. Trainer and bucket SHA-256 values remain unchanged through the run.
- Training exits 0 with 10 batches, 10,000 accepted trajectories, zero discarded trajectories, `failed=false`, and a nonempty exported `policy.mwfb`. The training report's model identity and bucket-model hash are present.
- The monitor observes all 10 journaled and published checkpoints and all 9 checkpoint cycles. At least 3 complete two-cycle cohorts have increasing live database sizes. The three-cohort P99 hard stop must not trigger. Checkpoint write nanoseconds are reported separately from cycle durations.
- Peak working set stays at or below 40 GiB, private commit at or below 48 GiB, and available physical memory at or above 3 GiB. Disk amplification stays at or below 3x live bytes in every stable sample, including each checkpoint sample. The configured disk preflight succeeds.
- The checkpoint-only resume exits 0, retains 10,000 accepted trajectories, has the same model identity, and exports a byte-identical policy. Resume process memory remains within the same memory limits.

Any failed or missing gate blocks scaling. The retry3 run passed the 10K profile gates and supports moving to the next bounded scale-up gate; it does not qualify or authorize a 50M run. Prior diagnostics used a mismatched bucket config, and the 3K observation did not contain enough cycles.

## Evidence to retain

- `plan.json`, `preparation_evidence.json`, `training.conf`
- `monitor.jsonl`, `report.json`, `training.log`, `training.log.diagnostics.*.jsonl`
- `checkpoints/latest.bin` and `policy.mwfb`
- Preserve all files under each `resume_attempt_<resumeName>/`, including the equivalence/report JSON, resumed policy, resume config, stdout/stderr, diagnostics, checkpoints, and isolated store snapshot.
- Preserve older root-level resume files and failed named attempts; never overwrite evidence.
- The report and monitor end events, including cycle cohorts, peak memory, minimum available memory, maximum disk amplification, checkpoint-write nanoseconds, and policy hashes

Use a fresh run name for each training attempt and a fresh `ResumeName` for each resume attempt.
