[CmdletBinding()]
param([string]$InputDirectory = 'tmp\th1-th2-dynamic-variance-probe')

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not [System.IO.Path]::IsPathRooted($InputDirectory)) { $InputDirectory = Join-Path $repoRoot $InputDirectory }

function Read-Json([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required artifact not found: $Path" }
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}
function Normalize-Rows([object]$Value) {
    $rows = @()
    foreach ($item in @($Value)) { if ($item -is [System.Array]) { foreach ($nested in $item) { $rows += $nested } } else { $rows += $item } }
    return $rows
}
function Get-Median([double[]]$Values) {
    $ordered = @($Values | Sort-Object); if ($ordered.Count -eq 0) { return 0.0 }
    $middle = [int]($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) { return [double]$ordered[$middle] }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}
function Get-Correlation([object[]]$Rows, [string]$Metric) {
    $x = @($Rows | ForEach-Object { [double]$_.PSObject.Properties[$Metric].Value }); $y = @($Rows | ForEach-Object { [double]$_.trajectories_per_second })
    if ($x.Count -lt 3) { return $null }
    $xMean = ($x | Measure-Object -Average).Average; $yMean = ($y | Measure-Object -Average).Average
    $numerator = 0.0; $xSum = 0.0; $ySum = 0.0
    for ($i = 0; $i -lt $x.Count; ++$i) { $dx = $x[$i] - $xMean; $dy = $y[$i] - $yMean; $numerator += $dx * $dy; $xSum += $dx * $dx; $ySum += $dy * $dy }
    if ($xSum -le 0.0 -or $ySum -le 0.0) { return $null }
    return $numerator / [math]::Sqrt($xSum * $ySum)
}

$manifest = Read-Json (Join-Path $InputDirectory 'variance_probe_manifest.json')
$samples = Normalize-Rows (Read-Json (Join-Path $InputDirectory 'variance_probe_samples.json'))
$expected = [int]$manifest.protocol.repeats * 4
if ($samples.Count -ne $expected) { throw "Expected $expected probe samples, found $($samples.Count)." }
$fingerprints = @($samples | ForEach-Object { [string]$_.fingerprint } | Sort-Object -Unique)
if ($fingerprints.Count -ne 1 -or $fingerprints[0] -eq '0') { throw "Fingerprint gate failed: $($fingerprints -join ',')" }

$groups = @(
    foreach ($group in ($samples | Group-Object { "$($_.scheduler)|$($_.pinning)" })) {
        $rows = @($group.Group); $values = @($rows | ForEach-Object { [double]$_.trajectories_per_second }); $mean = ($values | Measure-Object -Average).Average
        $stddev = [math]::Sqrt((($values | ForEach-Object { ($_ - $mean) * ($_ - $mean) } | Measure-Object -Sum).Sum) / $values.Count)
        [pscustomobject][ordered]@{
            scheduler = $rows[0].scheduler; pinning = $rows[0].pinning; sample_count = $rows.Count
            minimum_trajectories_per_second = ($values | Measure-Object -Minimum).Minimum; median_trajectories_per_second = Get-Median $values; maximum_trajectories_per_second = ($values | Measure-Object -Maximum).Maximum
            throughput_coefficient_of_variation = if ($mean -gt 0) { $stddev / $mean } else { $null }
            throughput_relative_range = if ($mean -gt 0) { (($values | Measure-Object -Maximum).Maximum - ($values | Measure-Object -Minimum).Minimum) / $mean } else { $null }
            maximum_host_clock_delta_mhz = ($rows | Measure-Object -Property host_clock_delta_mhz -Maximum).Maximum
            maximum_host_load_delta_percent = ($rows | Measure-Object -Property host_load_delta_percent -Maximum).Maximum
            lowest_repeat = ($rows | Sort-Object trajectories_per_second | Select-Object -First 1).probe_repeat
            highest_repeat = ($rows | Sort-Object trajectories_per_second -Descending | Select-Object -First 1).probe_repeat
        }
    }
)
$dynamicRows = @($samples | Where-Object { $_.scheduler -eq 'dynamic' })
$correlations = [ordered]@{}
foreach ($metric in @('barrier_wait_nanoseconds', 'worker_cv_wait_nanoseconds', 'worker_scheduler_claim_nanoseconds', 'worker_tail_idle_nanoseconds', 'process_cpu_nanoseconds')) { $correlations[$metric] = Get-Correlation $dynamicRows $metric }
$summary = [ordered]@{
    schema_version = 1; input_directory = $InputDirectory; sample_count = $samples.Count; fingerprint = $fingerprints[0]; groups = $groups
    dynamic_correlations = $correlations
    host_context_complete = @($samples | Where-Object { $null -eq $_.host_before.current_clock_mhz -or $null -eq $_.host_after.current_clock_mhz -or $null -eq $_.host_before.load_percent -or $null -eq $_.host_after.load_percent }).Count -eq 0
    decision = 'diagnostic_only_no_solver_change'
}
$summaryPath = Join-Path $InputDirectory 'variance_probe_summary.json'
$groups | Export-Csv -LiteralPath (Join-Path $InputDirectory 'variance_probe_summary.csv') -NoTypeInformation
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$groups | Format-Table -AutoSize | Out-String | Write-Output
Write-Output "[th1-th2-variance-probe] summary=$summaryPath"
Write-Output '[th1-th2-variance-probe] decision=diagnostic_only_no_solver_change'
