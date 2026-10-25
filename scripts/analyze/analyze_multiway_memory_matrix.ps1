[CmdletBinding()]
param(
    [string]$InputDirectory = '',

    [string]$OutputDirectory = '',

    [ValidateRange(1, 20)]
    [int]$Repeats = 3,

    [ValidateRange(1, 1000)]
    [int]$TrajectoriesPerBatch = 1000,

    [string]$BaselineDirectory = '',

    [ValidateRange(1, 20)]
    [int]$BaselineRepeats = 3,

    [switch]$SkipTracy,

    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$requiredPhases = @(
    [ordered]@{ id = 0; name = 'process_start' },
    [ordered]@{ id = 1; name = 'before_bucket_map' },
    [ordered]@{ id = 2; name = 'after_bucket_map_metadata' },
    [ordered]@{ id = 3; name = 'after_bucket_assignment_validation' },
    [ordered]@{ id = 4; name = 'before_memory_preflight' },
    [ordered]@{ id = 5; name = 'after_coordinator_construction' },
    [ordered]@{ id = 6; name = 'after_worker_pool_construction' },
    [ordered]@{ id = 7; name = 'after_cold_batch' },
    [ordered]@{ id = 8; name = 'after_steady_batch' },
    [ordered]@{ id = 9; name = 'before_checkpoint' },
    [ordered]@{ id = 10; name = 'after_checkpoint' },
    [ordered]@{ id = 11; name = 'before_export' },
    [ordered]@{ id = 12; name = 'after_export' }
)
$caseDefinitions = @(
    [pscustomobject]@{ id = 'A1'; workers = 1; batches = 1; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A2'; workers = 1; batches = 5; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A3'; workers = 4; batches = 1; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A4'; workers = 4; batches = 5; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A5'; workers = 4; batches = 5; scheduler = 'dynamic' }
)
$memoryNoiseFloorBytes = [int64](256MB)

function New-FailureList {
    return New-Object 'System.Collections.Generic.List[string]'
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$Reason
    )
    if (-not [string]::IsNullOrWhiteSpace($Reason) -and -not $List.Contains($Reason)) {
        [void]$List.Add($Reason)
    }
}

function Test-Property {
    param([object]$Object, [string]$Name)
    if ($Object -is [System.Collections.IDictionary]) { return $Object.Contains($Name) }
    return $null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name
}

function Get-JsonFile {
    param(
        [string]$Path,
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Failure $Failures "$Label missing: $Path"
        return $null
    }
    try {
        return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    } catch {
        Add-Failure $Failures "$Label invalid JSON: $Path"
        return $null
    }
}

function Get-DeepCopy {
    param([object]$Object)
    return $Object | ConvertTo-Json -Depth 30 | ConvertFrom-Json
}

function Get-IdentityKey {
    param([object]$Object)
    if ($null -eq $Object) {
        return ''
    }
    return $Object | ConvertTo-Json -Compress -Depth 12
}

function Get-Median {
    param([object[]]$Values)
    $numbers = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ } | Sort-Object)
    if ($numbers.Count -eq 0) {
        return $null
    }
    $middle = [int][Math]::Floor($numbers.Count / 2)
    if (($numbers.Count % 2) -eq 1) {
        return $numbers[$middle]
    }
    return ($numbers[$middle - 1] + $numbers[$middle]) / 2.0
}

function Get-Minimum {
    param([object[]]$Values)
    $numbers = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    if ($numbers.Count -eq 0) { return $null }
    return ($numbers | Measure-Object -Minimum).Minimum
}

function Get-Maximum {
    param([object[]]$Values)
    $numbers = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    if ($numbers.Count -eq 0) { return $null }
    return ($numbers | Measure-Object -Maximum).Maximum
}

function Get-PhaseMap {
    param([object[]]$Snapshots)
    $map = @{}
    foreach ($snapshot in @($Snapshots)) {
        if ($null -ne $snapshot -and (Test-Property $snapshot 'phase')) {
            $map[[string]$snapshot.phase] = $snapshot
        }
    }
    return $map
}

function Get-ComponentCapacity {
    param([object]$Snapshot, [string]$Name)
    if ($null -eq $Snapshot -or -not (Test-Property $Snapshot 'components')) { return [int64]0 }
    $component = $Snapshot.components.PSObject.Properties[$Name]
    if ($null -eq $component -or $null -eq $component.Value) { return [int64]0 }
    return [int64]$component.Value.retained_capacity_bytes
}

function Get-WorkerCapacity {
    param([object]$Snapshot)
    [int64]$total = 0
    if ($null -eq $Snapshot -or -not (Test-Property $Snapshot 'workers')) { return $total }
    foreach ($worker in @($Snapshot.workers)) {
        foreach ($name in @('regret_delta_stream', 'continuation_delta_stream', 'discovery_public_states', 'discovery_rows', 'action_menu_scratch')) {
            if (Test-Property $worker $name) {
                $total += [int64]$worker.$name.retained_capacity_bytes
            }
        }
    }
    return $total
}

function Get-PreflightEstimate {
    param(
        [object]$Report,
        [object]$Memory,
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Label
    )
    $ledger = if ($null -ne $Report -and (Test-Property $Report 'memory_ledger')) { $Report.memory_ledger } else { $null }
    $fields = @(
        'existing_working_set_bytes',
        'existing_private_committed_bytes',
        'additional_private_capacity_bytes',
        'mapped_virtual_bytes',
        'mapped_resident_allowance_bytes',
        'checkpoint_transient_bytes',
        'export_transient_bytes',
        'allocator_overhead_bytes',
        'estimated_private_peak_bytes',
        'estimated_working_set_peak_bytes'
    )
    $hasExplicitFields = $null -ne $ledger -and @($fields | Where-Object { -not (Test-Property $ledger $_) }).Count -eq 0
    if ($hasExplicitFields) {
        foreach ($field in $fields) {
            if ([int64]$ledger.$field -lt 0) { Add-Failure $Failures "$Label negative preflight category: $field" }
        }
        if ([int64]$ledger.estimated_private_peak_bytes -lt ([int64]$ledger.existing_private_committed_bytes + [int64]$ledger.additional_private_capacity_bytes + [int64](Get-Maximum @($ledger.checkpoint_transient_bytes, $ledger.export_transient_bytes)) + [int64]$ledger.allocator_overhead_bytes)) {
            Add-Failure $Failures "$Label private preflight peak below explicit category sum"
        }
        if ([int64]$ledger.estimated_working_set_peak_bytes -lt ([int64]$ledger.existing_working_set_bytes + [int64]$ledger.additional_private_capacity_bytes + [int64](Get-Maximum @($ledger.checkpoint_transient_bytes, $ledger.export_transient_bytes)) + [int64]$ledger.allocator_overhead_bytes)) {
            Add-Failure $Failures "$Label working-set preflight peak below explicit category sum"
        }
        return [pscustomobject]@{
            schema_version = if (Test-Property $ledger 'schema_version') { [int]$ledger.schema_version } else { 2 }
            compatibility = 'explicit'
            existing_working_set_bytes = [int64]$ledger.existing_working_set_bytes
            existing_private_committed_bytes = [int64]$ledger.existing_private_committed_bytes
            additional_private_capacity_bytes = [int64]$ledger.additional_private_capacity_bytes
            mapped_virtual_bytes = [int64]$ledger.mapped_virtual_bytes
            mapped_resident_allowance_bytes = [int64]$ledger.mapped_resident_allowance_bytes
            checkpoint_transient_bytes = [int64]$ledger.checkpoint_transient_bytes
            export_transient_bytes = [int64]$ledger.export_transient_bytes
            allocator_overhead_bytes = [int64]$ledger.allocator_overhead_bytes
            estimated_private_peak_bytes = [int64]$ledger.estimated_private_peak_bytes
            estimated_working_set_peak_bytes = [int64]$ledger.estimated_working_set_peak_bytes
            configured_capacity_growth_bytes = if (Test-Property $ledger 'configured_capacity_growth_bytes') { [int64]$ledger.configured_capacity_growth_bytes } else { $null }
            actual_retained_capacity_bytes = if (Test-Property $ledger 'actual_retained_capacity_bytes') { [int64]$ledger.actual_retained_capacity_bytes } else { $null }
        }
    }

    $phaseMap = Get-PhaseMap $(if ($null -ne $Memory) { $Memory.process_snapshots } else { @() })
    $metadata = $phaseMap['after_bucket_map_metadata']
    $preflight = $phaseMap['before_memory_preflight']
    $existingWorkingSet = if ($null -ne $ledger -and (Test-Property $ledger 'resident_baseline_bytes')) { [int64]$ledger.resident_baseline_bytes } elseif ($null -ne $preflight) { [int64]$preflight.current_working_set_bytes } else { [int64]0 }
    $existingPrivate = if ($null -ne $metadata) { [int64]$metadata.private_committed_bytes } else { [int64]0 }
    $mappedVirtual = if ($null -ne $ledger -and (Test-Property $ledger 'mapped_bucket_artifact_bytes')) { [int64]$ledger.mapped_bucket_artifact_bytes } else { [int64]0 }
    $additionalCapacity = if ($null -ne $ledger) { [int64]$ledger.worker_stream_bytes + [int64]$ledger.discovery_stream_bytes } else { [int64]0 }
    $checkpoint = if ($null -ne $ledger -and (Test-Property $ledger 'checkpoint_peak_bytes')) { [int64]$ledger.checkpoint_peak_bytes } else { [int64]0 }
    $export = if ($null -ne $ledger -and (Test-Property $ledger 'export_peak_bytes')) { [int64]$ledger.export_peak_bytes } else { [int64]0 }
    $allocator = if ($null -ne $ledger -and (Test-Property $ledger 'allocator_overhead_bytes')) { [int64]$ledger.allocator_overhead_bytes } else { [int64]0 }
    $privatePeak = if ($null -ne $ledger -and (Test-Property $ledger 'committed_bytes')) { [int64]$ledger.committed_bytes } elseif ($null -ne $Memory) { [int64]$Memory.peak_private_committed_bytes } else { [int64]0 }
    $workingSetPeak = if ($null -ne $Report -and (Test-Property $Report 'memory_preflight_estimate_bytes')) { [int64]$Report.memory_preflight_estimate_bytes } elseif ($null -ne $ledger -and (Test-Property $ledger 'live_bytes')) { [int64]$ledger.live_bytes } else { [int64]0 }
    return [pscustomobject]@{
        schema_version = 2
        compatibility = 'legacy_fallback'
        existing_working_set_bytes = $existingWorkingSet
        existing_private_committed_bytes = $existingPrivate
        additional_private_capacity_bytes = $additionalCapacity
        mapped_virtual_bytes = $mappedVirtual
        mapped_resident_allowance_bytes = [int64]0
        checkpoint_transient_bytes = $checkpoint
        export_transient_bytes = $export
        allocator_overhead_bytes = $allocator
        estimated_private_peak_bytes = $privatePeak
        estimated_working_set_peak_bytes = $workingSetPeak
        configured_capacity_growth_bytes = $null
        actual_retained_capacity_bytes = $null
    }
}

