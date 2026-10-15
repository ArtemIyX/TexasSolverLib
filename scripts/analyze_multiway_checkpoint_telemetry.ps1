param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [Parameter(Mandatory = $true)]
    [string]$CsvPath,
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumPhaseCoverage = 0.95,
    [string]$ExpectedMergeFingerprint = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
    throw "Training report was not found: $ReportPath"
}
if (-not (Test-Path -LiteralPath $CsvPath -PathType Leaf)) {
    throw "Tracy CSV was not found: $CsvPath"
}

$resolvedReportPath = (Resolve-Path -LiteralPath $ReportPath).Path
$resolvedCsvPath = (Resolve-Path -LiteralPath $CsvPath).Path
$reportText = Get-Content -Raw -LiteralPath $resolvedReportPath
$report = $reportText | ConvertFrom-Json

if ($null -eq $report.evidence -or $report.evidence.schema_version -ne 7) {
    throw 'CP1 requires training report schema 7'
}

$telemetry = $report.checkpoint_telemetry
if ($null -eq $telemetry) {
    throw 'Training report does not contain checkpoint_telemetry'
}

$phaseNames = @(
    'snapshot_lock_wait_nanoseconds',
    'public_catalog_export_nanoseconds',
    'compact_storage_export_nanoseconds',
    'canonicalization_nanoseconds',
    'validation_nanoseconds',
    'payload_hash_nanoseconds',
    'binary_encoding_write_nanoseconds',
    'stream_close_or_flush_nanoseconds',
    'atomic_publication_nanoseconds'
)
$byteNames = @(
    'public_state_bytes',
    'row_shape_bytes',
    'regret_bytes',
    'strategy_sum_bytes',
    'late_window_baseline_bytes'
)

foreach ($name in ($phaseNames + $byteNames)) {
    if (-not ($telemetry.PSObject.Properties.Name -contains $name)) {
        throw "Training report is missing checkpoint telemetry field: $name"
    }
}

$requiredCsvZones = @(
    'mw.checkpoint.snapshot_lock_wait',
    'mw.checkpoint.public_catalog_export',
    'mw.checkpoint.compact_storage_export',
    'mw.checkpoint.canonicalization',
    'mw.checkpoint.training_validation',
    'mw.checkpoint.artifact_validation',
    'mw.checkpoint.payload_hash',
    'mw.checkpoint.binary_encoding_write',
    'mw.checkpoint.stream_close_or_flush',
    'mw.checkpoint.atomic_publication'
)
$csvRows = Import-Csv -LiteralPath $resolvedCsvPath
$csvZoneTotals = [ordered]@{}
$csvZonesPass = $true
foreach ($zoneName in $requiredCsvZones) {
    $zoneRows = @($csvRows | Where-Object { $_.name -eq $zoneName })
    if ($zoneRows.Count -eq 0) {
        $csvZonesPass = $false
        $csvZoneTotals[$zoneName] = $null
        continue
    }
    $zoneTotal = [uint64]0
    foreach ($zoneRow in $zoneRows) {
        $zoneTotal += [uint64]$zoneRow.total_ns
    }
    $csvZoneTotals[$zoneName] = $zoneTotal
}

$phaseNanoseconds = [ordered]@{}
$phaseTotalNanoseconds = [uint64]0
foreach ($name in $phaseNames) {
    $value = [uint64]$telemetry.$name
    $phaseNanoseconds[$name] = $value
    $phaseTotalNanoseconds += $value
}

$byteAccounting = [ordered]@{}
$payloadSectionBytes = [uint64]0
foreach ($name in $byteNames) {
    $value = [uint64]$telemetry.$name
    $byteAccounting[$name] = $value
    $payloadSectionBytes += $value
}

$checkpointWriteNanoseconds = [uint64]$report.checkpoint_write_nanoseconds
$phaseCoverage = if ($checkpointWriteNanoseconds -eq 0) {
    0.0
} else {
    [double]$phaseTotalNanoseconds / [double]$checkpointWriteNanoseconds
}

$checkpointBytes = [int64]$report.checkpoint_bytes
$unattributedCheckpointBytes = $checkpointBytes - [int64]$payloadSectionBytes
$discardedTrajectories = [uint64]$report.discarded_trajectories
$mergeFingerprintMatch = [regex]::Match(
    $reportText,
    '"deterministic_merge_fingerprint"\s*:\s*(\d+)')
$mergeFingerprint = if ($mergeFingerprintMatch.Success) {
    $mergeFingerprintMatch.Groups[1].Value
} else {
    ''
}
$mergeFingerprintChecked = -not [string]::IsNullOrWhiteSpace($ExpectedMergeFingerprint)
$mergeFingerprintMatches = if ($mergeFingerprintChecked) {
    $mergeFingerprint -eq $ExpectedMergeFingerprint
} else {
    $null
}

$summary = [ordered]@{
    report_path = $resolvedReportPath
    csv_path = $resolvedCsvPath
    schema_version = [uint32]$report.evidence.schema_version
    csv_zone_totals = $csvZoneTotals
    csv_zones_pass = $csvZonesPass
    phase_nanoseconds = $phaseNanoseconds
    phase_total_nanoseconds = $phaseTotalNanoseconds
    checkpoint_write_nanoseconds = $checkpointWriteNanoseconds
    phase_coverage = $phaseCoverage
    minimum_phase_coverage = $MinimumPhaseCoverage
    phase_coverage_pass = $phaseCoverage -ge $MinimumPhaseCoverage
    byte_accounting = $byteAccounting
    payload_section_bytes = $payloadSectionBytes
    checkpoint_bytes = $checkpointBytes
    unattributed_checkpoint_bytes = $unattributedCheckpointBytes
    byte_accounting_pass = $checkpointBytes -ge 0 -and [int64]$payloadSectionBytes -le $checkpointBytes
    discarded_trajectories = $discardedTrajectories
    discarded_trajectories_pass = $discardedTrajectories -eq 0
    failed = [bool]$report.failed
    failed_pass = -not [bool]$report.failed
    deterministic_merge_fingerprint = $mergeFingerprint
    expected_merge_fingerprint = if ($mergeFingerprintChecked) { $ExpectedMergeFingerprint } else { $null }
    merge_fingerprint_checked = $mergeFingerprintChecked
    merge_fingerprint_matches = $mergeFingerprintMatches
}

$summary['acceptance_pass'] = [bool]($summary.phase_coverage_pass -and
    $summary.csv_zones_pass -and
    $summary.byte_accounting_pass -and
    $summary.discarded_trajectories_pass -and
    $summary.failed_pass -and
    $summary.merge_fingerprint_checked -and
    $summary.merge_fingerprint_matches)

$summary | ConvertTo-Json -Depth 8

if (-not $summary.acceptance_pass) {
    exit 1
}
