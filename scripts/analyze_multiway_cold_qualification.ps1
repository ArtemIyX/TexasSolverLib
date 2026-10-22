[CmdletBinding()]
param(
    [string]$InputRoot = 'tmp\tracy',

    [ValidatePattern('^[0-9]+$')]
    [string]$ExpectedMergeFingerprint = '9806774443936985566'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$analyzer = Join-Path $PSScriptRoot 'analyze_multiway_checkpoint_telemetry.ps1'

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

if (-not (Test-Path -LiteralPath $analyzer -PathType Leaf)) {
    throw "Telemetry analyzer was not found: $analyzer"
}

$results = @()
$failedIds = @()

foreach ($captureId in $captureIds) {
    $captureDirectory = Join-Path $resolvedInputRoot $captureId
    $reportPath = Join-Path $captureDirectory 'report.json'
    $csvPath = Join-Path $captureDirectory "$captureId.csv"
    $summaryPath = Join-Path $captureDirectory 'telemetry_acceptance.json'

    Write-Output "[analyze] capture_id=$captureId"

    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        Write-Warning "Training report was not found: $reportPath"
        $failedIds += $captureId
        continue
    }
    if (-not (Test-Path -LiteralPath $csvPath -PathType Leaf)) {
        Write-Warning "Tracy CSV was not found: $csvPath"
        $failedIds += $captureId
        continue
    }

    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $analyzer `
        -ReportPath $reportPath `
        -CsvPath $csvPath `
        -ExpectedMergeFingerprint $ExpectedMergeFingerprint `
        -AllowMissingArtifactValidation 2>&1

    $exitCode = $LASTEXITCODE
    $output | Out-File -LiteralPath $summaryPath -Encoding utf8
    $output | Write-Output

    if ($exitCode -ne 0) {
        $failedIds += $captureId
        continue
    }

    try {
        $summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json
        $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
        $diagnostics = $report.admission_diagnostics
        $memoryLedger = $report.memory_ledger
        $allocationTelemetryPresent = $null -ne $memoryLedger -and
            ($memoryLedger.PSObject.Properties.Name -contains 'coordinator_allocation_count') -and
            ($memoryLedger.PSObject.Properties.Name -contains 'coordinator_allocation_bytes')
        $copyTelemetryPresent = $null -ne $diagnostics -and
            ($diagnostics.PSObject.Properties.Name -contains 'discovery_public_descriptor_copy_bytes') -and
            ($diagnostics.PSObject.Properties.Name -contains 'catalog_public_descriptor_copy_bytes')

        $results += [pscustomobject]@{
            id = $captureId
            acceptance_pass = [bool]$summary.acceptance_pass
            phase_coverage = [double]$summary.phase_coverage
            admission_ms = [math]::Round([double]$report.coordinator_admission_nanoseconds / 1e6, 3)
            identity_ms = [math]::Round([double]$diagnostics.public_state_history_identity_validation_nanoseconds / 1e6, 3)
            validation_ms = [math]::Round([double]$diagnostics.public_state_validation_nanoseconds / 1e6, 3)
            catalog_commit_ms = [math]::Round([double]$diagnostics.catalog_commit_nanoseconds / 1e6, 3)
            allocation_probe_valid = [bool]$report.allocation_probe_valid
            allocation_telemetry_present = $allocationTelemetryPresent
            copy_telemetry_present = $copyTelemetryPresent
            coordinator_allocation_count = if ($allocationTelemetryPresent) { [int64]$memoryLedger.coordinator_allocation_count } else { $null }
            coordinator_allocation_bytes = if ($allocationTelemetryPresent) { [int64]$memoryLedger.coordinator_allocation_bytes } else { $null }
            discovery_descriptor_copy_bytes = if ($copyTelemetryPresent) { [int64]$diagnostics.discovery_public_descriptor_copy_bytes } else { $null }
            catalog_descriptor_copy_bytes = if ($copyTelemetryPresent) { [int64]$diagnostics.catalog_public_descriptor_copy_bytes } else { $null }
            discovery_nested_copy_bytes = if ($copyTelemetryPresent) { [int64]$diagnostics.discovery_nested_descriptor_copy_bytes } else { $null }
            catalog_nested_copy_bytes = if ($copyTelemetryPresent) { [int64]$diagnostics.catalog_nested_descriptor_copy_bytes } else { $null }
            checkpoint_bytes = [int64]$report.checkpoint_bytes
            fingerprint = [string]$report.deterministic_merge_fingerprint
            failed = [bool]$report.failed
            discarded = [int64]$report.discarded_trajectories
        }

        if (-not $summary.acceptance_pass) {
            $failedIds += $captureId
        }
    } catch {
        Write-Warning "Could not read analysis output for $captureId`: $($_.Exception.Message)"
        $failedIds += $captureId
    }
}

if ($results.Count -gt 0) {
    Write-Output ''
    Write-Output '[analyze] summary'
    $results | Format-Table -AutoSize | Out-String | Write-Output
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $resolvedInputRoot 'cold_qualification_summary.json') -Encoding utf8
}

if ($failedIds.Count -gt 0) {
    throw "Cold qualification failed for: $($failedIds -join ', ')"
}

Write-Output '[analyze] all captures passed'
