param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumPhaseCoverage = 0.95,
    [string]$ExpectedMergeFingerprint = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
    throw "Training report was not found: $ReportPath"
}

$resolvedReportPath = (Resolve-Path -LiteralPath $ReportPath).Path
$report = Get-Content -Raw -LiteralPath $resolvedReportPath | ConvertFrom-Json

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
$phaseCoverage = if ($checkpointWriteNanoseconds -eq 0U) {
    0.0
} else {
    [double]$phaseTotalNanoseconds / [double]$checkpointWriteNanoseconds
}

$checkpointBytes = [int64]$report.checkpoint_bytes
$unattributedCheckpointBytes = $checkpointBytes - [int64]$payloadSectionBytes
$discardedTrajectories = [uint64]$report.discarded_trajectories
$mergeFingerprint = if ($null -eq $report.deterministic_merge_fingerprint) {
    ''
} else {
    $report.deterministic_merge_fingerprint.ToString()
}
$mergeFingerprintChecked = -not [string]::IsNullOrWhiteSpace($ExpectedMergeFingerprint)
$mergeFingerprintMatches = if ($mergeFingerprintChecked) {
    $mergeFingerprint -eq $ExpectedMergeFingerprint
} else {
    $null
}

$summary = [ordered]@{
    report_path = $resolvedReportPath
    schema_version = [uint32]$report.evidence.schema_version
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
    discarded_trajectories_pass = $discardedTrajectories -eq 0U
    failed = [bool]$report.failed
    failed_pass = -not [bool]$report.failed
    deterministic_merge_fingerprint = $mergeFingerprint
    expected_merge_fingerprint = if ($mergeFingerprintChecked) { $ExpectedMergeFingerprint } else { $null }
    merge_fingerprint_checked = $mergeFingerprintChecked
    merge_fingerprint_matches = $mergeFingerprintMatches
}

$summary['acceptance_pass'] = [bool]($summary.phase_coverage_pass -and
    $summary.byte_accounting_pass -and
    $summary.discarded_trajectories_pass -and
    $summary.failed_pass -and
    $summary.merge_fingerprint_checked -and
    $summary.merge_fingerprint_matches)

$summary | ConvertTo-Json -Depth 8

if (-not $summary.acceptance_pass) {
    exit 1
}
