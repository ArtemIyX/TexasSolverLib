[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Executable,
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildConfiguration,
    [ValidateRange(1, 120)] [int]$MaximumSeconds = 120,
    [switch]$Filter
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$binary = (Get-Item -LiteralPath $Executable -ErrorAction Stop).FullName
if ([IO.Path]::GetFileName($binary) -ine 'test_multiway_blueprint_training.exe') {
    throw 'This runner accepts only test_multiway_blueprint_training.exe.'
}
$caseName = 'multiway_synthetic_external_export_pages_and_runs_without_training'
$tmpRoot = Join-Path $repoRoot 'tmp'
if (-not (Test-Path -LiteralPath $tmpRoot -PathType Container)) { throw 'Repository tmp directory is missing.' }
$runDirectory = Join-Path $tmpRoot ('pb12_export_fixture_' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $runDirectory -ErrorAction Stop
$fixtureTemp = Join-Path $runDirectory 'fixture-temp'
$null = New-Item -ItemType Directory -Path $fixtureTemp -ErrorAction Stop
$stdoutPath = Join-Path $runDirectory 'stdout.txt'
$stderrPath = Join-Path $runDirectory 'stderr.txt'
$summaryPath = Join-Path $runDirectory 'summary.json'

$sourceRevision = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source revision.' }
$sourceStatus = @(& git -C $repoRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify worktree status.' }
$binaryHash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
$systemReserveBytes = [long]1536 * 1024 * 1024
$diskReserveBytes = [long]1536 * 1024 * 1024
$workingSetLimit = [long]48 * 1024 * 1024 * 1024
$privateLimit = [long]64 * 1024 * 1024 * 1024
$initialOs = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 2 -ErrorAction Stop
$initialDrive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($runDirectory))
$minimumAvailablePhysical = [long]$initialOs.FreePhysicalMemory * 1024
$minimumAvailableDisk = [long]$initialDrive.AvailableFreeSpace
if ($minimumAvailablePhysical -lt $systemReserveBytes) {
    throw 'System memory reserve preflight rejected the fixture.'
}
if ($minimumAvailableDisk -lt $diskReserveBytes) {
    throw 'Disk reserve preflight rejected the fixture.'
}
$startedUtc = [DateTime]::UtcNow.ToString('o')
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $binary
$startInfo.Arguments = $caseName
$startInfo.WorkingDirectory = $repoRoot
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.EnvironmentVariables['TEMP'] = $fixtureTemp
$startInfo.EnvironmentVariables['TMP'] = $fixtureTemp
$startInfo.EnvironmentVariables['TEXASSOLVER_PB12_FIXTURE_PROFILE'] = '1'
$startInfo.EnvironmentVariables['TEXASSOLVER_PB12_EXPORT_OUTPUT'] = (Join-Path $runDirectory 'policy.mwfb')
if ($Filter) { $startInfo.EnvironmentVariables['TEXASSOLVER_PB12_EXPORT_MODE'] = 'filter' }
else { $startInfo.EnvironmentVariables['TEXASSOLVER_PB12_EXPORT_MODE'] = 'baseline' }
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
if (-not $process.Start()) { throw 'Fixture process did not start.' }
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()

$guardReason = $null
$peakWorkingSet = [long]0
$peakPrivate = [long]0
$lastCpuTicks = $null
$samples = 0
try {
    while (-not $process.WaitForExit(20)) {
        if ($stopwatch.Elapsed.TotalSeconds -ge $MaximumSeconds) { $guardReason = 'timeout'; break }
        try {
            $sample = Get-Process -Id $process.Id -ErrorAction Stop
            $os = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 2 -ErrorAction Stop
            $drive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($runDirectory))
            $availableMemory = [long]$os.FreePhysicalMemory * 1024
            $availableDisk = [long]$drive.AvailableFreeSpace
            if ($availableMemory -lt 0 -or $availableDisk -lt 0) { throw 'Invalid resource telemetry.' }
            $minimumAvailablePhysical = [Math]::Min($minimumAvailablePhysical, $availableMemory)
            $minimumAvailableDisk = [Math]::Min($minimumAvailableDisk, $availableDisk)
            $peakWorkingSet = [Math]::Max($peakWorkingSet, [long]$sample.WorkingSet64)
            $peakPrivate = [Math]::Max($peakPrivate, [long]$sample.PrivateMemorySize64)
            $lastCpuTicks = [long]$sample.TotalProcessorTime.Ticks
            ++$samples
            if ($sample.WorkingSet64 -gt $workingSetLimit) { $guardReason = 'working_set_limit'; break }
            if ($sample.PrivateMemorySize64 -gt $privateLimit) { $guardReason = 'private_commit_limit'; break }
            if ($availableMemory -lt $systemReserveBytes) { $guardReason = 'system_memory_reserve'; break }
            if ($availableDisk -lt $diskReserveBytes) { $guardReason = 'disk_reserve'; break }
        } catch {
            if (-not $process.HasExited) { $guardReason = 'resource_telemetry_unavailable'; break }
        }
    }
    if ($null -ne $guardReason -and -not $process.HasExited) {
        & taskkill /PID $process.Id /T /F | Out-Null
    }
    if (-not $process.WaitForExit(5000)) { throw 'Fixture process did not terminate within five seconds.' }
    $null = $process.WaitForExit()
} catch {
    if (-not $process.HasExited) { & taskkill /PID $process.Id /T /F | Out-Null }
    throw
} finally {
    $stopwatch.Stop()
}

$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
[IO.File]::WriteAllText($stdoutPath, $stdout, [Text.Encoding]::UTF8)
[IO.File]::WriteAllText($stderrPath, $stderr, [Text.Encoding]::UTF8)
$process.Refresh()
$exitCode = $process.ExitCode
if ($null -eq $exitCode) { $guardReason = 'exit_status_unavailable' }
$binaryHashAfter = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
if ($binaryHashAfter -ne $binaryHash) { $guardReason = 'binary_changed_during_run' }
$lines = [regex]::Matches($stdout, '(?m)^PB12_FIXTURE_PROFILE\s+[^\r\n]*')
$metrics = [ordered]@{}
if ($lines.Count -eq 1) {
    foreach ($pair in [regex]::Matches($lines[0].Value, '([a-z_]+)=(\d+)')) {
        if ($metrics.Contains($pair.Groups[1].Value)) { throw 'Duplicate fixture metric.' }
        $metrics[$pair.Groups[1].Value] = $pair.Groups[2].Value
    }
}
$encodedBytesPerSecond = $null
if ($metrics.Contains('encoded_bytes') -and $metrics.Contains('ordered_visit_ns') -and
    [decimal]$metrics.ordered_visit_ns -gt 0) {
    $encodedBytesPerSecond = ([decimal]$metrics.encoded_bytes * 1000000000 / [decimal]$metrics.ordered_visit_ns).ToString('F2', [Globalization.CultureInfo]::InvariantCulture)
}
$passed = $null -eq $guardReason -and $exitCode -eq 0 -and $lines.Count -eq 1 -and
    $stdout.Contains('[PASS] ' + $caseName)
$summary = [ordered]@{
    fixture_only = $true
    filter_enabled = [bool]$Filter
    fixture_setup = '1,538 rows, 98 pages, twelve overlapping two-page synthetic update batches and reopened storage, with one profiled export.'
    run_directory = $runDirectory
    executable = $binary
    binary_sha256 = $binaryHash
    binary_sha256_after = $binaryHashAfter
    declared_build_configuration = $BuildConfiguration
    source_revision = $sourceRevision
    source_dirty = ($sourceStatus.Count -gt 0)
    command = @($binary, $caseName)
    stdout_path = $stdoutPath
    stderr_path = $stderrPath
    started_utc = $startedUtc
    ended_utc = [DateTime]::UtcNow.ToString('o')
    elapsed_nanoseconds = [long]($stopwatch.ElapsedTicks * (1000000000.0 / [Diagnostics.Stopwatch]::Frequency))
    process_cpu_ticks_last_sample = $lastCpuTicks
    sampled_peak_working_set_bytes = if ($samples -gt 0) { $peakWorkingSet } else { $null }
    sampled_peak_private_bytes = if ($samples -gt 0) { $peakPrivate } else { $null }
    minimum_sampled_available_physical_bytes = $minimumAvailablePhysical
    minimum_sampled_free_disk_bytes = $minimumAvailableDisk
    process_io_bytes = $null
    encoded_bytes_per_ordered_visit_second = $encodedBytesPerSecond
    samples = $samples
    maximum_seconds = $MaximumSeconds
    guard_reason = $guardReason
    exit_code = $exitCode
    passed = $passed
    metrics = $metrics
    limitations = @('Configuration is caller-declared; binary/source correspondence is unverified.',
        'Whole-process timing includes synthetic fixture setup and storage writes, not export alone.',
        'Synthetic fixture timing is not production checkpoint export throughput or evidence of a measured improvement.',
        'Resource peaks are sampled; short runs may have no sample.',
        'Nested timings overlap and row samples are not full totals.',
        'Run probes and stream writes are not physical disk I/O.')
}
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 6), [Text.Encoding]::UTF8)
Write-Output "Fixture results: $runDirectory"
if (-not $passed) { throw "Fixture did not pass; guard=$guardReason exit=$exitCode" }


\n