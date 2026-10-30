param(
    [Parameter(Mandatory = $true)][string]$RunName
)

# Run only after separate authorization. This launches training.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunName -notmatch '^pb12_diagnostic_[a-zA-Z0-9_-]{1,64}$') { throw 'Invalid diagnostic run name.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runRoot = Join-Path (Join-Path $repo 'tmp') $RunName
$exe = Join-Path $repo 'build/Debug/texas_multiway_train.exe'
$config = Join-Path $runRoot 'training.conf'
$planPath = Join-Path $runRoot 'plan.json'
$evidencePath = Join-Path $runRoot 'preparation_evidence.json'
$store = Join-Path $runRoot 'store'
$checkpoint = Join-Path $runRoot 'checkpoints/latest.bin'
$monitorPath = Join-Path $runRoot 'monitor.jsonl'
. (Join-Path $PSScriptRoot 'pb12_diagnostic_process.ps1')
. (Join-Path $PSScriptRoot 'pb12_diagnostic_p99.ps1')
. (Join-Path $PSScriptRoot 'pb12_diagnostic_storage.ps1')
. (Join-Path $PSScriptRoot 'pb12_diagnostic_journal_tail.ps1')
$bucketPath = Join-Path $repo 'artifacts/f1_sizing_v1/buckets/buckets.bin'
foreach ($file in @($exe, $config, $planPath, $evidencePath, $bucketPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required input is missing: $file" }
}
$plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
$evidence = Get-Content -LiteralPath $evidencePath -Raw | ConvertFrom-Json
$expectedBatches = [int]$plan.batches
$targetProperty = $plan.PSObject.Properties['target_trajectories']
$profileProperty = $plan.PSObject.Properties['profile_id']
$expectedTrajectories = if ($null -ne $targetProperty) { [long]$targetProperty.Value } else { [long]$expectedBatches * 1000 }
$isF1Qualification = $null -ne $profileProperty -and $profileProperty.Value -eq 'F1-QUAL-10K-12-v1'
$evidenceConfigProperty = $evidence.PSObject.Properties['config_sha256']
if ($null -eq $evidenceConfigProperty) { $evidenceConfigProperty = $evidence.PSObject.Properties['prepared_config_sha256'] }
if ($expectedBatches -notin @(3, 10) -or $expectedTrajectories -ne ([long]$expectedBatches * 1000) -or
    $plan.trajectories_per_batch -ne 1000 -or $plan.checkpoint_interval -ne 1 -or
    (Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash -ne $plan.config_sha256 -or
    $null -eq $evidenceConfigProperty -or $evidenceConfigProperty.Value -ne $plan.config_sha256) {
    throw 'Prepared plan or configuration does not match the bounded diagnostic.'
}
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $evidence.binary_sha256) {
    throw 'Trainer binary changed after preparation; prepare a fresh diagnostic directory.'
}
$bucket = Get-Item -LiteralPath $bucketPath
if ($bucket.Length -ne [long]$evidence.bucket_bytes -or
    $bucket.LastWriteTimeUtc -ne ([DateTime]$evidence.bucket_last_write_utc).ToUniversalTime()) {
    throw 'Bucket-file metadata changed after preparation.'
}
if ($isF1Qualification -and (Get-FileHash -LiteralPath $bucketPath -Algorithm SHA256).Hash -ne $evidence.bucket_sha256) {
    throw 'F1 qualification bucket artifact changed after preparation.'
}
if (-not $isF1Qualification -and -not $evidence.bucket_size_and_timestamp_match_stage94) {
    throw 'Diagnostic bucket file does not match the preserved Stage 9.4 input.'
}
$requiredConfig = @{
    storage_backend = 'ExternalCompactInt32'; deterministic_seed = '1'; training_worker_count = '1'; scheduler_mode = 'fixed'
    trajectories_per_batch = '1000'; target_trajectories = "$expectedTrajectories"; checkpoint_interval = '1'; batches = "$expectedBatches"; synchronous_durability = 'true'
    public_catalog_cache_bytes = '4194304'; row_page_cache_bytes = '67108864'; maximum_wal_bytes = '67108864'
    segment_target_bytes = '67108864'; compaction_fan_in = '4'; compaction_scratch_bytes = '67108864'
    disk_reserve_bytes = '67108864'; disk_space_requirement_bytes = '1073741824'; process_memory_limit_bytes = '68719476736'
    input_path = 'artifacts/f1_sizing_v1/buckets/buckets.bin'; storage_directory = $store
    checkpoint_dir = (Join-Path $runRoot 'checkpoints'); output_path = (Join-Path $runRoot 'policy.mwfb')
    report_path = (Join-Path $runRoot 'report.json'); log_path = (Join-Path $runRoot 'training.log')
}
$configLines = Get-Content -LiteralPath $config
foreach ($key in $requiredConfig.Keys) {
    $found = @($configLines | Where-Object { $_ -match ('^' + [regex]::Escape($key) + '=') })
    if ($found.Count -ne 1 -or $found[0] -cne "$key=$($requiredConfig[$key])") { throw "Unsafe configuration: $key" }
}
if ($isF1Qualification) {
    $expectedBucketConfig = @{ profile_id = 'F1-QUAL-10K-12-v1'; flop_buckets = '12'; turn_buckets = '12'; river_buckets = '12' }
    foreach ($key in $expectedBucketConfig.Keys) {
        $found = @($configLines | Where-Object { $_ -match ('^' + [regex]::Escape($key) + '=') })
        if ($found.Count -ne 1 -or $found[0] -cne "$key=$($expectedBucketConfig[$key])") { throw "F1 qualification configuration mismatch: $key" }
    }
}
foreach ($name in @('store', 'checkpoints', 'policy.mwfb', 'report.json', 'training.log', 'stdout.log', 'stderr.log', 'monitor.jsonl')) {
    if (Test-Path -LiteralPath (Join-Path $runRoot $name)) { throw "Diagnostic output already exists: $name" }
}
if (@(Get-ChildItem -LiteralPath $runRoot -Filter 'training.log.diagnostics.*.jsonl' -File).Count -ne 0) { throw 'A diagnostic journal already exists.' }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Pb12DiagnosticMemoryStatus {
  [StructLayout(LayoutKind.Sequential)] public struct MEMORYSTATUSEX {
    public uint dwLength; public uint dwMemoryLoad; public ulong ullTotalPhys; public ulong ullAvailPhys;
    public ulong ullTotalPageFile; public ulong ullAvailPageFile; public ulong ullTotalVirtual;
    public ulong ullAvailVirtual; public ulong ullAvailExtendedVirtual;
  }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX status);
  public static ulong Available() {
    var s = new MEMORYSTATUSEX(); s.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
    if (!GlobalMemoryStatusEx(ref s)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    return s.ullAvailPhys;
  }
}
'@
if ([Pb12DiagnosticMemoryStatus]::Available() -lt 3GB) { throw 'Available physical memory is below 3 GiB.' }
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($runRoot))
if ($drive.AvailableFreeSpace -lt (1GB + 64MB + 64MB + 128MB + 64MB)) { throw 'Insufficient disk for the configured reserve and bounded storage operations.' }
$log = [IO.FileStream]::new($monitorPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
function Write-Monitor([object]$Event) {
    $line = ($Event | ConvertTo-Json -Compress -Depth 8) + "`n"
    $bytes = [Text.Encoding]::UTF8.GetBytes($line)
    $log.Write($bytes, 0, $bytes.Length)
    $log.Flush($true)
}
$process = $null
$diskJob = $null
$stopReason = $null
$lastDisk = $null
$diskStarted = $null
$lastDiskLaunch = [DateTime]::MinValue
$lastSample = [DateTime]::MinValue
$peakWorkingSetBytes = [long]0
$peakPrivateCommitBytes = [long]0
$minimumAvailablePhysicalBytes = [uint64]::MaxValue
$maximumDiskAmplification = [double]0
$previousLoopMonitorNs = [long]0
$lastStamp = ''
$previousCheckpointUtc = $null
$cycles = [Collections.Generic.List[object]]::new()
$checkpoints = 0
$seenManifests = @{}
$manifestIncompleteSince = $null
$started = [DateTime]::UtcNow
$binaryHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$journalState = @{ Offset = [long]0; Pending = '' }
$journalPath = $null
$journalStarted = $false
$journalBatches = 0
$journalCheckpoints = 0
$bucketSha256 = if ($isF1Qualification) { [string]$evidence.bucket_sha256 } else { $null }
$journalGeneration = [uint64]0
$journalCheckpointGenerations = @{}
$observedCheckpointGenerations = @{}
function Observe-Journal {
    if ($null -eq $script:journalPath) {
        $candidates = @(Get-ChildItem -LiteralPath $runRoot -Filter 'training.log.diagnostics.*.jsonl' -File)
        if ($candidates.Count -gt 1) { throw 'Multiple diagnostic journals appeared.' }
        if ($candidates.Count -eq 0) { return }
        $script:journalPath = $candidates[0].FullName
        Write-Monitor ([ordered]@{ event = 'journal'; path = $candidates[0].Name })
    }
    $events = @(Read-Pb12DiagnosticJournalTail $script:journalPath $journalState)
    foreach ($item in $events) {
        switch ($item.event) {
            'start' {
                if ($script:journalStarted -or $item.schema -ne 1 -or $item.workflow_batch -ne 0) { throw 'Invalid diagnostic journal start.' }
                $script:journalStarted = $true
            }
            'component_publication' {
                if (-not $script:journalStarted -or [long]$item.workflow_batch -ne ($script:journalBatches + 1) -or
                    [int]$item.component -lt 0 -or [int]$item.component -gt 2 -or [long]$item.records -le 0 -or $item.aggregate_committed) { throw 'Invalid tentative component publication.' }
            }
            'batch' {
                if (-not $script:journalStarted -or [long]$item.workflow_batch -ne ($script:journalBatches + 1) -or
                    [long]$item.invocation_batch -ne [long]$item.workflow_batch -or [uint64]$item.storage_generation -le $script:journalGeneration) { throw 'Invalid diagnostic batch sequence.' }
                $script:journalBatches++
                $script:journalGeneration = [uint64]$item.storage_generation
                Write-Monitor ([ordered]@{ event = 'journal_batch'; workflow_batch = $item.workflow_batch; storage_generation = $item.storage_generation; store_batch = $item.store_committed_batch })
            }
            'checkpoint' {
                if ([long]$item.workflow_batch -ne ($script:journalCheckpoints + 1) -or
                    [long]$item.workflow_batch -ne $script:journalBatches -or [uint64]$item.storage_generation -ne $script:journalGeneration) { throw 'Invalid diagnostic checkpoint sequence.' }
                $script:journalCheckpoints++
                $script:journalCheckpointGenerations[[string]$item.storage_generation] = [long]$item.workflow_batch
                Write-Monitor ([ordered]@{ event = 'journal_checkpoint'; workflow_batch = $item.workflow_batch; storage_generation = $item.storage_generation; store_batch = $item.store_committed_batch; checkpoint_ns = $item.checkpoint_ns })
            }
            default { throw 'Unknown diagnostic journal event.' }
        }
    }
}
try {
    Write-Monitor ([ordered]@{ event = 'start'; utc = $started.ToString('o'); binary_sha256 = $binaryHash; config_sha256 = $plan.config_sha256; target_trajectories = $expectedTrajectories; planned_batches = $expectedBatches; p99_cohorts_required = 3; bucket_sha256 = $bucketSha256; bucket_counts = if ($isF1Qualification) { '12/12/12' } else { '96/128/192' } })
    $arguments = '--config "{0}" --batches {1} --threads 1 --scheduler fixed --pb12-targeted-coverage --pb12-disk-amplification-bound --progress true --progress-every 1 --progress-memory true --progress-speed true' -f $config, $expectedBatches
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $repo -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runRoot 'stdout.log') -RedirectStandardError (Join-Path $runRoot 'stderr.log')
    $null = $process.Handle
    Write-Monitor ([ordered]@{ event = 'process'; pid = $process.Id; process_start_utc = $process.StartTime.ToUniversalTime().ToString('o') })
    while (-not $process.HasExited) {
        $loopStarted = [Diagnostics.Stopwatch]::GetTimestamp()
        $process.Refresh()
        if ($process.HasExited) { break }
        $now = [DateTime]::UtcNow
        $deadlineProperty = $plan.PSObject.Properties['observation_deadline_hours']
        $deadlineHours = if ($null -ne $deadlineProperty) { [double]$deadlineProperty.Value } else { 3.0 }
        if (($now - $started).TotalHours -ge $deadlineHours) { $stopReason = 'diagnostic observation deadline reached'; break }
        $available = [Pb12DiagnosticMemoryStatus]::Available()
        $workingSet = [long]$process.WorkingSet64
        $privateCommit = [long]$process.PrivateMemorySize64
        $peakWorkingSetBytes = [Math]::Max($peakWorkingSetBytes, $workingSet)
        $peakPrivateCommitBytes = [Math]::Max($peakPrivateCommitBytes, $privateCommit)
        $minimumAvailablePhysicalBytes = [Math]::Min($minimumAvailablePhysicalBytes, $available)
        $reason = Get-Pb12DiagnosticMemoryStop $workingSet $privateCommit $available
        if ($reason) {
            $stopReason = $reason
            Stop-Pb12DiagnosticProcess $process
            Write-Monitor ([ordered]@{ event = 'memory_stop'; utc = $now.ToString('o'); reason = $reason; working_set_bytes = $workingSet; private_commit_bytes = $privateCommit; available_physical_bytes = $available })
            break
        }
        if (($now - $lastSample).TotalSeconds -ge 1) {
            Write-Monitor ([ordered]@{ event = 'resources'; utc = $now.ToString('o'); working_set_bytes = $workingSet; private_commit_bytes = $privateCommit; available_physical_bytes = $available; previous_loop_monitor_ns = $previousLoopMonitorNs })
            $lastSample = $now
        }
        Observe-Journal
        $missingPreviouslySeen = 0
        $missingPreviouslySeenNames = [Collections.Generic.List[string]]::new()
        foreach ($name in @('TRAINING_CURRENT', 'catalog/CURRENT', 'rows/CURRENT', 'pages/CURRENT')) {
            if (Test-Path -LiteralPath (Join-Path $store $name) -PathType Leaf) { $seenManifests[$name] = $true }
            elseif ($seenManifests.ContainsKey($name)) { $missingPreviouslySeen++; $missingPreviouslySeenNames.Add($name) }
        }
        $storeExists = Test-Path -LiteralPath $store -PathType Container
        $trainingManifestExists = Test-Path -LiteralPath (Join-Path $store 'TRAINING_CURRENT') -PathType Leaf
        $storeHasFiles = $false
        if ($storeExists -and -not $trainingManifestExists) {
            $iterator = [IO.Directory]::EnumerateFiles($store, '*', [IO.SearchOption]::AllDirectories).GetEnumerator()
            try { $storeHasFiles = $iterator.MoveNext() } finally { $iterator.Dispose() }
        }
        if ($trainingManifestExists) { $manifestIncompleteSince = $null }
        elseif ($storeHasFiles) {
            if ($null -eq $manifestIncompleteSince) { $manifestIncompleteSince = $now }
        }
        else { $manifestIncompleteSince = $null }
        $incompleteSeconds = if ($null -eq $manifestIncompleteSince) { 0 } else { ($now - $manifestIncompleteSince).TotalSeconds }
        $reason = Get-Pb12ManifestAvailabilityStop $missingPreviouslySeen $storeHasFiles $trainingManifestExists $incompleteSeconds
        if ($reason) {
            $stopReason = $reason
            Stop-Pb12DiagnosticProcess $process
            Write-Monitor ([ordered]@{ event = 'manifest_stop'; utc = $now.ToString('o'); reason = $reason; missing_previously_seen = @($missingPreviouslySeenNames); store_has_files = $storeHasFiles; incomplete_seconds = $incompleteSeconds })
            break
        }
        if ($null -ne $diskJob) {
            if ($diskJob.State -eq 'Completed') {
                $result = @(Receive-Job -Job $diskJob -ErrorAction Stop)
                Remove-Job -Job $diskJob
                $diskJob = $null
                if ($result.Count -ne 1 -or [long]$result[0].live_bytes -le 0) { $stopReason = 'disk telemetry unavailable'; break }
                $lastDisk = $result[0]
                $ratio = [double]$lastDisk.store_bytes / [double]$lastDisk.live_bytes
                $maximumDiskAmplification = [Math]::Max($maximumDiskAmplification, $ratio)
                # A run_*.tmp file alone does not prove that compaction is in flight.
                $reason = Get-Pb12DiagnosticDiskStop ([long]$lastDisk.live_bytes) ([long]$lastDisk.store_bytes) ([long]$lastDisk.compaction_temp_bytes) $false
                if ($reason) { $stopReason = $reason; Stop-Pb12DiagnosticProcess $process }
                Write-Monitor ([ordered]@{ event = 'disk'; utc = $now.ToString('o'); live_bytes = $lastDisk.live_bytes; store_bytes = $lastDisk.store_bytes; temporary_bytes = $lastDisk.compaction_temp_bytes; amplification = $ratio; scan_elapsed_ms = ($now - $diskStarted).TotalMilliseconds; stop_reason = $reason })
                if ($reason) { break }
            } elseif ($diskJob.State -notin @('NotStarted', 'Running') -or ($now - $diskStarted).TotalSeconds -gt 30) {
                $stopReason = 'disk telemetry failed or exceeded its observation deadline'
                $jobState = [string]$diskJob.State
                $jobErrors = @($diskJob.ChildJobs | ForEach-Object { $_.Error | ForEach-Object { $_.ToString() } })
                Stop-Pb12DiagnosticProcess $process
                Write-Monitor ([ordered]@{ event = 'disk_monitor_stop'; utc = $now.ToString('o'); reason = $stopReason; job_state = $jobState; elapsed_ms = ($now - $diskStarted).TotalMilliseconds; errors = $jobErrors })
                break
            }
        }
        if ($trainingManifestExists -and $null -eq $diskJob -and ($now - $lastDiskLaunch).TotalSeconds -ge 1) {
            $diskStarted = $now
            $lastDiskLaunch = $now
            $helper = Join-Path $PSScriptRoot 'pb12_diagnostic_storage.ps1'
            $diskJob = Start-Job -ScriptBlock { param($helperPath, $storePath); . $helperPath; Get-StableExternalStorageUsage $storePath -AllowAbsentComponents } -ArgumentList $helper, $store
        }
        if (Test-Path -LiteralPath $checkpoint -PathType Leaf) {
            $info = Get-Item -LiteralPath $checkpoint
            $stamp = '{0}:{1}' -f $info.Length, $info.LastWriteTimeUtc.Ticks
            if ($stamp -ne $lastStamp) {
                $metadata = Get-Pb12CheckpointHeader $checkpoint
                $after = Get-Item -LiteralPath $checkpoint
                if ($after.Length -ne $info.Length -or $after.LastWriteTimeUtc -ne $info.LastWriteTimeUtc) { $stopReason = 'checkpoint changed during observation'; break }
                $checkpointUsage = Get-StableExternalStorageUsage $store
                $checkpointAmplification = [double]$checkpointUsage.store_bytes / [double]$checkpointUsage.live_bytes
                $maximumDiskAmplification = [Math]::Max($maximumDiskAmplification, $checkpointAmplification)
                $checkpointDiskStop = Get-Pb12DiagnosticDiskStop ([long]$checkpointUsage.live_bytes) ([long]$checkpointUsage.store_bytes) ([long]$checkpointUsage.compaction_temp_bytes) $false
                Write-Monitor ([ordered]@{ event = 'checkpoint_size'; ordinal = ($checkpoints + 1); storage_generation = $metadata.training_generation; live_bytes = $checkpointUsage.live_bytes; store_bytes = $checkpointUsage.store_bytes; amplification = $checkpointAmplification })
                if ($checkpointDiskStop) { $stopReason = $checkpointDiskStop; Stop-Pb12DiagnosticProcess $process; break }
                $checkpoints++
                Write-Monitor ([ordered]@{ event = 'checkpoint_observed'; ordinal = $checkpoints; utc = $now.ToString('o'); checkpoint_write_utc = $after.LastWriteTimeUtc.ToString('o'); storage_generation = $metadata.training_generation; store_batch = $metadata.committed_batch; live_bytes = $checkpointUsage.live_bytes })
                if ($null -ne $previousCheckpointUtc) {
                    $duration = ($after.LastWriteTimeUtc - $previousCheckpointUtc).TotalMilliseconds
                    if ($duration -le 0) { $stopReason = 'checkpoint cycle telemetry invalid'; break }
                    $cycles.Add([pscustomobject]@{ duration_ms = $duration; live_bytes = [long]$checkpointUsage.live_bytes })
                    Write-Monitor ([ordered]@{ event = 'checkpoint_cycle'; ordinal = $cycles.Count; duration_ms = $duration; live_bytes = [long]$checkpointUsage.live_bytes })
                    $p99Stop = Get-Pb12DiagnosticP99Stop $cycles.ToArray()
                    if ($null -ne $p99Stop) {
                        $stopReason = $p99Stop.reason
                        Write-Monitor ([ordered]@{ event = 'p99_stop'; utc = $now.ToString('o'); reason = $stopReason; evidence = $p99Stop.evidence })
                        Stop-Pb12DiagnosticProcess $process
                        break
                    }
                }
                $previousCheckpointUtc = $after.LastWriteTimeUtc
                $lastStamp = $stamp
                $observedCheckpointGenerations[[string]$metadata.training_generation] = $true
                if ($stopReason) { break }
            }
        }
        $previousLoopMonitorNs = [long](([Diagnostics.Stopwatch]::GetTimestamp() - $loopStarted) * 1000000000.0 / [Diagnostics.Stopwatch]::Frequency)
        Start-Sleep -Milliseconds 250
    }
    if ($stopReason) { Stop-Pb12DiagnosticProcess $process }
    else { $process.WaitForExit() }
    $process.Refresh()
    if ($isF1Qualification) {
        $bucketHashAfter = (Get-FileHash -LiteralPath $bucketPath -Algorithm SHA256).Hash
        $bucketUnchanged = $bucketHashAfter -eq $evidence.bucket_sha256
        Write-Monitor ([ordered]@{ event = 'bucket_artifact_integrity'; unchanged = $bucketUnchanged; sha256 = $bucketHashAfter })
        if (-not $bucketUnchanged -and -not $stopReason) { $stopReason = 'bucket artifact changed during qualification' }
    }
    if ($null -ne $stopReason) {
        try {
            do {
                $oldOffset = $journalState.Offset
                Observe-Journal
            } while ($journalState.Offset -gt $oldOffset)
        } catch {
            Write-Monitor ([ordered]@{ event = 'journal_incomplete_after_stop'; error = $_.Exception.Message })
        }
    }
    if ($null -eq $stopReason) {
        do {
            $oldOffset = $journalState.Offset
            Observe-Journal
        } while ($journalState.Offset -gt $oldOffset)
        $journalCheckpointsObserved = $true
        foreach ($generation in $journalCheckpointGenerations.Keys) {
            if (-not $observedCheckpointGenerations.ContainsKey($generation)) {
                $journalCheckpointsObserved = $false
                break
            }
        }
        $stopReason = Get-Pb12DiagnosticCompletionStop -ExitCode $process.ExitCode `
            -ExecutableUnchanged ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $binaryHash) `
            -JournalComplete ($journalStarted -and $journalBatches -eq $expectedBatches -and $journalCheckpoints -eq $expectedBatches -and $journalState.Pending.Length -eq 0) `
            -DiskAndCheckpointObservationsComplete ($null -ne $lastDisk -and $checkpoints -eq $expectedBatches -and $cycles.Count -eq ($expectedBatches - 1)) `
            -JournalCheckpointsObserved $journalCheckpointsObserved
        $reportPath = Join-Path $runRoot 'report.json'
        if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
            if (-not $stopReason) { $stopReason = 'final training report is unavailable' }
        } else {
            $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
            $policyInfo = Get-Item -LiteralPath (Join-Path $runRoot 'policy.mwfb') -ErrorAction SilentlyContinue
            Write-Monitor ([ordered]@{ event = 'report'; sha256 = (Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash; batches = $report.batches; trajectories = $report.trajectories; accepted_trajectories = $report.accepted_trajectories; discarded_trajectories = $report.discarded_trajectories; failed = $report.failed; model_identity = $report.evidence.producer.model_identity; bucket_model_hash = $report.evidence.producer.bucket_model_hash; checkpoint_write_nanoseconds = $report.checkpoint_write_nanoseconds })
            if (($report.batches -ne $expectedBatches -or $report.trajectories -ne $expectedTrajectories -or
                    $report.accepted_trajectories -ne $expectedTrajectories -or $report.discarded_trajectories -ne 0 -or
                    $report.failed -or [uint64]$report.evidence.producer.bucket_model_hash -eq [uint64]0 -or
                    [uint64]$report.checkpoint_write_nanoseconds -eq [uint64]0 -or
                    $null -eq $policyInfo -or $policyInfo.Length -le 0) -and -not $stopReason) {
                $stopReason = 'final report does not show the configured clean trajectory count'
            }
        }
    }
    $p99Gate = if ($isF1Qualification -and $cycles.Count -ge 6 -and $null -eq (Get-Pb12DiagnosticP99Stop $cycles.ToArray())) { 'pass: three or more cohorts completed without three increasing cohorts' } elseif ($isF1Qualification) { 'fail: insufficient cohorts or increasing P99 cohorts' } else { 'inconclusive: bounded three-batch diagnostic' }
    if ($isF1Qualification -and $null -eq $stopReason -and $cycles.Count -lt 6) { $stopReason = 'fewer than three complete checkpoint-cycle cohorts' }
    Write-Monitor ([ordered]@{ event = 'end'; utc = [DateTime]::UtcNow.ToString('o'); reason = $stopReason; exit_code = $process.ExitCode; journal_batches = $journalBatches; journal_checkpoints = $journalCheckpoints; checkpoints_observed = $checkpoints; cycles_observed = $cycles.Count; p99_gate = $p99Gate; peak_working_set_bytes = $peakWorkingSetBytes; peak_private_commit_bytes = $peakPrivateCommitBytes; minimum_available_physical_bytes = $minimumAvailablePhysicalBytes; maximum_disk_amplification = $maximumDiskAmplification })
} catch {
    if ($null -ne $process) { Stop-Pb12DiagnosticProcess $process }
    try { Write-Monitor ([ordered]@{ event = 'monitor_failure'; utc = [DateTime]::UtcNow.ToString('o'); error = $_.Exception.Message }) } catch { }
    throw
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Pb12DiagnosticProcess $process
            try { Write-Monitor ([ordered]@{ event = 'monitor_interrupted'; utc = [DateTime]::UtcNow.ToString('o') }) } catch { }
        }
    }
    if ($null -ne $diskJob) { Stop-Job -Job $diskJob -ErrorAction SilentlyContinue; Remove-Job -Job $diskJob -Force -ErrorAction SilentlyContinue }
    $log.Dispose()
}
if ($stopReason) { throw "Diagnostic stopped or incomplete: $stopReason. Evidence is in $runRoot." }