function Get-ReportMemoryData {
    param(
        [object]$Report,
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Label
    )
    $processSnapshots = @()
    $subsystemSnapshots = @()
    if (Test-Property $Report 'memory_process_snapshots') { $processSnapshots = @($Report.memory_process_snapshots) }
    if (Test-Property $Report 'memory_subsystems' -and $null -ne $Report.memory_subsystems -and (Test-Property $Report.memory_subsystems 'snapshots')) {
        $subsystemSnapshots = @($Report.memory_subsystems.snapshots)
    }
    if ($processSnapshots.Count -ne $requiredPhases.Count) {
        Add-Failure $Failures "$Label missing phase snapshot count: expected $($requiredPhases.Count), got $($processSnapshots.Count)"
    }
    if ($subsystemSnapshots.Count -ne $requiredPhases.Count) {
        Add-Failure $Failures "$Label missing subsystem snapshot count: expected $($requiredPhases.Count), got $($subsystemSnapshots.Count)"
    }
    $processMap = Get-PhaseMap $processSnapshots
    $subsystemMap = Get-PhaseMap $subsystemSnapshots
    for ($index = 0; $index -lt $requiredPhases.Count; ++$index) {
        $expected = $requiredPhases[$index]
        if ($index -ge $processSnapshots.Count -or $processSnapshots[$index].phase_id -ne $expected.id -or $processSnapshots[$index].phase -ne $expected.name) {
            Add-Failure $Failures "$Label process phase order mismatch at index $index"
        }
        if ($index -ge $subsystemSnapshots.Count -or $subsystemSnapshots[$index].phase_id -ne $expected.id -or $subsystemSnapshots[$index].phase -ne $expected.name) {
            Add-Failure $Failures "$Label subsystem phase order mismatch at index $index"
        }
    }
    $privateAvailable = $true
    foreach ($snapshot in $processSnapshots) {
        if (-not (Test-Property $snapshot 'availability') -or -not [bool]$snapshot.availability.private_committed) {
            $privateAvailable = $false
            Add-Failure $Failures "$Label private committed bytes unavailable"
        }
        if ([int64]$snapshot.peak_working_set_bytes -lt [int64]$snapshot.current_working_set_bytes) {
            Add-Failure $Failures "$Label impossible working-set peak below current value"
        }
        if ([int64]$snapshot.private_committed_bytes -lt 0) {
            Add-Failure $Failures "$Label negative private committed bytes"
        }
    }
    foreach ($snapshot in $subsystemSnapshots) {
        if ([int64]$snapshot.retained_capacity_bytes -lt [int64]$snapshot.live_bytes) {
            Add-Failure $Failures "$Label subsystem retained capacity below live bytes"
        }
        if (Test-Property $snapshot 'components') {
            foreach ($componentProperty in $snapshot.components.PSObject.Properties) {
                $component = $componentProperty.Value
                if ([int64]$component.retained_capacity_bytes -lt [int64]$component.live_element_bytes) {
                    Add-Failure $Failures "$Label component capacity below live bytes: $($componentProperty.Name)"
                }
            }
        }
    }
    if ($processSnapshots.Count -eq $requiredPhases.Count -and $subsystemSnapshots.Count -eq $requiredPhases.Count) {
        for ($index = 0; $index -lt $requiredPhases.Count; ++$index) {
            $process = $processSnapshots[$index]
            $subsystem = $subsystemSnapshots[$index]
            if ([int64]$process.private_committed_bytes -lt [int64]$subsystem.retained_capacity_bytes) {
                Add-Failure $Failures "$Label private bytes below attributed capacity at phase $($process.phase)"
            }
        }
    }
    if ($null -eq $Report.memory_subsystems -or -not [bool]$Report.memory_subsystems.enabled -or -not [bool]$Report.memory_subsystems.completed) {
        Add-Failure $Failures "$Label subsystem telemetry is incomplete"
    }
    $deltas = @()
    for ($index = 1; $index -lt $processSnapshots.Count; ++$index) {
        $previous = $processSnapshots[$index - 1]
        $current = $processSnapshots[$index]
        $deltas += [ordered]@{
            from = $previous.phase
            to = $current.phase
            working_set_delta_bytes = [int64]$current.current_working_set_bytes - [int64]$previous.current_working_set_bytes
            private_committed_delta_bytes = [int64]$current.private_committed_bytes - [int64]$previous.private_committed_bytes
        }
    }
    $phaseMap = Get-PhaseMap $processSnapshots
    $subsystemPhaseMap = Get-PhaseMap $subsystemSnapshots
    $finalProcess = if ($processSnapshots.Count -gt 0) { $processSnapshots[-1] } else { $null }
    $finalSubsystem = if ($subsystemSnapshots.Count -gt 0) { $subsystemSnapshots[-1] } else { $null }
    $validationProcess = $phaseMap['after_bucket_assignment_validation']
    $metadataProcess = $phaseMap['after_bucket_map_metadata']
    $constructionProcess = $phaseMap['after_coordinator_construction']
    $workerConstructionProcess = $phaseMap['after_worker_pool_construction']
    $coldProcess = $phaseMap['after_cold_batch']
    $steadyProcess = $phaseMap['after_steady_batch']
    $beforeCheckpointProcess = $phaseMap['before_checkpoint']
    $afterCheckpointProcess = $phaseMap['after_checkpoint']
    $beforeExportProcess = $phaseMap['before_export']
    $afterExportProcess = $phaseMap['after_export']
    $metadataSubsystem = $subsystemPhaseMap['after_bucket_map_metadata']
    $constructionSubsystem = $subsystemPhaseMap['after_coordinator_construction']
    $workerConstructionSubsystem = $subsystemPhaseMap['after_worker_pool_construction']
    $coldSubsystem = $subsystemPhaseMap['after_cold_batch']
    $steadySubsystem = $subsystemPhaseMap['after_steady_batch']
    $peakWorkingSet = Get-Maximum @($processSnapshots | ForEach-Object { $_.peak_working_set_bytes })
    $peakPrivate = Get-Maximum @($processSnapshots | ForEach-Object { $_.private_committed_bytes })
    $remainderAvailable = $null -ne $finalProcess -and $null -ne $finalSubsystem -and [int64]$finalProcess.private_committed_bytes -ge [int64]$finalSubsystem.retained_capacity_bytes
    $remainder = if ($remainderAvailable) { [int64]$finalProcess.private_committed_bytes - [int64]$finalSubsystem.retained_capacity_bytes } else { $null }
    $trainingPrivateDeltas = @()
    $trainingWorkingSetDeltas = @()
    if ($null -ne $steadyProcess -and $null -ne $afterCheckpointProcess -and $null -ne $afterExportProcess) {
        $trainingPrivateDeltas += ([int64]$afterCheckpointProcess.private_committed_bytes - [int64]$steadyProcess.private_committed_bytes)
        $trainingPrivateDeltas += ([int64]$afterExportProcess.private_committed_bytes - [int64]$steadyProcess.private_committed_bytes)
        $trainingWorkingSetDeltas += ([int64]$afterCheckpointProcess.current_working_set_bytes - [int64]$steadyProcess.current_working_set_bytes)
        $trainingWorkingSetDeltas += ([int64]$afterExportProcess.current_working_set_bytes - [int64]$steadyProcess.current_working_set_bytes)
    }
    $checkpointExportCapacities = @()
    if ($null -ne $steadySubsystem) {
        $checkpointExportCapacities += Get-ComponentCapacity $steadySubsystem 'checkpoint_retained_buffers'
        $checkpointExportCapacities += Get-ComponentCapacity $steadySubsystem 'export_retained_buffers'
    }
    $derivedChecks = [ordered]@{
        bucket_validation_working_set_delta_bytes = if ($null -ne $validationProcess -and $null -ne $metadataProcess) { [int64]$validationProcess.current_working_set_bytes - [int64]$metadataProcess.current_working_set_bytes } else { $null }
        bucket_validation_private_delta_bytes = if ($null -ne $validationProcess -and $null -ne $metadataProcess) { [int64]$validationProcess.private_committed_bytes - [int64]$metadataProcess.private_committed_bytes } else { $null }
        coordinator_construction_private_delta_bytes = if ($null -ne $constructionProcess -and $null -ne $metadataProcess) { [int64]$constructionProcess.private_committed_bytes - [int64]$metadataProcess.private_committed_bytes } else { $null }
        worker_construction_private_delta_bytes = if ($null -ne $workerConstructionProcess -and $null -ne $constructionProcess) { [int64]$workerConstructionProcess.private_committed_bytes - [int64]$constructionProcess.private_committed_bytes } else { $null }
        cold_to_steady_retained_capacity_delta_bytes = if ($null -ne $coldSubsystem -and $null -ne $steadySubsystem) { [int64]$steadySubsystem.retained_capacity_bytes - [int64]$coldSubsystem.retained_capacity_bytes } else { $null }
    }
    foreach ($derivedProperty in $derivedChecks.GetEnumerator()) {
        if ($null -ne $derivedProperty.Value -and [int64]$derivedProperty.Value -lt 0) {
            Add-Failure $Failures "$Label negative derived delta: $($derivedProperty.Key)=$($derivedProperty.Value)"
        }
    }
    return [pscustomobject]@{
        valid = $Failures.Count -eq 0
        process_snapshots = $processSnapshots
        subsystem_snapshots = $subsystemSnapshots
        phase_deltas = $deltas
        current_working_set_bytes = if ($null -ne $finalProcess) { [int64]$finalProcess.current_working_set_bytes } else { $null }
        current_private_committed_bytes = if ($null -ne $finalProcess) { [int64]$finalProcess.private_committed_bytes } else { $null }
        peak_working_set_bytes = $peakWorkingSet
        peak_private_committed_bytes = $peakPrivate
        bucket_mapping_virtual_bytes = Get-ComponentCapacity $metadataSubsystem 'bucket_mapping_virtual'
        bucket_validation_working_set_delta_bytes = if ($null -ne $validationProcess -and $null -ne $metadataProcess) { [int64]$validationProcess.current_working_set_bytes - [int64]$metadataProcess.current_working_set_bytes } else { $null }
        bucket_validation_private_delta_bytes = if ($null -ne $validationProcess -and $null -ne $metadataProcess) { [int64]$validationProcess.private_committed_bytes - [int64]$metadataProcess.private_committed_bytes } else { $null }
        coordinator_construction_private_delta_bytes = if ($null -ne $constructionProcess -and $null -ne $metadataProcess) { [int64]$constructionProcess.private_committed_bytes - [int64]$metadataProcess.private_committed_bytes } else { $null }
        worker_construction_private_delta_bytes = if ($null -ne $workerConstructionProcess -and $null -ne $constructionProcess) { [int64]$workerConstructionProcess.private_committed_bytes - [int64]$constructionProcess.private_committed_bytes } else { $null }
        worker_construction_capacity_bytes = Get-WorkerCapacity $workerConstructionSubsystem
        cold_to_steady_retained_capacity_delta_bytes = if ($null -ne $coldSubsystem -and $null -ne $steadySubsystem) { [int64]$steadySubsystem.retained_capacity_bytes - [int64]$coldSubsystem.retained_capacity_bytes } else { $null }
        cold_admission_scratch_capacity_bytes = Get-ComponentCapacity $coldSubsystem 'coordinator_admission_scratch'
        steady_admission_scratch_capacity_bytes = Get-ComponentCapacity $steadySubsystem 'coordinator_admission_scratch'
        compact_row_metadata_construction_capacity_bytes = Get-ComponentCapacity $constructionSubsystem 'compact_row_metadata'
        training_private_peak_extra_bytes = Get-Maximum $trainingPrivateDeltas
        training_working_set_peak_extra_bytes = Get-Maximum $trainingWorkingSetDeltas
        checkpoint_export_capacity_bytes = Get-Maximum $checkpointExportCapacities
        final_subsystem_live_bytes = if ($null -ne $finalSubsystem) { [int64]$finalSubsystem.live_bytes } else { $null }
        final_subsystem_capacity_bytes = if ($null -ne $finalSubsystem) { [int64]$finalSubsystem.retained_capacity_bytes } else { $null }
        unexplained_private_bytes = $remainder
        before_checkpoint_private_bytes = if ($null -ne $beforeCheckpointProcess) { [int64]$beforeCheckpointProcess.private_committed_bytes } else { $null }
        before_export_private_bytes = if ($null -ne $beforeExportProcess) { [int64]$beforeExportProcess.private_committed_bytes } else { $null }
        metadata_process = $metadataProcess
        worker_construction_process = $workerConstructionProcess
        final_process = $finalProcess
        final_subsystem = $finalSubsystem
    }
}

