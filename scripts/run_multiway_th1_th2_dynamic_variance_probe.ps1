[CmdletBinding()]
param(
    [string]$BinaryPath = '',
    [string]$OutputDirectory = 'tmp\th1-th2-dynamic-variance-probe',
    [ValidateRange(1, 1000000)] [int]$BatchSize = 1000,
    [ValidateRange(1, 16)] [int]$WarmupBatches = 1,
    [ValidateRange(1, 16)] [int]$TimedBatches = 4,
    [ValidateRange(1, 20)] [int]$Repeats = 5,
    [UInt64]$Seed = 0x77,
    [ValidateRange(1, [uint32]::MaxValue)] [UInt32]$DynamicChunkSize = 4
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot 'build\RelWithDebInfo\texas_solver_multiway_training_scaling.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot $BinaryPath
}
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) { throw "Benchmark binary not found: $BinaryPath" }
if ($WarmupBatches + $TimedBatches -gt 16) { throw 'Warmup plus timed batches exceeds the benchmark capacity.' }
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot $OutputDirectory }

function Get-HostSnapshot {
    $processors = @(); try { $processors = @(Get-CimInstance Win32_Processor -ErrorAction Stop) } catch { }
    $current = @($processors | Where-Object { $_.CurrentClockSpeed -gt 0 } | ForEach-Object { [double]$_.CurrentClockSpeed })
    $maximum = @($processors | Where-Object { $_.MaxClockSpeed -gt 0 } | ForEach-Object { [double]$_.MaxClockSpeed })
    $loads = @($processors | Where-Object { $null -ne $_.LoadPercentage } | ForEach-Object { [double]$_.LoadPercentage })
    $totalCpu = $null
    try {
        $counter = Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 1 -ErrorAction Stop
        $totalCpu = [math]::Round([double]$counter.CounterSamples[0].CookedValue, 2)
    } catch { }
    [ordered]@{
        captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        processor_name = if ($processors.Count -gt 0) { [string]$processors[0].Name } else { $null }
        logical_processor_count = if ($processors.Count -gt 0) { [int]$processors[0].NumberOfLogicalProcessors } else { $null }
        current_clock_mhz = if ($current.Count -gt 0) { [math]::Round(($current | Measure-Object -Average).Average, 2) } else { $null }
        max_clock_mhz = if ($maximum.Count -gt 0) { [math]::Round(($maximum | Measure-Object -Average).Average, 2) } else { $null }
        load_percent = if ($loads.Count -gt 0) { [math]::Round(($loads | Measure-Object -Average).Average, 2) } else { $totalCpu }
        total_cpu_percent = $totalCpu
    }
}

