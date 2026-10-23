[CmdletBinding()]
param(
    [string]$InputDirectory = 'tmp\th1-th2-formal-qualification',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($InputDirectory)) {
    $InputDirectory = Join-Path $repoRoot $InputDirectory
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $InputDirectory
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Read-Json([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required artifact not found: $Path"
    }
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

function Normalize-Rows([object]$Value) {
    $rows = @()
    foreach ($item in @($Value)) {
        if ($item -is [System.Array]) {
            foreach ($nestedItem in $item) {
                $rows += $nestedItem
            }
        } else {
            $rows += $item
        }
    }
    return $rows
}

function Get-Number([object]$Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return 0.0
    }
    return [double]$property.Value
}

function Get-Correlation([object[]]$Rows, [string]$XName, [string]$YName) {
    $pairs = @(
        foreach ($row in $Rows) {
            $x = Get-Number $row $XName
            $y = Get-Number $row $YName
            if (-not [double]::IsNaN($x) -and -not [double]::IsInfinity($x) -and
                -not [double]::IsNaN($y) -and -not [double]::IsInfinity($y)) {
                [pscustomobject]@{ x = $x; y = $y }
            }
        }
    )
    if ($pairs.Count -lt 3) {
        return $null
    }
    $xMean = ($pairs | Measure-Object -Property x -Average).Average
    $yMean = ($pairs | Measure-Object -Property y -Average).Average
    $numerator = 0.0
    $xSum = 0.0
    $ySum = 0.0
    foreach ($pair in $pairs) {
        $xDelta = $pair.x - $xMean
        $yDelta = $pair.y - $yMean
        $numerator += $xDelta * $yDelta
        $xSum += $xDelta * $xDelta
        $ySum += $yDelta * $yDelta
    }
    if ($xSum -le 0.0 -or $ySum -le 0.0) {
        return $null
    }
    return $numerator / [math]::Sqrt($xSum * $ySum)
}

function Get-DerivedSample([object]$Row) {
    $active = Get-Number $Row 'worker_active_nanoseconds'
    $minimum = Get-Number $Row 'minimum_worker_active_nanoseconds'
    $maximum = Get-Number $Row 'maximum_worker_active_nanoseconds'
    $safeActive = [math]::Max($active, 1.0)
    $safeMaximum = [math]::Max($maximum, 1.0)
    return [pscustomobject][ordered]@{
        scheduler = [string]$Row.scheduler
        pinning = if ([bool]$Row.pin_workers) { 'pinned' } else { 'unpinned' }
        workers = [int]$Row.workers
        repeat = [int]$Row.repeat
        trajectories_per_second = Get-Number $Row 'trajectories_per_second'
        worker_active_nanoseconds = $active
        process_cpu_nanoseconds = Get-Number $Row 'process_cpu_nanoseconds'
        active_imbalance = ($maximum - $minimum) / $safeMaximum
        barrier_share = (Get-Number $Row 'barrier_wait_nanoseconds') / $safeActive
        cv_wait_share = (Get-Number $Row 'worker_cv_wait_nanoseconds') / $safeActive
        wake_latency_share = (Get-Number $Row 'worker_wake_latency_nanoseconds') / $safeActive
        scheduler_claim_share = (Get-Number $Row 'worker_scheduler_claim_nanoseconds') / $safeActive
        tail_idle_share = (Get-Number $Row 'worker_tail_idle_nanoseconds') / $safeActive
        process_cpu_ratio = (Get-Number $Row 'process_cpu_nanoseconds') / $safeActive
        tail_idle_nanoseconds = Get-Number $Row 'worker_tail_idle_nanoseconds'
        stdout = [string]$Row.stdout
    }
}

$sampleRows = Normalize-Rows (Read-Json (Join-Path $InputDirectory 'steady_state_samples.json'))
$varianceRows = Normalize-Rows (Read-Json (Join-Path $InputDirectory 'steady_state_variance_summary.json'))
if ($sampleRows.Count -eq 0) {
    throw 'No steady-state samples were found.'
}

$derivedRows = @(
    foreach ($sampleRow in $sampleRows) {
        try {
            Get-DerivedSample -Row $sampleRow
        } catch {
            throw "Failed to derive sample scheduler=$($sampleRow.scheduler) workers=$($sampleRow.workers) repeat=$($sampleRow.repeat): $($_.Exception.Message)"
        }
    }
)
$targetRows = @($derivedRows | Where-Object { $_.scheduler -eq 'dynamic' -and $_.workers -eq 2 })
$controlRows = @($derivedRows | Where-Object { $_.scheduler -eq 'fixed' -and $_.workers -eq 2 })
if ($targetRows.Count -ne 10) {
    throw "Expected 10 dynamic 2-worker samples, found $($targetRows.Count)."
}
if ($controlRows.Count -ne 10) {
    throw "Expected 10 fixed 2-worker control samples, found $($controlRows.Count)."
}

$targetGroups = @(
    foreach ($pinning in @('pinned', 'unpinned')) {
        $rows = @($targetRows | Where-Object { $_.pinning -eq $pinning })
        if ($rows.Count -ne 5) {
            throw "Expected 5 dynamic 2-worker $pinning samples, found $($rows.Count)."
        }
        $throughput = @($rows | ForEach-Object { [double]$_.trajectories_per_second })
        $mean = ($throughput | Measure-Object -Average).Average
        $stddev = [math]::Sqrt((($throughput | ForEach-Object { ($_ - $mean) * ($_ - $mean) } | Measure-Object -Sum).Sum) / $throughput.Count)
        [pscustomobject][ordered]@{
            scheduler = 'dynamic'
            pinning = $pinning
            workers = 2
            sample_count = $rows.Count
            minimum_trajectories_per_second = ($throughput | Measure-Object -Minimum).Minimum
            median_trajectories_per_second = [double](($varianceRows | Where-Object { $_.scheduler -eq 'dynamic' -and $_.workers -eq 2 -and (([bool]$_.pin_workers) -eq ($pinning -eq 'pinned')) }).median_trajectories_per_second)
            maximum_trajectories_per_second = ($throughput | Measure-Object -Maximum).Maximum
            throughput_standard_deviation = $stddev
            throughput_coefficient_of_variation = if ($mean -gt 0) { $stddev / $mean } else { $null }
            throughput_relative_range = (($throughput | Measure-Object -Maximum).Maximum - ($throughput | Measure-Object -Minimum).Minimum) / $mean
            lowest_repeat = $rows | Sort-Object trajectories_per_second | Select-Object -First 1
            highest_repeat = $rows | Sort-Object trajectories_per_second -Descending | Select-Object -First 1
        }
    }
)

$hostRows = @(
    foreach ($pinning in @('pinned', 'unpinned')) {
        $pinValue = $pinning -eq 'pinned'
        $row = $varianceRows | Where-Object { $_.scheduler -eq 'dynamic' -and $_.workers -eq 2 -and ([bool]$_.pin_workers) -eq $pinValue }
        [pscustomobject][ordered]@{
            pinning = $pinning
            host_context_complete = [bool]$row.host_context_complete
            host_clock_delta_mhz = [double]$row.host_clock_delta_mhz
            host_load_delta_percent = [double]$row.host_load_delta_percent
            benchmark_affinity = [string]$row.benchmark_affinity
            hardware_concurrency = [int]$row.hardware_concurrency
        }
    }
)

$correlationMetrics = @('active_imbalance', 'barrier_share', 'cv_wait_share', 'wake_latency_share', 'scheduler_claim_share', 'tail_idle_share', 'process_cpu_ratio')
$correlations = [ordered]@{}
foreach ($metric in $correlationMetrics) {
    $correlations[$metric] = Get-Correlation $targetRows $metric 'trajectories_per_second'
}

$targetOutliers = @(
    foreach ($group in $targetGroups) {
        [pscustomobject][ordered]@{
            pinning = $group.pinning
            lowest_repeat = $group.lowest_repeat.repeat
            lowest_throughput = $group.lowest_repeat.trajectories_per_second
            lowest_active_imbalance = $group.lowest_repeat.active_imbalance
            lowest_tail_idle_share = $group.lowest_repeat.tail_idle_share
            lowest_cv_wait_share = $group.lowest_repeat.cv_wait_share
            highest_repeat = $group.highest_repeat.repeat
            highest_throughput = $group.highest_repeat.trajectories_per_second
            highest_active_imbalance = $group.highest_repeat.active_imbalance
            highest_tail_idle_share = $group.highest_repeat.tail_idle_share
            highest_cv_wait_share = $group.highest_repeat.cv_wait_share
        }
    }
)

$summary = [ordered]@{
    schema_version = 1
    analysis = 'TH1-TH2 dynamic 2-worker variance review'
    input_directory = $InputDirectory
    target = 'dynamic workers=2'
    target_sample_count = $targetRows.Count
    control = 'fixed workers=2'
    control_sample_count = $controlRows.Count
    target_groups = $targetGroups
    target_host_context = $hostRows
    target_throughput_metric_correlations = $correlations
    target_outliers = $targetOutliers
    host_boundary_explains_variance = [bool](($hostRows | Where-Object { -not $_.host_context_complete -or $_.host_clock_delta_mhz -ne 0 -or $_.host_load_delta_percent -gt 5 }).Count -gt 0)
    decision = 'diagnostic_only_no_solver_change'
    next_measurement = 'If investigation continues, record per-repeat host snapshots and scheduler event counters; do not select an optimization from this sample alone.'
}

$sampleCsv = Join-Path $OutputDirectory 'th1_th2_dynamic_2worker_variance_samples.csv'
$controlCsv = Join-Path $OutputDirectory 'th1_th2_fixed_2worker_control_samples.csv'
$summaryPath = Join-Path $OutputDirectory 'th1_th2_dynamic_2worker_variance_summary.json'
$targetRows | Export-Csv -LiteralPath $sampleCsv -NoTypeInformation
$controlRows | Export-Csv -LiteralPath $controlCsv -NoTypeInformation
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

$targetGroups | Select-Object scheduler, pinning, workers, sample_count, minimum_trajectories_per_second, median_trajectories_per_second, maximum_trajectories_per_second, throughput_coefficient_of_variation, throughput_relative_range | Format-Table -AutoSize | Out-String | Write-Output
Write-Output "[th1-th2-variance] summary=$summaryPath"
Write-Output "[th1-th2-variance] samples=$sampleCsv"
Write-Output "[th1-th2-variance] controls=$controlCsv"
Write-Output '[th1-th2-variance] decision=diagnostic_only_no_solver_change'
