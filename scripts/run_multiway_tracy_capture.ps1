[CmdletBinding()]
param(
    [string]$CaptureToolPath = '',
    [string]$DiagnosticBinaryPath = '',
    [string]$OutputDirectory = '',
    [string]$CaptureId = '',
    [ValidateRange(1, 60)]
    [int]$CaptureSeconds = 10,
    [ValidateRange(2, 60)]
    [int]$ConnectionTimeoutSeconds = 15
)

$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($CaptureToolPath)) {
    $CaptureToolPath = Join-Path $repoRoot 'build-tracy-capture\RelWithDebInfo\tracy-capture.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($CaptureToolPath)) {
    $CaptureToolPath = Join-Path $repoRoot $CaptureToolPath
}

if ([string]::IsNullOrWhiteSpace($DiagnosticBinaryPath)) {
    $DiagnosticBinaryPath = Join-Path $repoRoot 'build-tracy\RelWithDebInfo\texas_solver_multiway_training_scaling_tracy.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($DiagnosticBinaryPath)) {
    $DiagnosticBinaryPath = Join-Path $repoRoot $DiagnosticBinaryPath
}

if ([string]::IsNullOrWhiteSpace($CaptureId)) {
    $CaptureId = 'tracy-smoke-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'tmp\tracy\smoke'
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

foreach ($requiredPath in @($CaptureToolPath, $DiagnosticBinaryPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

$captureVersionOutput = (& $CaptureToolPath 2>&1 | Out-String)
if ($captureVersionOutput -notmatch 'tracy-capture\s+0\.14\.1\b') {
    throw "Expected Tracy capture tool version 0.14.1, got: $captureVersionOutput"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$tracePath = Join-Path $OutputDirectory ($CaptureId + '.tracy')
$captureStdoutPath = Join-Path $OutputDirectory 'capture.stdout.log'
$captureStderrPath = Join-Path $OutputDirectory 'capture.stderr.log'
$diagnosticStdoutPath = Join-Path $OutputDirectory 'diagnostic.stdout.log'
$diagnosticStderrPath = Join-Path $OutputDirectory 'diagnostic.stderr.log'
$manifestPath = Join-Path $OutputDirectory 'run_manifest.json'

$gitCommit = (& git -C $repoRoot rev-parse HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($gitCommit)) {
    throw 'Could not determine git commit for run provenance'
}

$env:TEXASSOLVER_GIT_COMMIT = $gitCommit
$env:TEXASSOLVER_BUILD_CONFIGURATION = 'RelWithDebInfo'
$env:TEXASSOLVER_COMPILER_IDENTITY = if ([string]::IsNullOrWhiteSpace($env:VCToolsVersion)) { 'MSVC' } else { "MSVC $($env:VCToolsVersion)" }
$env:TEXASSOLVER_TRACE_CAPTURE_ID = $CaptureId

$diagnosticArguments = @(
    '--workers', '1',
    '--batch-sizes', '1000',
    '--warmup-batches', '1',
    '--timed-batches', '15',
    '--repeats', '1'
)

$captureArguments = @('-o', $tracePath, '-f', '-s', $CaptureSeconds.ToString())

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

$captureProcess = Start-Process -FilePath $CaptureToolPath `
    -ArgumentList $captureArguments `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $captureStdoutPath `
    -RedirectStandardError $captureStderrPath `
    -WindowStyle Hidden `
    -PassThru

try {
    Start-Sleep -Milliseconds 500
    if ($captureProcess.HasExited) {
        throw "Tracy capture tool exited before the diagnostic process connected: $($captureProcess.ExitCode)"
    }

    $diagnosticProcess = Start-Process -FilePath $DiagnosticBinaryPath `
        -ArgumentList $diagnosticArguments `
        -WorkingDirectory $repoRoot `
        -RedirectStandardOutput $diagnosticStdoutPath `
        -RedirectStandardError $diagnosticStderrPath `
        -WindowStyle Hidden `
        -PassThru

    $diagnosticProcess.WaitForExit()
    $diagnosticProcess.Refresh()

    $captureDeadline = (Get-Date).AddSeconds($ConnectionTimeoutSeconds)
    while (-not $captureProcess.HasExited -and (Get-Date) -lt $captureDeadline) {
        Start-Sleep -Milliseconds 200
    }
    if (-not $captureProcess.HasExited) {
        throw "Tracy capture did not connect within $ConnectionTimeoutSeconds seconds"
    }
    $captureProcess.Refresh()

    $diagnosticExitCode = Get-ProcessExitCode $diagnosticProcess
    $captureExitCode = Get-ProcessExitCode $captureProcess
    if ($null -ne $diagnosticExitCode -and $diagnosticExitCode -ne 0) {
        throw "Diagnostic Tracy binary failed with exit code $diagnosticExitCode"
    }
    if ($null -ne $captureExitCode -and $captureExitCode -ne 0) {
        throw "Tracy capture tool failed with exit code $captureExitCode"
    }
} finally {
    if ($null -ne $captureProcess -and -not $captureProcess.HasExited) {
        $captureProcess.Kill()
        $captureProcess.WaitForExit()
    }
}

if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) {
    throw "Tracy capture did not create $tracePath"
}
$traceBytes = (Get-Item -LiteralPath $tracePath).Length
if ($traceBytes -le 0) {
    throw "Tracy capture is empty: $tracePath"
}

$manifest = [ordered]@{
    schema_version = 1
    tracy_version = '0.14.1'
    capture_id = $CaptureId
    captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    git_commit = $gitCommit
    build_configuration = $env:TEXASSOLVER_BUILD_CONFIGURATION
    compiler_identity = $env:TEXASSOLVER_COMPILER_IDENTITY
    capture_tool = [ordered]@{
        path = $CaptureToolPath
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $CaptureToolPath).Hash
        arguments = $captureArguments
    }
    diagnostic_binary = [ordered]@{
        path = $DiagnosticBinaryPath
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $DiagnosticBinaryPath).Hash
        arguments = $diagnosticArguments
    }
    trace = [ordered]@{
        path = $tracePath
        bytes = $traceBytes
    }
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Output "[tracy] capture_id=$CaptureId"
Write-Output "[tracy] trace_path=$tracePath"
Write-Output "[tracy] trace_bytes=$traceBytes"
Write-Output "[tracy] manifest_path=$manifestPath"
