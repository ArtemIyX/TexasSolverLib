# F1 50K checkpoint qualification

This is the next bounded scale-up after the completed F1 10K gate. It keeps the six-player 12/12/12 model, seed 1, one fixed worker, 1,000 trajectories per batch, and checkpoint interval 1. It runs 50 batches for 50,000 trajectories, producing 50 checkpoint observations and 49 cycles.

The profile raises the public-state and sparse-row caps to 5,000,000, configures a 256 MiB row/page cache, and reserves 16 GiB for disk operations. The external page store uses a direct page-ID slot index bounded by `maximum_sparse_rows + maximum_sparse_values / page_width`; the index consumes part of that configured cache budget. The 10K run reached 660,520 peak public states, 272,737 peak sparse rows, and 890,742 generated discovery states. These are capacity bounds for the larger bounded run. The qualification gates remain unchanged: working set at or below 40 GiB, private commit at or below 48 GiB, available physical memory at or above 3 GiB, disk amplification at or below 3x, and no three increasing two-cycle P99 cohorts.

Run with a fresh name:

```powershell
powershell.exe -NoProfile -File .\scripts\run\test_pb12_diagnostic_monitor.ps1
$runName = 'pb12_diagnostic_f1_50k_20261030_gate1'
& .\scripts\run\prepare_f1_50k_checkpoint_qualification.ps1 -RunName $runName
& .\scripts\run\run_pb12_diagnostic.ps1 -RunName $runName
& .\scripts\run\resume_f1_10k_checkpoint_qualification.ps1 -RunName $runName -ResumeName 'resume1'
```

Run the resume only if the training monitor and report pass. Use another `ResumeName` if `resume_attempt_resume1` already exists. The resume restores the latest checkpoint with zero added batches into a copied store snapshot. It must exit 0, report 50,000 accepted and zero discarded trajectories, match the source model identity and policy SHA-256, and remain within the same memory limits.

Retain all files in `tmp/<runName>/` and `tmp/<runName>/resume_attempt_<resumeName>/`. A pass qualifies only the 50K profile; it does not qualify a larger run or authorize 50M.

## Gate 1 result

`pb12_diagnostic_f1_50k_20261030_gate1` stopped at the unchanged P99 hard gate after 6 of 50 checkpoints and 5 of 49 cycles. The first two complete cohort P99 values were 66,039.4304 ms and 67,013.8412 ms, with live-byte maxima of 143,844,948 and 206,790,582. Cycle 5 was 80,958.331 ms at 251,849,651 live bytes, establishing a strictly higher lower bound for the third cohort. This is the intended early-stop condition, not a monitor defect.

The final monitor event recorded exit code -1 from the monitor-terminated trainer and marked the P99 gate as failed. Peak working set was 16.92 GiB, peak private commit 2.68 GiB, minimum available memory 26.71 GiB, and maximum disk amplification 1.84x; those limits passed. No final report or policy was produced, so the checkpoint-only resume was not run and the 50K profile is not qualified.

Preserve the stopped attempt at `tmp/pb12_diagnostic_f1_50k_20261030_gate1/`. Do not retry or scale further by weakening the P99 gate. Investigate the increasing batch-to-checkpoint cycle durations before selecting another bounded run.
