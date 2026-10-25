[CmdletBinding()]
param(
    [ValidateRange(1, 10)]
    [int]$Batches = 3,

    [ValidateRange(1, [int]::MaxValue)]
    [int]$TrajectoriesPerBatch = 200,

    [ValidateSet('pinned', 'unpinned')]
    [string]$Pinning = 'pinned',

    [ValidateSet('both', 'fixed', 'static-block', 'dynamic')]
    [string]$Scheduling = 'both',

    [ValidateRange(1, 20)]
    [int]$Repeats = 3,

    [string[]]$Workers = @('4', '8'),

    [ValidateRange(1, [int]::MaxValue)]
    [int]$DynamicChunkSize = 4,

    [ValidateSet('os-default', 'physical-compact', 'physical-spread', 'cpu-set-hint', 'diagnostic-hard-affinity')]
    [string]$PlacementPolicy = 'os-default',

    [ValidateSet('enabled', 'disabled')]
    [string]$CheckpointMode = 'enabled',

    [ValidateRange(0, 20)]
    [int]$WarmupRuns = 0,

    [ValidateSet('cold-first', 'warm-first')]
    [string]$WarmColdOrder = 'cold-first',

    [switch]$RandomizeCaseOrder,

    [ValidateRange(1, [long]::MaxValue)]
    [long]$CaseOrderSeed = 1,

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildConfiguration = 'Debug',

    [string]$BinaryPath = '',

    [string]$SourceConfigPath = '',

    [string]$OutputDirectory = ''
)

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runner = Join-Path $PSScriptRoot 'run_multiway_ram_1000.ps1'
$workerCounts = @(
    $Workers |
        ForEach-Object { $_ -split ',' } |
        ForEach-Object {
            $value = $_.Trim()
            if ([string]::IsNullOrWhiteSpace($value)) {
                throw 'Workers cannot contain empty values'
            }
            try {
                [int]$value
            } catch {
                throw "Invalid worker count: $value"
            }
        }
)
$pinningModes = @($Pinning)
$schedulingModes = if ($Scheduling -eq 'both') { @('fixed', 'dynamic') } else { @($Scheduling) }

if ($workerCounts.Count -eq 0 -or @($workerCounts | Where-Object { $_ -lt 1 -or $_ -gt 16 }).Count -ne 0) {
    throw 'Workers must contain values in the range 1..16'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot ("tmp\multiway_scheduler_{0}" -f ($Batches * $TrajectoriesPerBatch))
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Write-Output '[matrix] starting focused worker scheduler comparison'
Write-Output "[matrix] repository_root=$repoRoot"
Write-Output "[matrix] pinning=$($pinningModes -join ',') workers=$($workerCounts -join ',') scheduling=$($schedulingModes -join ',') repeats=$Repeats warmup_runs=$WarmupRuns warm_cold_order=$WarmColdOrder randomized_case_order=$RandomizeCaseOrder case_order_seed=$CaseOrderSeed batches=$Batches trajectories_per_batch=$TrajectoriesPerBatch"

$cases = @()
foreach ($pinningMode in $pinningModes) {
    foreach ($schedulerMode in $schedulingModes) {
        foreach ($workerCount in $workerCounts) {
            $case = [ordered]@{ pinning = $pinningMode; scheduler = $schedulerMode; workers = $workerCount }
            if ($WarmColdOrder -eq 'warm-first') {
                for ($warmup = 1; $warmup -le $WarmupRuns; ++$warmup) {
                    $cases += [pscustomobject]@{ pinning = $pinningMode; scheduler = $schedulerMode; workers = $workerCount; repeat = 0; warmup = $warmup; sample_kind = 'warmup' }
                }
            }
            for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
                $cases += [pscustomobject]@{ pinning = $pinningMode; scheduler = $schedulerMode; workers = $workerCount; repeat = $repeat; warmup = 0; sample_kind = 'measured' }
            }
            if ($WarmColdOrder -eq 'cold-first') {
                for ($warmup = 1; $warmup -le $WarmupRuns; ++$warmup) {
                    $cases += [pscustomobject]@{ pinning = $pinningMode; scheduler = $schedulerMode; workers = $workerCount; repeat = 0; warmup = $warmup; sample_kind = 'warmup' }
                }
            }
        }
    }
}
if ($RandomizeCaseOrder -and $cases.Count -gt 1) {
    $random = [System.Random]::new([int]($CaseOrderSeed % [int64][int]::MaxValue))
    for ($index = $cases.Count - 1; $index -gt 0; --$index) {
        $swapIndex = $random.Next($index + 1)
        $temporary = $cases[$index]
        $cases[$index] = $cases[$swapIndex]
        $cases[$swapIndex] = $temporary
    }
}

