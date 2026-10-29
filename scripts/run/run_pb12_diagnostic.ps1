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
if ($plan.batches -ne 3 -or $plan.trajectories_per_batch -ne 1000 -or $plan.checkpoint_interval -ne 1 -or
    (Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash -ne $plan.config_sha256 -or
    $evidence.prepared_config_sha256 -ne $plan.config_sha256) {
    throw 'Prepared plan or configuration does not match the bounded diagnostic.'
}
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $evidence.binary_sha256) {
    throw 'Trainer binary changed after preparation; prepare a fresh diagnostic directory.'
}
$bucket = Get-Item -LiteralPath $bucketPath
if ($bucket.Length -ne [long]$evidence.bucket_bytes -or
    $bucket.LastWriteTimeUtc -ne ([DateTime]$evidence.bucket_last_write_utc).ToUniversalTime() -or
    -not $evidence.bucket_size_and_timestamp_match_stage94) {
    throw 'Bucket-file metadata changed after preparation.'
}
$requiredConfig = @{
    storage_backend = 'ExternalCompactInt32'; deterministic_seed = '1'; training_worker_count = '1'; scheduler_mode = 'fixed'
    trajectories_per_batch = '1000'; target_trajectories = '3000'; checkpoint_interval = '1'; batches = '3'; synchronous_durability = 'true'
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
    Write-Monitor ([ordered]@{ event = 'start'; utc = $started.ToString('o'); binary_sha256 = $binaryHash; config_sha256 = $plan.config_sha256; p99_cohorts_required = 3; planned_cycles = 2 })
    $arguments = '--config "{0}" --batches 3 --threads 1 --scheduler fixed --pb12-targeted-coverage --pb12-disk-amplification-bound --progress true --progress-every 1 --progress-memory true --progress-speed true' -f $config
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $repo -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runRoot 'stdout.log') -RedirectStandardError (Join-Path $runRoot 'stderr.log')
    $null = $process.Handle
    Write-Monitor ([ordered]@{ event = 'process'; pid = $process.Id; process_start_utc = $process.StartTime.ToUniversalTime().ToString('o') })
    while (-not $process.HasExited) {
        $loopStarted = [Diagnostics.Stopwatch]::GetTimestamp()
        $process.Refresh()
        if ($process.HasExited) { break }
        $now = [DateTime]::UtcNow
        if (($now - $started).TotalHours -ge 3) { $stopReason = 'three-hour diagnostic observation deadline reached'; break }
        $available = [Pb12DiagnosticMemoryStatus]::Available()
        $workingSet = [long]$process.WorkingSet64
        $privateCommit = [long]$process.PrivateMemorySize64
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
                $checkpoints++
                if ($null -ne $previousCheckpointUtc) {
                    $duration = ($after.LastWriteTimeUtc - $previousCheckpointUtc).TotalMilliseconds
                    if ($duration -le 0) { $stopReason = 'checkpoint cycle telemetry invalid'; break }
                    # Three batches produce at most two cycles. The P99 stop
                    # cannot be established, so do not attach stale disk sizes.
                    $cycles.Add([pscustomobject]@{ duration_ms = $duration; live_bytes = $null })
                }
                $previousCheckpointUtc = $after.LastWriteTimeUtc
                $lastStamp = $stamp
                $observedCheckpointGenerations[[string]$metadata.training_generation] = $true
                Write-Monitor ([ordered]@{ event = 'checkpoint_observed'; ordinal = $checkpoints; utc = $now.ToString('o'); checkpoint_write_utc = $after.LastWriteTimeUtc.ToString('o'); storage_generation = $metadata.training_generation; store_batch = $metadata.committed_batch })
                if ($stopReason) { break }
            }
        }
        $previousLoopMonitorNs = [long](([Diagnostics.Stopwatch]::GetTimestamp() - $loopStarted) * 1000000000.0 / [Diagnostics.Stopwatch]::Frequency)
        Start-Sleep -Milliseconds 250
    }
    if ($stopReason) { Stop-Pb12DiagnosticProcess $process }
    else { $process.WaitForExit() }
    $process.Refresh()
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
            -JournalComplete ($journalStarted -and $journalBatches -eq 3 -and $journalCheckpoints -eq 3 -and $journalState.Pending.Length -eq 0) `
            -DiskAndCheckpointObservationsComplete ($null -ne $lastDisk -and $checkpoints -eq 3 -and $cycles.Count -eq 2) `
            -JournalCheckpointsObserved $journalCheckpointsObserved
        $reportPath = Join-Path $runRoot 'report.json'
        if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
            if (-not $stopReason) { $stopReason = 'final training report is unavailable' }
        } else {
            $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
            Write-Monitor ([ordered]@{ event = 'report'; sha256 = (Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash; batches = $report.batches; accepted_trajectories = $report.accepted_trajectories; discarded_trajectories = $report.discarded_trajectories; failed = $report.failed })
            if (($report.batches -ne 3 -or $report.accepted_trajectories -ne 3000 -or $report.discarded_trajectories -ne 0 -or $report.failed) -and -not $stopReason) {
                $stopReason = 'final report does not show three clean batches'
            }
        }
    }
    Write-Monitor ([ordered]@{ event = 'end'; utc = [DateTime]::UtcNow.ToString('o'); reason = $stopReason; exit_code = $process.ExitCode; journal_batches = $journalBatches; journal_checkpoints = $journalCheckpoints; checkpoints_observed = $checkpoints; cycles_observed = $cycles.Count; p99_gate = 'inconclusive: fewer than three two-cycle cohorts' })
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
