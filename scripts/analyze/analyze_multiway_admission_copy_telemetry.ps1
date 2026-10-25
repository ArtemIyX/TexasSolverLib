[CmdletBinding()]
param(
    [string]$InputRoot = 'tmp\tracy',

    [string[]]$CaptureId = @(),

    [string]$OutputPath = '',

    [switch]$RequireCompleteTelemetry
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if ([System.IO.Path]::IsPathRooted($InputRoot)) {
    $resolvedInputRoot = (Resolve-Path -LiteralPath $InputRoot).Path
} else {
    $resolvedInputRoot = (Resolve-Path -LiteralPath (Join-Path $repoRoot $InputRoot)).Path
}

if ($CaptureId.Count -eq 0) {
    $CaptureId = @(
        Get-ChildItem -LiteralPath $resolvedInputRoot -Directory |
            Where-Object { $_.Name -like 'ad7-admission-split-cold-*' } |
            Sort-Object Name |
            Select-Object -ExpandProperty Name
    )
}
if ($CaptureId.Count -eq 0) {
    throw "No admission-split capture directories were found under $resolvedInputRoot"
}

function Get-OptionalUInt64 {
    param(
        [AllowNull()]
        [object]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )
    if ($null -eq $Object -or -not ($Object.PSObject.Properties.Name -contains $Name)) {
        return $null
    }
    return [uint64]$Object.$Name
}

function Convert-NanosecondsToMilliseconds {
    param([AllowNull()][object]$Nanoseconds)
    if ($null -eq $Nanoseconds) {
        return $null
    }
    return [math]::Round([double]$Nanoseconds / 1e6, 3)
}

function Get-OptionalSum {
    param(
        [AllowNull()][object]$Left,
        [AllowNull()][object]$Right
    )
    if ($null -eq $Left -or $null -eq $Right) {
        return $null
    }
    return [uint64]$Left + [uint64]$Right
}

$results = @()
$incomplete = @()
foreach ($id in $CaptureId) {
    $reportPath = Join-Path (Join-Path $resolvedInputRoot $id) 'report.json'
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        $incomplete += $id
        Write-Warning "Training report was not found: $reportPath"
        continue
    }

    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    $diagnostics = $report.admission_diagnostics
    $memoryLedger = $report.memory_ledger

    $coordinatorAllocationCount = Get-OptionalUInt64 $memoryLedger 'coordinator_allocation_count'
    $coordinatorAllocationBytes = Get-OptionalUInt64 $memoryLedger 'coordinator_allocation_bytes'
    $discoveryCopyBytes = Get-OptionalUInt64 $diagnostics 'discovery_public_descriptor_copy_bytes'
    $discoveryNestedCopyBytes = Get-OptionalUInt64 $diagnostics 'discovery_nested_descriptor_copy_bytes'
    $discoveryReuseBytes = Get-OptionalUInt64 $diagnostics 'discovery_output_capacity_reuse_bytes'
    $catalogCopyBytes = Get-OptionalUInt64 $diagnostics 'catalog_public_descriptor_copy_bytes'
    $catalogNestedCopyBytes = Get-OptionalUInt64 $diagnostics 'catalog_nested_descriptor_copy_bytes'
    $copyTelemetryPresent = $null -ne $discoveryCopyBytes -and
        $null -ne $discoveryNestedCopyBytes -and
        $null -ne $discoveryReuseBytes -and
        $null -ne $catalogCopyBytes -and
        $null -ne $catalogNestedCopyBytes
    $allocationTelemetryPresent = $null -ne $coordinatorAllocationCount -and
        $null -ne $coordinatorAllocationBytes
    $telemetryComplete = $copyTelemetryPresent -and $allocationTelemetryPresent
    if (-not $telemetryComplete) {
        $incomplete += $id
    }

    $discoveryMerge = Get-OptionalUInt64 $diagnostics 'discovery_merge_nanoseconds'
    $discoveryPublicMerge = Get-OptionalUInt64 $diagnostics 'discovery_public_state_merge_nanoseconds'
    $discoveryRowMerge = Get-OptionalUInt64 $diagnostics 'discovery_row_merge_nanoseconds'
    $parentTransition = Get-OptionalUInt64 $diagnostics 'parent_transition_validation_nanoseconds'
    $parentFixed = Get-OptionalUInt64 $diagnostics 'parent_transition_fixed_state_nanoseconds'
    $parentAction = Get-OptionalUInt64 $diagnostics 'parent_transition_action_edge_nanoseconds'
    $parentChance = Get-OptionalUInt64 $diagnostics 'parent_transition_board_chance_edge_nanoseconds'
    $parentStreet = Get-OptionalUInt64 $diagnostics 'parent_transition_street_edge_nanoseconds'

    $discoveryResidual = if ($null -ne $discoveryMerge -and $null -ne $discoveryPublicMerge -and $null -ne $discoveryRowMerge) {
        [int64]$discoveryMerge - [int64]$discoveryPublicMerge - [int64]$discoveryRowMerge
    } else { $null }
    $parentResidual = if ($null -ne $parentTransition -and $null -ne $parentFixed -and $null -ne $parentAction -and $null -ne $parentChance -and $null -ne $parentStreet) {
        [int64]$parentTransition - [int64]$parentFixed - [int64]$parentAction - [int64]$parentChance - [int64]$parentStreet
    } else { $null }

    $results += [pscustomobject]@{
        id = $id
        report_path = (Resolve-Path -LiteralPath $reportPath).Path
        allocation_probe_valid = [bool]$report.allocation_probe_valid
        allocation_telemetry_present = $allocationTelemetryPresent
        copy_telemetry_present = $copyTelemetryPresent
        telemetry_complete = $telemetryComplete
        admission_ms = Convert-NanosecondsToMilliseconds (Get-OptionalUInt64 $report 'coordinator_admission_nanoseconds')
        discovery_merge_ms = Convert-NanosecondsToMilliseconds $discoveryMerge
        discovery_public_merge_ms = Convert-NanosecondsToMilliseconds $discoveryPublicMerge
        discovery_row_merge_ms = Convert-NanosecondsToMilliseconds $discoveryRowMerge
        discovery_residual_ms = Convert-NanosecondsToMilliseconds $discoveryResidual
        parent_transition_ms = Convert-NanosecondsToMilliseconds $parentTransition
        parent_transition_residual_ms = Convert-NanosecondsToMilliseconds $parentResidual
        coordinator_allocation_count = $coordinatorAllocationCount
        coordinator_allocation_bytes = $coordinatorAllocationBytes
        discovery_copy_bytes = $discoveryCopyBytes
        discovery_nested_copy_bytes = $discoveryNestedCopyBytes
        discovery_output_capacity_reuse_bytes = $discoveryReuseBytes
        catalog_copy_bytes = $catalogCopyBytes
        catalog_nested_copy_bytes = $catalogNestedCopyBytes
        total_public_copy_bytes = Get-OptionalSum $discoveryCopyBytes $catalogCopyBytes
        total_nested_copy_bytes = Get-OptionalSum $discoveryNestedCopyBytes $catalogNestedCopyBytes
        fingerprint = [string]$report.deterministic_merge_fingerprint
        failed = [bool]$report.failed
        discarded = [uint64]$report.discarded_trajectories
    }
}

if ($results.Count -eq 0) {
    throw "No admission reports were analyzed under $resolvedInputRoot"
}

$summary = [ordered]@{
    input_root = $resolvedInputRoot
    capture_count = $results.Count
    complete_telemetry_count = @($results | Where-Object { $_.telemetry_complete }).Count
    incomplete_capture_ids = @($incomplete | Sort-Object -Unique)
    captures = $results
}

$results | Format-Table id, allocation_probe_valid, telemetry_complete, admission_ms, discovery_public_merge_ms, discovery_row_merge_ms, coordinator_allocation_bytes, discovery_copy_bytes, discovery_nested_copy_bytes, discovery_output_capacity_reuse_bytes -AutoSize | Out-String | Write-Output
$summary | ConvertTo-Json -Depth 8 | Write-Output

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}

if ($RequireCompleteTelemetry -and $summary.complete_telemetry_count -ne $summary.capture_count) {
    throw "Admission copy telemetry is incomplete for: $($summary.incomplete_capture_ids -join ', ')"
}