function Get-ReportIdentity {
    param([object]$Report, [object]$Metrics, [object]$Manifest)
    $producer = if (Test-Property $Report 'evidence') { $Report.evidence.producer } else { $null }
    $configuration = if ($null -ne $producer -and (Test-Property $producer 'build_configuration')) { [string]$producer.build_configuration } elseif ($null -ne $Metrics) { [string]$Metrics.build_configuration } else { '' }
    $commit = if ($null -ne $producer -and (Test-Property $producer 'git_commit')) { [string]$producer.git_commit } elseif ($null -ne $Metrics) { [string]$Metrics.git_commit } else { '' }
    $compiler = if ($null -ne $producer -and (Test-Property $producer 'compiler_identity')) { [string]$producer.compiler_identity } elseif ($null -ne $Metrics) { [string]$Metrics.compiler_identity } else { '' }
    return [ordered]@{
        git_commit = $commit
        build_configuration = $configuration
        compiler_identity = $compiler
        workflow_config_fingerprint = if ($null -ne $producer) { [string]$producer.workflow_config_fingerprint } else { '' }
        model_identity = if ($null -ne $producer) { [string]$producer.model_identity } else { '' }
        bucket_artifact_schema = if ($null -ne $producer) { [string]$producer.bucket_artifact_schema } else { '' }
    }
}

function Get-ArtifactIdentity {
    param([object]$Metrics, [object]$Manifest)
    if ($null -ne $Metrics -and (Test-Property $Metrics 'artifact_identity') -and $null -ne $Metrics.artifact_identity) {
        return $Metrics.artifact_identity
    }
    if ($null -ne $Manifest -and (Test-Property $Manifest 'artifact') -and $null -ne $Manifest.artifact) {
        return $Manifest.artifact
    }
    return $null
}

function Read-NormalSample {
    param(
        [string]$Root,
        [object]$Case,
        [int]$Repeat,
        [int]$Trajectories
    )
    $failures = New-FailureList
    $directory = Join-Path $Root (Join-Path $Case.id $Repeat.ToString())
    $reportPath = Join-Path $directory 'report.json'
    $metricsPath = Join-Path $directory 'run_metrics.json'
    $manifestPath = Join-Path $directory 'run_manifest.json'
    $report = Get-JsonFile $reportPath $failures "normal $($Case.id)/$Repeat report"
    $metrics = Get-JsonFile $metricsPath $failures "normal $($Case.id)/$Repeat metrics"
    $manifest = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) { Get-JsonFile $manifestPath $failures "normal $($Case.id)/$Repeat manifest" } else { $null }
    $memory = if ($null -ne $report) { Get-ReportMemoryData $report $failures "normal $($Case.id)/$Repeat" } else { $null }
    if ($null -ne $metrics) {
        if ([int]$metrics.workers -ne $Case.workers -or [int]$metrics.batches -ne $Case.batches -or [string]$metrics.scheduler -ne $Case.scheduler -or [int]$metrics.trajectories_per_batch -ne $Trajectories -or [string]$metrics.pinning -ne 'unpinned') {
            Add-Failure $failures "normal $($Case.id)/$Repeat workload mismatch"
        }
        if ((Test-Property $metrics 'exit_code_available') -and [bool]$metrics.exit_code_available -and [int]$metrics.exit_code -ne 0) {
            Add-Failure $failures "normal $($Case.id)/$Repeat nonzero process exit"
        }
    }
    if ($null -ne $report) {
        $expected = [int64]$Case.batches * [int64]$Trajectories
        if ([int64]$report.trajectories -ne $expected -or [int64]$report.accepted_trajectories -ne $expected -or [int64]$report.discarded_trajectories -ne 0 -or [bool]$report.failed -or [int]$report.capacity_exhaustion_stage -ne 0) {
            Add-Failure $failures "normal $($Case.id)/$Repeat trajectory acceptance gate failed"
        }
        if ([string]$report.instrumentation_kind -ne 'none' -or -not [bool]$report.allocation_probe_valid -or [bool]$report.tracy_memory_events_enabled) {
            Add-Failure $failures "normal $($Case.id)/$Repeat is not a normal uninstrumented report"
        }
    }
    $identity = if ($null -ne $report) { Get-ReportIdentity $report $metrics $manifest } else { $null }
    $artifact = Get-ArtifactIdentity $metrics $manifest
    if ($null -eq $artifact) { Add-Failure $failures "normal $($Case.id)/$Repeat artifact identity missing" }
    $preflight = if ($null -ne $report) { Get-PreflightEstimate $report $memory $failures "normal $($Case.id)/$Repeat" } else { $null }
    return [pscustomobject]@{
        kind = 'normal'
        case_id = $Case.id
        repeat = $Repeat
        workers = $Case.workers
        batches = $Case.batches
        scheduler = $Case.scheduler
        directory = $directory
        report = $report
        metrics = $metrics
        manifest = $manifest
        identity = $identity
        identity_key = Get-IdentityKey $identity
        artifact = $artifact
        artifact_key = Get-IdentityKey $artifact
        fingerprint = if ($null -ne $report) { [string]$report.deterministic_merge_fingerprint } else { '' }
        throughput_trajectories_per_second = if ($null -ne $report -and (Test-Property $report 'trajectories_per_second')) { [double]$report.trajectories_per_second } else { $null }
        elapsed_wall_nanoseconds = if ($null -ne $report -and (Test-Property $report 'elapsed_wall_nanoseconds')) { [int64]$report.elapsed_wall_nanoseconds } else { $null }
        process_cpu_nanoseconds = if ($null -ne $report -and (Test-Property $report 'process_cpu_nanoseconds')) { [int64]$report.process_cpu_nanoseconds } else { $null }
        preflight = $preflight
        memory = $memory
        failure_reasons = @($failures)
        valid = $failures.Count -eq 0 -and $null -ne $memory -and $memory.valid
    }
}

function Read-TracySample {
    param(
        [string]$Root,
        [object]$Case,
        [int]$Trajectories
    )
    $failures = New-FailureList
    $directory = Join-Path $Root (Join-Path $Case.id 'tracy')
    $manifestPath = Join-Path $directory 'run_manifest.json'
    $manifest = Get-JsonFile $manifestPath $failures "Tracy $($Case.id) manifest"
    $report = $null
    $metrics = $null
    $events = $null
    if ($null -ne $manifest -and (Test-Property $manifest 'training') -and (Test-Property $manifest.training 'report_json')) {
        $report = Get-JsonFile ([string]$manifest.training.report_json) $failures "Tracy $($Case.id) report"
        if (Test-Property $manifest.training 'metrics_json') {
            $metrics = Get-JsonFile ([string]$manifest.training.metrics_json) $failures "Tracy $($Case.id) metrics"
        }
    } else {
        $report = Get-JsonFile (Join-Path $directory 'report.json') $failures "Tracy $($Case.id) report"
        $metrics = Get-JsonFile (Join-Path $directory 'run_metrics.json') $failures "Tracy $($Case.id) metrics"
    }
    $eventsPath = Join-Path $directory 'memory_events.json'
    if (Test-Property $manifest 'memory_event_inspection' -and (Test-Property $manifest.memory_event_inspection 'path')) {
        $eventsPath = [string]$manifest.memory_event_inspection.path
    }
    $events = Get-JsonFile $eventsPath $failures "Tracy $($Case.id) allocation events"
    $memory = if ($null -ne $report) { Get-ReportMemoryData $report $failures "Tracy $($Case.id)" } else { $null }
    if ($null -ne $manifest -and (Test-Property $manifest 'workload')) {
        $workload = $manifest.workload
        if ([int]$workload.workers -ne $Case.workers -or [int]$workload.batches -ne $Case.batches -or [string]$workload.scheduler -ne $Case.scheduler -or [int]$workload.trajectories_per_batch -ne $Trajectories -or [bool]$workload.pin_workers) {
            Add-Failure $failures "Tracy $($Case.id) workload mismatch"
        }
    } else {
        Add-Failure $failures "Tracy $($Case.id) workload metadata missing"
    }
    if ($null -ne $manifest -and [string]$manifest.tracy_version -ne '0.14.1') { Add-Failure $failures "Tracy $($Case.id) Tracy version mismatch" }
    if ($null -ne $manifest -and (-not [bool]$manifest.tracy_memory_events_enabled)) { Add-Failure $failures "Tracy $($Case.id) memory events disabled" }
    if ($null -ne $report -and ([string]$report.instrumentation_kind -ne 'tracy' -or -not [bool]$report.tracy_memory_events_enabled)) { Add-Failure $failures "Tracy $($Case.id) report provenance mismatch" }
    if ($null -ne $events) {
        if ([int64]$events.allocation_events -le 0) { Add-Failure $failures "Tracy $($Case.id) allocation events missing" }
    }
    if ($null -ne $manifest -and (Test-Property $manifest 'memory_event_inspection')) {
        if ([int64]$manifest.memory_event_inspection.allocation_events -le 0) { Add-Failure $failures "Tracy $($Case.id) manifest allocation events missing" }
    }
    $tracePath = if ($null -ne $manifest -and (Test-Property $manifest 'trace')) { [string]$manifest.trace.path } else { '' }
    if (-not [string]::IsNullOrWhiteSpace($tracePath) -and -not (Test-Path -LiteralPath $tracePath -PathType Leaf)) { Add-Failure $failures "Tracy $($Case.id) trace artifact missing" }
    $identity = if ($null -ne $report) { Get-ReportIdentity $report $metrics $manifest } else { $null }
    $artifact = Get-ArtifactIdentity $metrics $manifest
    if ($null -eq $artifact) { Add-Failure $failures "Tracy $($Case.id) artifact identity missing" }
    $preflight = if ($null -ne $report) { Get-PreflightEstimate $report $memory $failures "Tracy $($Case.id)" } else { $null }
    return [pscustomobject]@{
        kind = 'tracy'
        case_id = $Case.id
        repeat = 0
        directory = $directory
        report = $report
        metrics = $metrics
        manifest = $manifest
        identity = $identity
        identity_key = Get-IdentityKey $identity
        artifact = $artifact
        artifact_key = Get-IdentityKey $artifact
        memory = $memory
        throughput_trajectories_per_second = if ($null -ne $report -and (Test-Property $report 'trajectories_per_second')) { [double]$report.trajectories_per_second } else { $null }
        elapsed_wall_nanoseconds = if ($null -ne $report -and (Test-Property $report 'elapsed_wall_nanoseconds')) { [int64]$report.elapsed_wall_nanoseconds } else { $null }
        process_cpu_nanoseconds = if ($null -ne $report -and (Test-Property $report 'process_cpu_nanoseconds')) { [int64]$report.process_cpu_nanoseconds } else { $null }
        preflight = $preflight
        allocation_events = if ($null -ne $events) { [int64]$events.allocation_events } else { $null }
        free_events = if ($null -ne $events) { [int64]$events.free_events } else { $null }
        allocated_bytes = if ($null -ne $events -and (Test-Property $events 'allocated_bytes')) { [int64]$events.allocated_bytes } else { $null }
        freed_bytes = if ($null -ne $events -and (Test-Property $events 'freed_bytes')) { [int64]$events.freed_bytes } else { $null }
        live_bytes = if ($null -ne $events -and (Test-Property $events 'live_bytes')) { [int64]$events.live_bytes } else { $null }
        failure_reasons = @($failures)
        valid = $failures.Count -eq 0 -and $null -ne $memory -and $memory.valid
    }
}

