[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Plan,
    [Parameter(Mandatory = $true)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string]$ExpectedPlanSha256,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
function Get-CheckedSha256([string]$Path) {
    $before = Get-Item -LiteralPath $Path -ErrorAction Stop
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($before.FullName)
    try { $hash = ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose(); $algorithm.Dispose() }
    $after = Get-Item -LiteralPath $before.FullName -ErrorAction Stop
    if ($before.Length -ne $after.Length -or $before.LastWriteTimeUtc -ne $after.LastWriteTimeUtc) {
        throw "File changed while hashing: $Path"
    }
    return $hash
}
$planPath = (Get-Item -LiteralPath $Plan -ErrorAction Stop).FullName
$planBytes = [IO.File]::ReadAllBytes($planPath)
$sha = [Security.Cryptography.SHA256]::Create()
try { $actualPlanHash = ([BitConverter]::ToString($sha.ComputeHash($planBytes))).Replace('-', '') }
finally { $sha.Dispose() }
if ($actualPlanHash -ne $ExpectedPlanSha256) { throw 'Prepared plan hash does not match.' }
$spec = [Text.Encoding]::UTF8.GetString($planBytes) | ConvertFrom-Json
$repo = (Resolve-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))).Path
if ($spec.schema -ne 1 -or $spec.repository -ne $repo -or $spec.wall_seconds -ne 1800 -or
    $spec.working_set_limit_bytes -ne ([long]48 * 1024 * 1024 * 1024) -or
    $spec.private_limit_bytes -ne ([long]64 * 1024 * 1024 * 1024) -or
    $spec.memory_reserve_bytes -ne ([long]1536 * 1024 * 1024) -or
    $spec.disk_reserve_bytes -ne ([long]1536 * 1024 * 1024) -or
    $spec.disk_preflight_bytes -ne ([long]100 * 1024 * 1024 * 1024)) {
    Write-Output "Plan repository=$($spec.repository) actual repository=$repo; wall=$($spec.wall_seconds); limits=$($spec.working_set_limit_bytes),$($spec.private_limit_bytes),$($spec.memory_reserve_bytes),$($spec.disk_reserve_bytes),$($spec.disk_preflight_bytes)"
    throw 'Prepared plan or resource limits are invalid.'
}
$planDir = Split-Path -Parent $planPath
$attempt = [IO.Path]::GetFullPath($spec.run_directory)
if ($attempt -ne (Join-Path $planDir 'attempt') -or (Test-Path -LiteralPath $attempt)) {
    throw 'Prepared attempt directory is not fresh.'
}
if ((Resolve-Path -LiteralPath $spec.snapshot).Path -ne (Join-Path $planDir 'fresh_snapshot\store')) {
    throw 'Prepared snapshot path is invalid.'
}
if (Get-Process -Name texas_multiway_train -ErrorAction SilentlyContinue) {
    throw 'A multiway training executable is already running.'
}
foreach ($item in $spec.hashes.PSObject.Properties) {
    if ((Get-CheckedSha256 $item.Name) -ne $item.Value) {
        throw "Prepared input hash changed: $($item.Name)"
    }
}
$wrapper = Join-Path $repo 'scripts\codex_powershell.ps1'
$auditTool = Join-Path $repo 'scripts\analyze\audit_pb12_snapshot.py'
$audit = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $wrapper python $auditTool $spec.snapshot_manifest)
if ($LASTEXITCODE -ne 0) { throw 'Snapshot or source audit failed.' }
$auditResult = ($audit -join "`n") | ConvertFrom-Json
if (-not $auditResult.passed -or $auditResult.snapshot -ne $spec.snapshot) {
    throw 'Snapshot audit did not verify the prepared snapshot.'
}
$os = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 2 -ErrorAction Stop
$drive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($planDir))
if ([long]$os.FreePhysicalMemory * 1024 -lt [long]$spec.memory_reserve_bytes -or
    [long]$drive.AvailableFreeSpace -lt [long]$spec.disk_preflight_bytes) {
    throw 'System memory or 100 GiB disk preflight rejected the attempt.'
}
$checkpointDir = Join-Path $attempt 'checkpoints'
$output = Join-Path $attempt 'policy.mwfb'
$report = Join-Path $attempt 'report.json'
$arguments = @('--config', $spec.config, '--input', $spec.bucket, '--output', $output,
    '--report', $report, '--checkpoint-dir', $checkpointDir, '--resume', $spec.checkpoint,
    '--external-storage-snapshot-dir', $spec.snapshot, '--batches', '0',
    '--profile-external-export', '--progress', 'true', '--progress-memory', 'true',
    '--progress-speed', 'true', '--no-log')