$results = @()
$matrixBinarySha256 = $null
$matrixBinaryProvenance = $null
$matrixBinaryPath = $null
for ($caseIndex = 0; $caseIndex -lt $cases.Count; ++$caseIndex) {
                $case = $cases[$caseIndex]
                $pinningMode = $case.pinning
                $schedulerMode = $case.scheduler
                $workerCount = [int]$case.workers
                $repeat = [int]$case.repeat
                $sampleKind = [string]$case.sample_kind
                $runLabel = if ($sampleKind -eq 'warmup') { "warmup_$($case.warmup)" } else { "repeat_$repeat" }
                $runDirectory = Join-Path $OutputDirectory ("{0}\{1}\workers_{2}\{3}" -f $pinningMode, $schedulerMode, $workerCount, $runLabel)
                Write-Output "[matrix] running case_index=$caseIndex sample_kind=$sampleKind pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat output_directory=$runDirectory"

                $runnerArguments = @(
                    '-NoProfile',
                    '-ExecutionPolicy', 'Bypass',
                    '-File', $runner,
                    '-Workers', $workerCount.ToString(),
                    '-Batches', $Batches.ToString(),
                    '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
                    '-Scheduler', $schedulerMode,
                    '-DynamicChunkSize', $DynamicChunkSize.ToString(),
                    '-PlacementPolicy', $PlacementPolicy,
                    '-CheckpointMode', $CheckpointMode,
                    '-BuildConfiguration', $BuildConfiguration,
                    '-RunDirectory', $runDirectory
                )
                if (-not [string]::IsNullOrWhiteSpace($BinaryPath)) {
                    $runnerArguments += @('-BinaryPath', $BinaryPath)
                }
                if (-not [string]::IsNullOrWhiteSpace($SourceConfigPath)) {
                    $runnerArguments += @('-SourceConfigPath', $SourceConfigPath)
                }
                if ($pinningMode -eq 'pinned') {
                    $runnerArguments += '-PinWorkers'
                }
                & powershell.exe @runnerArguments
                if ($LASTEXITCODE -ne 0) {
                    throw "Worker matrix run failed for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat with exit code $LASTEXITCODE"
                }

                $reportPath = Join-Path $runDirectory 'report.json'
                $metricsPath = Join-Path $runDirectory 'run_metrics.json'
                if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or -not (Test-Path -LiteralPath $metricsPath -PathType Leaf)) {
                    throw "Worker matrix artifacts were not created for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }

                $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
                $metrics = Get-Content -Raw -LiteralPath $metricsPath | ConvertFrom-Json
                $runBinarySha256 = [string]$metrics.binary_sha256
                $runBinaryProvenance = [string]$metrics.binary_source_commit + '|' + [string]$metrics.binary_dirty_state + '|' + [string]$metrics.binary_provenance.binary_compiler_identity + '|' + [string]$metrics.binary_provenance.binary_build_configuration
                if ([string]::IsNullOrWhiteSpace($runBinarySha256) -or [string]::IsNullOrWhiteSpace($runBinaryProvenance) -or [string]::IsNullOrWhiteSpace([string]$metrics.binary_path)) {
                    throw "Worker matrix provenance gate failed: incomplete binary identity for case_index=$caseIndex"
                }
                if ($null -eq $matrixBinarySha256) {
                    $matrixBinarySha256 = $runBinarySha256
                    $matrixBinaryProvenance = $runBinaryProvenance
                    $matrixBinaryPath = [string]$metrics.binary_path
                } elseif ($runBinarySha256 -ne $matrixBinarySha256 -or $runBinaryProvenance -ne $matrixBinaryProvenance) {
                    throw "Worker matrix provenance gate failed: expected sha256=$matrixBinarySha256 provenance=$matrixBinaryProvenance actual sha256=$runBinarySha256 provenance=$runBinaryProvenance"
                }
                $expectedTrajectories = $Batches * $TrajectoriesPerBatch
                if ($metrics.scheduler -ne $schedulerMode -or
                    [int]$metrics.dynamic_chunk_size -ne $DynamicChunkSize -or
                    [int]$metrics.chunk_size -ne $DynamicChunkSize -or
                    [string]$metrics.placement_policy -ne $PlacementPolicy -or
                    [string]$metrics.checkpoint_mode -ne $CheckpointMode) {
                    throw "Run metadata mismatch for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }
                if ($report.trajectories -ne $expectedTrajectories -or
                    $report.accepted_trajectories -ne $expectedTrajectories -or
                    $report.discarded_trajectories -ne 0 -or
                    $report.failed -or
                    $report.capacity_exhaustion_stage -ne 0) {
                    throw "Correctness gate failed for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }
                if ($report.evidence.schema_version -ne 7 -or
                    $report.instrumentation_kind -ne 'none' -or
                    -not $report.allocation_probe_valid) {
                    throw "Worker matrix requires an unprofiled schema 7 report for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }
                if ([string]$report.placement_policy -ne $PlacementPolicy -or
                    [int]$report.dynamic_chunk_size -ne $DynamicChunkSize -or
                    [int64]$report.export_result_payload_hash -eq 0 -or
                    [int64]$report.export_policy_content_hash -eq 0) {
                    throw "Qualification evidence mismatch for case_index=$caseIndex"
                }
                if ($report.peak_process_rss_bytes -gt $report.memory_preflight_estimate_bytes) {
                    throw "Peak RSS exceeded the memory preflight estimate for pinning=$pinningMode scheduler=$schedulerMode workers=$workerCount repeat=$repeat"
                }

                $activePerTrajectory = @()
                for ($worker = 0; $worker -lt $report.worker_active_nanoseconds_by_worker.Count; ++$worker) {
                    $active = [int64]$report.worker_active_nanoseconds_by_worker[$worker]
                    $count = [int64]$report.worker_trajectory_counts[$worker]
                    if ($count -gt 0) {
                        $activePerTrajectory += [double]$active / [double]$count
                    } else {
                        $activePerTrajectory += 0.0
                    }
                }
                $activeRates = @($activePerTrajectory | Where-Object { $_ -gt 0.0 })
                $activeImbalance = 0.0
                if ($activeRates.Count -gt 0) {
                    $activeMean = ($activeRates | Measure-Object -Average).Average
                    $activeImbalance = ($activeRates | Measure-Object -Maximum).Maximum / $activeMean - 1.0
                }
                $elapsed = [double]$report.elapsed_wall_nanoseconds
                $workerActive = [double]$report.worker_active_nanoseconds
                $barrierShare = if ($elapsed -gt 0.0) { [double]$report.barrier_wait_nanoseconds / $elapsed } else { 0.0 }
                $schedulerClaimShare = if ($workerActive -gt 0.0) { [double]$report.worker_scheduler_claim_nanoseconds / $workerActive } else { 0.0 }
                $lockShare = if ($workerActive -gt 0.0) { [double]$report.coordinator_lock_wait_nanoseconds / $workerActive } else { 0.0 }
                $mergeShare = if ($elapsed -gt 0.0) { [double]$report.merge_nanoseconds / $elapsed } else { 0.0 }
                $normalizedCpu = if ($elapsed -gt 0.0 -and $workerCount -gt 0) {
                    [double]$metrics.process_cpu_nanoseconds / ($elapsed * [double]$workerCount)
                } else { 0.0 }

                $results += [pscustomobject]@{
                    case_index = $caseIndex
                    sample_kind = $sampleKind
                    pinning = $pinningMode
                    scheduler = $schedulerMode
                    chunk_size = [int]$metrics.chunk_size
                    dynamic_chunk_size = [int]$metrics.dynamic_chunk_size
                    placement_policy = [string]$metrics.placement_policy
                    checkpoint_mode = [string]$metrics.checkpoint_mode
                    warm_cold_order = $WarmColdOrder
                    randomized_case_order = $RandomizeCaseOrder.IsPresent
                    case_order_seed = $CaseOrderSeed
                    workers = $workerCount
                    repeat = $repeat
                    batches = $Batches
                    trajectories_per_batch = $TrajectoriesPerBatch
                    trajectories = $report.trajectories
                    trajectories_per_second = $report.trajectories_per_second
                    elapsed_wall_nanoseconds = $report.elapsed_wall_nanoseconds
                    process_cpu_nanoseconds = $metrics.process_cpu_nanoseconds
                    worker_active_nanoseconds = $report.worker_active_nanoseconds
                    minimum_worker_active_nanoseconds = $report.minimum_worker_active_nanoseconds
                    maximum_worker_active_nanoseconds = $report.maximum_worker_active_nanoseconds
                    coordinator_wait_nanoseconds = $report.coordinator_wait_nanoseconds
                    barrier_wait_nanoseconds = $report.barrier_wait_nanoseconds
                    coordinator_lock_wait_nanoseconds = $report.coordinator_lock_wait_nanoseconds
                    delta_sort_nanoseconds = $report.delta_sort_nanoseconds
                    merge_nanoseconds = $report.merge_nanoseconds
                    coordinator_admission_nanoseconds = $report.coordinator_admission_nanoseconds
                    trainer_only_nanoseconds = $report.trainer_only_nanoseconds
                    training_wall_nanoseconds = $report.training_wall_nanoseconds
                    checkpoint_wall_nanoseconds = $report.checkpoint_wall_nanoseconds
                    export_wall_nanoseconds = $report.export_wall_nanoseconds
                    total_wrapper_wall_nanoseconds = $report.total_wrapper_wall_nanoseconds
                    process_cpu_nanoseconds_reported = $report.process_cpu_nanoseconds
                    peak_working_set_bytes = $report.peak_working_set_bytes
                    active_nanoseconds_per_trajectory = ($activePerTrajectory -join ',')
                    active_time_imbalance = $activeImbalance
                    barrier_share = $barrierShare
                    scheduler_claim_share = $schedulerClaimShare
                    lock_contention_share = $lockShare
                    serial_merge_share = $mergeShare
                    normalized_process_cpu_use = $normalizedCpu
                    peak_process_rss_bytes = $report.peak_process_rss_bytes
                    memory_preflight_estimate_bytes = $report.memory_preflight_estimate_bytes
                    worker_trajectory_counts = ($report.worker_trajectory_counts -join ',')
                    worker_active_nanoseconds_by_worker = ($report.worker_active_nanoseconds_by_worker -join ',')
                    worker_wake_latency_nanoseconds_by_worker = ($report.worker_wake_latency_nanoseconds_by_worker -join ',')
                    worker_cv_wait_nanoseconds_by_worker = ($report.worker_cv_wait_nanoseconds_by_worker -join ',')
                    worker_scheduler_claim_nanoseconds = $report.worker_scheduler_claim_nanoseconds
                    worker_scheduler_claim_nanoseconds_by_worker = ($report.worker_scheduler_claim_nanoseconds_by_worker -join ',')
                    worker_tail_idle_nanoseconds_by_worker = ($report.worker_tail_idle_nanoseconds_by_worker -join ',')
                    worker_allocation_counts = ($report.worker_allocation_counts -join ',')
                    worker_allocated_bytes = ($report.worker_allocated_bytes -join ',')
                    worker_resolved_processor = ($report.worker_resolved_processor -join ',')
                    worker_requested_cpu_set = ($report.worker_requested_cpu_set -join ',')
                    worker_resolved_cpu_set = ($report.worker_resolved_cpu_set -join ',')
                    worker_requested_physical_core = ($report.worker_requested_physical_core -join ',')
                    worker_resolved_physical_core = ($report.worker_resolved_physical_core -join ',')
                    worker_requested_cache_domain = ($report.worker_requested_cache_domain -join ',')
                    worker_resolved_cache_domain = ($report.worker_resolved_cache_domain -join ',')
                    worker_migration_sampled = ($report.worker_migration_sampled -join ',')
                    worker_migration_detected = ($report.worker_migration_detected -join ',')
                    worker_affinity_masks = ($report.worker_affinity_masks -join ',')
                    worker_affinity_succeeded = ($report.worker_affinity_succeeded -join ',')
                    migration_sampled = (@($report.worker_migration_sampled | ForEach-Object { [int64]$_ } | Measure-Object -Sum).Sum)
                    migration_detected = (@($report.worker_migration_detected | ForEach-Object { [int64]$_ } | Measure-Object -Sum).Sum)
                    minor_page_faults = $report.minor_page_faults
                    major_page_faults = $report.major_page_faults
                    total_page_faults = $report.total_page_faults
                    minor_page_faults_available = $report.authoritative_memory_availability.minor_page_faults
                    major_page_faults_available = $report.authoritative_memory_availability.major_page_faults
                    total_page_faults_available = $report.authoritative_memory_availability.total_page_faults
                    binary_sha256 = $runBinarySha256
                    binary_source_commit = [string]$metrics.binary_source_commit
                    binary_dirty_state = [string]$metrics.binary_dirty_state
                    binary_compiler_identity = [string]$metrics.binary_provenance.binary_compiler_identity
                    binary_build_configuration = [string]$metrics.binary_provenance.binary_build_configuration
                    binary_path = [string]$metrics.binary_path
                    export_result_payload_hash = $report.export_result_payload_hash
                    export_policy_content_hash = $report.export_policy_content_hash
                    fingerprint = $report.deterministic_merge_fingerprint
                    report_path = $reportPath
                }
}

$measuredResults = @($results | Where-Object { $_.sample_kind -eq 'measured' })
if ($measuredResults.Count -eq 0) {
    throw 'Worker matrix produced no measured samples'
}

$fingerprints = @($measuredResults | Select-Object -ExpandProperty fingerprint | Sort-Object -Unique)
if ($fingerprints.Count -ne 1 -or $fingerprints[0] -eq 0) {
    throw "Focused scheduler correctness gate failed: expected one non-zero merge fingerprint, got $($fingerprints -join ',')"
}

function Get-Median {
    param([object[]]$Values)
    $numbers = @($Values | ForEach-Object { [double]$_ } | Sort-Object)
    if ($numbers.Count -eq 0) { throw 'Cannot calculate a median for an empty sample set' }
    $middle = [int][Math]::Floor($numbers.Count / 2)
    if ($numbers.Count % 2 -eq 1) { return [double]$numbers[$middle] }
    return ([double]$numbers[$middle - 1] + [double]$numbers[$middle]) / 2.0
}

function Get-StandardDeviation {
    param([object[]]$Values)
    $numbers = @($Values | ForEach-Object { [double]$_ })
    if ($numbers.Count -le 1) { return 0.0 }
    $mean = ($numbers | Measure-Object -Average).Average
    $sum = 0.0
    foreach ($number in $numbers) { $sum += ([double]$number - [double]$mean) * ([double]$number - [double]$mean) }
    return [Math]::Sqrt($sum / [double]$numbers.Count)
}

$summaries = @()
foreach ($group in ($measuredResults | Group-Object { "$($_.pinning)|$($_.scheduler)|$($_.workers)|$($_.dynamic_chunk_size)|$($_.placement_policy)|$($_.checkpoint_mode)" })) {
    $throughputs = @($group.Group | Select-Object -ExpandProperty trajectories_per_second | Sort-Object)
    $median = Get-Median $throughputs
    $minimum = [double]($throughputs | Measure-Object -Minimum).Minimum
    $maximum = [double]($throughputs | Measure-Object -Maximum).Maximum
    $standardDeviation = Get-StandardDeviation $throughputs
    $coefficientOfVariation = if ($median -ne 0.0) { $standardDeviation / $median } else { 0.0 }
    $relativeRange = if ($median -ne 0.0) { ($maximum - $minimum) / $median } else { 0.0 }
    $summary = [pscustomobject]@{
        pinning = $group.Group[0].pinning
        scheduler = $group.Group[0].scheduler
        workers = $group.Group[0].workers
        dynamic_chunk_size = $group.Group[0].dynamic_chunk_size
        placement_policy = $group.Group[0].placement_policy
        checkpoint_mode = $group.Group[0].checkpoint_mode
        sample_count = $group.Group.Count
        throughput_median = $median
        throughput_min = $minimum
        throughput_max = $maximum
        throughput_standard_deviation = $standardDeviation
        throughput_coefficient_of_variation = $coefficientOfVariation
        throughput_relative_range = $relativeRange
        training_wall_median = Get-Median @($group.Group | ForEach-Object { $_.training_wall_nanoseconds })
        checkpoint_wall_median = Get-Median @($group.Group | ForEach-Object { $_.checkpoint_wall_nanoseconds })
        export_wall_median = Get-Median @($group.Group | ForEach-Object { $_.export_wall_nanoseconds })
        total_wrapper_wall_median = Get-Median @($group.Group | ForEach-Object { $_.total_wrapper_wall_nanoseconds })
        migration_sampled_median = Get-Median @($group.Group | ForEach-Object { $_.migration_sampled })
        migration_detected_median = Get-Median @($group.Group | ForEach-Object { $_.migration_detected })
        total_page_faults_median = Get-Median @($group.Group | ForEach-Object { $_.total_page_faults })
        minor_page_faults_median = Get-Median @($group.Group | ForEach-Object { $_.minor_page_faults })
        major_page_faults_median = Get-Median @($group.Group | ForEach-Object { $_.major_page_faults })
        binary_sha256 = $group.Group[0].binary_sha256
        binary_source_commit = $group.Group[0].binary_source_commit
        binary_dirty_state = $group.Group[0].binary_dirty_state
        binary_compiler_identity = $group.Group[0].binary_compiler_identity
        binary_build_configuration = $group.Group[0].binary_build_configuration
    }
    $summaries += $summary
    foreach ($row in $group.Group) {
        $row | Add-Member -NotePropertyName throughput_median -NotePropertyValue $median -Force
        $row | Add-Member -NotePropertyName throughput_min -NotePropertyValue $minimum -Force
        $row | Add-Member -NotePropertyName throughput_max -NotePropertyValue $maximum -Force
        $row | Add-Member -NotePropertyName throughput_standard_deviation -NotePropertyValue $standardDeviation -Force
        $row | Add-Member -NotePropertyName throughput_coefficient_of_variation -NotePropertyValue $coefficientOfVariation -Force
        $row | Add-Member -NotePropertyName throughput_relative_range -NotePropertyValue $relativeRange -Force
    }
}

$jsonPath = Join-Path $OutputDirectory 'worker_matrix.json'
$csvPath = Join-Path $OutputDirectory 'worker_matrix.csv'
$summaryPath = Join-Path $OutputDirectory 'worker_matrix_summary.json'
$results | Sort-Object case_index | Format-Table case_index, sample_kind, pinning, scheduler, workers, repeat, trajectories_per_second, throughput_median, throughput_standard_deviation, active_time_imbalance, barrier_share, scheduler_claim_share, lock_contention_share, serial_merge_share, normalized_process_cpu_use, peak_process_rss_bytes
$results | Sort-Object pinning, scheduler, workers, repeat | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$results | Sort-Object pinning, scheduler, workers, repeat | Export-Csv -LiteralPath $csvPath -NoTypeInformation
$matrixSummary = [ordered]@{
    schema_version = 1
    binary_identity = [ordered]@{
        sha256 = $matrixBinarySha256
        provenance = $matrixBinaryProvenance
        build_configuration = $BuildConfiguration
        binary_path = $matrixBinaryPath
    }
    protocol = [ordered]@{
        workers = $workerCounts
        scheduling = $schedulingModes
        pinning = $pinningModes
        dynamic_chunk_size = $DynamicChunkSize
        placement_policy = $PlacementPolicy
        checkpoint_mode = $CheckpointMode
        batches = $Batches
        trajectories_per_batch = $TrajectoriesPerBatch
        repeats = $Repeats
        warmup_runs = $WarmupRuns
        warm_cold_order = $WarmColdOrder
        randomized_case_order = $RandomizeCaseOrder.IsPresent
        case_order_seed = $CaseOrderSeed
    }
    measured_sample_count = $measuredResults.Count
    total_sample_count = $results.Count
    summaries = $summaries
}
$matrixSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Output "[matrix] JSON summary=$jsonPath"
Write-Output "[matrix] CSV summary=$csvPath"
Write-Output "[matrix] qualification summary=$summaryPath"
Write-Output "[matrix] PASS: $($measuredResults.Count) measured runs and $($results.Count - $measuredResults.Count) warmup runs completed with zero discards, bounded RSS, and one merge fingerprint"