function Get-CaseSummary {
    param([object[]]$Samples, [object]$Case)
    $group = @($Samples | Where-Object { $_.case_id -eq $Case.id -and $_.valid })
    if ($group.Count -eq 0) { return $null }
    $firstMemory = $group[0].memory
    $phaseDeltaSummary = @()
    for ($index = 0; $index -lt $firstMemory.phase_deltas.Count; ++$index) {
        $phaseDeltaSummary += [ordered]@{
            from = $firstMemory.phase_deltas[$index].from
            to = $firstMemory.phase_deltas[$index].to
            working_set_delta_bytes = Get-Median @($group | ForEach-Object { $_.memory.phase_deltas[$index].working_set_delta_bytes })
            private_committed_delta_bytes = Get-Median @($group | ForEach-Object { $_.memory.phase_deltas[$index].private_committed_delta_bytes })
        }
    }
    $metricNames = @('current_working_set_bytes', 'current_private_committed_bytes', 'peak_working_set_bytes', 'peak_private_committed_bytes', 'bucket_mapping_virtual_bytes', 'bucket_validation_working_set_delta_bytes', 'bucket_validation_private_delta_bytes', 'coordinator_construction_private_delta_bytes', 'worker_construction_private_delta_bytes', 'worker_construction_capacity_bytes', 'cold_to_steady_retained_capacity_delta_bytes', 'cold_admission_scratch_capacity_bytes', 'steady_admission_scratch_capacity_bytes', 'compact_row_metadata_construction_capacity_bytes', 'training_private_peak_extra_bytes', 'training_working_set_peak_extra_bytes', 'checkpoint_export_capacity_bytes', 'final_subsystem_live_bytes', 'final_subsystem_capacity_bytes', 'unexplained_private_bytes')
    $performanceNames = @('throughput_trajectories_per_second', 'elapsed_wall_nanoseconds', 'process_cpu_nanoseconds')
    $preflightNames = @('existing_working_set_bytes', 'existing_private_committed_bytes', 'additional_private_capacity_bytes', 'mapped_virtual_bytes', 'mapped_resident_allowance_bytes', 'checkpoint_transient_bytes', 'export_transient_bytes', 'allocator_overhead_bytes', 'estimated_private_peak_bytes', 'estimated_working_set_peak_bytes', 'configured_capacity_growth_bytes', 'actual_retained_capacity_bytes')
    $summary = [ordered]@{ id = $Case.id; workers = $Case.workers; batches = $Case.batches; scheduler = $Case.scheduler; repeat_count = $group.Count; phase_deltas = $phaseDeltaSummary }
    foreach ($name in $metricNames) {
        $summary[$name] = Get-Median @($group | ForEach-Object { $_.memory.$name })
        $summary[($name + '_minimum')] = Get-Minimum @($group | ForEach-Object { $_.memory.$name })
        $summary[($name + '_maximum')] = Get-Maximum @($group | ForEach-Object { $_.memory.$name })
    }
    foreach ($name in $performanceNames) {
        $summary[$name] = Get-Median @($group | ForEach-Object { $_.$name })
        $summary[($name + '_minimum')] = Get-Minimum @($group | ForEach-Object { $_.$name })
        $summary[($name + '_maximum')] = Get-Maximum @($group | ForEach-Object { $_.$name })
    }
    $preflightSummary = [ordered]@{}
    foreach ($name in $preflightNames) {
        $preflightSummary[$name] = Get-Median @($group | ForEach-Object { if ($null -ne $_.preflight) { $_.preflight.$name } else { $null } })
        $preflightSummary[($name + '_minimum')] = Get-Minimum @($group | ForEach-Object { if ($null -ne $_.preflight) { $_.preflight.$name } else { $null } })
        $preflightSummary[($name + '_maximum')] = Get-Maximum @($group | ForEach-Object { if ($null -ne $_.preflight) { $_.preflight.$name } else { $null } })
    }
    $preflightSummary.compatibility = @($group | ForEach-Object { if ($null -ne $_.preflight) { $_.preflight.compatibility } } | Sort-Object -Unique) -join ','
    $summary.preflight_estimates = $preflightSummary
    return [pscustomobject]$summary
}

function Get-GroupMedian {
    param([object[]]$CaseSummaries, [string[]]$CaseIds, [string]$Property)
    return Get-Median @($CaseSummaries | Where-Object { $CaseIds -contains $_.id } | ForEach-Object { $_.$Property })
}

function Get-SummaryMetricRange {
    param([object]$Summary, [string]$Property)
    if ($null -eq $Summary) {
        return [ordered]@{ median = $null; minimum = $null; maximum = $null }
    }
    return [ordered]@{
        median = if (Test-Property $Summary $Property) { $Summary.$Property } else { $null }
        minimum = if (Test-Property $Summary ($Property + '_minimum')) { $Summary.($Property + '_minimum') } else { $null }
        maximum = if (Test-Property $Summary ($Property + '_maximum')) { $Summary.($Property + '_maximum') } else { $null }
    }
}

function Get-ComparableArtifactKey {
    param([object]$Artifact)
    if ($null -eq $Artifact) { return '' }
    return [ordered]@{
        path = if (Test-Property $Artifact 'path') { [string]$Artifact.path } else { '' }
        bytes = if (Test-Property $Artifact 'bytes') { [int64]$Artifact.bytes } else { $null }
        last_write_time_utc = if (Test-Property $Artifact 'last_write_time_utc') { [string]$Artifact.last_write_time_utc } else { '' }
    } | ConvertTo-Json -Compress
}

