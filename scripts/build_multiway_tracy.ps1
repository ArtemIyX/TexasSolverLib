[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-tracy',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'RelWithDebInfo',
    [ValidateRange(1, 32)]
    [int]$Parallel = 1,
    [switch]$Configure
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$cmake = (Get-Command cmake -ErrorAction Stop).Source

if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $resolvedBuildDirectory = $BuildDirectory
} else {
    $resolvedBuildDirectory = Join-Path $repoRoot $BuildDirectory
}

New-Item -ItemType Directory -Force -Path $resolvedBuildDirectory | Out-Null
$cachePath = Join-Path $resolvedBuildDirectory 'CMakeCache.txt'
$logPath = Join-Path $resolvedBuildDirectory 'multiway_tracy_build.log'

function Invoke-CMake([string[]]$Arguments, [string]$OutputPath = $logPath) {
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $cmake @Arguments *> $OutputPath
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

if ($Configure -or -not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    $configureArguments = @(
        '-S', $repoRoot,
        '-B', $resolvedBuildDirectory,
        '-DTEXASSOLVER_BUILD_EXAMPLES=ON',
        '-DTEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=ON',
        '-DTEXASSOLVER_TRACY_ON_DEMAND=ON',
        '-DTEXASSOLVER_TRACY_SAMPLING=OFF',
        '-DTEXASSOLVER_TRACY_CALLSTACKS=OFF',
        '-DTEXASSOLVER_TRACY_MEMORY=ON'
    )
    Write-Output "[tracy-build] configuring $resolvedBuildDirectory"
    $configureExitCode = Invoke-CMake $configureArguments
    if ($configureExitCode -ne 0) {
        Get-Content -LiteralPath $logPath
        throw "CMake configure failed with exit code $configureExitCode"
    }
}

$targets = @(
    'texas_multiway_train_tracy',
    'texas_solver_multiway_training_scaling_tracy'
)
$buildArguments = @(
    '--build', $resolvedBuildDirectory,
    '--config', $Configuration,
    '--target'
) + $targets + @('--parallel', $Parallel.ToString())

Write-Output "[tracy-build] building $($targets -join ', ')"
$buildExitCode = Invoke-CMake $buildArguments
if ($buildExitCode -ne 0) {
    Get-Content -LiteralPath $logPath
    throw "CMake Tracy build failed with exit code $buildExitCode"
}

foreach ($target in $targets) {
    $binaryPath = Join-Path $resolvedBuildDirectory (Join-Path $Configuration ($target + '.exe'))
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
        throw "CMake reported success, but the Tracy binary was not found: $binaryPath"
    }
    Write-Output "[tracy-build] ready=$binaryPath"
}

$captureBuildDirectory = Join-Path $repoRoot 'build-tracy-capture'
$captureSourceDirectory = Join-Path $repoRoot 'external\tracy\capture'
$captureCachePath = Join-Path $captureBuildDirectory 'CMakeCache.txt'
$captureLogPath = Join-Path $captureBuildDirectory 'tracy_capture_build.log'
New-Item -ItemType Directory -Force -Path $captureBuildDirectory | Out-Null
if (-not (Test-Path -LiteralPath $captureCachePath -PathType Leaf)) {
    $captureConfigureArguments = @(
        '-S', $captureSourceDirectory,
        '-B', $captureBuildDirectory,
        '-DNO_ISA_EXTENSIONS=ON',
        '-DNO_LTO=ON'
    )
    Write-Output "[tracy-build] configuring Tracy capture daemon $captureBuildDirectory"
    $captureConfigureExitCode = Invoke-CMake $captureConfigureArguments $captureLogPath
    if ($captureConfigureExitCode -ne 0) {
        Get-Content -LiteralPath $captureLogPath
        throw "Tracy capture daemon configure failed with exit code $captureConfigureExitCode"
    }
}
$captureTargets = @(
    'tracy-capture',
    'tracy-capture-daemon'
)
$captureBuildArguments = @(
    '--build', $captureBuildDirectory,
    '--config', $Configuration,
    '--target'
) + $captureTargets + @(
    '--parallel', $Parallel.ToString()
)
Write-Output "[tracy-build] building $($captureTargets -join ', ')"
$captureBuildExitCode = Invoke-CMake $captureBuildArguments $captureLogPath
if ($captureBuildExitCode -ne 0) {
    Get-Content -LiteralPath $captureLogPath
    throw "Tracy capture daemon build failed with exit code $captureBuildExitCode"
}
$captureDaemonPath = Join-Path $captureBuildDirectory (Join-Path $Configuration 'tracy-capture-daemon.exe')
if (-not (Test-Path -LiteralPath $captureDaemonPath -PathType Leaf)) {
    throw "CMake reported success, but the Tracy capture daemon was not found: $captureDaemonPath"
}
Write-Output "[tracy-build] capture_daemon_ready=$captureDaemonPath"
$capturePath = Join-Path $captureBuildDirectory (Join-Path $Configuration 'tracy-capture.exe')
if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) {
    throw "CMake reported success, but the Tracy capture binary was not found: $capturePath"
}
Write-Output "[tracy-build] capture_ready=$capturePath"

$memoryExportBuildDirectory = Join-Path $repoRoot 'build-tracy-csvexport'
$memoryExportSourceDirectory = Join-Path $repoRoot 'external\tracy\csvexport'
$memoryExportCachePath = Join-Path $memoryExportBuildDirectory 'CMakeCache.txt'
$memoryExportLogPath = Join-Path $memoryExportBuildDirectory 'tracy_memoryexport_build.log'
New-Item -ItemType Directory -Force -Path $memoryExportBuildDirectory | Out-Null
if (-not (Test-Path -LiteralPath $memoryExportCachePath -PathType Leaf)) {
    $memoryExportConfigureArguments = @(
        '-S', $memoryExportSourceDirectory,
        '-B', $memoryExportBuildDirectory,
        '-DNO_ISA_EXTENSIONS=ON',
        '-DNO_LTO=ON'
    )
    Write-Output "[tracy-build] configuring Tracy memory exporter $memoryExportBuildDirectory"
    $memoryExportConfigureExitCode = Invoke-CMake $memoryExportConfigureArguments $memoryExportLogPath
    if ($memoryExportConfigureExitCode -ne 0) {
        Get-Content -LiteralPath $memoryExportLogPath
        throw "Tracy memory exporter configure failed with exit code $memoryExportConfigureExitCode"
    }
}
$memoryExportBuildArguments = @(
    '--build', $memoryExportBuildDirectory,
    '--config', $Configuration,
    '--target', 'tracy-memoryexport',
    '--parallel', $Parallel.ToString()
)
Write-Output '[tracy-build] building tracy-memoryexport'
$memoryExportBuildExitCode = Invoke-CMake $memoryExportBuildArguments $memoryExportLogPath
if ($memoryExportBuildExitCode -ne 0) {
    Get-Content -LiteralPath $memoryExportLogPath
    throw "Tracy memory exporter build failed with exit code $memoryExportBuildExitCode"
}
$memoryExportBinaryPath = Join-Path $memoryExportBuildDirectory (Join-Path $Configuration 'tracy-memoryexport.exe')
if (-not (Test-Path -LiteralPath $memoryExportBinaryPath -PathType Leaf)) {
    throw "CMake reported success, but the Tracy memory exporter was not found: $memoryExportBinaryPath"
}
Write-Output "[tracy-build] memory_export_ready=$memoryExportBinaryPath"
Write-Output "[tracy-build] log=$logPath"
