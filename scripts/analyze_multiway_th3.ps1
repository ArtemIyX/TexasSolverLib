[CmdletBinding()]
param(
    [string]$InputDirectory = 'tmp\multiway-th3-smoke',
    [string]$RequiredChunkSizes = '1,2,4,8,16'
)

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($InputDirectory)) {
    $InputDirectory = Join-Path $repoRoot $InputDirectory
}
if ($RequiredChunkSizes -notmatch '^\d+(,\d+)*$') {
    throw 'RequiredChunkSizes must be a comma-separated list of positive integers.'
}
$expectedChunks = @($RequiredChunkSizes -split ',' | ForEach-Object { [int]$_ })
if (@($expectedChunks | Where-Object { $_ -lt 1 }).Count -ne 0) {
    throw 'RequiredChunkSizes must contain only positive integers.'
}

$manifestPath = Join-Path $InputDirectory 'matrix_manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "TH3 manifest not found: $manifestPath"
}

$manifest = ConvertFrom-Json -InputObject (Get-Content -Raw -LiteralPath $manifestPath)
$expectedWorkers = @($manifest.protocol.workers | ForEach-Object { [int]$_ })
$summaryPath = Join-Path $InputDirectory 'steady_state_matrix_summary.json'
$samplePath = Join-Path $InputDirectory 'steady_state_sample_metrics.json'
if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
    throw "Run analyze_multiway_steady_state_matrix.ps1 first: $summaryPath"
}
$parsedRows = ConvertFrom-Json -InputObject (Get-Content -Raw -LiteralPath $summaryPath)
$rows = [System.Collections.Generic.List[object]]::new()
foreach ($item in $parsedRows) {
    [void]$rows.Add($item)
}
$sampleRows = @()
if (Test-Path -LiteralPath $samplePath -PathType Leaf) {
    $parsedSamples = ConvertFrom-Json -InputObject (Get-Content -Raw -LiteralPath $samplePath)
    $sampleRows = [System.Collections.Generic.List[object]]::new()
    foreach ($item in $parsedSamples) {
        [void]$sampleRows.Add($item)
    }
}

$dynamicRows = @($rows | Where-Object { $_.scheduler -eq 'dynamic' })
$fixedRows = @($rows | Where-Object { $_.scheduler -eq 'fixed' })
if ($dynamicRows.Count -eq 0) {
    throw 'TH3 requires dynamic scheduler summaries.'
}
if ($fixedRows.Count -eq 0) {
    throw 'TH3 requires fixed scheduler summaries for comparison.'
}

$actualChunks = [System.Collections.Generic.List[int]]::new()
foreach ($row in $dynamicRows) {
    [void]$actualChunks.Add([int]$row.dynamic_chunk_size)
}
$actualChunks = @($actualChunks | Sort-Object -Unique)
$missingChunks = @($expectedChunks | Where-Object { $actualChunks -notcontains $_ })
if ($missingChunks.Count -ne 0) {
    throw "TH3 chunk coverage failed. Missing: $($missingChunks -join ', ')"
}

foreach ($chunk in $expectedChunks) {
    foreach ($worker in $expectedWorkers) {
        $dynamic = @($dynamicRows | Where-Object { $_.dynamic_chunk_size -eq $chunk -and $_.workers -eq $worker })
        if ($dynamic.Count -ne 1) {
            throw "TH3 dynamic coverage failed for chunk=$chunk workers=$worker"
        }
        if ([double]$dynamic[0].median_trajectories_per_second -le 0 -or [uint64]$dynamic[0].fingerprint -eq 0) {
            throw "TH3 invalid dynamic result for chunk=$chunk workers=$worker"
        }
    }
}

$dynamicFour = @($dynamicRows | Where-Object { $_.dynamic_chunk_size -eq 4 })
foreach ($worker in $expectedWorkers) {
    $fixed = @($fixedRows | Where-Object { $_.workers -eq $worker })
    $dynamic = @($dynamicFour | Where-Object { $_.workers -eq $worker })
    if ($fixed.Count -ne 1 -or $dynamic.Count -ne 1) {
        throw "TH3 fixed/dynamic comparison coverage failed for workers=$worker"
    }
    if ([uint64]$fixed[0].fingerprint -ne [uint64]$dynamic[0].fingerprint) {
        throw "TH3 fingerprint mismatch for workers=$worker"
    }
}

$sampleDynamic = @($sampleRows | Where-Object { $_.scheduler -eq 'dynamic' })
foreach ($chunk in $expectedChunks) {
    foreach ($worker in $expectedWorkers) {
        $samples = @($sampleDynamic | Where-Object { $_.dynamic_chunk_size -eq $chunk -and $_.workers -eq $worker })
        if ($samples.Count -eq 0) {
            throw "TH3 sample metrics missing for chunk=$chunk workers=$worker"
        }
    }
}

function Get-Median([double[]]$Values) {
    $ordered = @($Values | Sort-Object)
    $middle = [int]($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) { return [double]$ordered[$middle] }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

$chunkSummary = @(
    foreach ($chunk in $expectedChunks) {
        $chunkRows = @($dynamicRows | Where-Object { $_.dynamic_chunk_size -eq $chunk })
        $chunkSamples = @($sampleDynamic | Where-Object { $_.dynamic_chunk_size -eq $chunk })
        [pscustomobject]@{
            scheduler = 'dynamic'
            dynamic_chunk_size = $chunk
            worker_count = @($chunkRows | Select-Object -ExpandProperty workers -Unique).Count
            sample_count = $chunkSamples.Count
            median_trajectories_per_second = Get-Median @($chunkRows | ForEach-Object { [double]$_.median_trajectories_per_second })
            median_scheduler_claim_nanoseconds = Get-Median @($chunkSamples | ForEach-Object { [double]$_.median_worker_scheduler_claim_nanoseconds })
            median_tail_idle_nanoseconds = Get-Median @($chunkSamples | ForEach-Object { [double]$_.median_worker_tail_idle_nanoseconds })
            median_process_cpu_nanoseconds = Get-Median @($chunkSamples | ForEach-Object { [double]$_.median_process_cpu_nanoseconds })
            fingerprint = [uint64]$chunkRows[0].fingerprint
        }
    }
)

$outputJson = Join-Path $InputDirectory 'th3_dynamic_scheduler_summary.json'
$outputCsv = Join-Path $InputDirectory 'th3_dynamic_scheduler_summary.csv'
$chunkSummary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $outputJson -Encoding UTF8
$chunkSummary | Export-Csv -LiteralPath $outputCsv -NoTypeInformation
$chunkSummary | Format-Table dynamic_chunk_size, worker_count, sample_count, median_trajectories_per_second, median_scheduler_claim_nanoseconds, median_tail_idle_nanoseconds, median_process_cpu_nanoseconds -AutoSize | Out-String | Write-Output
Write-Output "[th3] summary=$outputJson"
Write-Output "[th3] csv=$outputCsv"
Write-Output "[th3] PASS: chunks=$($expectedChunks -join ',') workers=$($expectedWorkers -join ',') fixed_dynamic_fingerprint_match=true"
