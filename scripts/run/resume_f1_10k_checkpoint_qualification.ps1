param(
    [Parameter(Mandatory = $true)][string]$RunName,
    [string]$ResumeName = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunName -notmatch '^pb12_diagnostic_f1_(10k|50k)_[a-zA-Z0-9_-]{1,48}$') { throw 'Invalid F1 qualification run name.' }
if ($ResumeName -and $ResumeName -notmatch '^[a-zA-Z0-9_-]{1,48}$') { throw 'Invalid resume attempt name.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'pb12_diagnostic_process.ps1')
$runRoot = Join-Path (Join-Path $repo 'tmp') $RunName
$planPath = Join-Path $runRoot 'plan.json'
$configPath = Join-Path $runRoot 'training.conf'
$exe = Get-Pb12DiagnosticTrainerExecutable $repo
$reportPath = Join-Path $runRoot 'report.json'
$policyPath = Join-Path $runRoot 'policy.mwfb'
$checkpointPath = Join-Path $runRoot 'checkpoints/latest.bin'
$storePath = Join-Path $runRoot 'store'
$monitorPath = Join-Path $runRoot 'monitor.jsonl'
foreach ($path in @($exe, $planPath, $configPath, $reportPath, $policyPath, $checkpointPath, $monitorPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Completed qualification input is missing: $path" }
}
$plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
$runTarget = if ($RunName -match '^pb12_diagnostic_f1_10k_') { [long]10000 } else { [long]50000 }
$profileByTarget = @{ 10000 = 'F1-QUAL-10K-12-v1'; 50000 = 'F1-QUAL-50K-12-v1' }
$expectedTarget = [long]$plan.target_trajectories
$expectedBatches = [int]$plan.batches
if ($expectedTarget -ne $runTarget -or $expectedBatches -ne ($runTarget / 1000) -or
    $plan.trajectories_per_batch -ne 1000 -or $plan.checkpoint_interval -ne 1 -or
    $plan.profile_id -ne $profileByTarget[[int]$runTarget] -or
    (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash -ne $plan.config_sha256) {
    throw 'Qualification plan or training configuration does not match the named F1 profile.'
}
$finalMonitorLine = Get-Content -LiteralPath $monitorPath -Tail 1 | ConvertFrom-Json
if ($finalMonitorLine.event -ne 'end' -or $finalMonitorLine.reason -or $finalMonitorLine.exit_code -ne 0 -or
    $finalMonitorLine.p99_gate -notlike 'pass:*' -or $finalMonitorLine.checkpoints_observed -ne $expectedBatches -or
    $finalMonitorLine.cycles_observed -ne ($expectedBatches - 1) -or $finalMonitorLine.peak_working_set_bytes -gt 40GB -or
    $finalMonitorLine.peak_private_commit_bytes -gt 48GB -or $finalMonitorLine.minimum_available_physical_bytes -lt 3GB -or
    $finalMonitorLine.maximum_disk_amplification -gt 3) {
    throw 'Training report did not pass the checkpoint-cycle, memory, and disk gates.'
}
$sourceReport = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if ($sourceReport.batches -ne $expectedBatches -or $sourceReport.trajectories -ne $expectedTarget -or
    $sourceReport.accepted_trajectories -ne $expectedTarget -or
    $sourceReport.discarded_trajectories -ne 0 -or $sourceReport.failed) {
    throw "Training report does not show $expectedTarget clean trajectories."
}
$outputRoot = if ($ResumeName) { Join-Path $runRoot "resume_attempt_$ResumeName" } else { $runRoot }
if ($ResumeName -and (Test-Path -LiteralPath $outputRoot)) { throw "Resume attempt already exists: $outputRoot" }
$resumeRoot = Join-Path $outputRoot 'resume_snapshot'
$snapshotStore = Join-Path $resumeRoot 'store'
$resumeCheckpointDir = Join-Path $outputRoot 'resume_checkpoints'
$resumePolicy = Join-Path $outputRoot 'policy_resumed.mwfb'
$resumeReportPath = Join-Path $outputRoot 'resume_report.json'
$resumeLogPath = Join-Path $outputRoot 'resume_training.log'
foreach ($path in @($resumeRoot, $resumeCheckpointDir, $resumePolicy, $resumeReportPath, $resumeLogPath)) {
    if (Test-Path -LiteralPath $path) { throw "Resume output already exists: $path" }
}
$storeBytes = [int64]0
foreach ($file in [IO.Directory]::EnumerateFiles($storePath, '*', [IO.SearchOption]::AllDirectories)) {
    $storeBytes += [int64](Get-Item -LiteralPath $file -ErrorAction Stop).Length
}
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($runRoot))
$diskPreflight = [long]($storeBytes + 1073741824 + 5 * 67108864)
if ($drive.AvailableFreeSpace -lt $diskPreflight) { throw 'Insufficient disk for an isolated External store snapshot and resume export.' }

if ($ResumeName) { $null = New-Item -ItemType Directory -Path $outputRoot }
$resumeConfig = Join-Path $outputRoot 'resume.conf'
$configLines = foreach ($line in Get-Content -LiteralPath (Join-Path $runRoot 'training.conf')) {
    if ($line -match '^log_path=') { "log_path=$resumeLogPath" } else { $line }
}
[IO.File]::WriteAllLines($resumeConfig, [string[]]$configLines, [Text.UTF8Encoding]::new($false))
$null = New-Item -ItemType Directory -Path $resumeRoot
Copy-Item -LiteralPath $storePath -Destination $snapshotStore -Recurse
$null = New-Item -ItemType Directory -Path $resumeCheckpointDir

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class F1ResumeMemoryStatus {
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
$processArguments = @('--config', $resumeConfig, '--batches', '0', '--threads', '1', '--scheduler', 'fixed',
    '--resume', $checkpointPath, '--checkpoint-dir', $resumeCheckpointDir, '--output', $resumePolicy,
    '--report', $resumeReportPath, '--external-storage-snapshot-dir', $snapshotStore,
    '--progress', 'false', '--progress-memory', 'true', '--progress-speed', 'true')
$quotedArgs = @($processArguments | ForEach-Object { '"' + ([string]$_).Replace('"', '\"') + '"' })
$process = Start-Process -FilePath $exe -ArgumentList ($quotedArgs -join ' ') -WorkingDirectory $repo -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $outputRoot 'resume_stdout.log') -RedirectStandardError (Join-Path $outputRoot 'resume_stderr.log')
$null = $process.Handle
$peakWorkingSet = [long]0
$peakPrivateCommit = [long]0
$minimumAvailable = [uint64]::MaxValue
$stopReason = $null
try {
    $process.Refresh()
    $available = [F1ResumeMemoryStatus]::Available()
    $peakWorkingSet = [long]$process.WorkingSet64
    $peakPrivateCommit = [long]$process.PrivateMemorySize64
    $minimumAvailable = $available
    if ($peakWorkingSet -gt 40GB) { $stopReason = 'working set exceeded 40 GiB' }
    if ($peakPrivateCommit -gt 48GB) { $stopReason = 'private commit exceeded 48 GiB' }
    if ($available -lt 3GB) { $stopReason = 'available physical memory fell below 3 GiB' }
    while (-not $stopReason -and -not $process.WaitForExit(250)) {
        $process.Refresh()
        $available = [F1ResumeMemoryStatus]::Available()
        $workingSet = [long]$process.WorkingSet64
        $privateCommit = [long]$process.PrivateMemorySize64
        $peakWorkingSet = [Math]::Max($peakWorkingSet, $workingSet)
        $peakPrivateCommit = [Math]::Max($peakPrivateCommit, $privateCommit)
        $minimumAvailable = [Math]::Min($minimumAvailable, $available)
        if ($workingSet -gt 40GB) { $stopReason = 'working set exceeded 40 GiB'; break }
        if ($privateCommit -gt 48GB) { $stopReason = 'private commit exceeded 48 GiB'; break }
        if ($available -lt 3GB) { $stopReason = 'available physical memory fell below 3 GiB'; break }
    }
    if ($stopReason) {
        $process.Kill()
        $null = $process.WaitForExit(10000)
    } else { $process.WaitForExit() }
} finally {
    $process.Refresh()
    if (-not $process.HasExited) { $process.Kill(); $null = $process.WaitForExit(10000) }
}
if ($stopReason -or $process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $resumePolicy -PathType Leaf) -or
    -not (Test-Path -LiteralPath $resumeReportPath -PathType Leaf)) {
    throw "Checkpoint-only resume failed: reason=$stopReason exit=$($process.ExitCode)"
}
if ((Get-Item -LiteralPath $resumePolicy).Length -le 0) { throw 'Checkpoint-only resume exported an empty policy.' }
$resumedReport = Get-Content -LiteralPath $resumeReportPath -Raw | ConvertFrom-Json
$originalHash = (Get-FileHash -LiteralPath $policyPath -Algorithm SHA256).Hash
$resumedHash = (Get-FileHash -LiteralPath $resumePolicy -Algorithm SHA256).Hash
if ($resumedReport.trajectories -ne $expectedTarget -or $resumedReport.accepted_trajectories -ne $expectedTarget -or
    $resumedReport.discarded_trajectories -ne 0 -or $resumedReport.failed -or
    $resumedReport.evidence.producer.model_identity -ne $sourceReport.evidence.producer.model_identity -or $originalHash -ne $resumedHash) {
    throw 'Checkpoint-only resume did not reproduce the source policy and model identity.'
}
$result = [ordered]@{
    passed = $true
    target_trajectories = $expectedTarget
    trajectories = $resumedReport.trajectories
    accepted_trajectories = $resumedReport.accepted_trajectories
    discarded_trajectories = $resumedReport.discarded_trajectories
    original_policy_sha256 = $originalHash
    resumed_policy_sha256 = $resumedHash
    model_identity = $resumedReport.evidence.producer.model_identity
    peak_working_set_bytes = $peakWorkingSet
    peak_private_commit_bytes = $peakPrivateCommit
    minimum_available_physical_bytes = $minimumAvailable
    snapshot_store_bytes = $storeBytes
    resume_exit_code = $process.ExitCode
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'resume_equivalence.json'), ($result | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
Write-Output "Checkpoint-only resume matched the original policy. Evidence: $outputRoot\resume_equivalence.json"
