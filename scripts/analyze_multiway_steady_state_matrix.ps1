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

$rows | Sort-Object id, dynamic_chunk_size, workers |
    Format-Table id, scheduler, pin_workers, dynamic_chunk_size, workers, batch_size, median_trajectories_per_second, fingerprint -AutoSize |
    Out-String | Write-Output
Write-Output "[steady-state] summary=$summaryPath"
Write-Output "[steady-state] csv=$csvPath"
Write-Output "[steady-state] PASS: $($rows.Count) worker summaries, one non-zero fingerprint=$expectedFingerprint"
