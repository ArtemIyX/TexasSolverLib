[CmdletBinding()]
param(
    [string]$InputDirectory = 'tmp\multiway-steady-state'
)

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($InputDirectory)) {
    $InputDirectory = Join-Path $repoRoot $InputDirectory
}
$manifestPath = Join-Path $InputDirectory 'matrix_manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Steady-state manifest not found: $manifestPath"
}

$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$expectedWorkers = @($manifest.protocol.workers | ForEach-Object { [int]$_ })
if ($expectedWorkers.Count -eq 0) {
    throw 'Steady-state manifest has no worker protocol.'
}

$rows = @()
$sampleRows = @()
$groupCount = 0
foreach ($group in @($manifest.groups)) {
    ++$groupCount
    $stdoutPath = [string]$group.stdout
    if (-not [System.IO.Path]::IsPathRooted($stdoutPath)) {
        $stdoutPath = Join-Path $InputDirectory $stdoutPath
    }
    if (-not (Test-Path -LiteralPath $stdoutPath -PathType Leaf)) {
        throw "Steady-state stdout log not found: $stdoutPath"
    }

    $groupRows = @()
    foreach ($line in Get-Content -LiteralPath $stdoutPath) {
        $sampleMatched = $line -match '^sample workers=(?<workers>\d+) batch_size=(?<batch>\d+) dynamic_chunk_size=(?<chunk>\d+) repeat=(?<repeat>\d+) .*? trajectories_per_second=(?<throughput>[0-9.]+) .*? barrier_wait_ns=(?<barrier>\d+) .*? worker_active_ns=(?<active>\d+) .*? worker_wake_latency_ns=(?<wake>\d+) .*? worker_cv_wait_ns=(?<cv>\d+)(?: worker_scheduler_claim_ns=(?<claim>\d+))? .*? worker_tail_idle_ns=(?<tail>\d+) .*? worker_active_min_ns=(?<minimum>\d+) .*? worker_active_max_ns=(?<maximum>\d+) .*? process_cpu_ns=(?<cpu>\d+) '
        if ($sampleMatched) {
            $sampleRows += [pscustomobject]@{
                id = [string]$group.id
                scheduler = [string]$group.scheduler
                pin_workers = [bool]$group.pin_workers
                workers = [int]$matches.workers
                batch_size = [int]$matches.batch
                dynamic_chunk_size = [int]$matches.chunk
                repeat = [int]$matches.repeat
                trajectories_per_second = [double]::Parse(
                    $matches.throughput,
                    [Globalization.CultureInfo]::InvariantCulture)
                barrier_wait_nanoseconds = [uint64]$matches.barrier
                worker_active_nanoseconds = [uint64]$matches.active
                worker_wake_latency_nanoseconds = [uint64]$matches.wake
                worker_cv_wait_nanoseconds = [uint64]$matches.cv
                worker_scheduler_claim_nanoseconds = if ([string]::IsNullOrWhiteSpace($matches.claim)) { 0U } else { [uint64]$matches.claim }
                worker_tail_idle_nanoseconds = [uint64]$matches.tail
                minimum_worker_active_nanoseconds = [uint64]$matches.minimum
                maximum_worker_active_nanoseconds = [uint64]$matches.maximum
                process_cpu_nanoseconds = [uint64]$matches.cpu
                stdout = $stdoutPath
            }
            continue
        }
        $matched = $line -match '^summary workers=(?<workers>\d+) batch_size=(?<batch>\d+)(?: dynamic_chunk_size=(?<chunk>\d+))? median_trajectories_per_second=(?<throughput>[0-9.]+) fingerprint=(?<fingerprint>\d+)$'
        if (-not $matched) {
            continue
        }
        $chunkSize = if ([string]::IsNullOrWhiteSpace($matches.chunk)) { 4 } else { [int]$matches.chunk }
        $groupRows += [pscustomobject]@{
            id = [string]$group.id
            scheduler = [string]$group.scheduler
            pin_workers = [bool]$group.pin_workers
            workers = [int]$matches.workers
            batch_size = [int]$matches.batch
            dynamic_chunk_size = $chunkSize
            median_trajectories_per_second = [double]::Parse(
                $matches.throughput,
                [Globalization.CultureInfo]::InvariantCulture)
            fingerprint = [uint64]$matches.fingerprint
            stdout = $stdoutPath
        }
    }

    if ($groupRows.Count -eq 0) {
        throw "No benchmark summaries found in: $stdoutPath"
    }
    $rows += $groupRows
}

if ($groupCount -eq 0 -or $rows.Count -eq 0) {
    throw 'Steady-state manifest contains no benchmark groups.'
}