function Compare-MemoryQualification {
    param(
        [object]$Baseline,
        [object]$Final,
        [string]$BaselineDirectory,
        [string]$FinalDirectory
    )
    $failures = New-FailureList
    if ($Baseline.acceptance.status -ne 'pass') { Add-Failure $failures 'baseline analysis did not pass' }
    if ($Final.acceptance.status -ne 'pass') { Add-Failure $failures 'final analysis did not pass' }
    $baselineArtifactKey = Get-ComparableArtifactKey $Baseline.artifact_identity
    $finalArtifactKey = Get-ComparableArtifactKey $Final.artifact_identity
    $artifactMatch = $baselineArtifactKey -eq $finalArtifactKey
    if (-not $artifactMatch) { Add-Failure $failures 'baseline/final artifact identity mismatch' }
    $caseComparisons = @()
    $memoryFields = @('current_working_set_bytes', 'peak_working_set_bytes', 'current_private_committed_bytes', 'peak_private_committed_bytes', 'unexplained_private_bytes', 'bucket_mapping_virtual_bytes', 'bucket_validation_working_set_delta_bytes', 'cold_to_steady_retained_capacity_delta_bytes')
    $performanceFields = @('throughput_trajectories_per_second', 'elapsed_wall_nanoseconds', 'process_cpu_nanoseconds')
    foreach ($case in $caseDefinitions) {
        $baselineCase = @($Baseline.normal_case_summaries | Where-Object { $_.id -eq $case.id } | Select-Object -First 1)
        $finalCase = @($Final.normal_case_summaries | Where-Object { $_.id -eq $case.id } | Select-Object -First 1)
        if ($baselineCase.Count -ne 1 -or $finalCase.Count -ne 1) {
            Add-Failure $failures "missing baseline/final case summary: $($case.id)"
            continue
        }
        $baselineFingerprints = @($Baseline.samples | Where-Object { $_.case -eq $case.id } | ForEach-Object { [string]$_.fingerprint } | Sort-Object -Unique)
        $finalFingerprints = @($Final.samples | Where-Object { $_.case -eq $case.id } | ForEach-Object { [string]$_.fingerprint } | Sort-Object -Unique)
        $fingerprintsMatch = (Get-IdentityKey $baselineFingerprints) -eq (Get-IdentityKey $finalFingerprints)
        if (-not $fingerprintsMatch) { Add-Failure $failures "baseline/final fingerprint mismatch: $($case.id)" }
        $memory = [ordered]@{}
        foreach ($field in $memoryFields) {
            $before = Get-SummaryMetricRange $baselineCase[0] $field
            $after = Get-SummaryMetricRange $finalCase[0] $field
            $memory[$field] = [ordered]@{
                baseline = $before
                final = $after
                reduction_bytes = if ($null -ne $before.median -and $null -ne $after.median) { [double]$before.median - [double]$after.median } else { $null }
            }
        }
        $performance = [ordered]@{}
        foreach ($field in $performanceFields) {
            $before = Get-SummaryMetricRange $baselineCase[0] $field
            $after = Get-SummaryMetricRange $finalCase[0] $field
            $performance[$field] = [ordered]@{
                baseline = $before
                final = $after
                final_minus_baseline = if ($null -ne $before.median -and $null -ne $after.median) { [double]$after.median - [double]$before.median } else { $null }
            }
        }
        $baselinePreflight = if (Test-Property $baselineCase[0] 'preflight_estimates') { $baselineCase[0].preflight_estimates } else { $null }
        $finalPreflight = if (Test-Property $finalCase[0] 'preflight_estimates') { $finalCase[0].preflight_estimates } else { $null }
        $caseComparisons += [ordered]@{
            case = $case.id
            workers = $case.workers
            batches = $case.batches
            scheduler = $case.scheduler
            fingerprints_match = $fingerprintsMatch
            baseline_fingerprints = $baselineFingerprints
            final_fingerprints = $finalFingerprints
            memory = $memory
            performance = $performance
            preflight = [ordered]@{ baseline = $baselinePreflight; final = $finalPreflight }
        }
    }
    $baselineZeroDiscards = @($Baseline.samples | Where-Object { [bool]$_.valid -and $_.failure_reasons.Count -eq 0 }).Count -eq $Baseline.acceptance.valid_normal_sample_count
    $finalZeroDiscards = @($Final.samples | Where-Object { [bool]$_.valid -and $_.failure_reasons.Count -eq 0 }).Count -eq $Final.acceptance.valid_normal_sample_count
    $tracyComplete = $Final.acceptance.valid_tracy_sample_count -eq $caseDefinitions.Count
    if (-not $baselineZeroDiscards -or -not $finalZeroDiscards) { Add-Failure $failures 'normal qualification samples are not all valid and discard-free' }
    if (-not $tracyComplete) { Add-Failure $failures 'final Tracy matrix is incomplete' }
    $peakPrivateReductions = @($caseComparisons | ForEach-Object { $_.memory.peak_private_committed_bytes.reduction_bytes } | Where-Object { $null -ne $_ })
    $peakWorkingSetReductions = @($caseComparisons | ForEach-Object { $_.memory.peak_working_set_bytes.reduction_bytes } | Where-Object { $null -ne $_ })
    $throughputRegressions = @($caseComparisons | ForEach-Object {
        $baselineThroughput = $_.performance.throughput_trajectories_per_second.baseline.median
        $finalThroughput = $_.performance.throughput_trajectories_per_second.final.median
        if ($null -ne $baselineThroughput -and $baselineThroughput -gt 0 -and $null -ne $finalThroughput) { 1.0 - ([double]$finalThroughput / [double]$baselineThroughput) }
    })
    $maxThroughputRegression = Get-Maximum $throughputRegressions
    $memoryReductionExceedsNoise = ($null -ne (Get-Maximum $peakPrivateReductions) -and (Get-Maximum $peakPrivateReductions) -gt [double]$memoryNoiseFloorBytes) -or ($null -ne (Get-Maximum $peakWorkingSetReductions) -and (Get-Maximum $peakWorkingSetReductions) -gt [double]$memoryNoiseFloorBytes)
    if (-not $memoryReductionExceedsNoise) { Add-Failure $failures 'no measured peak memory reduction exceeded the noise floor' }
    if ($null -ne $maxThroughputRegression -and $maxThroughputRegression -gt 0.20) { Add-Failure $failures "throughput regression exceeded 20 percent: $maxThroughputRegression" }

    $candidateAttribution = @()
    foreach ($candidate in @('F', 'G', 'H', 'I', 'J')) {
        $baselineCandidate = @($Baseline.candidate_decision.ranked_candidates | Where-Object { $_.candidate -eq $candidate } | Select-Object -First 1)
        $finalCandidate = @($Final.candidate_decision.ranked_candidates | Where-Object { $_.candidate -eq $candidate } | Select-Object -First 1)
        $baselineScore = if ($candidate -eq 'G') { [double]$Baseline.derived.one_to_four_worker_private_memory_slope_bytes } elseif ($baselineCandidate.Count -eq 1) { [double]$baselineCandidate[0].score_bytes } else { $null }
        $finalScore = if ($candidate -eq 'G') { [double]$Final.derived.one_to_four_worker_private_memory_slope_bytes } elseif ($finalCandidate.Count -eq 1) { [double]$finalCandidate[0].score_bytes } else { $null }
        $reduction = if ($null -ne $baselineScore -and $null -ne $finalScore) { $baselineScore - $finalScore } else { $null }
        $candidateAttribution += [ordered]@{
            candidate = $candidate
            implemented = $candidate -eq 'G'
            baseline_score_bytes = $baselineScore
            final_score_bytes = $finalScore
            attributable_reduction_bytes = $reduction
            exceeds_noise = $null -ne $reduction -and $reduction -gt [double]$memoryNoiseFloorBytes
            status = if ($candidate -eq 'G') { 'implemented_and_measured' } else { 'not_implemented_in_task_k' }
        }
    }
    $slopeAttribution = [ordered]@{
        baseline_private_slope_bytes = $Baseline.derived.one_to_four_worker_private_memory_slope_bytes
        final_private_slope_bytes = $Final.derived.one_to_four_worker_private_memory_slope_bytes
        reduction_bytes = [double]$Baseline.derived.one_to_four_worker_private_memory_slope_bytes - [double]$Final.derived.one_to_four_worker_private_memory_slope_bytes
        baseline_capacity_slope_bytes = $Baseline.derived.one_to_four_worker_stream_capacity_slope_bytes
        final_capacity_slope_bytes = $Final.derived.one_to_four_worker_stream_capacity_slope_bytes
        reduction_capacity_bytes = [double]$Baseline.derived.one_to_four_worker_stream_capacity_slope_bytes - [double]$Final.derived.one_to_four_worker_stream_capacity_slope_bytes
    }
    $allChecksPass = $failures.Count -eq 0
    return [ordered]@{
        decision = if ($allChecksPass) { 'bounded_memory_qualification_pass_with_limitations' } else { 'bounded_memory_qualification_fail' }
        baseline_directory = $BaselineDirectory
        final_directory = $FinalDirectory
        baseline_commit = $Baseline.commit
        final_commit = $Final.commit
        artifact_match = $artifactMatch
        source_config_changed = (Get-IdentityKey $Baseline.artifact_identity) -ne (Get-IdentityKey $Final.artifact_identity)
        fixed_dynamic_equivalent = $caseComparisons.Count -eq $caseDefinitions.Count -and @($caseComparisons | Where-Object { -not $_.fingerprints_match }).Count -eq 0
        baseline_zero_discards = $baselineZeroDiscards
        final_zero_discards = $finalZeroDiscards
        tracy_complete = $tracyComplete
        memory_reduction_exceeds_noise = $memoryReductionExceedsNoise
        maximum_throughput_regression_fraction = $maxThroughputRegression
        working_set_reduction_exceeds_noise = $null -ne (Get-Maximum $peakWorkingSetReductions) -and (Get-Maximum $peakWorkingSetReductions) -gt [double]$memoryNoiseFloorBytes
        measurement_noise_floor_bytes = $memoryNoiseFloorBytes
        case_comparisons = $caseComparisons
        slope_attribution = $slopeAttribution
        candidate_attribution = $candidateAttribution
        limitations = @('Qualification uses the bounded five-case matrix at 1,000 trajectories per batch and is not a 50M-trajectory production claim.', 'Tracy allocation counts are instrumentation evidence and are not production throughput measurements.', 'Working-set peaks remained within measurement noise because the mapped artifact and validation residency were unchanged; the measured reduction is in private commitment.', 'Task E corrects accounting categories; it is not a RAM optimization.')
        failure_reasons = @($failures)
    }
}

