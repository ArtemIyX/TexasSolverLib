# Process identity and resource checks for the PB-12 diagnostic monitor.
# This file defines functions only; it does not launch or stop a process.

function Get-Pb12DiagnosticProcess([int]$ProcessId, [string]$ExpectedExecutable,
    [DateTime]$ExpectedStartUtc) {
    $process = Get-Process -Id $ProcessId -ErrorAction Stop
    $actualExecutable = [IO.Path]::GetFullPath($process.Path)
    $expectedPath = [IO.Path]::GetFullPath($ExpectedExecutable)
    if (-not [string]::Equals($actualExecutable, $expectedPath, [StringComparison]::OrdinalIgnoreCase) -or
        $process.StartTime.ToUniversalTime() -ne $ExpectedStartUtc.ToUniversalTime()) {
        throw 'Diagnostic process identity does not match the launched trainer.'
    }
    $null = $process.Handle
    return $process
}

function Stop-Pb12DiagnosticProcess([Diagnostics.Process]$Process) {
    # The caller must pass the process object obtained from its own launch.
    # Kill through its process handle, not a name search or a newly resolved PID.
    $Process.Refresh()
    if (-not $Process.HasExited) {
        $Process.Kill()
        if (-not $Process.WaitForExit(10000)) { throw 'Diagnostic process did not exit after termination.' }
    }
}

function Get-Pb12DiagnosticMemoryStop([long]$WorkingSetBytes, [long]$PrivateCommitBytes,
    [uint64]$AvailablePhysicalBytes) {
    if ($WorkingSetBytes -gt 40GB) { return 'working set exceeded 40 GiB' }
    if ($PrivateCommitBytes -gt 48GB) { return 'private commit exceeded 48 GiB' }
    if ($AvailablePhysicalBytes -lt 3GB) { return 'available physical memory fell below 3 GiB' }
    return $null
}

function Get-Pb12DiagnosticCompletionStop([int]$ExitCode, [bool]$ExecutableUnchanged,
    [bool]$JournalComplete, [bool]$DiskAndCheckpointObservationsComplete,
    [bool]$JournalCheckpointsObserved) {
    if ($ExitCode -ne 0) { return 'trainer exited unsuccessfully' }
    if (-not $ExecutableUnchanged) { return 'trainer executable changed during diagnostic' }
    if (-not $JournalComplete) { return 'incomplete diagnostic journal; no completed-run conclusion' }
    if (-not $DiskAndCheckpointObservationsComplete) {
        return 'incomplete disk or checkpoint observations; no completed-run conclusion'
    }
    if (-not $JournalCheckpointsObserved) { return 'a journal checkpoint was not observed by the monitor' }
    return $null
}

function Get-Pb12DiagnosticDiskStop([long]$LiveBytes, [long]$StoredBytes,
    [long]$CompactionTempBytes, [bool]$BoundedCompactionConfirmed) {
    if ($LiveBytes -le 0 -or $StoredBytes -lt 0 -or $CompactionTempBytes -lt 0) {
        throw 'Disk amplification inputs are unavailable or invalid.'
    }
    if (([double]$StoredBytes / [double]$LiveBytes) -gt 3 -and
        (-not $BoundedCompactionConfirmed -or $CompactionTempBytes -eq 0 -or $CompactionTempBytes -gt 128MB)) {
        return 'disk amplification exceeded 3x outside a bounded compaction temporary'
    }
    return $null
}

function Get-Pb12ManifestAvailabilityStop([int]$MissingPreviouslySeen, [bool]$StoreHasFiles,
    [bool]$TrainingManifestExists, [double]$IncompleteSeconds) {
    if ($MissingPreviouslySeen -gt 0) { return 'storage manifest disappeared' }
    if ($StoreHasFiles -and -not $TrainingManifestExists -and $IncompleteSeconds -gt 30) {
        return 'storage files exist without a training manifest; disk telemetry unavailable'
    }
    return $null
}
