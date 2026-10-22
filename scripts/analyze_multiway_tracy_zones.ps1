[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CsvPath,

    [string]$NamePrefix = 'mw.',

    [ValidateRange(1, 1000)]
    [int]$Top = 100,

    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $CsvPath -PathType Leaf)) {
    throw "Tracy CSV was not found: $CsvPath"
}

$resolvedCsvPath = (Resolve-Path -LiteralPath $CsvPath).Path
$rows = @(Import-Csv -LiteralPath $resolvedCsvPath)
if ($rows.Count -eq 0) {
    throw "Tracy CSV contains no rows: $resolvedCsvPath"
}

$firstRow = $rows[0]
if (-not ($firstRow.PSObject.Properties.Name -contains 'name') -or
    -not ($firstRow.PSObject.Properties.Name -contains 'total_ns')) {
    throw "Tracy CSV must contain name and total_ns columns: $resolvedCsvPath"
}

$zoneRows = @($rows | Where-Object { $_.name -like "$NamePrefix*" })
if ($zoneRows.Count -eq 0) {
    throw "No Tracy zones matched prefix '$NamePrefix' in $resolvedCsvPath"
}

$zoneTotals = @(
    $zoneRows |
        Group-Object -Property name |
        ForEach-Object {
            [uint64]$totalNanoseconds = 0
            [uint64]$totalCount = 0
            $sourceFile = ''
            $sourceLine = 0
            foreach ($row in $_.Group) {
                $totalNanoseconds += [uint64]$row.total_ns
                if ($row.PSObject.Properties.Name -contains 'counts') {
                    $totalCount += [uint64]$row.counts
                }
                if ([string]::IsNullOrWhiteSpace($sourceFile) -and $row.PSObject.Properties.Name -contains 'src_file') {
                    $sourceFile = [string]$row.src_file
                }
                if ($sourceLine -eq 0 -and $row.PSObject.Properties.Name -contains 'src_line') {
                    $sourceLine = [int]$row.src_line
                }
            }
            [pscustomobject]@{
                name = [string]$_.Name
                total_ns = $totalNanoseconds
                total_ms = [math]::Round([double]$totalNanoseconds / 1e6, 3)
                counts = $totalCount
                source_file = $sourceFile
                source_line = $sourceLine
            }
        } |
        Sort-Object total_ns -Descending |
        Select-Object -First $Top
)

$report = [ordered]@{
    csv_path = $resolvedCsvPath
    name_prefix = $NamePrefix
    input_row_count = $rows.Count
    matched_row_count = $zoneRows.Count
    reported_zone_count = $zoneTotals.Count
    zones = $zoneTotals
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}

$zoneTotals | Format-Table name, total_ms, counts, source_line -AutoSize | Out-String | Write-Output
$report | ConvertTo-Json -Depth 8