function Invoke-MemoryMatrixAnalysis {
    param(
        [string]$Root,
        [int]$ExpectedRepeats,
        [int]$ExpectedTrajectories,
        [bool]$RequireTracy
    )
    $failures = New-FailureList
    $matrixManifest = $null
    $manifestPath = Join-Path $Root 'matrix_manifest.json'
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) { $matrixManifest = Get-JsonFile $manifestPath $failures 'matrix manifest' }
    if ($null -ne $matrixManifest) {
        if ((Test-Property $matrixManifest 'repeats') -and [int]$matrixManifest.repeats -ne $ExpectedRepeats) { Add-Failure $failures 'matrix repeat count mismatch' }
        if ((Test-Property $matrixManifest 'trajectories_per_batch') -and [int]$matrixManifest.trajectories_per_batch -ne $ExpectedTrajectories) { Add-Failure $failures 'matrix trajectories-per-batch mismatch' }
    }
    $normalSamples = @()
    foreach ($case in $caseDefinitions) {
        for ($repeat = 1; $repeat -le $ExpectedRepeats; ++$repeat) {
            $normalSamples += Read-NormalSample $Root $case $repeat $ExpectedTrajectories
        }
    }
    foreach ($sample in $normalSamples) {
        foreach ($reason in @($sample.failure_reasons)) { Add-Failure $failures $reason }
    }
    $validNormal = @($normalSamples | Where-Object { $_.valid })
    if ($validNormal.Count -ne $caseDefinitions.Count * $ExpectedRepeats) { Add-Failure $failures 'normal matrix does not contain complete valid samples' }
    $reference = $validNormal | Select-Object -First 1
    if ($null -ne $reference) {
        foreach ($sample in $validNormal) {
            if ($sample.artifact_key -ne $reference.artifact_key) { Add-Failure $failures "normal artifact identity mismatch: $($sample.case_id)/$($sample.repeat)" }
            if ([string]$sample.identity.build_configuration -ne 'RelWithDebInfo') { Add-Failure $failures "normal build configuration is not RelWithDebInfo: $($sample.case_id)/$($sample.repeat)" }
            if ([string]::IsNullOrWhiteSpace([string]$sample.fingerprint) -or [string]$sample.fingerprint -eq '0') { Add-Failure $failures "normal deterministic fingerprint missing: $($sample.case_id)/$($sample.repeat)" }
        }
        foreach ($case in $caseDefinitions) {
            $caseSamples = @($validNormal | Where-Object { $_.case_id -eq $case.id })
            $caseReference = $caseSamples | Select-Object -First 1
            if ($null -eq $caseReference) { continue }
            foreach ($sample in $caseSamples) {
                if ($sample.identity_key -ne $caseReference.identity_key) { Add-Failure $failures "normal build/workload identity mismatch: $($sample.case_id)/$($sample.repeat)" }
            }
            $fingerprints = @($caseSamples | ForEach-Object { $_.fingerprint } | Sort-Object -Unique)
            if ($fingerprints.Count -ne 1) { Add-Failure $failures "normal deterministic fingerprints differ: $($case.id): $($fingerprints -join ',')" }
        }
    }
    $tracySamples = @()
    if ($RequireTracy) {
        foreach ($case in $caseDefinitions) {
            $tracySamples += Read-TracySample $Root $case $ExpectedTrajectories
        }
        foreach ($sample in $tracySamples) {
            foreach ($reason in @($sample.failure_reasons)) { Add-Failure $failures $reason }
            $normalReference = $validNormal | Where-Object { $_.case_id -eq $sample.case_id } | Select-Object -First 1
            if ($null -ne $normalReference) {
                if ($sample.identity_key -ne $normalReference.identity_key) { Add-Failure $failures "normal/Tracy identity mismatch: $($sample.case_id)" }
                if ($sample.artifact_key -ne $normalReference.artifact_key) { Add-Failure $failures "normal/Tracy artifact mismatch: $($sample.case_id)" }
                if ($null -ne $sample.report -and [string]$sample.report.deterministic_merge_fingerprint -ne [string]$normalReference.fingerprint) { Add-Failure $failures "normal/Tracy fingerprint mismatch: $($sample.case_id)" }
            }
        }
        if (@($tracySamples | Where-Object { $_.valid }).Count -ne $caseDefinitions.Count) { Add-Failure $failures 'Tracy capture matrix is incomplete' }
    }
    $caseSummaries = @()
    foreach ($case in $caseDefinitions) {
        $summary = Get-CaseSummary $normalSamples $case
        if ($null -ne $summary) { $caseSummaries += $summary }
    }
    $summaryById = @{}
    foreach ($summary in $caseSummaries) { $summaryById[$summary.id] = $summary }
    $oneWorker = @($caseSummaries | Where-Object { $_.workers -eq 1 -and $_.batches -eq 1 } | Select-Object -First 1)
    $fourWorker = @($caseSummaries | Where-Object { $_.workers -eq 4 -and $_.batches -eq 1 } | Select-Object -First 1)
    $oneToFourSlope = $null
    $workerCapacitySlope = $null
    if ($oneWorker.Count -eq 1 -and $fourWorker.Count -eq 1) {
        $oneToFourSlope = ([double]$fourWorker[0].worker_construction_private_delta_bytes - [double]$oneWorker[0].worker_construction_private_delta_bytes) / 3.0
        $workerCapacitySlope = ([double]$fourWorker[0].worker_construction_capacity_bytes - [double]$oneWorker[0].worker_construction_capacity_bytes) / 3.0
        if ($oneToFourSlope -lt 0 -or $workerCapacitySlope -lt 0) { Add-Failure $failures 'negative one-to-four-worker derived slope' }
    } else {
        Add-Failure $failures 'one-to-four-worker slope inputs missing'
    }
    $validationWorkingSet = Get-Maximum @($caseSummaries | ForEach-Object { $_.bucket_validation_working_set_delta_bytes })
    $validationPrivate = Get-Maximum @($caseSummaries | ForEach-Object { $_.bucket_validation_private_delta_bytes })
    $otherWorkingSetDeltas = @()
    foreach ($summary in $caseSummaries) {
        foreach ($delta in @($summary.phase_deltas)) {
            if ($delta.from -ne 'after_bucket_map_metadata' -or $delta.to -ne 'after_bucket_assignment_validation') {
                if ([double]$delta.working_set_delta_bytes -gt 0) { $otherWorkingSetDeltas += [double]$delta.working_set_delta_bytes }
            }
        }
    }
    $otherWorkingSetMaximum = Get-Maximum $otherWorkingSetDeltas
    $fEligible = $false
    if ($null -ne $validationWorkingSet -and $null -ne $validationPrivate) {
        $fEligible = $validationWorkingSet -gt (2.0 * $memoryNoiseFloorBytes) -and $validationWorkingSet -gt [double]$otherWorkingSetMaximum * 1.25 -and $validationPrivate -le [double]$validationWorkingSet * 0.25
    }
    $gEligible = $null -ne $oneToFourSlope -and $null -ne $workerCapacitySlope -and $oneToFourSlope -gt $memoryNoiseFloorBytes -and $workerCapacitySlope -gt $memoryNoiseFloorBytes -and $workerCapacitySlope -ge $oneToFourSlope * 0.5
    $rowCapacity = Get-Maximum @($caseSummaries | ForEach-Object { $_.compact_row_metadata_construction_capacity_bytes })
    $coordinatorDelta = Get-Maximum @($caseSummaries | ForEach-Object { $_.coordinator_construction_private_delta_bytes })
    $hEligible = $null -ne $rowCapacity -and $rowCapacity -gt $memoryNoiseFloorBytes -and ($null -eq $coordinatorDelta -or $rowCapacity -ge $coordinatorDelta * 0.25)
    $steadyScratch = Get-Maximum @($caseSummaries | ForEach-Object { $_.steady_admission_scratch_capacity_bytes })
    $steadyRetained = Get-Maximum @($caseSummaries | ForEach-Object { $_.final_subsystem_capacity_bytes })
    $iEligible = $null -ne $steadyScratch -and $null -ne $steadyRetained -and $steadyScratch -gt $memoryNoiseFloorBytes -and $steadyScratch -ge $steadyRetained * 0.2
    $checkpointPrivateExtra = Get-Maximum @($caseSummaries | ForEach-Object { $_.training_private_peak_extra_bytes })
    $checkpointWorkingSetExtra = Get-Maximum @($caseSummaries | ForEach-Object { $_.training_working_set_peak_extra_bytes })
    $jScore = Get-Maximum @($checkpointPrivateExtra, $checkpointWorkingSetExtra)
    $jEligible = $null -ne $jScore -and $jScore -gt $memoryNoiseFloorBytes
    $candidates = @(
        [ordered]@{ candidate = 'F'; eligible = $fEligible; score_bytes = if ($fEligible) { [int64]$validationWorkingSet } else { [int64]0 }; evidence = "validation_working_set_delta=$validationWorkingSet; validation_private_delta=$validationPrivate; other_phase_working_set_max=$otherWorkingSetMaximum" },
        [ordered]@{ candidate = 'G'; eligible = $gEligible; score_bytes = if ($gEligible) { [int64]$oneToFourSlope } else { [int64]0 }; evidence = "one_to_four_private_slope=$oneToFourSlope; worker_capacity_slope=$workerCapacitySlope" },
        [ordered]@{ candidate = 'H'; eligible = $hEligible; score_bytes = if ($hEligible) { [int64]$rowCapacity } else { [int64]0 }; evidence = "row_metadata_construction_capacity=$rowCapacity; coordinator_private_delta=$coordinatorDelta" },
        [ordered]@{ candidate = 'I'; eligible = $iEligible; score_bytes = if ($iEligible) { [int64]$steadyScratch } else { [int64]0 }; evidence = "steady_admission_scratch_capacity=$steadyScratch; final_subsystem_capacity=$steadyRetained" },
        [ordered]@{ candidate = 'J'; eligible = $jEligible; score_bytes = if ($jEligible) { [int64]$jScore } else { [int64]0 }; evidence = "training_private_peak_extra=$checkpointPrivateExtra; training_working_set_peak_extra=$checkpointWorkingSetExtra" }
    )
    $selected = $null
    if ($failures.Count -eq 0) {
        $eligibleCandidates = @($candidates | Where-Object { $_.eligible } | Sort-Object @{ Expression = { $_.score_bytes }; Descending = $true }, candidate)
        if ($eligibleCandidates.Count -gt 0) { $selected = $eligibleCandidates[0].candidate }
    }
    $candidateStatus = if ($null -eq $selected) { 'no_candidate_selected' } else { 'candidate_selected' }
    $identity = if ($null -ne $reference) { $reference.identity } else { $null }
    $artifact = if ($null -ne $reference) { $reference.artifact } else { $null }
    $tracySummary = @()
    foreach ($sample in $tracySamples) {
        $tracySummary += [ordered]@{
            case = $sample.case_id
            allocation_count = $sample.allocation_events
            allocated_bytes = $sample.allocated_bytes
            free_count = $sample.free_events
            freed_bytes = $sample.freed_bytes
            live_bytes = $sample.live_bytes
        }
    }
    $acceptanceStatus = if ($failures.Count -eq 0) { 'pass' } else { 'fail' }
    return [ordered]@{
        schema_version = 1
        commit = if ($null -ne $identity) { $identity.git_commit } else { $null }
        build_identity = $identity
        artifact_identity = $artifact
        workload_identity = [ordered]@{ trajectories_per_batch = $ExpectedTrajectories; repeats = $ExpectedRepeats; pin_workers = $false; cases = @($caseDefinitions | ForEach-Object { [ordered]@{ id = $_.id; workers = $_.workers; batches = $_.batches; scheduler = $_.scheduler } }) }
        normal_case_summaries = @($caseSummaries)
        tracy = $tracySummary
        derived = [ordered]@{
            mapped_artifact_virtual_size_bytes = Get-Maximum @($caseSummaries | ForEach-Object { $_.bucket_mapping_virtual_bytes })
            startup_validation_working_set_delta_bytes = $validationWorkingSet
            startup_validation_private_delta_bytes = $validationPrivate
            one_to_four_worker_private_memory_slope_bytes = $oneToFourSlope
            one_to_four_worker_stream_capacity_slope_bytes = $workerCapacitySlope
            cold_to_steady_retained_memory_delta_bytes = Get-Maximum @($caseSummaries | ForEach-Object { $_.cold_to_steady_retained_capacity_delta_bytes })
            measurement_noise_floor_bytes = $memoryNoiseFloorBytes
            unexplained_private_remainder_bytes = Get-Maximum @($caseSummaries | ForEach-Object { $_.unexplained_private_bytes })
        }
        candidate_decision = [ordered]@{ status = $candidateStatus; selected_candidate = $selected; ranked_candidates = @($candidates); no_candidate_reason = if ($null -eq $selected) { if ($failures.Count -ne 0) { 'incomplete_or_mismatched_measurements' } else { 'no_component_exceeded_baseline_noise' } } else { $null } }
        acceptance = [ordered]@{ status = $acceptanceStatus; failure_reasons = @($failures); normal_sample_count = $normalSamples.Count; valid_normal_sample_count = $validNormal.Count; tracy_sample_count = $tracySamples.Count; valid_tracy_sample_count = @($tracySamples | Where-Object { $_.valid }).Count }
        samples = @($normalSamples | Sort-Object case_id, repeat | ForEach-Object {
            [ordered]@{
                case = $_.case_id
                repeat = $_.repeat
                workers = $_.workers
                batches = $_.batches
                scheduler = $_.scheduler
                valid = $_.valid
                fingerprint = $_.fingerprint
                current_working_set_bytes = if ($null -ne $_.memory) { $_.memory.current_working_set_bytes } else { $null }
                peak_working_set_bytes = if ($null -ne $_.memory) { $_.memory.peak_working_set_bytes } else { $null }
                current_private_committed_bytes = if ($null -ne $_.memory) { $_.memory.current_private_committed_bytes } else { $null }
                peak_private_committed_bytes = if ($null -ne $_.memory) { $_.memory.peak_private_committed_bytes } else { $null }
                unexplained_private_bytes = if ($null -ne $_.memory) { $_.memory.unexplained_private_bytes } else { $null }
                throughput_trajectories_per_second = $_.throughput_trajectories_per_second
                elapsed_wall_nanoseconds = $_.elapsed_wall_nanoseconds
                process_cpu_nanoseconds = $_.process_cpu_nanoseconds
                preflight = $_.preflight
                phase_deltas = if ($null -ne $_.memory) { $_.memory.phase_deltas } else { @() }
                failure_reasons = $_.failure_reasons
            }
        })
    }
}

function Format-Bytes {
    param([object]$Value)
    if ($null -eq $Value) { return 'n/a' }
    return ('{0:N2} GiB' -f ([double]$Value / 1GB))
}

function Format-Seconds {
    param([object]$Value)
    if ($null -eq $Value) { return 'n/a' }
    return ('{0:N3} s' -f ([double]$Value))
}

