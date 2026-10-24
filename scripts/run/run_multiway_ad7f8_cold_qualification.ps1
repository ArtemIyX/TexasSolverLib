[CmdletBinding()]
param(
    [string]$OutputRoot = 'tmp\ad7f8-cold-qualification',
    [ValidateRange(1, 300)]
    [int]$CaptureSeconds = 60,
    [ValidateRange(2, 120)]
    [int]$ConnectionTimeoutSeconds = 30,
    [switch]$PinWorkers
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runner = Join-Path $PSScriptRoot 'run_multiway_tracy_training_capture.ps1'

if ([System.IO.Path]::IsPathRooted($OutputRoot)) {
    $resolvedOutputRoot = $OutputRoot
} else {
    $resolvedOutputRoot = Join-Path $repoRoot $OutputRoot
}

function Get-HostSnapshot {
    $processors = @()
    try {
        $processors = @(Get-CimInstance Win32_Processor -ErrorAction Stop)
    } catch {
    }
    $currentClockValues = @($processors | Where-Object { $_.CurrentClockSpeed -gt 0 } | ForEach-Object { [double]$_.CurrentClockSpeed })
    $maxClockValues = @($processors | Where-Object { $_.MaxClockSpeed -gt 0 } | ForEach-Object { [double]$_.MaxClockSpeed })
    $loadValues = @($processors | Where-Object { $null -ne $_.LoadPercentage } | ForEach-Object { [double]$_.LoadPercentage })
    $totalCpuPercent = $null
    try {
        $counter = Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 1 -ErrorAction Stop
        $totalCpuPercent = [math]::Round([double]$counter.CounterSamples[0].CookedValue, 2)
    } catch {
    }

    return [ordered]@{
        captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        processor_name = if ($processors.Count -gt 0) { [string]$processors[0].Name } else { $null }
        processor_count = $processors.Count
        logical_processor_count = if ($processors.Count -gt 0) { [int]$processors[0].NumberOfLogicalProcessors } else { $null }
        current_clock_mhz = if ($currentClockValues.Count -gt 0) { [math]::Round(($currentClockValues | Measure-Object -Average).Average, 2) } else { $null }
        max_clock_mhz = if ($maxClockValues.Count -gt 0) { [math]::Round(($maxClockValues | Measure-Object -Average).Average, 2) } else { $null }
        processor_load_percent = if ($loadValues.Count -gt 0) { [math]::Round(($loadValues | Measure-Object -Average).Average, 2) } else { $null }
        total_cpu_percent = $totalCpuPercent
        process_count = try { @(Get-Process -ErrorAction Stop).Count } catch { $null }
    }
}

function Get-FileBytes([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return [int64](Get-Item -LiteralPath $Path).Length
    }
    return $null
}

if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "Capture runner was not found: $runner"
}

$captures = @(
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-fixed-1w-r1'; Scheduler = 'fixed' },
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-fixed-1w-r2'; Scheduler = 'fixed' },
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-dynamic-1w-r1'; Scheduler = 'dynamic' },
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-dynamic-1w-r2'; Scheduler = 'dynamic' }
)

New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
for ($index = 0; $index -lt $captures.Count; $index++) {
    $capture = $captures[$index]
    $outputDirectory = Join-Path $resolvedOutputRoot $capture.Id
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    Write-Output "[ad7f8] capture $($index + 1)/$($captures.Count) is in progress: $($capture.Id)"

    $before = Get-HostSnapshot
    $runnerLogPath = Join-Path $outputDirectory 'ad7f8_runner.log'
    $runnerArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $runner,
        '-CaptureId', $capture.Id,
        '-OutputDirectory', $outputDirectory,
        '-Workers', '1',
        '-Batches', '1',
        '-TrajectoriesPerBatch', '1000',
        '-Scheduler', $capture.Scheduler,
        '-CaptureSeconds', $CaptureSeconds.ToString(),
        '-ConnectionTimeoutSeconds', $ConnectionTimeoutSeconds.ToString()
    )
    if ($PinWorkers.IsPresent) {
        $runnerArguments += '-PinWorkers'
    }

    & powershell.exe @runnerArguments *> $runnerLogPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "AD7F.8 capture failed: id=$($capture.Id) exit_code=$exitCode; see $runnerLogPath"
    }

    $reportPath = Join-Path $outputDirectory 'report.json'
    $manifestPath = Join-Path $outputDirectory 'run_manifest.json'
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "AD7F.8 capture did not produce required artifacts: $outputDirectory"
    }

    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    $after = Get-HostSnapshot
    $tracePath = [string]$manifest.trace.path
    $csvPath = [string]$manifest.csv.path
    $context = [ordered]@{
        schema_version = 1
        capture_id = $capture.Id
        scheduler = $capture.Scheduler
        workers = 1
        batches = 1
        trajectories_per_batch = 1000
        pin_workers = [bool]$PinWorkers.IsPresent
        captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        host_before = $before
        host_after = $after
        tracy = [ordered]@{
            version = [string]$manifest.tracy_version
            instrumentation_kind = [string]$report.instrumentation_kind
            report_tracy_version = [string]$report.tracy_version
            trace_capture_id = [string]$report.trace_capture_id
            trace_path = $tracePath
            trace_bytes = Get-FileBytes $tracePath
            csv_path = $csvPath
            csv_bytes = Get-FileBytes $csvPath
        }
        processor_placement = [ordered]@{
            affinity_requested = [bool]$report.affinity_requested
            requested_processor = @($report.worker_requested_processor)
            resolved_processor = @($report.worker_resolved_processor)
            affinity_masks = @($report.worker_affinity_masks)
            affinity_succeeded = @($report.worker_affinity_succeeded)
        }
    }
    $context | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDirectory 'operator_context.json') -Encoding utf8
    Write-Output "[ad7f8] capture $($index + 1)/$($captures.Count) completed: $($capture.Id)"
}

Write-Output '[ad7f8] all four cold captures completed'