Write-Output "Preflight passed. Executable: $($spec.executable)"
Write-Output ('Arguments: ' + (($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '))
Write-Output "Plan SHA-256: $actualPlanHash"
if (-not $Run) { Write-Output 'Validation only; no export launched.'; exit 0 }

New-Item -ItemType Directory -Path $attempt -ErrorAction Stop | Out-Null
New-Item -ItemType Directory -Path $checkpointDir -ErrorAction Stop | Out-Null
$quoted = @($arguments | ForEach-Object {
    if ($_ -match '"' -or $_.EndsWith('\')) { throw 'Unsafe process argument.' }
    '"' + $_ + '"'
})
$process = Start-Process -FilePath $spec.executable -ArgumentList $quoted -WorkingDirectory $repo `
    -RedirectStandardOutput (Join-Path $attempt 'stdout.log') `
    -RedirectStandardError (Join-Path $attempt 'stderr.log') -PassThru -WindowStyle Hidden
$clock = [Diagnostics.Stopwatch]::StartNew()
$guard = $null
$peakWorking = [long]0
$peakPrivate = [long]0
$samples = 0
try {
    while (-not $process.WaitForExit(1000)) {
        if ($clock.Elapsed.TotalSeconds -ge [long]$spec.wall_seconds) { $guard = 'timeout'; break }
        try {
            $sample = Get-Process -Id $process.Id -ErrorAction Stop
            $os = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 2 -ErrorAction Stop
            $drive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($attempt))
            $available = [long]$os.FreePhysicalMemory * 1024
            $freeDisk = [long]$drive.AvailableFreeSpace
            if ($available -le 0 -or $freeDisk -lt 0) { throw 'Invalid telemetry.' }
            $peakWorking = [Math]::Max($peakWorking, [long]$sample.WorkingSet64)
            $peakPrivate = [Math]::Max($peakPrivate, [long]$sample.PrivateMemorySize64)
            ++$samples
            if ($sample.WorkingSet64 -gt [long]$spec.working_set_limit_bytes) { $guard = 'working_set_limit'; break }
            if ($sample.PrivateMemorySize64 -gt [long]$spec.private_limit_bytes) { $guard = 'private_limit'; break }
            if ($available -lt [long]$spec.memory_reserve_bytes) { $guard = 'memory_reserve'; break }
            if ($freeDisk -lt [long]$spec.disk_reserve_bytes) { $guard = 'disk_reserve'; break }
        } catch {
            if (-not $process.HasExited) { $guard = 'telemetry_unavailable'; break }
        }
    }
    if ($null -ne $guard -and -not $process.HasExited) {
        & taskkill /PID $process.Id /T /F | Out-Null
    }
    if (-not $process.WaitForExit(5000)) { throw 'Export process did not terminate.' }
} catch {
    if (-not $process.HasExited) { & taskkill /PID $process.Id /T /F | Out-Null }
    throw
} finally { $clock.Stop() }
$process.Refresh()
$summary = [ordered]@{ plan_sha256 = $actualPlanHash; guard = $guard; exit_code = $process.ExitCode;
    elapsed_seconds = $clock.Elapsed.TotalSeconds; samples = $samples; peak_working_set_bytes = $peakWorking;
    peak_private_bytes = $peakPrivate; output_verification_required = $true }
[IO.File]::WriteAllText((Join-Path $attempt 'watchdog_summary.json'), ($summary | ConvertTo-Json), [Text.Encoding]::UTF8)
Write-Output 'Process stopped. Verify source integrity and all policy targets before treating output as complete.'
if ($null -ne $guard -or $process.ExitCode -ne 0) { throw "Export failed or stopped: guard=$guard exit=$($process.ExitCode)" }