function Write-MemoryEvidence {
    param(
        [object]$Summary,
        [string]$OutputDirectory
    )
    $jsonPath = Join-Path $OutputDirectory 'memory_summary.json'
    $csvPath = Join-Path $OutputDirectory 'memory_samples.csv'
    $evidencePath = Join-Path $OutputDirectory 'evidence.md'
    $Summary | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
    $csvRows = @()
    foreach ($sample in @($Summary.samples)) {
        $tracy = @($Summary.tracy | Where-Object { $_.case -eq $sample.case } | Select-Object -First 1)
        $csvRows += [pscustomobject]@{
            kind = 'normal'
            case = $sample.case
            repeat = $sample.repeat
            workers = $sample.workers
            batches = $sample.batches
            scheduler = $sample.scheduler
            valid = $sample.valid
            fingerprint = $sample.fingerprint
            current_working_set_bytes = $sample.current_working_set_bytes
            peak_working_set_bytes = $sample.peak_working_set_bytes
            current_private_committed_bytes = $sample.current_private_committed_bytes
            peak_private_committed_bytes = $sample.peak_private_committed_bytes
            unexplained_private_bytes = $sample.unexplained_private_bytes
            throughput_trajectories_per_second = $sample.throughput_trajectories_per_second
            elapsed_wall_nanoseconds = $sample.elapsed_wall_nanoseconds
            process_cpu_nanoseconds = $sample.process_cpu_nanoseconds
            preflight_json = ($sample.preflight | ConvertTo-Json -Compress -Depth 8)
            tracy_allocation_count = if ($tracy.Count -eq 1) { $tracy[0].allocation_count } else { $null }
            tracy_allocated_bytes = if ($tracy.Count -eq 1) { $tracy[0].allocated_bytes } else { $null }
            tracy_free_count = if ($tracy.Count -eq 1) { $tracy[0].free_count } else { $null }
            tracy_freed_bytes = if ($tracy.Count -eq 1) { $tracy[0].freed_bytes } else { $null }
            tracy_live_bytes = if ($tracy.Count -eq 1) { $tracy[0].live_bytes } else { $null }
            phase_deltas_json = ($sample.phase_deltas | ConvertTo-Json -Compress -Depth 6)
            failure_reasons = ($sample.failure_reasons -join '|')
        }
    }
    if ($csvRows.Count -gt 0) { $csvRows | Export-Csv -LiteralPath $csvPath -NoTypeInformation } else { 'kind,case,repeat' | Set-Content -LiteralPath $csvPath -Encoding UTF8 }
    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# Bounded memory attribution evidence')
    [void]$lines.Add('')
    [void]$lines.Add("- Acceptance: **$($Summary.acceptance.status)**")
    [void]$lines.Add("- Commit: $($Summary.commit)")
    [void]$lines.Add("- Normal build: $($Summary.build_identity.build_configuration) / $($Summary.build_identity.compiler_identity)")
    [void]$lines.Add("- Candidate decision: **$($Summary.candidate_decision.status)**, selected $($Summary.candidate_decision.selected_candidate)")
    if (Test-Property $Summary 'qualification') {
        [void]$lines.Add("- Production decision: **$($Summary.qualification.decision)**")
        [void]$lines.Add("- Baseline/final artifact match: $($Summary.qualification.artifact_match); fixed/dynamic fingerprints equivalent: $($Summary.qualification.fixed_dynamic_equivalent)")
    }
    [void]$lines.Add('')
    [void]$lines.Add('| Case | Workers | Batches | Scheduler | Median current WS | Median peak WS | Median current private | Median peak private | Throughput/s | CPU | Fingerprint | Tracy alloc/free |')
    [void]$lines.Add('| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
    foreach ($case in @($Summary.normal_case_summaries)) {
        $tracy = @($Summary.tracy | Where-Object { $_.case -eq $case.id } | Select-Object -First 1)
        $events = if ($tracy.Count -eq 1) { "$($tracy[0].allocation_count)/$($tracy[0].free_count)" } else { 'n/a' }
        $fingerprint = @($Summary.samples | Where-Object { $_.case -eq $case.id } | Select-Object -First 1).fingerprint
        [void]$lines.Add("| $($case.id) | $($case.workers) | $($case.batches) | $($case.scheduler) | $(Format-Bytes $case.current_working_set_bytes) | $(Format-Bytes $case.peak_working_set_bytes) | $(Format-Bytes $case.current_private_committed_bytes) | $(Format-Bytes $case.peak_private_committed_bytes) | $([double]$case.throughput_trajectories_per_second) | $(Format-Seconds ([double]$case.process_cpu_nanoseconds / 1e9)) | $fingerprint | $events |")
    }
    [void]$lines.Add('')
    [void]$lines.Add('## Derived attribution')
    [void]$lines.Add('')
    [void]$lines.Add("- Startup validation working-set delta: $(Format-Bytes $Summary.derived.startup_validation_working_set_delta_bytes)")
    [void]$lines.Add("- One-to-four-worker private-memory slope: $(Format-Bytes $Summary.derived.one_to_four_worker_private_memory_slope_bytes)")
    [void]$lines.Add("- One-to-four-worker stream-capacity slope: $(Format-Bytes $Summary.derived.one_to_four_worker_stream_capacity_slope_bytes)")
    [void]$lines.Add("- Cold-to-steady retained-memory delta: $(Format-Bytes $Summary.derived.cold_to_steady_retained_memory_delta_bytes)")
    [void]$lines.Add("- Mapped artifact virtual size: $(Format-Bytes $Summary.derived.mapped_artifact_virtual_size_bytes)")
    [void]$lines.Add("- Unexplained private remainder: $(Format-Bytes $Summary.derived.unexplained_private_remainder_bytes)")
    [void]$lines.Add('')
    [void]$lines.Add('## Preflight estimate categories')
    [void]$lines.Add('')
    [void]$lines.Add('| Case | Compatibility | Existing WS | Existing private | Additional private capacity | Mapped virtual | Mapped resident allowance | Checkpoint transient | Export transient | Allocator overhead | Estimated private peak | Estimated WS peak |')
    [void]$lines.Add('| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
    foreach ($case in @($Summary.normal_case_summaries)) {
        $preflight = $case.preflight_estimates
        [void]$lines.Add("| $($case.id) | $($preflight.compatibility) | $(Format-Bytes $preflight.existing_working_set_bytes) | $(Format-Bytes $preflight.existing_private_committed_bytes) | $(Format-Bytes $preflight.additional_private_capacity_bytes) | $(Format-Bytes $preflight.mapped_virtual_bytes) | $(Format-Bytes $preflight.mapped_resident_allowance_bytes) | $(Format-Bytes $preflight.checkpoint_transient_bytes) | $(Format-Bytes $preflight.export_transient_bytes) | $(Format-Bytes $preflight.allocator_overhead_bytes) | $(Format-Bytes $preflight.estimated_private_peak_bytes) | $(Format-Bytes $preflight.estimated_working_set_peak_bytes) |")
    }
    [void]$lines.Add('')
    if (Test-Property $Summary 'qualification') {
        [void]$lines.Add('## Baseline versus final')
        [void]$lines.Add('')
        [void]$lines.Add('| Case | Peak private reduction | Peak WS reduction | Throughput delta | CPU delta | Fingerprints |')
        [void]$lines.Add('| --- | ---: | ---: | ---: | ---: | --- |')
        foreach ($comparison in @($Summary.qualification.case_comparisons)) {
            [void]$lines.Add("| $($comparison.case) | $(Format-Bytes $comparison.memory.peak_private_committed_bytes.reduction_bytes) | $(Format-Bytes $comparison.memory.peak_working_set_bytes.reduction_bytes) | $([double]$comparison.performance.throughput_trajectories_per_second.final_minus_baseline) | $(Format-Seconds ([double]$comparison.performance.process_cpu_nanoseconds.final_minus_baseline / 1e9)) | $($comparison.fingerprints_match) |")
        }
        [void]$lines.Add('')
        [void]$lines.Add('## Candidate attributable reductions')
        [void]$lines.Add('')
        [void]$lines.Add('| Candidate | Implemented | Baseline score | Final score | Attributable reduction | Exceeds noise | Status |')
        [void]$lines.Add('| --- | --- | ---: | ---: | ---: | --- | --- |')
        foreach ($candidate in @($Summary.qualification.candidate_attribution)) {
            [void]$lines.Add("| $($candidate.candidate) | $($candidate.implemented) | $(Format-Bytes $candidate.baseline_score_bytes) | $(Format-Bytes $candidate.final_score_bytes) | $(Format-Bytes $candidate.attributable_reduction_bytes) | $($candidate.exceeds_noise) | $($candidate.status) |")
        }
        [void]$lines.Add('')
        [void]$lines.Add('Qualification limitations:')
        foreach ($limitation in @($Summary.qualification.limitations)) { [void]$lines.Add("- $limitation") }
    }
    [void]$lines.Add('## Candidate ranking')
    [void]$lines.Add('')
    foreach ($candidate in @($Summary.candidate_decision.ranked_candidates)) {
        [void]$lines.Add("- $($candidate.candidate): eligible=$($candidate.eligible), score=$(Format-Bytes $candidate.score_bytes), $($candidate.evidence)")
    }
    if (@($Summary.acceptance.failure_reasons).Count -gt 0) {
        [void]$lines.Add('')
        [void]$lines.Add('## Failure reasons')
        [void]$lines.Add('')
        foreach ($reason in @($Summary.acceptance.failure_reasons)) { [void]$lines.Add("- $reason") }
    }
    [void]$lines.Add('')
    [void]$lines.Add('Tracy allocation byte totals are reported only when the exporter supplied them. Mapped virtual size is never subtracted from working set.')
    $lines | Set-Content -LiteralPath $evidencePath -Encoding UTF8
}

function New-SyntheticReport {
    param(
        [object]$Case,
        [string]$Mode,
        [string]$Fingerprint = '777'
    )
    $process = @()
    $subsystems = @()
    [int64]$workerCapacity = if ($Mode -eq 'G') { 400MB } else { 4MB }
    [int64]$rowCapacity = if ($Mode -eq 'H') { 400MB } else { 4MB }
    [int64]$scratchCapacity = if ($Mode -eq 'I') { 400MB } else { 4MB }
    foreach ($phase in $requiredPhases) {
        [int64]$workingSet = 1GB + ($phase.id * 4MB)
        [int64]$private = 512MB + ($phase.id * 4MB)
        if ($phase.id -eq 3 -and $Mode -eq 'F') { $workingSet += 2GB; $private += 16MB }
        if ($phase.id -ge 5 -and $Mode -eq 'H') { $private += 600MB }
        [int64]$perWorkerPrivate = if ($Mode -eq 'G') { 300MB } else { 8MB }
        if ($phase.id -eq 6) { $private += [int64]$Case.workers * $perWorkerPrivate }
        if ($phase.id -eq 7 -and $Mode -eq 'I') { $private += 500MB }
        if (($phase.id -eq 10 -or $phase.id -eq 12) -and $Mode -eq 'J') { $private += 2GB }
        if ($phase.id -ge 5) { $private += $rowCapacity }
        if ($phase.id -ge 6) { $private += [int64]$Case.workers * $workerCapacity }
        if ($phase.id -ge 7) { $private += $scratchCapacity }
        $process += [ordered]@{
            phase_id = $phase.id
            phase = $phase.name
            current_working_set_bytes = $workingSet
            peak_working_set_bytes = $workingSet + 1MB
            private_committed_bytes = $private
            peak_private_committed_bytes = 0
            virtual_address_bytes = 0
            anonymous_resident_bytes = 0
            file_resident_bytes = 0
            availability = [ordered]@{ current_working_set = $true; peak_working_set = $true; private_committed = $true; peak_private_committed = $false; virtual_address = $false; anonymous_resident = $false; file_resident = $false }
        }
        $retained = [int64](20MB + ($phase.id * 1MB))
        if ($phase.id -ge 6) { $retained += [int64]$Case.workers * $workerCapacity }
        if ($phase.id -ge 5) { $retained += $rowCapacity }
        if ($phase.id -ge 7) { $retained += $scratchCapacity }
        $components = [ordered]@{
            bucket_mapping_virtual = [ordered]@{ live_element_bytes = 8GB; retained_capacity_bytes = 8GB; grouping_only = $true }
            compact_row_metadata = [ordered]@{ live_element_bytes = $rowCapacity; retained_capacity_bytes = $rowCapacity; grouping_only = $false }
            coordinator_admission_scratch = [ordered]@{ live_element_bytes = $scratchCapacity; retained_capacity_bytes = $scratchCapacity; grouping_only = $false }
        }
        $worker = [ordered]@{ regret_delta_stream = [ordered]@{ live_element_bytes = $workerCapacity; retained_capacity_bytes = $workerCapacity }; continuation_delta_stream = [ordered]@{ live_element_bytes = 0; retained_capacity_bytes = 0 }; discovery_public_states = [ordered]@{ live_element_bytes = 0; retained_capacity_bytes = 0 }; discovery_rows = [ordered]@{ live_element_bytes = 0; retained_capacity_bytes = 0 }; action_menu_scratch = [ordered]@{ live_element_bytes = 0; retained_capacity_bytes = 0 } }
        $workers = if ($phase.id -ge 6) { @($worker) * $Case.workers } else { @() }
        $workerCount = if ($phase.id -ge 6) { $Case.workers } else { 0 }
        $subsystems += [ordered]@{ phase_id = $phase.id; phase = $phase.name; worker_count = $workerCount; live_bytes = $retained; retained_capacity_bytes = $retained; components = $components; workers = $workers }
    }
    return [ordered]@{
        evidence = [ordered]@{ schema_version = 7; producer = [ordered]@{ git_commit = 'synthetic-commit'; build_configuration = 'RelWithDebInfo'; compiler_identity = 'synthetic'; workflow_config_fingerprint = '1'; model_identity = '2'; bucket_artifact_schema = '4' } }
        instrumentation_kind = 'none'
        tracy_version = ''
        trace_capture_id = ''
        allocation_probe_valid = $true
        tracy_memory_events_enabled = $false
        batches = $Case.batches
        trajectories = [int64]$Case.batches * 1000
        accepted_trajectories = [int64]$Case.batches * 1000
        discarded_trajectories = 0
        deterministic_merge_fingerprint = [int64]$Fingerprint
        failed = $false
        capacity_exhaustion_stage = 0
        memory_process_snapshots = $process
        memory_subsystems = [ordered]@{ enabled = $true; completed = $true; snapshots = $subsystems }
    }
}

function Write-SyntheticMatrix {
    param([string]$Root, [string]$Mode = 'none', [string]$Mutation = '')
    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    $artifact = [ordered]@{ path = 'synthetic-buckets.bin'; bytes = 8000000000; last_write_time_utc = '2026-10-23T00:00:00.0000000Z'; source_config_sha256 = 'synthetic-config' }
    $cases = @($caseDefinitions)
    foreach ($case in $cases) {
        for ($repeat = 1; $repeat -le 3; ++$repeat) {
            $directory = Join-Path $Root (Join-Path $case.id $repeat.ToString())
            New-Item -ItemType Directory -Force -Path $directory | Out-Null
            $report = New-SyntheticReport $case $Mode
            $metrics = [ordered]@{ pinning = 'unpinned'; scheduler = $case.scheduler; workers = $case.workers; batches = $case.batches; trajectories_per_batch = 1000; build_configuration = 'RelWithDebInfo'; git_commit = 'synthetic-commit'; compiler_identity = 'synthetic'; artifact_identity = $artifact }
            $manifest = [ordered]@{ schema_version = 2; git_commit = 'synthetic-commit'; build_configuration = 'RelWithDebInfo'; compiler_identity = 'synthetic'; artifact = $artifact; workload = [ordered]@{ workers = $case.workers; batches = $case.batches; trajectories_per_batch = 1000; scheduler = $case.scheduler; pin_workers = $false } }
            if ($Mutation -eq 'missing_phase' -and $case.id -eq 'A1' -and $repeat -eq 1) { $report.memory_process_snapshots = @($report.memory_process_snapshots | Select-Object -First 12) }
            if ($Mutation -eq 'private_unavailable' -and $case.id -eq 'A1' -and $repeat -eq 1) { $report.memory_process_snapshots[0].availability.private_committed = $false }
            if ($Mutation -eq 'mismatched_fingerprint' -and $case.id -eq 'A1' -and $repeat -eq 1) { $report.deterministic_merge_fingerprint = 778 }
            if ($Mutation -eq 'mismatched_build' -and $case.id -eq 'A1' -and $repeat -eq 1) { $report.evidence.producer.build_configuration = 'Debug' }
            if ($Mutation -eq 'impossible_delta' -and $case.id -eq 'A1' -and $repeat -eq 1) { $report.memory_process_snapshots[3].peak_working_set_bytes = 1 }
            if ($Mutation -eq 'negative_derived_delta' -and $case.id -eq 'A1' -and $repeat -eq 1) { $report.memory_process_snapshots[3].current_working_set_bytes = 1; $report.memory_process_snapshots[3].peak_working_set_bytes = 1; $report.memory_process_snapshots[3].private_committed_bytes = 1 }
            $report | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $directory 'report.json') -Encoding UTF8
            $metrics | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory 'run_metrics.json') -Encoding UTF8
            $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory 'run_manifest.json') -Encoding UTF8
        }
        $tracyDirectory = Join-Path $Root (Join-Path $case.id 'tracy')
        New-Item -ItemType Directory -Force -Path $tracyDirectory | Out-Null
        $tracyReport = New-SyntheticReport $case $Mode
        $tracyReport.instrumentation_kind = 'tracy'
        $tracyReport.tracy_memory_events_enabled = $true
        $tracyReport.allocation_probe_valid = $false
        $tracyMetrics = [ordered]@{ build_configuration = 'RelWithDebInfo'; git_commit = 'synthetic-commit'; compiler_identity = 'synthetic'; artifact_identity = $artifact }
        $eventCount = if ($Mutation -eq 'missing_tracy_events') { 0 } else { 10 }
        $tracyManifest = [ordered]@{ schema_version = 2; tracy_version = '0.14.1'; git_commit = 'synthetic-commit'; build_configuration = 'RelWithDebInfo'; compiler_identity = 'synthetic'; tracy_memory_events_enabled = $true; artifact = $artifact; workload = [ordered]@{ workers = $case.workers; batches = $case.batches; trajectories_per_batch = 1000; scheduler = $case.scheduler; pin_workers = $false }; training = [ordered]@{ report_json = (Join-Path $tracyDirectory 'report.json'); metrics_json = (Join-Path $tracyDirectory 'run_metrics.json') }; memory_event_inspection = [ordered]@{ path = (Join-Path $tracyDirectory 'memory_events.json'); allocation_events = $eventCount; free_events = $eventCount } }
        if ($Mutation -eq 'tracy_workload_mismatch' -and $case.id -eq 'A1') { $tracyManifest.workload.batches = 9 }
        $tracyReport | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $tracyDirectory 'report.json') -Encoding UTF8
        $tracyMetrics | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $tracyDirectory 'run_metrics.json') -Encoding UTF8
        $tracyManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $tracyDirectory 'run_manifest.json') -Encoding UTF8
        [ordered]@{ allocation_events = $eventCount; free_events = $eventCount } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $tracyDirectory 'memory_events.json') -Encoding UTF8
    }
    [ordered]@{ schema_version = 1; repeats = 3; trajectories_per_batch = 1000; git_commit = 'synthetic-commit'; normal_build_configuration = 'RelWithDebInfo'; tracy_build_configuration = 'RelWithDebInfo'; tracy_capture_required = $true } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'matrix_manifest.json') -Encoding UTF8
}

