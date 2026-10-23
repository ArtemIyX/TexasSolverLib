[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$Workers = 8,

    [ValidateRange(1, 10)]
    [int]$Batches = 1,

    [ValidateRange(1, [int]::MaxValue)]
    [int]$TrajectoriesPerBatch = 1000,

    [switch]$PinWorkers,

    [ValidateSet('fixed', 'dynamic')]
    [string]$Scheduler = 'fixed',

    [ValidateSet('normal', 'tracy')]
    [string]$BinaryKind = 'normal',

    [string]$BinaryPath = '',

    [string]$TraceCaptureId = '',

    [string]$RunDirectory = ''
)

$repoRoot = Split-Path -Parent $PSScriptRoot

Write-Output '[ram1000] starting bounded RAM measurement'
Write-Output "[ram1000] repository_root=$repoRoot"
Write-Output "[ram1000] requested_workers=$Workers"
Write-Output "[ram1000] requested_batches=$Batches"
Write-Output "[ram1000] trajectories_per_batch=$TrajectoriesPerBatch"
Write-Output "[ram1000] pin_workers=$($PinWorkers.IsPresent)"
Write-Output "[ram1000] scheduler=$Scheduler"
Write-Output "[ram1000] binary_kind=$BinaryKind"

if ([string]::IsNullOrWhiteSpace($RunDirectory)) {
    $RunDirectory = Join-Path $repoRoot ("tmp\ram_{0}_after" -f ($Batches * $TrajectoriesPerBatch))
} elseif (-not [System.IO.Path]::IsPathRooted($RunDirectory)) {
    $RunDirectory = Join-Path $repoRoot $RunDirectory
}

