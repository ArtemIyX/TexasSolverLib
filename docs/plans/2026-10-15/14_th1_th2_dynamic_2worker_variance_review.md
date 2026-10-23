# TH1-TH2 - Dynamic 2-worker variance review

## Goal

Determine whether the formal TH1-TH2 dynamic 2-worker throughput variance is
explained by measured scheduler telemetry, worker imbalance, or host context.
This is a diagnostic follow-up. It does not qualify a new performance result
and does not authorize a solver hot-path change.

## Scope

- Reuse the completed formal qualification artifacts.
- Analyze dynamic workers `2`, pinned and unpinned, as the target.
- Compare against fixed workers `2`, pinned and unpinned, as the control.
- Derive active-time imbalance, barrier share, condition-variable wait share,
  wake latency share, scheduler-claim share, tail-idle share, and process CPU
  ratio.
- Correlate each derived metric with per-repeat throughput.
- Report the lowest and highest throughput repeat for each target placement.

## Exclusions

- No new large matrix.
- No worker `8` run.
- No memory qualification.
- No solver or scheduler implementation change based only on this sample.

## Acceptance

- Ten dynamic 2-worker samples are analyzed.
- Ten fixed 2-worker control samples are analyzed.
- Both target placement modes have five samples.
- Host-context completeness and boundary deltas are reported.
- Derived samples, controls, correlations, and outliers are written to files.

## Command

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\analyze_multiway_th1_th2_dynamic_variance.ps1 `
  -InputDirectory tmp\th1-th2-formal-qualification
```

## Implementation status

Complete 2026-10-23.

The replay analyzer processed the existing formal qualification artifacts:

- 10 dynamic 2-worker target samples.
- 10 fixed 2-worker control samples.
- Complete host context for both dynamic placement modes.
- Zero MHz boundary delta; load deltas were `2%` pinned and `0%` unpinned.
- The largest target-throughput correlations were scheduler-claim share
  (`0.976`) and condition-variable wait share (`0.947`). These are diagnostic
  associations only because the sample is small and the counters are measured
  over the same timed interval as throughput.
- Lowest/highest throughput repeats and all derived metrics were exported.

The result is `diagnostic_only_no_solver_change`. A follow-up benchmark should
only add per-repeat host snapshots and scheduler event counters if this issue
needs further investigation.

The bounded probe implementation is available:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_multiway_th1_th2_dynamic_variance_probe.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\analyze_multiway_th1_th2_dynamic_variance_probe.ps1
```

It runs only fixed/dynamic, pinned/unpinned, workers `2`, with one process per
repeat. It is intentionally not run as part of this implementation change.

### Outputs

- `tmp/th1-th2-formal-qualification/th1_th2_dynamic_2worker_variance_summary.json`
- `tmp/th1-th2-formal-qualification/th1_th2_dynamic_2worker_variance_samples.csv`
- `tmp/th1-th2-formal-qualification/th1_th2_fixed_2worker_control_samples.csv`
- `scripts/run_multiway_th1_th2_dynamic_variance_probe.ps1`
- `scripts/analyze_multiway_th1_th2_dynamic_variance_probe.ps1`