function Read-Sample([string]$Path, [object]$Case, [int]$Repeat, [object]$Before, [object]$After) {
    $line = @(Get-Content -LiteralPath $Path | Where-Object { $_ -match '^sample ' } | Select-Object -First 1)
    if ($line.Count -eq 0) { throw "No sample line found in $Path" }
    $pattern = '^sample workers=(?<workers>\d+) batch_size=(?<batch>\d+) dynamic_chunk_size=(?<chunk>\d+) repeat=(?<source_repeat>\d+) .*?trajectories=(?<trajectories>\d+) elapsed_ns=(?<elapsed>\d+) trajectories_per_second=(?<throughput>[0-9.]+) coordinator_wait_ns=(?<coordinator_wait>\d+) barrier_wait_ns=(?<barrier>\d+) coordinator_lock_wait_ns=(?<lock>\d+) worker_active_ns=(?<active>\d+) worker_wake_latency_ns=(?<wake>\d+) worker_cv_wait_ns=(?<cv>\d+) worker_scheduler_claim_ns=(?<claim>\d+) worker_tail_idle_ns=(?<tail>\d+) worker_active_min_ns=(?<minimum>\d+) worker_active_max_ns=(?<maximum>\d+) process_cpu_ns=(?<cpu>\d+) .*?fingerprint=(?<fingerprint>\d+)$'
    if ($line[0] -notmatch $pattern) { throw "Sample line format is not recognized in $Path" }
    [pscustomobject][ordered]@{
        scheduler = [string]$Case.scheduler; pinning = if ($Case.pin_workers) { 'pinned' } else { 'unpinned' }
        pin_workers = [bool]$Case.pin_workers; probe_repeat = $Repeat
        workers = [int]$matches.workers; batch_size = [int]$matches.batch; dynamic_chunk_size = [int]$matches.chunk
        source_repeat = [int]$matches.source_repeat; trajectories = [uint64]$matches.trajectories; elapsed_nanoseconds = [uint64]$matches.elapsed
        trajectories_per_second = [double]::Parse($matches.throughput, [Globalization.CultureInfo]::InvariantCulture)
        coordinator_wait_nanoseconds = [uint64]$matches.coordinator_wait; barrier_wait_nanoseconds = [uint64]$matches.barrier
        coordinator_lock_wait_nanoseconds = [uint64]$matches.lock; worker_active_nanoseconds = [uint64]$matches.active
        worker_wake_latency_nanoseconds = [uint64]$matches.wake; worker_cv_wait_nanoseconds = [uint64]$matches.cv
        worker_scheduler_claim_nanoseconds = [uint64]$matches.claim; worker_tail_idle_nanoseconds = [uint64]$matches.tail
        minimum_worker_active_nanoseconds = [uint64]$matches.minimum; maximum_worker_active_nanoseconds = [uint64]$matches.maximum
        process_cpu_nanoseconds = [uint64]$matches.cpu; fingerprint = [uint64]$matches.fingerprint
        host_before = $Before; host_after = $After
        host_clock_delta_mhz = if ($null -ne $Before.current_clock_mhz -and $null -ne $After.current_clock_mhz) { [math]::Abs([double]$After.current_clock_mhz - [double]$Before.current_clock_mhz) } else { $null }
        host_load_delta_percent = if ($null -ne $Before.load_percent -and $null -ne $After.load_percent) { [math]::Abs([double]$After.load_percent - [double]$Before.load_percent) } else { $null }
        stdout = $Path
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$cases = @(
    [pscustomobject]@{ scheduler = 'dynamic'; pin_workers = $true; id = 'dynamic-pinned' },
    [pscustomobject]@{ scheduler = 'dynamic'; pin_workers = $false; id = 'dynamic-unpinned' },
    [pscustomobject]@{ scheduler = 'fixed'; pin_workers = $true; id = 'fixed-pinned' },
    [pscustomobject]@{ scheduler = 'fixed'; pin_workers = $false; id = 'fixed-unpinned' }
)
$samples = @(); $phase = 0; $totalPhases = $cases.Count * $Repeats
foreach ($case in $cases) {
    for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
        ++$phase
        $stdoutPath = Join-Path $OutputDirectory ('{0}-repeat-{1}.stdout.log' -f $case.id, $repeat)
        $stderrPath = Join-Path $OutputDirectory ('{0}-repeat-{1}.stderr.log' -f $case.id, $repeat)
        $before = Get-HostSnapshot
        $arguments = @('--workers', '2', '--batch-sizes', $BatchSize.ToString(), '--warmup-batches', $WarmupBatches.ToString(), '--timed-batches', $TimedBatches.ToString(), '--repeats', '1', '--seed', $Seed.ToString(), '--scheduler', $case.scheduler, '--dynamic-chunk-sizes', $DynamicChunkSize.ToString(), '--pin-workers', $(if ($case.pin_workers) { 'true' } else { 'false' }))
        Write-Output "Phase $phase is in progress."
        & $BinaryPath @arguments 1> $stdoutPath 2> $stderrPath
        if ($LASTEXITCODE -ne 0) { throw "Phase $phase failed for $($case.id) repeat=$repeat (exit code $LASTEXITCODE). Details: $stderrPath" }
        $after = Get-HostSnapshot
        $samples += Read-Sample $stdoutPath $case $repeat $before $after
        Write-Output "Phase $phase/$totalPhases completed."
    }
}

$binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BinaryPath).Hash
$samplePath = Join-Path $OutputDirectory 'variance_probe_samples.json'
$csvPath = Join-Path $OutputDirectory 'variance_probe_samples.csv'
$manifestPath = Join-Path $OutputDirectory 'variance_probe_manifest.json'
$samples | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $samplePath -Encoding UTF8
$samples | Export-Csv -LiteralPath $csvPath -NoTypeInformation
[ordered]@{
    schema_version = 1
    captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    binary = [ordered]@{ path = $BinaryPath; sha256 = $binaryHash }
    protocol = [ordered]@{ workers = @(2); schedulers = @('dynamic', 'fixed'); pinning_modes = @('pinned', 'unpinned'); batch_size = $BatchSize; warmup_batches = $WarmupBatches; timed_batches = $TimedBatches; repeats = $Repeats; dynamic_chunk_size = $DynamicChunkSize; seed = $Seed; process_per_repeat = $true }
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output "[th1-th2-variance-probe] manifest=$manifestPath"
Write-Output "[th1-th2-variance-probe] samples=$samplePath"
Write-Output "[th1-th2-variance-probe] csv=$csvPath"