function Invoke-AnalyzerSelfTest {
    $temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("texas-memory-analyzer-{0}" -f $PID)
    if (Test-Path -LiteralPath $temporaryRoot) { Remove-Item -LiteralPath $temporaryRoot -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
    try {
        $passingRoot = Join-Path $temporaryRoot 'passing'
        Write-SyntheticMatrix $passingRoot 'G'
        $passing = Invoke-MemoryMatrixAnalysis $passingRoot 3 1000 $true
        if ($passing.acceptance.status -ne 'pass' -or $passing.candidate_decision.selected_candidate -ne 'G') { throw 'self-test complete passing matrix failed' }
        foreach ($mutation in @('missing_phase', 'private_unavailable', 'mismatched_fingerprint', 'mismatched_build', 'tracy_workload_mismatch', 'missing_tracy_events', 'impossible_delta', 'negative_derived_delta')) {
            $root = Join-Path $temporaryRoot $mutation
            Write-SyntheticMatrix $root 'none' $mutation
            $result = Invoke-MemoryMatrixAnalysis $root 3 1000 $true
            if ($result.acceptance.status -ne 'fail') { throw "self-test did not reject $mutation" }
        }
        foreach ($candidate in @('F', 'G', 'H', 'I', 'J')) {
            $root = Join-Path $temporaryRoot ("candidate-$candidate")
            Write-SyntheticMatrix $root $candidate
            $result = Invoke-MemoryMatrixAnalysis $root 3 1000 $true
            if ($result.acceptance.status -ne 'pass' -or $result.candidate_decision.selected_candidate -ne $candidate) { throw "self-test candidate selection failed for $candidate" }
        }
        $noneRoot = Join-Path $temporaryRoot 'no-candidate'
        Write-SyntheticMatrix $noneRoot 'none'
        $none = Invoke-MemoryMatrixAnalysis $noneRoot 3 1000 $true
        if ($none.acceptance.status -ne 'pass' -or $none.candidate_decision.status -ne 'no_candidate_selected') { throw 'self-test no-candidate result failed' }
        Write-Output '[memory-analyzer] SELFTEST PASS: complete, missing, unavailable, mismatch, Tracy, derived-delta, candidate, and no-candidate fixtures'
    } finally {
        if (Test-Path -LiteralPath $temporaryRoot) { Remove-Item -LiteralPath $temporaryRoot -Recurse -Force }
    }
}

if ($SelfTest) {
    Invoke-AnalyzerSelfTest
    exit 0
}

if ([string]::IsNullOrWhiteSpace($InputDirectory)) { $InputDirectory = Join-Path $repoRoot 'tmp\memory-attribution\baseline' }
if (-not [System.IO.Path]::IsPathRooted($InputDirectory)) { $InputDirectory = Join-Path $repoRoot $InputDirectory }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = $InputDirectory }
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot $OutputDirectory }
if (-not (Test-Path -LiteralPath $InputDirectory -PathType Container)) { throw "Input directory not found: $InputDirectory" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$summary = Invoke-MemoryMatrixAnalysis $InputDirectory $Repeats $TrajectoriesPerBatch (-not $SkipTracy)
if (-not [string]::IsNullOrWhiteSpace($BaselineDirectory)) {
    if (-not [System.IO.Path]::IsPathRooted($BaselineDirectory)) { $BaselineDirectory = Join-Path $repoRoot $BaselineDirectory }
    if (-not (Test-Path -LiteralPath $BaselineDirectory -PathType Container)) { throw "Baseline directory not found: $BaselineDirectory" }
    $baselineSummary = Invoke-MemoryMatrixAnalysis $BaselineDirectory $BaselineRepeats $TrajectoriesPerBatch (-not $SkipTracy)
    $summary.qualification = Compare-MemoryQualification $baselineSummary $summary $BaselineDirectory $InputDirectory
}
Write-MemoryEvidence $summary $OutputDirectory
Write-Output "[memory-analyzer] summary=$OutputDirectory\memory_summary.json"
Write-Output "[memory-analyzer] samples=$OutputDirectory\memory_samples.csv"
Write-Output "[memory-analyzer] evidence=$OutputDirectory\evidence.md"
Write-Output "[memory-analyzer] acceptance=$($summary.acceptance.status) candidate_status=$($summary.candidate_decision.status) selected=$($summary.candidate_decision.selected_candidate)"
if (Test-Property $summary 'qualification') { Write-Output "[memory-analyzer] production_decision=$($summary.qualification.decision)" }
if ($summary.acceptance.status -ne 'pass') {
    throw "Memory matrix acceptance failed: $($summary.acceptance.failure_reasons -join '; ')"
}
if ((Test-Property $summary 'qualification') -and [string]$summary.qualification.decision -eq 'bounded_memory_qualification_fail') {
    throw "Bounded memory qualification failed: $($summary.qualification.failure_reasons -join '; ')"
}
