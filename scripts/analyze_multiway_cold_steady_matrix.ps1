[CmdletBinding()]
param(
    [string]$InputRoot = 'tmp\tracy',
    [string]$ExpectedColdFingerprint = '9806774443936985566',
    [string]$ExpectedSteadyFingerprint = '3496596242701454594',
    [Int64]$ExpectedColdCheckpointBytes = 137380835,
    [Int64]$ExpectedSteadyCheckpointBytes = 387676624,
    [switch]$AllowMissingArtifactValidation
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$analyzer = Join-Path $PSScriptRoot 'analyze_multiway_checkpoint_telemetry.ps1'
if (-not (Test-Path -LiteralPath $analyzer -PathType Leaf)) {
    throw "Telemetry analyzer was not found: $analyzer"
}

if ([System.IO.Path]::IsPathRooted($InputRoot)) {
    $resolvedInputRoot = $InputRoot
} else {
    $resolvedInputRoot = Join-Path $repoRoot $InputRoot
}

$captureIds = @(
    'ad7-matrix-cold-fixed-1w',
    'ad7-matrix-cold-dynamic-1w',
    'ad7-matrix-cold-fixed-4w',
    'ad7-matrix-cold-dynamic-4w',
    'ad7-matrix-steady-fixed-1w',
    'ad7-matrix-steady-dynamic-1w',
    'ad7-matrix-steady-fixed-4w',
    'ad7-matrix-steady-dynamic-4w'
)

$results = @()
$failures = @()
foreach ($captureId in $captureIds) {
    $captureDirectory = Join-Path $resolvedInputRoot $captureId
    $reportPath = Join-Path $captureDirectory 'report.json'
    $csvPath = Join-Path $captureDirectory ($captureId + '.csv')
    $summaryPath = Join-Path $captureDirectory 'telemetry_acceptance.json'

    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $csvPath -PathType Leaf)) {
        $failures += $captureId
        Write-Warning "Missing report or Tracy CSV: $captureId"
        continue
    }

    $isCold = $captureId -match '-cold-'
    $expectedFingerprint = if ($isCold) { $ExpectedColdFingerprint } else { $ExpectedSteadyFingerprint }
    $expectedCheckpointBytes = if ($isCold) { $ExpectedColdCheckpointBytes } else { $ExpectedSteadyCheckpointBytes }

    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $analyzer,
        '-ReportPath', $reportPath,
        '-CsvPath', $csvPath,
        '-ExpectedMergeFingerprint', $expectedFingerprint
    )
    if ($AllowMissingArtifactValidation) {
        $arguments += '-AllowMissingArtifactValidation'
    }
    $analysisOutput = & powershell.exe @arguments 2>&1
    $analysisExitCode = $LASTEXITCODE
    $analysisOutput | Set-Content -LiteralPath $summaryPath -Encoding utf8
    if ($analysisExitCode -ne 0) {
        $failures += $captureId
        Write-Warning "Telemetry acceptance failed: $captureId"
        continue
    }

    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    $summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json
    $identity = $report.admission_diagnostics
    $correct = [bool]$summary.acceptance_pass -and
        [string]$report.deterministic_merge_fingerprint -eq $expectedFingerprint -and
        [Int64]$report.checkpoint_bytes -eq $expectedCheckpointBytes -and
        -not [bool]$report.failed -and
        [Int64]$report.discarded_trajectories -eq 0 -and
        [Int64]$report.trajectories -eq [Int64]$report.accepted_trajectories
    if (-not $correct) {
        $failures += $captureId
    }

    $results += [pscustomobject]@{
        id = $captureId
        phase = if ($isCold) { 'cold' } else { 'steady' }
        scheduler = if ($captureId -match '-fixed-') { 'fixed' } else { 'dynamic' }
        workers = if ($captureId -match '-1w') { 1 } else { 4 }
        acceptance_pass = [bool]$summary.acceptance_pass
        matrix_gate_pass = $correct
        allocation_probe_valid = [bool]$report.allocation_probe_valid
        trajectories_per_second = [double]$report.trajectories_per_second
        admission_ms = [math]::Round([double]$report.coordinator_admission_nanoseconds / 1e6, 3)
        identity_ms = [math]::Round([double]$identity.public_state_history_identity_validation_nanoseconds / 1e6, 3)
        validation_ms = [math]::Round([double]$identity.public_state_validation_nanoseconds / 1e6, 3)
        catalog_commit_ms = [math]::Round([double]$identity.catalog_commit_nanoseconds / 1e6, 3)
        peak_process_rss_bytes = [Int64]$report.peak_process_rss_bytes
        compact_storage_bytes = [Int64]$report.peak_compact_storage_bytes
        checkpoint_bytes = [Int64]$report.checkpoint_bytes
        fingerprint = [string]$report.deterministic_merge_fingerprint
        discarded = [Int64]$report.discarded_trajectories
        failed = [bool]$report.failed
        report_path = $reportPath
    }
}

$summaryOutputPath = Join-Path $resolvedInputRoot 'cold_steady_matrix_summary.json'
$results | Sort-Object phase, scheduler, workers | Format-Table id, acceptance_pass, matrix_gate_pass, allocation_probe_valid, trajectories_per_second, admission_ms, identity_ms, validation_ms, catalog_commit_ms, peak_process_rss_bytes, compact_storage_bytes, checkpoint_bytes, fingerprint, failed, discarded -AutoSize | Out-String | Write-Output
$results | Sort-Object phase, scheduler, workers | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryOutputPath -Encoding utf8
Write-Output "[matrix] summary=$summaryOutputPath"

if ($failures.Count -gt 0) {
    throw "Cold/steady matrix analysis failed for: $($failures -join ', ')"
}
if ($results.Count -ne $captureIds.Count) {
    throw "Expected $($captureIds.Count) matrix reports, found $($results.Count)"
}
Write-Output '[matrix] all eight existing reports passed'
