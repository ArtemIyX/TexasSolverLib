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

if ($Configure -or -not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    $configureArguments = @(
        '-S', $repoRoot,
        '-B', $resolvedBuildDirectory,
        '-DTEXASSOLVER_BUILD_EXAMPLES=ON',
        '-DTEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=ON',
        '-DTEXASSOLVER_TRACY_ON_DEMAND=ON',
        '-DTEXASSOLVER_TRACY_SAMPLING=OFF',
        '-DTEXASSOLVER_TRACY_CALLSTACKS=OFF',
        '-DTEXASSOLVER_TRACY_MEMORY=OFF'
    )
    Write-Output "[tracy-build] configuring $resolvedBuildDirectory"
    & $cmake @configureArguments *> $logPath
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $logPath
        throw "CMake configure failed with exit code $LASTEXITCODE"
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
& $cmake @buildArguments *> $logPath
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $logPath
    throw "CMake Tracy build failed with exit code $LASTEXITCODE"
}

foreach ($target in $targets) {
    $binaryPath = Join-Path $resolvedBuildDirectory (Join-Path $Configuration ($target + '.exe'))
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
        throw "CMake reported success, but the Tracy binary was not found: $binaryPath"
    }
    Write-Output "[tracy-build] ready=$binaryPath"
}
Write-Output "[tracy-build] log=$logPath"
