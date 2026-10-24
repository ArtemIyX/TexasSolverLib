[CmdletBinding()]
param(
    [string]$InputDirectory = 'tmp\th1-th2-formal-qualification'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not [System.IO.Path]::IsPathRooted($InputDirectory)) {
    $InputDirectory = Join-Path $repoRoot $InputDirectory
}

function Read-Json([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required qualification artifact not found: $Path"
    }
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

function Assert-Equal([object]$Actual, [object]$Expected, [string]$Message) {
    if ([string]$Actual -ne [string]$Expected) {
        throw "$Message. Expected=$Expected Actual=$Actual"
    }
}

function Assert-Set([object[]]$Actual, [object[]]$Expected, [string]$Message) {
    $actualText = @($Actual | ForEach-Object { [string]$_ } | Sort-Object) -join ','
    $expectedText = @($Expected | ForEach-Object { [string]$_ } | Sort-Object) -join ','
    if ($actualText -ne $expectedText) {
        throw "$Message. Expected=$expectedText Actual=$actualText"
    }
}

$manifest = Read-Json (Join-Path $InputDirectory 'matrix_manifest.json')
$summaryJson = Get-Content -Raw (Join-Path $InputDirectory 'steady_state_matrix_summary.json') | ConvertFrom-Json
$varianceJson = Get-Content -Raw (Join-Path $InputDirectory 'steady_state_variance_summary.json') | ConvertFrom-Json
$sampleJson = Get-Content -Raw (Join-Path $InputDirectory 'steady_state_samples.json') | ConvertFrom-Json
$summaryRows = @($summaryJson)
$varianceRows = @($varianceJson)
$sampleRows = @($sampleJson)

$expectedWorkers = @(1, 2, 4)
$expectedSchedulers = @('fixed', 'dynamic')
$expectedPinning = @('pinned', 'unpinned')
$protocol = $manifest.protocol
Assert-Set @($protocol.workers) $expectedWorkers 'Worker protocol mismatch'
Assert-Set @($protocol.schedulers) $expectedSchedulers 'Scheduler protocol mismatch'
Assert-Set @($protocol.pinning_modes) $expectedPinning 'Pinning protocol mismatch'
Assert-Equal $protocol.batch_size 1000 'Batch size mismatch'
Assert-Equal $protocol.warmup_batches 1 'Warmup batch count mismatch'
Assert-Equal $protocol.timed_batches 4 'Timed batch count mismatch'
Assert-Equal $protocol.repeats 5 'Repeat count mismatch'
Assert-Equal $protocol.checkpoint_output 'disabled' 'Checkpoint output is not disabled'
Assert-Equal $protocol.blueprint_output 'disabled' 'Blueprint output is not disabled'

$expectedGroupCount = $expectedSchedulers.Count * $expectedPinning.Count
$varianceReviewRows = @()
Assert-Equal $manifest.groups.Count $expectedGroupCount 'Group count mismatch'
Assert-Equal $summaryRows.Count ($expectedGroupCount * $expectedWorkers.Count) 'Worker summary count mismatch'
Assert-Equal $varianceRows.Count ($expectedGroupCount * $expectedWorkers.Count) 'Variance summary count mismatch'
Assert-Equal $sampleRows.Count ($expectedGroupCount * $expectedWorkers.Count * $protocol.repeats) 'Sample count mismatch'

$fingerprints = @($summaryRows | ForEach-Object { [string]$_.fingerprint } | Sort-Object -Unique)
if ($fingerprints.Count -ne 1 -or [string]::IsNullOrWhiteSpace($fingerprints[0]) -or $fingerprints[0] -eq '0') {
    throw "Fingerprint gate failed: $($fingerprints -join ',')"
}

foreach ($row in $varianceRows) {
    Assert-Equal $row.sample_count $protocol.repeats "Repeat count mismatch for $($row.id), workers=$($row.workers)"
    if (-not [bool]$row.host_context_complete) {
        throw "Host context incomplete for $($row.id), workers=$($row.workers)"
    }
    if ([string]$row.variance_classification -ne 'stable' -or [string]$row.variance_interpretation -ne 'stable') {
        $varianceReviewRows += $row
    }
    $expectedAffinity = if ([bool]$row.pin_workers) { 'pinned_logical_processor' } else { 'unrestricted' }
    Assert-Equal $row.benchmark_affinity $expectedAffinity "Affinity metadata mismatch for $($row.id)"
    if ([int]$row.hardware_concurrency -le 0) {
        throw "Hardware concurrency is invalid for $($row.id)"
    }
}

$comparisons = @(
    foreach ($scheduler in $expectedSchedulers) {
        foreach ($worker in $expectedWorkers) {
            $pinned = $varianceRows | Where-Object { $_.scheduler -eq $scheduler -and $_.workers -eq $worker -and [bool]$_.pin_workers }
            $unpinned = $varianceRows | Where-Object { $_.scheduler -eq $scheduler -and $_.workers -eq $worker -and -not [bool]$_.pin_workers }
            if ($null -eq $pinned -or $null -eq $unpinned) {
                throw "Pinned/unpinned comparison coverage failed for scheduler=$scheduler workers=$worker"
            }
            $unpinnedThroughput = [double]$unpinned.median_trajectories_per_second
            $pinnedThroughput = [double]$pinned.median_trajectories_per_second
            [pscustomobject]@{
                scheduler = $scheduler
                workers = $worker
                unpinned_trajectories_per_second = $unpinnedThroughput
                pinned_trajectories_per_second = $pinnedThroughput
                pinned_vs_unpinned_percent = if ($unpinnedThroughput -gt 0.0) { (($pinnedThroughput / $unpinnedThroughput) - 1.0) * 100.0 } else { $null }
                pinned_relative_range = [double]$pinned.throughput_relative_range
                unpinned_relative_range = [double]$unpinned.throughput_relative_range
            }
        }
    }
)

$result = [ordered]@{
    schema_version = 1
    qualification = 'TH1-TH2'
    input_directory = $InputDirectory
    protocol_gate_pass = $true
    worker_summary_count = $summaryRows.Count
    sample_count = $sampleRows.Count
    fingerprint = $fingerprints[0]
    host_context_complete = $true
    variance_review_required = $varianceReviewRows.Count -gt 0
    qualification_pass = $true
    performance_variance_pass = $varianceReviewRows.Count -eq 0
    variance_review = @($varianceReviewRows | Select-Object scheduler, pin_workers, workers, median_trajectories_per_second, minimum_trajectories_per_second, maximum_trajectories_per_second, throughput_coefficient_of_variation, throughput_relative_range, variance_classification, variance_interpretation)
    comparisons = $comparisons
}
$resultPath = Join-Path $InputDirectory 'th1_th2_qualification_summary.json'
$csvPath = Join-Path $InputDirectory 'th1_th2_pinned_unpinned_comparison.csv'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding UTF8
$comparisons | Export-Csv -LiteralPath $csvPath -NoTypeInformation

$comparisons | Select-Object scheduler, workers, unpinned_trajectories_per_second, pinned_trajectories_per_second, pinned_vs_unpinned_percent, pinned_relative_range, unpinned_relative_range |
    Format-Table -AutoSize | Out-String | Write-Output
Write-Output "[th1-th2] qualification_summary=$resultPath"
Write-Output "[th1-th2] comparison_csv=$csvPath"
if ($varianceReviewRows.Count -eq 0) {
    Write-Output "[th1-th2] PASS: protocol, $($summaryRows.Count) worker summaries, $($sampleRows.Count) samples, fingerprint=$($fingerprints[0]), host context, variance, and pinning coverage"
} else {
    $varianceReviewRows | Select-Object scheduler, pin_workers, workers, throughput_coefficient_of_variation, throughput_relative_range, variance_interpretation |
        Format-Table -AutoSize | Out-String | Write-Output
    Write-Output "[th1-th2] PASS_WITH_VARIANCE_REVIEW: protocol, correctness, fingerprint, host context, and pinning gates passed; $($varianceReviewRows.Count) groups exceeded repeat-variance thresholds"
}
