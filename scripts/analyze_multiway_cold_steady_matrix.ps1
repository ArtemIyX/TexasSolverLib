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
    $checkpointTelemetry = $report.checkpoint_telemetry
    $memoryLedger = $report.memory_ledger
    $allocationTelemetryPresent = $null -ne $memoryLedger -and
        ($memoryLedger.PSObject.Properties.Name -contains 'coordinator_allocation_count') -and
        ($memoryLedger.PSObject.Properties.Name -contains 'coordinator_allocation_bytes')
    $copyTelemetryPresent = $null -ne $identity -and
        ($identity.PSObject.Properties.Name -contains 'discovery_public_descriptor_copy_bytes') -and
        ($identity.PSObject.Properties.Name -contains 'catalog_public_descriptor_copy_bytes')
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
        allocation_telemetry_present = $allocationTelemetryPresent
        copy_telemetry_present = $copyTelemetryPresent
        coordinator_allocation_count = if ($allocationTelemetryPresent) { [Int64]$memoryLedger.coordinator_allocation_count } else { $null }
        coordinator_allocation_bytes = if ($allocationTelemetryPresent) { [Int64]$memoryLedger.coordinator_allocation_bytes } else { $null }
        discovery_descriptor_copy_bytes = if ($copyTelemetryPresent) { [Int64]$identity.discovery_public_descriptor_copy_bytes } else { $null }
        catalog_descriptor_copy_bytes = if ($copyTelemetryPresent) { [Int64]$identity.catalog_public_descriptor_copy_bytes } else { $null }
        discovery_nested_copy_bytes = if ($copyTelemetryPresent) { [Int64]$identity.discovery_nested_descriptor_copy_bytes } else { $null }
        catalog_nested_copy_bytes = if ($copyTelemetryPresent) { [Int64]$identity.catalog_nested_descriptor_copy_bytes } else { $null }
        trajectories_per_second = [double]$report.trajectories_per_second
        scheduler_claim_ms = [math]::Round([double]$report.worker_scheduler_claim_nanoseconds / 1e6, 3)
        scheduler_claim_nanoseconds_by_worker = ($report.worker_scheduler_claim_nanoseconds_by_worker -join ',')
        admission_ms = [math]::Round([double]$report.coordinator_admission_nanoseconds / 1e6, 3)
        identity_ms = [math]::Round([double]$identity.public_state_history_identity_validation_nanoseconds / 1e6, 3)
        validation_ms = [math]::Round([double]$identity.public_state_validation_nanoseconds / 1e6, 3)
        catalog_commit_ms = [math]::Round([double]$identity.catalog_commit_nanoseconds / 1e6, 3)
        peak_process_rss_bytes = [Int64]$report.peak_process_rss_bytes
        compact_storage_bytes = [Int64]$report.peak_compact_storage_bytes
        peak_public_states = [Int64]$report.peak_public_states
        peak_sparse_rows = [Int64]$report.peak_sparse_rows
        peak_sparse_values = [Int64]$report.peak_sparse_values
        generated_update_count = [Int64]$report.generated_update_count
        generated_discovery_public_state_count = [Int64]$report.generated_discovery_public_state_count
        generated_discovery_row_count = [Int64]$report.generated_discovery_row_count
        total_peak_delta_entries = [Int64]$report.total_peak_delta_entries
        public_state_bytes = [Int64]$checkpointTelemetry.public_state_bytes
        row_shape_bytes = [Int64]$checkpointTelemetry.row_shape_bytes
        regret_bytes = [Int64]$checkpointTelemetry.regret_bytes
        strategy_sum_bytes = [Int64]$checkpointTelemetry.strategy_sum_bytes
        late_window_baseline_bytes = [Int64]$checkpointTelemetry.late_window_baseline_bytes
        checkpoint_bytes = [Int64]$report.checkpoint_bytes
        fingerprint = [string]$report.deterministic_merge_fingerprint
        discarded = [Int64]$report.discarded_trajectories
        failed = [bool]$report.failed
        report_path = $reportPath
    }
}

$equivalenceFields = @(
    'trajectories',
    'accepted_trajectories',
    'discarded',
    'failed',
    'fingerprint',
    'checkpoint_bytes',
    'peak_public_states',
    'peak_sparse_rows',
    'peak_sparse_values',
    'generated_update_count',
    'generated_discovery_public_state_count',
    'generated_discovery_row_count',
    'total_peak_delta_entries',
    'compact_storage_bytes',
    'public_state_bytes',
    'row_shape_bytes',
    'regret_bytes',
    'strategy_sum_bytes',
    'late_window_baseline_bytes'
)
foreach ($group in ($results | Group-Object { "$($_.phase)|$($_.workers)" })) {
    $fixed = @($group.Group | Where-Object { $_.scheduler -eq 'fixed' })
    $dynamic = @($group.Group | Where-Object { $_.scheduler -eq 'dynamic' })
    $equivalent = $fixed.Count -eq 1 -and $dynamic.Count -eq 1
    if ($equivalent) {
        foreach ($field in $equivalenceFields) {
            if (($fixed[0].$field | ConvertTo-Json -Compress -Depth 8) -ne
                ($dynamic[0].$field | ConvertTo-Json -Compress -Depth 8)) {
                $equivalent = $false
                break
            }
        }
    }
    foreach ($row in $group.Group) {
        $row | Add-Member -NotePropertyName scheduler_equivalent -NotePropertyValue $equivalent -Force
        if (-not $equivalent) {
            $row.matrix_gate_pass = $false
        }
    }
    if (-not $equivalent) {
        $failures += "scheduler-equivalence:$($group.Name)"
    }
}

$summaryOutputPath = Join-Path $resolvedInputRoot 'cold_steady_matrix_summary.json'
$results | Sort-Object phase, scheduler, workers | Format-Table id, acceptance_pass, matrix_gate_pass, scheduler_equivalent, allocation_probe_valid, trajectories_per_second, scheduler_claim_ms, admission_ms, identity_ms, validation_ms, catalog_commit_ms, peak_process_rss_bytes, compact_storage_bytes, checkpoint_bytes, fingerprint, failed, discarded -AutoSize | Out-String | Write-Output
$results | Sort-Object phase, scheduler, workers | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryOutputPath -Encoding utf8
Write-Output "[matrix] summary=$summaryOutputPath"

if ($failures.Count -gt 0) {
    throw "Cold/steady matrix analysis failed for: $($failures -join ', ')"
}
if ($results.Count -ne $captureIds.Count) {
    throw "Expected $($captureIds.Count) matrix reports, found $($results.Count)"
}
Write-Output '[matrix] all eight existing reports passed'
