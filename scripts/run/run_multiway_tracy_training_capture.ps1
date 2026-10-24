[CmdletBinding()]
param(
    [string]$CaptureToolPath = '',
    [string]$CsvExportToolPath = '',
    [string]$MemoryExportToolPath = '',
    [string]$TrainingScriptPath = '',
    [string]$OutputDirectory = '',
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$CaptureId = '',
    [ValidateRange(1, 300)]
    [int]$CaptureSeconds = 60,
    [ValidateRange(2, 120)]
    [int]$ConnectionTimeoutSeconds = 60,
    [ValidateRange(1, 16)]
    [int]$Workers = 1,
    [ValidateRange(1, 10)]
    [int]$Batches = 2,
    [ValidateRange(1, [int]::MaxValue)]
    [int]$TrajectoriesPerBatch = 1000,
    [ValidateSet('fixed', 'dynamic')]
    [string]$Scheduler = 'fixed',
    [switch]$PinWorkers
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Resolve-RepoPath([string]$Path, [string]$DefaultRelativePath) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return Join-Path $repoRoot $DefaultRelativePath
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Get-ProcessExitCode([System.Diagnostics.Process]$Process) {
    try {
        $exitCodeProperty = $Process.GetType().GetProperty('ExitCode')
        $exitCodeValue = $exitCodeProperty.GetValue($Process, $null)
        if ($null -ne $exitCodeValue) {
            return [int]$exitCodeValue
        }
    } catch {
    }
    return $null
}

if ([string]::IsNullOrWhiteSpace($CaptureId)) {
    $CaptureId = 'tracy-training-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot ("tmp\tracy\{0}" -f $CaptureId)
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

$CaptureToolPath = Resolve-RepoPath $CaptureToolPath 'build-tracy-capture\RelWithDebInfo\tracy-capture-daemon.exe'
$CsvExportToolPath = Resolve-RepoPath $CsvExportToolPath 'build-tracy-csvexport\RelWithDebInfo\tracy-csvexport.exe'
$MemoryExportToolPath = Resolve-RepoPath $MemoryExportToolPath 'build-tracy-csvexport\RelWithDebInfo\tracy-memoryexport.exe'
$TrainingScriptPath = Resolve-RepoPath $TrainingScriptPath 'scripts\run\run_multiway_ram_1000.ps1'

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$scriptLogPath = Join-Path $OutputDirectory 'capture_wrapper.log'
Set-Content -LiteralPath $scriptLogPath -Value ("started_utc=" + (Get-Date).ToUniversalTime().ToString('o')) -Encoding UTF8
function Write-ScriptLog([string]$Message) {
    Add-Content -LiteralPath $scriptLogPath -Value $Message
    Write-Output $Message
}
Write-ScriptLog "capture_id=$CaptureId"
Write-ScriptLog "output_directory=$OutputDirectory"
Write-ScriptLog "workers=$Workers batches=$Batches trajectories_per_batch=$TrajectoriesPerBatch scheduler=$Scheduler pin_workers=$($PinWorkers.IsPresent)"

foreach ($requiredPath in @($CaptureToolPath, $CsvExportToolPath, $MemoryExportToolPath, $TrainingScriptPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

$captureVersionOutput = (& $CaptureToolPath -V 2>&1 | Out-String)
if ($captureVersionOutput -notmatch 'tracy-capture(?:-daemon)?\s+0\.14\.1\b') {
    throw "Expected Tracy capture tool version 0.14.1, got: $captureVersionOutput"
}
Write-ScriptLog 'validated required tools and Tracy capture version 0.14.1'

$tracePath = Join-Path $OutputDirectory ($CaptureId + '.tracy')
$csvPath = Join-Path $OutputDirectory ($CaptureId + '.csv')
$captureStdoutPath = Join-Path $OutputDirectory 'capture.stdout.log'
$captureStderrPath = Join-Path $OutputDirectory 'capture.stderr.log'
$orchestratorLogPath = Join-Path $OutputDirectory 'orchestrator.log'
$manifestPath = Join-Path $OutputDirectory 'run_manifest.json'

$gitCommit = (& git -C $repoRoot rev-parse HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($gitCommit)) {
    throw 'Could not determine git commit for run provenance'
}

$env:TEXASSOLVER_GIT_COMMIT = $gitCommit
$env:TEXASSOLVER_BUILD_CONFIGURATION = 'RelWithDebInfo'
$env:TEXASSOLVER_COMPILER_IDENTITY = if ([string]::IsNullOrWhiteSpace($env:VCToolsVersion)) { 'MSVC' } else { "MSVC $($env:VCToolsVersion)" }
$env:TEXASSOLVER_TRACE_CAPTURE_ID = $CaptureId

$captureDaemon = [System.IO.Path]::GetFileNameWithoutExtension($CaptureToolPath) -eq 'tracy-capture-daemon'
$captureArguments = if ($captureDaemon) {
    @('-o', $OutputDirectory, '-p', '8086')
} else {
    @('-o', $tracePath, '-a', '127.0.0.1', '-p', '8086', '-f', '-s', $CaptureSeconds.ToString())
}
$trainingArguments = @(
    '-Workers', $Workers.ToString(),
    '-Batches', $Batches.ToString(),
    '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
    '-BinaryKind', 'tracy',
    '-BuildConfiguration', 'RelWithDebInfo',
    '-TraceCaptureId', $CaptureId,
    '-Scheduler', $Scheduler,
    '-RunDirectory', $OutputDirectory
)
$trainingParameters = @{
    Workers = $Workers
    Batches = $Batches
    TrajectoriesPerBatch = $TrajectoriesPerBatch
    BinaryKind = 'tracy'
    BuildConfiguration = 'RelWithDebInfo'
    TraceCaptureId = $CaptureId
    Scheduler = $Scheduler
    RunDirectory = $OutputDirectory
}
if ($PinWorkers.IsPresent) {
    $trainingArguments += '-PinWorkers'
    $trainingParameters.PinWorkers = $true
}

$captureProcess = $null
$trainingFailure = $null
try {
    $captureProcess = Start-Process -FilePath $CaptureToolPath `
        -ArgumentList $captureArguments `
        -WorkingDirectory $repoRoot `
        -RedirectStandardOutput $captureStdoutPath `
        -RedirectStandardError $captureStderrPath `
        -WindowStyle Hidden `
        -PassThru

    Start-Sleep -Milliseconds 500
    if ($captureProcess.HasExited) {
        throw 'Tracy capture tool exited before the trainer started'
    }
    Write-ScriptLog "capture_started pid=$($captureProcess.Id)"

    Write-ScriptLog 'training_wrapper_started'
    & $TrainingScriptPath @trainingParameters 2>&1 |
        Tee-Object -FilePath $orchestratorLogPath |
        Tee-Object -FilePath $scriptLogPath -Append
    Write-ScriptLog 'training_wrapper_finished'
} catch {
    $trainingFailure = $_
    Write-ScriptLog "training_wrapper_failed=$($_.Exception.Message)"
} finally {
    if ($null -ne $captureProcess) {
        $captureDeadline = (Get-Date).AddSeconds($ConnectionTimeoutSeconds)
        while (-not $captureProcess.HasExited -and (Get-Date) -lt $captureDeadline) {
            Start-Sleep -Milliseconds 200
        }
        $captureProcess.Refresh()
        if (-not $captureProcess.HasExited) {
            $captureProcess.Kill()
            $captureProcess.WaitForExit()
        }
    }
}

if ($null -ne $trainingFailure) {
    throw $trainingFailure
}

$captureExitCode = Get-ProcessExitCode $captureProcess
if ($captureDaemon) {
    $traceCandidate = Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.tracy' -File |
        Where-Object { $_.Length -gt 0 } |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -ne $traceCandidate) {
        $tracePath = $traceCandidate.FullName
    }
}
if (-not $captureDaemon -and $null -ne $captureExitCode -and $captureExitCode -ne 0) {
    throw "Tracy capture tool failed with exit code $captureExitCode"
}

if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) {
    throw "Tracy capture did not create $tracePath"
}
$traceBytes = (Get-Item -LiteralPath $tracePath).Length
if ($traceBytes -le 0) {
    throw "Tracy capture is empty: $tracePath"
}
Write-ScriptLog "capture_finished exit_code=$captureExitCode trace_bytes=$traceBytes"

$memoryInspectionPath = Join-Path $OutputDirectory 'memory_events.json'
$memoryInspectionStderrPath = Join-Path $OutputDirectory 'memory_events.stderr.log'
Write-ScriptLog 'memory_event_inspection_started'
$memoryInspectionProcess = Start-Process -FilePath $MemoryExportToolPath `
    -ArgumentList @($tracePath) `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $memoryInspectionPath `
    -RedirectStandardError $memoryInspectionStderrPath `
    -WindowStyle Hidden `
    -Wait `
    -PassThru
$memoryInspectionExitCode = Get-ProcessExitCode $memoryInspectionProcess
if ($null -ne $memoryInspectionExitCode -and $memoryInspectionExitCode -ne 0) {
    throw "Tracy memory-event inspection failed with exit code $memoryInspectionExitCode"
}
if (-not (Test-Path -LiteralPath $memoryInspectionPath -PathType Leaf) -or
    (Get-Item -LiteralPath $memoryInspectionPath).Length -le 0) {
    throw "Tracy memory-event inspection produced no result: $memoryInspectionPath"
}
$memoryInspection = Get-Content -Raw -LiteralPath $memoryInspectionPath | ConvertFrom-Json
$allocationEventCount = [int64]$memoryInspection.allocation_events
$freeEventCount = [int64]$memoryInspection.free_events
if ($allocationEventCount -le 0 -or $freeEventCount -le 0) {
    throw "Tracy capture lacks allocation/free memory events: allocations=$allocationEventCount frees=$freeEventCount"
}
Write-ScriptLog "memory_event_inspection_finished exit_code=$memoryInspectionExitCode allocations=$allocationEventCount frees=$freeEventCount active=$([int64]$memoryInspection.active_allocations)"

Write-ScriptLog 'csv_export_started'
$csvProcess = Start-Process -FilePath $CsvExportToolPath `
    -ArgumentList @('-f', 'mw.', $tracePath) `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $csvPath `
    -RedirectStandardError ($csvPath + '.stderr.log') `
    -WindowStyle Hidden `
    -Wait `
    -PassThru
$csvExitCode = Get-ProcessExitCode $csvProcess
if ($null -ne $csvExitCode -and $csvExitCode -ne 0) {
    throw "Tracy CSV export failed with exit code $csvExitCode"
}
if (-not (Test-Path -LiteralPath $csvPath -PathType Leaf) -or (Get-Item -LiteralPath $csvPath).Length -le 0) {
    throw "Tracy CSV export is empty: $csvPath"
}
Write-ScriptLog "csv_export_finished exit_code=$csvExitCode csv_bytes=$((Get-Item -LiteralPath $csvPath).Length)"

$reportPath = Join-Path $OutputDirectory 'report.json'
$metricsPath = Join-Path $OutputDirectory 'run_metrics.json'
foreach ($requiredArtifact in @($reportPath, $metricsPath, $orchestratorLogPath)) {
    if (-not (Test-Path -LiteralPath $requiredArtifact -PathType Leaf)) {
        throw "Training artifact not found: $requiredArtifact"
    }
}

$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
$runMetrics = Get-Content -Raw -LiteralPath $metricsPath | ConvertFrom-Json
if (-not ($report.PSObject.Properties.Name -contains 'tracy_memory_events_enabled') -or
    -not [bool]$report.tracy_memory_events_enabled) {
    throw 'Training report does not prove that Tracy memory events were enabled'
}

$configPath = Join-Path $OutputDirectory 'run.cfg'
$blueprintPath = Join-Path $OutputDirectory 'blueprint.bin'
$checkpointDirectory = Join-Path $OutputDirectory 'checkpoints'
$manifest = [ordered]@{
    schema_version = 1
    tracy_version = '0.14.1'
    tracy_memory_events_enabled = [bool]$report.tracy_memory_events_enabled
    capture_id = $CaptureId
    captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    git_commit = $gitCommit
    build_configuration = $env:TEXASSOLVER_BUILD_CONFIGURATION
    compiler_identity = $env:TEXASSOLVER_COMPILER_IDENTITY
    build = $runMetrics.build_identity
    artifact = $runMetrics.artifact_identity
    workload = [ordered]@{
        workers = $Workers
        batches = $Batches
        trajectories_per_batch = $TrajectoriesPerBatch
        scheduler = $Scheduler
        pin_workers = $PinWorkers.IsPresent
    }
    capture_tool = [ordered]@{
        path = $CaptureToolPath
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $CaptureToolPath).Hash
        arguments = $captureArguments
    }
    csv_export_tool = [ordered]@{
        path = $CsvExportToolPath
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $CsvExportToolPath).Hash
        arguments = @('-f', 'mw.', $tracePath)
    }
    memory_export_tool = [ordered]@{
        path = $MemoryExportToolPath
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $MemoryExportToolPath).Hash
        arguments = @($tracePath)
    }
    memory_event_inspection = [ordered]@{
        path = $memoryInspectionPath
        stderr = $memoryInspectionStderrPath
        allocation_events = $allocationEventCount
        free_events = $freeEventCount
        active_allocations = [int64]$memoryInspection.active_allocations
        required = $true
    }
    training = [ordered]@{
        wrapper = $TrainingScriptPath
        arguments = $trainingArguments
        report_json = $reportPath
        metrics_json = $metricsPath
        config = $configPath
        blueprint = $blueprintPath
        checkpoint_directory = $checkpointDirectory
        orchestrator_log = $orchestratorLogPath
        wrapper_log = $scriptLogPath
    }
    trace = [ordered]@{
        path = $tracePath
        bytes = $traceBytes
        capture_stdout = $captureStdoutPath
        capture_stderr = $captureStderrPath
    }
    csv = [ordered]@{
        path = $csvPath
        bytes = (Get-Item -LiteralPath $csvPath).Length
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-ScriptLog "[tracy-training] output_directory=$OutputDirectory"
Write-ScriptLog "[tracy-training] trace=$tracePath"
Write-ScriptLog "[tracy-training] csv=$csvPath"
Write-ScriptLog "[tracy-training] memory_events=$memoryInspectionPath allocations=$allocationEventCount frees=$freeEventCount"
Write-ScriptLog "[tracy-training] report_json=$reportPath"
Write-ScriptLog "[tracy-training] metrics_json=$metricsPath"
Write-ScriptLog "[tracy-training] manifest=$manifestPath"
