param(
    [string]$ReportPath,
    [string]$ProgressLogPath,
    [string]$ProvenanceLogPath
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ReportPath) -and [string]::IsNullOrWhiteSpace($ProgressLogPath)) {
    throw 'Supply a completed report, a progress log, or both.'
}

function Read-BoundedText([string]$Path, [long]$Limit) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer -or $item.Length -gt $Limit) { throw "Input is not a bounded file: $Path" }
    return [System.IO.File]::ReadAllText($item.FullName)
}

# Keep all integers as decimal text. In particular the policy hash exceeds Int64.
function Get-IntegerText([string]$Text, [string]$Name) {
    if ($null -eq $Text) { return $null }
    $pattern = '(?m)^\s*"' + [regex]::Escape($Name) + '"\s*:\s*(\d+)\s*[,}]?\s*$'
    $matches = [regex]::Matches($Text, $pattern)
    if ($matches.Count -gt 1) { throw "Duplicate report field: $Name" }
    if ($matches.Count -eq 0) { return $null }
    return $matches[0].Groups[1].Value
}

$reportText = Read-BoundedText $ReportPath (8MB)
$progressText = Read-BoundedText $ProgressLogPath (16MB)
$provenanceText = Read-BoundedText $ProvenanceLogPath (1MB)
$result = [ordered]@{
    report_path = $ReportPath
    progress_log_path = $ProgressLogPath
    provenance_log_path = $ProvenanceLogPath
    report_export_succeeded = $false
    report_value_comparison = $null
    report_metrics = [ordered]@{}
    last_progress = $null
    binary_provenance_line = $null
    checkout_provenance_line = $null
    notes = @('Run probes are not physical reads.', 'Stream-write bytes are not durable bytes.',
        'Sampled timings are not complete totals.', 'Report comparisons do not validate process exit or artifact integrity.',
        'This script reads files only and does not launch an export.')
}

if ($null -ne $reportText) {
    # Reject malformed JSON independently of the exact-integer extraction below.
    $null = $reportText | ConvertFrom-Json
    $result.report_export_succeeded = [regex]::IsMatch($reportText, '(?m)^\s*"export_succeeded"\s*:\s*true\s*,?\s*$')
    $fields = @(
        'accepted_trajectories', 'discarded_trajectories', 'export_row_count', 'export_action_count',
        'export_result_bytes', 'export_policy_content_hash', 'export_wall_nanoseconds',
        'external_checkpoint_restore_nanoseconds', 'external_checkpoint_session_resume_nanoseconds',
        'total_wrapper_wall_nanoseconds', 'process_cpu_nanoseconds', 'peak_process_working_set_bytes',
        'external_export_profile_pages', 'external_export_profile_encoded_bytes',
        'external_export_profile_stream_write_bytes', 'external_export_ordered_visit_inclusive_nanoseconds',
        'external_export_visitor_inclusive_nanoseconds', 'external_export_pin_inclusive_nanoseconds',
        'external_export_unpin_inclusive_nanoseconds', 'external_export_page_cache_find_nanoseconds',
        'external_export_page_store_lookup_inclusive_nanoseconds', 'external_export_page_decode_validation_nanoseconds',
        'external_export_page_index_opens', 'external_export_page_index_reads',
        'external_export_page_data_opens', 'external_export_page_data_reads', 'external_export_page_requested_read_bytes',
        'external_export_page_index_open_nanoseconds', 'external_export_page_index_search_nanoseconds',
        'external_export_page_data_open_nanoseconds', 'external_export_page_data_read_nanoseconds',
        'external_export_page_zero_nanoseconds', 'external_export_writer_flushes', 'external_export_sampled_rows',
        'external_export_sampled_average_nanoseconds', 'external_export_sampled_quantize_nanoseconds',
        'external_export_writer_sampled_rows', 'external_export_writer_sampled_validation_nanoseconds',
        'external_export_writer_sampled_payload_hash_nanoseconds', 'external_export_writer_sampled_policy_hash_nanoseconds',
        'external_export_writer_sampled_encoding_inclusive_nanoseconds', 'external_export_page_store_run_probes'
    )
    foreach ($field in $fields) { $result.report_metrics[$field] = Get-IntegerText $reportText $field }
    if ($result.report_export_succeeded) {
        $expected = [ordered]@{
            export_policy_content_hash = '11500758613148677101'
            accepted_trajectories = '1000'
            discarded_trajectories = '0'
            export_row_count = '2434250'
            export_action_count = '6403142'
            export_result_bytes = '189818882'
        }
        $result.report_value_comparison = [ordered]@{}
        foreach ($field in $expected.Keys) {
            $actual = $result.report_metrics[$field]
            $result.report_value_comparison[$field] = [ordered]@{ expected = $expected[$field]; actual = $actual; match = ($actual -ceq $expected[$field]) }
        }
    }
}

if ($null -ne $progressText) {
    $lines = [regex]::Matches($progressText, '(?m)^external_export_progress\s+[^\r\n]*')
    if ($lines.Count -gt 0) {
        $last = [ordered]@{}
        foreach ($pair in [regex]::Matches($lines[$lines.Count - 1].Value, '([a-z_]+)=(\d+)')) {
            if ($last.Contains($pair.Groups[1].Value)) { throw 'Duplicate progress field.' }
            $last[$pair.Groups[1].Value] = $pair.Groups[2].Value
        }
        if ($last.Contains('elapsed_ns') -and $last.Contains('encoded_bytes') -and [decimal]$last.elapsed_ns -gt 0) {
            $last['encoded_bytes_per_second'] = ([decimal]$last.encoded_bytes * 1000000000 / [decimal]$last.elapsed_ns).ToString('F2', [System.Globalization.CultureInfo]::InvariantCulture)
        }
        $result.last_progress = $last
    }
}

if ($null -ne $provenanceText) {
    $binary = [regex]::Match($provenanceText, '(?m)^binary provenance: [^\r\n]*')
    $checkout = [regex]::Match($provenanceText, '(?m)^run checkout identity: [^\r\n]*')
    if ($binary.Success) { $result.binary_provenance_line = $binary.Value }
    if ($checkout.Success) { $result.checkout_provenance_line = $checkout.Value }
}

$result | ConvertTo-Json -Depth 8