if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = if ($BinaryKind -eq 'tracy') {
        Join-Path $repoRoot 'build-tracy\RelWithDebInfo\texas_multiway_train_tracy.exe'
    } else {
        Join-Path $repoRoot 'build\Debug\texas_multiway_train.exe'
    }
} elseif (-not [System.IO.Path]::IsPathRooted($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot $BinaryPath
}
$executable = $BinaryPath
$sourceConfig = Join-Path $repoRoot 'artifacts\f1_sizing_v1\pilot_batch1.cfg'
$bucketArtifact = Join-Path $repoRoot 'artifacts\f1_sizing_v1\buckets\buckets.bin'

$gitCommit = (& git -C $repoRoot rev-parse HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($gitCommit)) {
    throw 'Could not determine git commit for run provenance'
}
$env:TEXASSOLVER_GIT_COMMIT = $gitCommit
$env:TEXASSOLVER_BUILD_CONFIGURATION = if ($BinaryKind -eq 'tracy') { 'RelWithDebInfo' } else { 'Debug' }
$env:TEXASSOLVER_COMPILER_IDENTITY = if ([string]::IsNullOrWhiteSpace($env:VCToolsVersion)) { 'MSVC' } else { "MSVC $($env:VCToolsVersion)" }
if ($BinaryKind -eq 'tracy') {
    $env:TEXASSOLVER_TRACE_CAPTURE_ID = if ([string]::IsNullOrWhiteSpace($TraceCaptureId)) { 'not-captured' } else { $TraceCaptureId }
} else {
    Remove-Item Env:TEXASSOLVER_TRACE_CAPTURE_ID -ErrorAction SilentlyContinue
}

Write-Output '[ram1000] validating executable, expanded pilot config, and bucket artifact'
foreach ($requiredPath in @($executable, $sourceConfig, $bucketArtifact)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
    $requiredBytes = (Get-Item -LiteralPath $requiredPath).Length
    Write-Output "[ram1000] validated path=$requiredPath bytes=$requiredBytes"
}

$configValues = @{}
foreach ($configLine in Get-Content -LiteralPath $sourceConfig) {
    $trimmedConfigLine = $configLine.Trim()
    if ($trimmedConfigLine -match '^([^#=]+)=(.*)$') {
        $configValues[$matches[1].Trim()] = $matches[2].Trim()
    }
}

$configWorkerCount = [int]$configValues['training_worker_count']
Write-Output "[ram1000] config passed: schema_version=$($configValues['schema_version']) profile_kind=$($configValues['profile_kind']) profile_id=$($configValues['profile_id'])"
Write-Output "[ram1000] config passed: seed=$($configValues['deterministic_seed']) configured_workers=$configWorkerCount source_trajectories_per_batch=$($configValues['trajectories_per_batch']) target_trajectories=$($configValues['target_trajectories'])"
Write-Output "[ram1000] config passed: maximum_public_states=$($configValues['maximum_public_states']) maximum_sparse_rows=$($configValues['maximum_sparse_rows']) maximum_sparse_values=$($configValues['maximum_sparse_values'])"
$expectedTrajectories = $Batches * $TrajectoriesPerBatch
Write-Output "[ram1000] run passed: batches=$Batches total_trajectories=$expectedTrajectories worker_override=$Workers pin_workers=$($PinWorkers.IsPresent)"

New-Item -ItemType Directory -Force -Path $RunDirectory | Out-Null
$config = Join-Path $RunDirectory 'pilot_batch.cfg'
Copy-Item -LiteralPath $sourceConfig -Destination $config -Force
$configText = Get-Content -Raw -LiteralPath $config
$updatedConfigText = $configText -replace '(?m)^[ \t]*trajectories_per_batch[ \t]*=[ \t]*\d+[ \t]*\r?$', "trajectories_per_batch=$TrajectoriesPerBatch"
if ($updatedConfigText -eq $configText -and [int]$configValues['trajectories_per_batch'] -ne $TrajectoriesPerBatch) {
    throw "Could not replace trajectories_per_batch in run-local config: $config"
}
Set-Content -LiteralPath $config -Value $updatedConfigText -NoNewline
$checkpointDirectory = Join-Path $RunDirectory 'checkpoints'
New-Item -ItemType Directory -Force -Path $checkpointDirectory | Out-Null

$outputPath = Join-Path $RunDirectory 'blueprint.bin'
$reportPath = Join-Path $RunDirectory 'report.json'
$stdoutPath = Join-Path $RunDirectory 'stdout.log'
$stderrPath = Join-Path $RunDirectory 'stderr.log'

Write-Output "[ram1000] output directory ready: $RunDirectory"
Write-Output "[ram1000] report_path=$reportPath"
Write-Output "[ram1000] checkpoint_directory=$checkpointDirectory"
Write-Output '[ram1000] launching trainer'

$trainArguments = @(
    '--config', $config,
    '--input', $bucketArtifact,
    '--output', $outputPath,
    '--report', $reportPath,
    '--checkpoint-dir', $checkpointDirectory,
    '--batches', $Batches.ToString(),
    '--threads', $Workers.ToString(),
    '--scheduler', $Scheduler
)
if ($PinWorkers.IsPresent) {
    $trainArguments += @('--pin-workers', 'true')
} else {
    $trainArguments += @('--pin-workers', 'false')
}

$process = Start-Process -FilePath $executable `
    -ArgumentList $trainArguments `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath `
    -PassThru

Write-Output "[ram1000] trainer started: pid=$($process.Id)"

$peakWorkingSetBytes = [int64]0
$processCpuNanoseconds = [int64]0
$lastProgressAt = Get-Date
while (-not $process.HasExited) {
    try {
        $sample = Get-Process -Id $process.Id -ErrorAction Stop
        $peakWorkingSetBytes = [Math]::Max($peakWorkingSetBytes, [int64]$sample.WorkingSet64)
        $now = Get-Date
        if (($now - $lastProgressAt).TotalSeconds -ge 2) {
            Write-Output "[ram1000] trainer running: pid=$($process.Id) working_set_bytes=$([int64]$sample.WorkingSet64) peak_working_set_bytes=$peakWorkingSetBytes"
            $lastProgressAt = $now
        }
    } catch {
    }
    Start-Sleep -Milliseconds 200
}

$process.WaitForExit()
$process.Refresh()
try {
    $processCpuNanoseconds = [int64]$process.TotalProcessorTime.Ticks * 100
} catch {
}
$exitCode = $null
$exitCodeKnown = $false
try {
    $exitCodeProperty = $process.GetType().GetProperty('ExitCode')
    $exitCodeValue = $exitCodeProperty.GetValue($process, $null)
    if ($null -ne $exitCodeValue) {
        $exitCode = [int]$exitCodeValue
        $exitCodeKnown = $true
    }
} catch {
}

if ($exitCodeKnown) {
    Write-Output "[ram1000] trainer exited: exit_code=$exitCode external_peak_working_set_bytes=$peakWorkingSetBytes"
} else {
    Write-Output "[ram1000] trainer exited: exit_code=unavailable; report gate will determine success; external_peak_working_set_bytes=$peakWorkingSetBytes"
}

if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
    throw "Training report was not created: $reportPath"
}

Write-Output '[ram1000] training report found, reading metrics'
$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
[pscustomobject]@{
    exit_code = $exitCode
    exit_code_available = $exitCodeKnown
    pin_workers = $PinWorkers.IsPresent
    scheduler = $Scheduler
    binary_kind = $BinaryKind
    binary_path = $executable
    chunk_size = 4
    trajectories_per_batch = $TrajectoriesPerBatch
    process_cpu_nanoseconds = $processCpuNanoseconds
    trajectories = $report.trajectories
    accepted_trajectories = $report.accepted_trajectories
    discarded_trajectories = $report.discarded_trajectories
    failed = $report.failed
    peak_process_rss_bytes = $report.peak_process_rss_bytes
    current_process_rss_bytes = $report.current_process_rss_bytes
    memory_preflight_estimate_bytes = $report.memory_preflight_estimate_bytes
    worker_active_nanoseconds = $report.worker_active_nanoseconds
    process_cpu_nanoseconds_reported = $report.process_cpu_nanoseconds
    peak_working_set_bytes_reported = $report.peak_working_set_bytes
    instrumentation_kind = $report.instrumentation_kind
    tracy_version = $report.tracy_version
    trace_capture_id = $report.trace_capture_id
    allocation_probe_valid = $report.allocation_probe_valid
    minimum_worker_active_nanoseconds = $report.minimum_worker_active_nanoseconds
    maximum_worker_active_nanoseconds = $report.maximum_worker_active_nanoseconds
    coordinator_wait_nanoseconds = $report.coordinator_wait_nanoseconds
    barrier_wait_nanoseconds = $report.barrier_wait_nanoseconds
    coordinator_lock_wait_nanoseconds = $report.coordinator_lock_wait_nanoseconds
    delta_sort_nanoseconds = $report.delta_sort_nanoseconds
    merge_nanoseconds = $report.merge_nanoseconds
    coordinator_admission_nanoseconds = $report.coordinator_admission_nanoseconds
    trainer_only_nanoseconds = $report.trainer_only_nanoseconds
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
    affinity_requested = $report.affinity_requested
    peak_public_states = $report.peak_public_states
    peak_sparse_rows = $report.peak_sparse_rows
    peak_sparse_values = $report.peak_sparse_values
    peak_worker_delta_entries = $report.peak_worker_delta_entries
    checkpoint_bytes = $report.checkpoint_bytes
    blueprint_bytes = $report.blueprint_bytes
    checkpoint_telemetry = $report.checkpoint_telemetry
}

$metricsPath = Join-Path $RunDirectory 'run_metrics.json'
[pscustomobject]@{
    exit_code = $exitCode
    exit_code_available = $exitCodeKnown
    pinning = if ($PinWorkers.IsPresent) { 'pinned' } else { 'unpinned' }
    scheduler = $Scheduler
    chunk_size = 4
    workers = $Workers
    batches = $Batches
    trajectories_per_batch = $TrajectoriesPerBatch
    trajectories = $expectedTrajectories
    process_cpu_nanoseconds = $processCpuNanoseconds
    peak_working_set_bytes = $peakWorkingSetBytes
    report_path = $reportPath
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metricsPath -Encoding UTF8

Write-Output "[ram1000] report: trajectories=$($report.trajectories) accepted=$($report.accepted_trajectories) discarded=$($report.discarded_trajectories) failed=$($report.failed)"
Write-Output "[ram1000] report: peak_process_rss_bytes=$($report.peak_process_rss_bytes) memory_preflight_estimate_bytes=$($report.memory_preflight_estimate_bytes)"
Write-Output "[ram1000] report: peak_public_states=$($report.peak_public_states) peak_sparse_rows=$($report.peak_sparse_rows) peak_sparse_values=$($report.peak_sparse_values)"
Write-Output "[ram1000] timing: worker_active_ns=$($report.worker_active_nanoseconds) scheduler_claim_ns=$($report.worker_scheduler_claim_nanoseconds) min_worker_active_ns=$($report.minimum_worker_active_nanoseconds) max_worker_active_ns=$($report.maximum_worker_active_nanoseconds) coordinator_wait_ns=$($report.coordinator_wait_nanoseconds) barrier_wait_ns=$($report.barrier_wait_nanoseconds) lock_wait_ns=$($report.coordinator_lock_wait_nanoseconds) delta_sort_ns=$($report.delta_sort_nanoseconds) merge_ns=$($report.merge_nanoseconds) process_cpu_ns=$processCpuNanoseconds"
Write-Output "[ram1000] phases: admission_ns=$($report.coordinator_admission_nanoseconds) trainer_only_ns=$($report.trainer_only_nanoseconds) reported_cpu_ns=$($report.process_cpu_nanoseconds) peak_working_set_bytes=$($report.peak_working_set_bytes)"
Write-Output "[ram1000] workers: trajectories=$($report.worker_trajectory_counts -join ',') active_ns=$($report.worker_active_nanoseconds_by_worker -join ',') wake_ns=$($report.worker_wake_latency_nanoseconds_by_worker -join ',') cv_wait_ns=$($report.worker_cv_wait_nanoseconds_by_worker -join ',') scheduler_claim_ns=$($report.worker_scheduler_claim_nanoseconds_by_worker -join ',') tail_idle_ns=$($report.worker_tail_idle_nanoseconds_by_worker -join ',') allocations=$($report.worker_allocation_counts -join ',') allocated_bytes=$($report.worker_allocated_bytes -join ',') processors=$($report.worker_resolved_processor -join ',') affinity_ok=$($report.worker_affinity_succeeded -join ',')"
if ($null -ne $report.failed_batch) {
    Write-Output "[ram1000] failed_batch: attempted=$($report.failed_batch.trajectories_attempted) accepted=$($report.failed_batch.trajectories_accepted) discarded=$($report.failed_batch.trajectories_discarded) delta_entries=$($report.failed_batch.delta_entries_merged) discovery_capacity_exhausted=$($report.failed_batch.discovery_capacity_exhausted)"
    Write-Output "[ram1000] failed_batch timing: worker_active_ns=$($report.failed_batch.worker_active_nanoseconds) coordinator_wait_ns=$($report.failed_batch.coordinator_wait_nanoseconds) barrier_wait_ns=$($report.failed_batch.barrier_wait_nanoseconds) delta_sort_ns=$($report.failed_batch.delta_sort_nanoseconds) merge_ns=$($report.failed_batch.merge_nanoseconds)"
    Write-Output "[ram1000] failed_batch workers: trajectories=$($report.failed_batch.worker_trajectory_counts -join ',') active_ns=$($report.failed_batch.worker_active_nanoseconds_by_worker -join ',')"
}
Write-Output "[ram1000] logs: stdout=$stdoutPath stderr=$stderrPath"

if ($exitCodeKnown -and $exitCode -ne 0) {
    throw "Multiway $expectedTrajectories-trajectory run failed with exit code $exitCode. See $stdoutPath and $stderrPath"
}

if ($report.trajectories -ne $expectedTrajectories -or $report.accepted_trajectories -ne $expectedTrajectories -or $report.discarded_trajectories -ne 0 -or $report.failed -or $report.capacity_exhaustion_stage -ne 0) {
    throw "Multiway $expectedTrajectories-trajectory report failed the bounded pilot gate"
}

if ($null -eq $report.worker_trajectory_counts -or $null -eq $report.worker_active_nanoseconds_by_worker) {
    throw 'Training report lacks worker telemetry. Rebuild texas_multiway_train.exe before rerunning this script.'
}

Write-Output "[ram1000] PASS: configuration, execution, and $expectedTrajectories-trajectory report gates passed"
