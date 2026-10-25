[CmdletBinding()]
param(
    [string]$BinaryPath = '',
    [string]$OutputDirectory = 'tmp\multiway-steady-state',
    [ValidateRange(1, 1000000)]
    [int]$BatchSize = 1000,
    [ValidateRange(1, 16)]
    [int]$WarmupBatches = 1,
    [ValidateRange(1, 16)]
    [int]$TimedBatches = 4,
    [ValidateRange(1, 100)]
    [int]$Repeats = 5,
    [string]$DynamicChunkSizes = '4',
    [string]$Workers = '1,2,4,8',
    [ValidateSet('both', 'fixed', 'dynamic')]
    [string]$Schedulers = 'both',
    [ValidateSet('both', 'pinned', 'unpinned')]
    [string]$PinningModes = 'both',
    [UInt64]$Seed = 0x77
)

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

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
        logical_processor_count = if ($processors.Count -gt 0) { [int]$processors[0].NumberOfLogicalProcessors } else { $null }
        current_clock_mhz = if ($currentClockValues.Count -gt 0) { [math]::Round(($currentClockValues | Measure-Object -Average).Average, 2) } else { $null }
        max_clock_mhz = if ($maxClockValues.Count -gt 0) { [math]::Round(($maxClockValues | Measure-Object -Average).Average, 2) } else { $null }
        processor_load_percent = if ($loadValues.Count -gt 0) { [math]::Round(($loadValues | Measure-Object -Average).Average, 2) } else { $null }
        total_cpu_percent = $totalCpuPercent
        load_percent = if ($loadValues.Count -gt 0) { [math]::Round(($loadValues | Measure-Object -Average).Average, 2) } else { $totalCpuPercent }
        process_count = try { @(Get-Process -ErrorAction Stop).Count } catch { $null }
    }
}
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot 'build\RelWithDebInfo\texas_solver_multiway_training_scaling.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot $BinaryPath
}

if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Benchmark binary not found: $BinaryPath"
}
if ($WarmupBatches + $TimedBatches -gt 16) {
    throw 'Warmup plus timed batches exceeds the benchmark capacity.'
}
if ($DynamicChunkSizes -notmatch '^\d+(,\d+)*$' -or @($DynamicChunkSizes -split ',' | Where-Object { [int64]$_ -lt 1 -or [int64]$_ -gt [uint32]::MaxValue }).Count -ne 0) {
    throw 'DynamicChunkSizes must be a comma-separated list of positive uint32 values.'
}
if ($Workers -notmatch '^\d+(,\d+)*$' -or @($Workers -split ',' | Where-Object { [int64]$_ -lt 1 -or [int64]$_ -gt 16 }).Count -ne 0) {
    throw 'Workers must be a comma-separated list of values in the range 1..16.'
}

$workerValues = @($Workers -split ',' | ForEach-Object { [int]$_ })
$schedulerValues = if ($Schedulers -eq 'both') { @('fixed', 'dynamic') } else { @($Schedulers) }
$pinningValues = if ($PinningModes -eq 'both') { @($true, $false) } else { @($PinningModes -eq 'pinned') }
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BinaryPath).Hash
$groups = @()
$totalGroups = $schedulerValues.Count * $pinningValues.Count
$phase = 0

foreach ($scheduler in $schedulerValues) {
    foreach ($pinWorkers in $pinningValues) {
        ++$phase
        $pinValue = if ($pinWorkers) { 'true' } else { 'false' }
        $groupId = 'steady-{0}-{1}' -f $scheduler, $(if ($pinWorkers) { 'pinned' } else { 'unpinned' })
        $stdoutPath = Join-Path $OutputDirectory ($groupId + '.stdout.log')
        $stderrPath = Join-Path $OutputDirectory ($groupId + '.stderr.log')
        $hostBefore = Get-HostSnapshot
        $arguments = @(
            '--workers', $Workers,
            '--batch-sizes', $BatchSize.ToString(),
            '--warmup-batches', $WarmupBatches.ToString(),
            '--timed-batches', $TimedBatches.ToString(),
            '--repeats', $Repeats.ToString(),
            '--seed', $Seed.ToString(),
            '--scheduler', $scheduler,
            '--dynamic-chunk-sizes', $DynamicChunkSizes,
            '--pin-workers', $pinValue
        )

        Write-Output "Phase $phase is in progress."
        & $BinaryPath @arguments 1> $stdoutPath 2> $stderrPath
        if ($LASTEXITCODE -ne 0) {
            throw "Phase $phase failed for group $groupId (exit code $LASTEXITCODE). Details: $stderrPath"
        }
        $hostAfter = Get-HostSnapshot
        Write-Output "Phase $phase/$totalGroups completed."

        $fixtureLine = @(Get-Content -LiteralPath $stdoutPath | Where-Object { $_ -match '^fixture=' } | Select-Object -First 1)
        $benchmarkAffinity = $null
        $hardwareConcurrency = $null
        if ($fixtureLine.Count -gt 0 -and $fixtureLine[0] -match 'hardware_concurrency=(?<hardware>\d+).*worker_affinity=(?<affinity>\S+)') {
            $hardwareConcurrency = [int]$matches.hardware
            $benchmarkAffinity = [string]$matches.affinity
        }

        $groups += [ordered]@{
            id = $groupId
            scheduler = $scheduler
            pin_workers = $pinWorkers
            arguments = $arguments
            stdout = $stdoutPath
            stderr = $stderrPath
            host_before = $hostBefore
            host_after = $hostAfter
            host_context_schema_version = 1
            processor_placement = [ordered]@{
                requested = if ($pinWorkers) { 'pinned' } else { 'unpinned' }
                benchmark_affinity = $benchmarkAffinity
                hardware_concurrency = $hardwareConcurrency
            }
        }
    }
}

$manifest = [ordered]@{
    schema_version = 1
    captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    binary = [ordered]@{
        path = $BinaryPath
        sha256 = $binaryHash
    }
    protocol = [ordered]@{
        workers = $workerValues
        batch_size = $BatchSize
        warmup_batches = $WarmupBatches
        timed_batches = $TimedBatches
        repeats = $Repeats
        dynamic_chunk_sizes = @($DynamicChunkSizes -split ',' | ForEach-Object { [uint32]$_ })
        schedulers = $schedulerValues
        pinning_modes = @($pinningValues | ForEach-Object { if ($_ ) { 'pinned' } else { 'unpinned' } })
        seed = $Seed
        checkpoint_output = 'disabled'
        blueprint_output = 'disabled'
    }
    groups = $groups
}
$manifestPath = Join-Path $OutputDirectory 'matrix_manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output "[steady-state] manifest=$manifestPath"
