[CmdletBinding()]
param(
    [string]$InputRoot = 'tmp\ad7f8-cold-qualification',
    [ValidatePattern('^[0-9]+$')]
    [string]$ExpectedMergeFingerprint = '9806774443936985566',
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumPhaseCoverage = 0.95,
    [int64]$ExpectedCheckpointBytes = 137380835
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$baseAnalyzer = Join-Path $PSScriptRoot 'analyze_multiway_checkpoint_telemetry.ps1'
if ([System.IO.Path]::IsPathRooted($InputRoot)) {
    $resolvedInputRoot = $InputRoot
} else {
    $resolvedInputRoot = Join-Path $repoRoot $InputRoot
}

$captureIds = @(
    'ad7-admission-split-cold-fixed-1w-r1',
    'ad7-admission-split-cold-fixed-1w-r2',
    'ad7-admission-split-cold-dynamic-1w-r1',
    'ad7-admission-split-cold-dynamic-1w-r2'
)
$deterministicFields = @(
    'globally_unique_public_state_candidates',
    'globally_unique_row_candidates',
    'parent_resolution_probes',
    'parent_catalog_resolutions',
    'row_batch_state_resolutions',
    'row_catalog_state_resolutions',
    'catalog_batch_menu_reuse_hits',
    'catalog_new_menus',
    'discovery_single_stream_batches',
    'discovery_one_stream_fast_path_uses',
    'discovery_public_state_source_elements',
    'discovery_row_source_elements',
    'discovery_public_state_duplicate_candidates',
    'discovery_row_duplicate_candidates'
)

if (-not (Test-Path -LiteralPath $baseAnalyzer -PathType Leaf)) {
    throw "Base telemetry analyzer was not found: $baseAnalyzer"
}

$results = @()
$failedIds = @()
$baselineSignature = $null
for ($index = 0; $index -lt $captureIds.Count; $index++) {
    $captureId = $captureIds[$index]
    $captureDirectory = Join-Path $resolvedInputRoot $captureId
    $reportPath = Join-Path $captureDirectory 'report.json'
    $csvPath = Join-Path $captureDirectory "$captureId.csv"
    $contextPath = Join-Path $captureDirectory 'operator_context.json'
    $acceptancePath = Join-Path $captureDirectory 'ad7f8_acceptance.json'
    Write-Output "[ad7f8] analysis $($index + 1)/$($captureIds.Count) is in progress: $captureId"

    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $csvPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $contextPath -PathType Leaf)) {
        $failedIds += $captureId
        Write-Warning "Missing AD7F.8 artifact for $captureId"
        continue
    }

    $analysisArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $baseAnalyzer,
        '-ReportPath', $reportPath,
        '-CsvPath', $csvPath,
        '-MinimumPhaseCoverage', $MinimumPhaseCoverage.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-ExpectedMergeFingerprint', $ExpectedMergeFingerprint,
        '-AllowMissingArtifactValidation'
    )
    $analysisText = (& powershell.exe @analysisArguments 2>&1 | Out-String).Trim()
    $analysisExitCode = $LASTEXITCODE
    $analysisText | Set-Content -LiteralPath $acceptancePath -Encoding utf8
    if ($analysisExitCode -ne 0) {
        $failedIds += $captureId
        Write-Warning "Telemetry acceptance failed for $captureId"
        continue
    }

    $summary = $analysisText | ConvertFrom-Json
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    $context = Get-Content -Raw -LiteralPath $contextPath | ConvertFrom-Json
    $diagnostics = $report.admission_diagnostics
    $countValues = [ordered]@{}
    foreach ($field in $deterministicFields) {
        $countValues[$field] = if ($diagnostics.PSObject.Properties.Name -contains $field) { [int64]$diagnostics.$field } else { $null }
    }
    $signature = $countValues | ConvertTo-Json -Compress -Depth 4
    $countsMatch = if ($null -eq $baselineSignature) { $true } else { $signature -eq $baselineSignature }
    if ($null -eq $baselineSignature) { $baselineSignature = $signature }

    $tracyPass = [string]$context.tracy.version -eq '0.14.1' -and
        [int64]$context.tracy.trace_bytes -gt 0 -and
        [int64]$context.tracy.csv_bytes -gt 0 -and
        [string]$context.tracy.instrumentation_kind -match 'tracy'
    $placementRecorded = $null -ne $context.processor_placement -and
        $context.processor_placement.PSObject.Properties.Name -contains 'resolved_processor'
    $capturePass = [bool]$summary.acceptance_pass -and
        [int64]$report.checkpoint_bytes -eq $ExpectedCheckpointBytes -and
        [bool]$countsMatch -and
        [bool]$tracyPass -and
        [bool]$placementRecorded
    if (-not $capturePass) { $failedIds += $captureId }

    $results += [pscustomobject]@{
        id = $captureId
        scheduler = [string]$context.scheduler
        acceptance_pass = [bool]$summary.acceptance_pass
        capture_pass = $capturePass
        phase_coverage = [math]::Round([double]$summary.phase_coverage, 6)
        byte_accounting_pass = [bool]$summary.byte_accounting_pass
        unattributed_checkpoint_bytes = [int64]$summary.unattributed_checkpoint_bytes
        checkpoint_bytes = [int64]$report.checkpoint_bytes
        fingerprint = [string]$summary.deterministic_merge_fingerprint
        failed = [bool]$report.failed
        discarded = [int64]$report.discarded_trajectories
        tracy_pass = $tracyPass
        trace_bytes = [int64]$context.tracy.trace_bytes
        processor_load_before_percent = $context.host_before.total_cpu_percent
        processor_load_after_percent = $context.host_after.total_cpu_percent
        current_clock_before_mhz = $context.host_before.current_clock_mhz
        current_clock_after_mhz = $context.host_after.current_clock_mhz
        resolved_processor = (@($context.processor_placement.resolved_processor) -join ',')
        affinity_succeeded = (@($context.processor_placement.affinity_succeeded) -join ',')
        deterministic_counts_match = $countsMatch
        globally_unique_public_states = $countValues.globally_unique_public_state_candidates
        globally_unique_rows = $countValues.globally_unique_row_candidates
        admission_ms = [math]::Round([double]$report.coordinator_admission_nanoseconds / 1e6, 3)
    }
    Write-Output "[ad7f8] analysis $($index + 1)/$($captureIds.Count) completed: $captureId pass=$capturePass"
}

$summaryPath = Join-Path $resolvedInputRoot 'ad7f8_cold_qualification_summary.json'
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Output ''
$results | Format-Table id, scheduler, acceptance_pass, capture_pass, phase_coverage, byte_accounting_pass, checkpoint_bytes, fingerprint, resolved_processor, admission_ms -AutoSize | Out-String | Write-Output
Write-Output "[ad7f8] summary=$summaryPath"
if ($failedIds.Count -gt 0) {
    throw "AD7F.8 cold qualification failed for: $($failedIds -join ', ')"
}
Write-Output '[ad7f8] all four cold captures passed'