$expectedFingerprint = $null
foreach ($row in $rows) {
    if ($row.fingerprint -eq 0) {
        throw "Zero merge fingerprint in group $($row.id), workers=$($row.workers), chunk=$($row.dynamic_chunk_size)"
    }
    if ($null -eq $expectedFingerprint) {
        $expectedFingerprint = $row.fingerprint
    } elseif ($row.fingerprint -ne $expectedFingerprint) {
        throw "Merge fingerprint mismatch: expected $expectedFingerprint, got $($row.fingerprint)"
    }
}

foreach ($group in ($rows | Group-Object { "$($_.id)|$($_.dynamic_chunk_size)|$($_.batch_size)" })) {
    $actualWorkers = @($group.Group | Select-Object -ExpandProperty workers | Sort-Object -Unique)
    $missingWorkers = @($expectedWorkers | Where-Object { $actualWorkers -notcontains $_ })
    if ($missingWorkers.Count -ne 0 -or $actualWorkers.Count -ne $expectedWorkers.Count) {
        throw "Worker summary coverage failed for $($group.Name): missing=$($missingWorkers -join ',')"
    }
}

$summaryPath = Join-Path $InputDirectory 'steady_state_matrix_summary.json'
$csvPath = Join-Path $InputDirectory 'steady_state_matrix_summary.csv'
$rows | Sort-Object id, dynamic_chunk_size, workers |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$rows | Sort-Object id, dynamic_chunk_size, workers |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation

if ($sampleRows.Count -gt 0) {
    function Get-Median([double[]]$Values) {
        $ordered = @($Values | Sort-Object)
        if ($ordered.Count -eq 0) {
            return 0.0
        }
        $middle = [int]($ordered.Count / 2)
        if (($ordered.Count % 2) -eq 1) {
            return [double]$ordered[$middle]
        }
        return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
    }

    $sampleSummary = @(
        foreach ($sampleGroup in ($sampleRows | Group-Object { "$($_.id)|$($_.dynamic_chunk_size)|$($_.workers)|$($_.batch_size)" })) {
            $first = $sampleGroup.Group[0]
            [pscustomobject]@{
                id = $first.id
                scheduler = $first.scheduler
                pin_workers = $first.pin_workers
                workers = $first.workers
                batch_size = $first.batch_size
                dynamic_chunk_size = $first.dynamic_chunk_size
                sample_count = $sampleGroup.Count
                median_trajectories_per_second = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.trajectories_per_second })
                median_barrier_wait_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.barrier_wait_nanoseconds })
                median_worker_active_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.worker_active_nanoseconds })
                median_worker_wake_latency_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.worker_wake_latency_nanoseconds })
                median_worker_cv_wait_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.worker_cv_wait_nanoseconds })
                median_worker_scheduler_claim_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.worker_scheduler_claim_nanoseconds })
                median_worker_tail_idle_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.worker_tail_idle_nanoseconds })
                median_minimum_worker_active_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.minimum_worker_active_nanoseconds })
                median_maximum_worker_active_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.maximum_worker_active_nanoseconds })
                median_process_cpu_nanoseconds = Get-Median @($sampleGroup.Group | ForEach-Object { [double]$_.process_cpu_nanoseconds })
            }
        }
    )
    $samplePath = Join-Path $InputDirectory 'steady_state_sample_metrics.json'
    $sampleCsvPath = Join-Path $InputDirectory 'steady_state_sample_metrics.csv'
    $sampleRows | Sort-Object id, dynamic_chunk_size, workers, repeat |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $InputDirectory 'steady_state_samples.json') -Encoding UTF8
    $sampleRows | Sort-Object id, dynamic_chunk_size, workers, repeat |
        Export-Csv -LiteralPath (Join-Path $InputDirectory 'steady_state_samples.csv') -NoTypeInformation
    $sampleSummary | Sort-Object id, dynamic_chunk_size, workers |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $samplePath -Encoding UTF8
    $sampleSummary | Sort-Object id, dynamic_chunk_size, workers |
        Export-Csv -LiteralPath $sampleCsvPath -NoTypeInformation
    $sampleSummary | Sort-Object id, dynamic_chunk_size, workers |
        Format-Table id, scheduler, workers, dynamic_chunk_size, sample_count, median_trajectories_per_second, median_worker_scheduler_claim_nanoseconds, median_worker_tail_idle_nanoseconds, median_process_cpu_nanoseconds -AutoSize |
        Out-String | Write-Output
    Write-Output "[steady-state] sample_metrics=$samplePath"
    Write-Output "[steady-state] sample_csv=$sampleCsvPath"
}

$rows | Sort-Object id, dynamic_chunk_size, workers |
    Format-Table id, scheduler, pin_workers, dynamic_chunk_size, workers, batch_size, median_trajectories_per_second, fingerprint -AutoSize |
    Out-String | Write-Output
Write-Output "[steady-state] summary=$summaryPath"
Write-Output "[steady-state] csv=$csvPath"
Write-Output "[steady-state] PASS: $($rows.Count) worker summaries, one non-zero fingerprint=$expectedFingerprint"
