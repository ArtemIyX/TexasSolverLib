[CmdletBinding()]
param(
    [ValidateRange(1, 10)]
    [int]$Batches = 3,

    [ValidateRange(1, [int]::MaxValue)]
    [int]$TrajectoriesPerBatch = 200,

    [ValidateSet('pinned', 'unpinned')]
    [string]$Pinning = 'pinned',

    [ValidateSet('both', 'fixed', 'dynamic')]
    [string]$Scheduling = 'both',

    [ValidateRange(1, 20)]
    [int]$Repeats = 3,

    [string[]]$Workers = @('4', '8'),

    [string]$OutputDirectory = ''
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'run_multiway_ram_1000.ps1'
$workerCounts = @(
    $Workers |
        ForEach-Object { $_ -split ',' } |
        ForEach-Object {
            $value = $_.Trim()
            if ([string]::IsNullOrWhiteSpace($value)) {
                throw 'Workers cannot contain empty values'
            }
            try {
                [int]$value
            } catch {
                throw "Invalid worker count: $value"
            }
        }
)
$pinningModes = @($Pinning)
$schedulingModes = if ($Scheduling -eq 'both') { @('fixed', 'dynamic') } else { @($Scheduling) }

if ($workerCounts.Count -eq 0 -or @($workerCounts | Where-Object { $_ -lt 1 -or $_ -gt 16 }).Count -ne 0) {
    throw 'Workers must contain values in the range 1..16'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot ("tmp\multiway_scheduler_{0}" -f ($Batches * $TrajectoriesPerBatch))
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Write-Output '[matrix] starting focused worker scheduler comparison'
Write-Output "[matrix] repository_root=$repoRoot"
Write-Output "[matrix] pinning=$($pinningModes -join ',') workers=$($workerCounts -join ',') scheduling=$($schedulingModes -join ',') repeats=$Repeats batches=$Batches trajectories_per_batch=$TrajectoriesPerBatch"

$results = @()
foreach ($pinningMode in $pinningModes) {
    foreach ($schedulerMode in $schedulingModes) {
        foreach ($workerCount in $workerCounts) {
            for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
                $runDirectory = Join-Path $OutputDirectory ("{0}\{1}\workers_{2}\repeat_{3}" -f $pinningMode, $schedulerMode, $workerCount, $repeat)
                Write-Output "[matrix] running pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat output_directory=$runDirectory"

                $runnerArguments = @(
                    '-NoProfile',
                    '-ExecutionPolicy', 'Bypass',
                    '-File', $runner,
                    '-Workers', $workerCount.ToString(),
                    '-Batches', $Batches.ToString(),
                    '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
                    '-Scheduler', $schedulerMode,
                    '-RunDirectory', $runDirectory
                )
                if ($pinningMode -eq 'pinned') {
                    $runnerArguments += '-PinWorkers'
                }
                & powershell.exe @runnerArguments
                if ($LASTEXITCODE -ne 0) {
                    throw "Worker matrix run failed for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat with exit code $LASTEXITCODE"
                }

                $reportPath = Join-Path $runDirectory 'report.json'
                $metricsPath = Join-Path $runDirectory 'run_metrics.json'
                if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or -not (Test-Path -LiteralPath $metricsPath -PathType Leaf)) {
                    throw "Worker matrix artifacts were not created for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }

                $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
                $metrics = Get-Content -Raw -LiteralPath $metricsPath | ConvertFrom-Json
                $expectedTrajectories = $Batches * $TrajectoriesPerBatch
                if ($metrics.scheduler -ne $schedulerMode -or $metrics.chunk_size -ne 4) {
                    throw "Run metadata mismatch for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }
                if ($report.trajectories -ne $expectedTrajectories -or
                    $report.accepted_trajectories -ne $expectedTrajectories -or
                    $report.discarded_trajectories -ne 0 -or
                    $report.failed -or
                    $report.capacity_exhaustion_stage -ne 0) {
                    throw "Correctness gate failed for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }
                if ($report.evidence.schema_version -ne 7 -or
                    $report.instrumentation_kind -ne 'none' -or
                    -not $report.allocation_probe_valid) {
                    throw "Worker matrix requires an unprofiled schema 7 report for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }
                if ($report.peak_process_rss_bytes -gt $report.memory_preflight_estimate_bytes) {
                    throw "Peak RSS exceeded the memory preflight estimate for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }

                $activePerTrajectory = @()
                for ($worker = 0; $worker -lt $report.worker_active_nanoseconds_by_worker.Count; ++$worker) {
                    $active = [int64]$report.worker_active_nanoseconds_by_worker[$worker]
                    $count = [int64]$report.worker_trajectory_counts[$worker]
                    if ($count -gt 0) {
                        $activePerTrajectory += [double]$active / [double]$count
                    } else {
                        $activePerTrajectory += 0.0
                    }
                }
                $activeRates = @($activePerTrajectory | Where-Object { $_ -gt 0.0 })
                $activeImbalance = 0.0
                if ($activeRates.Count -gt 0) {
                    $activeMean = ($activeRates | Measure-Object -Average).Average
                    $activeImbalance = ($activeRates | Measure-Object -Maximum).Maximum / $activeMean - 1.0
                }
                $elapsed = [double]$report.elapsed_wall_nanoseconds
                $workerActive = [double]$report.worker_active_nanoseconds
                $barrierShare = if ($elapsed -gt 0.0) { [double]$report.barrier_wait_nanoseconds / $elapsed } else { 0.0 }
                $lockShare = if ($workerActive -gt 0.0) { [double]$report.coordinator_lock_wait_nanoseconds / $workerActive } else { 0.0 }
                $mergeShare = if ($elapsed -gt 0.0) { [double]$report.merge_nanoseconds / $elapsed } else { 0.0 }
                $normalizedCpu = if ($elapsed -gt 0.0 -and $workerCount -gt 0) {
                    [double]$metrics.process_cpu_nanoseconds / ($elapsed * [double]$workerCount)
                } else { 0.0 }

                $results += [pscustomobject]@{
                    pinning = $pinningMode
                    scheduler = $schedulerMode
                    chunk_size = [int]$metrics.chunk_size
                    workers = $workerCount
                    repeat = $repeat
                    batches = $Batches
                    trajectories_per_batch = $TrajectoriesPerBatch
                    trajectories = $report.trajectories
                    trajectories_per_second = $report.trajectories_per_second
                    elapsed_wall_nanoseconds = $report.elapsed_wall_nanoseconds
                    process_cpu_nanoseconds = $metrics.process_cpu_nanoseconds
                    worker_active_nanoseconds = $report.worker_active_nanoseconds
                    minimum_worker_active_nanoseconds = $report.minimum_worker_active_nanoseconds
                    maximum_worker_active_nanoseconds = $report.maximum_worker_active_nanoseconds
                    coordinator_wait_nanoseconds = $report.coordinator_wait_nanoseconds
                    barrier_wait_nanoseconds = $report.barrier_wait_nanoseconds
                    coordinator_lock_wait_nanoseconds = $report.coordinator_lock_wait_nanoseconds
                    delta_sort_nanoseconds = $report.delta_sort_nanoseconds
                    merge_nanoseconds = $report.merge_nanoseconds
                    coordinator_admission_nanoseconds = $report.coordinator_admission_nanoseconds
                    trainer_only_nanoseconds = $report.trainer_only_nanoseconds
                    process_cpu_nanoseconds_reported = $report.process_cpu_nanoseconds
                    peak_working_set_bytes = $report.peak_working_set_bytes
                    active_nanoseconds_per_trajectory = ($activePerTrajectory -join ',')
                    active_time_imbalance = $activeImbalance
                    barrier_share = $barrierShare
                    lock_contention_share = $lockShare
                    serial_merge_share = $mergeShare
                    normalized_process_cpu_use = $normalizedCpu
                    peak_process_rss_bytes = $report.peak_process_rss_bytes
                    memory_preflight_estimate_bytes = $report.memory_preflight_estimate_bytes
                    worker_trajectory_counts = ($report.worker_trajectory_counts -join ',')
                    worker_active_nanoseconds_by_worker = ($report.worker_active_nanoseconds_by_worker -join ',')
                    worker_wake_latency_nanoseconds_by_worker = ($report.worker_wake_latency_nanoseconds_by_worker -join ',')
                    worker_cv_wait_nanoseconds_by_worker = ($report.worker_cv_wait_nanoseconds_by_worker -join ',')
                    worker_tail_idle_nanoseconds_by_worker = ($report.worker_tail_idle_nanoseconds_by_worker -join ',')
                    worker_allocation_counts = ($report.worker_allocation_counts -join ',')
                    worker_allocated_bytes = ($report.worker_allocated_bytes -join ',')
                    worker_resolved_processor = ($report.worker_resolved_processor -join ',')
                    worker_affinity_masks = ($report.worker_affinity_masks -join ',')
                    worker_affinity_succeeded = ($report.worker_affinity_succeeded -join ',')
                    fingerprint = $report.deterministic_merge_fingerprint
                    report_path = $reportPath
                }
            }
        }
    }
}

$fingerprints = @($results | Select-Object -ExpandProperty fingerprint | Sort-Object -Unique)
if ($fingerprints.Count -ne 1 -or $fingerprints[0] -eq 0) {
    throw "Focused scheduler correctness gate failed: expected one non-zero merge fingerprint, got $($fingerprints -join ',')"
}

foreach ($group in ($results | Group-Object { "$($_.pinning)|$($_.scheduler)|$($_.workers)" })) {
    $throughputs = @($group.Group | Select-Object -ExpandProperty trajectories_per_second | Sort-Object)
    $middle = [int][Math]::Floor($throughputs.Count / 2)
    $median = if ($throughputs.Count % 2 -eq 1) {
        [double]$throughputs[$middle]
    } else {
        ([double]$throughputs[$middle - 1] + [double]$throughputs[$middle]) / 2.0
    }
    $minimum = [double]($throughputs | Measure-Object -Minimum).Minimum
    $maximum = [double]($throughputs | Measure-Object -Maximum).Maximum
    foreach ($row in $group.Group) {
        $row | Add-Member -NotePropertyName throughput_median -NotePropertyValue $median -Force
        $row | Add-Member -NotePropertyName throughput_min -NotePropertyValue $minimum -Force
        $row | Add-Member -NotePropertyName throughput_max -NotePropertyValue $maximum -Force
    }
}

$jsonPath = Join-Path $OutputDirectory 'worker_matrix.json'
$csvPath = Join-Path $OutputDirectory 'worker_matrix.csv'
$results | Sort-Object pinning, scheduler, workers, repeat | Format-Table pinning, scheduler, workers, repeat, trajectories_per_second, throughput_median, active_time_imbalance, barrier_share, lock_contention_share, serial_merge_share, normalized_process_cpu_use, peak_process_rss_bytes
$results | Sort-Object pinning, scheduler, workers, repeat | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$results | Sort-Object pinning, scheduler, workers, repeat | Export-Csv -LiteralPath $csvPath -NoTypeInformation
Write-Output "[matrix] JSON summary=$jsonPath"
Write-Output "[matrix] CSV summary=$csvPath"
Write-Output "[matrix] PASS: $($results.Count) runs completed with zero discards, bounded RSS, and one merge fingerprint"
