[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'RelWithDebInfo',
    [ValidateRange(1, 32)]
    [int]$Parallel = 1,
    [switch]$Configure
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$resolvedBuildDirectory = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repoRoot $BuildDirectory
}
$cachePath = Join-Path $resolvedBuildDirectory 'CMakeCache.txt'
$logPath = Join-Path $resolvedBuildDirectory 'multiway_normal_build.log'

New-Item -ItemType Directory -Force -Path $resolvedBuildDirectory | Out-Null
function Invoke-CMake([string[]]$Arguments) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $cmake @Arguments *> $logPath
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
}

if ($Configure -or -not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    $configureExitCode = Invoke-CMake @(
        '-S', $repoRoot,
        '-B', $resolvedBuildDirectory,
        '-DTEXASSOLVER_BUILD_EXAMPLES=ON',
        '-DTEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=OFF',
        '-DTEXASSOLVER_TRACY_MEMORY=OFF',
        '-DCMAKE_SUPPRESS_REGENERATION=ON'
    )
    if ($configureExitCode -ne 0) {
        Get-Content -LiteralPath $logPath
        throw "CMake normal configure failed with exit code $configureExitCode"
    }
}

$generator = (Get-Content -LiteralPath (Join-Path $resolvedBuildDirectory 'CMakeCache.txt') | Select-String '^CMAKE_GENERATOR:INTERNAL=' | Select-Object -First 1).Line
$buildProject = Join-Path $resolvedBuildDirectory 'texas_multiway_train.vcxproj'
$libraryProject = Join-Path $resolvedBuildDirectory 'texas.vcxproj'
$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if ($null -eq $msbuild) {
    $msbuild = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio' -Filter 'MSBuild.exe' -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\amd64\\' } |
        Select-Object -First 1
}
if ($generator -match 'Visual Studio' -and (Test-Path -LiteralPath $buildProject -PathType Leaf) -and $null -ne $msbuild) {
    $msbuildPath = if ($msbuild.PSObject.Properties.Name -contains 'Source' -and -not [string]::IsNullOrWhiteSpace($msbuild.Source)) {
        $msbuild.Source
    } else {
        $msbuild.FullName
    }
    Write-Output "[normal-build] using project=$buildProject msbuild=$msbuildPath"
    & $msbuildPath $libraryProject "/p:Configuration=$Configuration" '/p:Platform=x64' '/m:1' '/v:minimal' *> $logPath
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -eq 0) {
        & $msbuildPath $buildProject "/p:Configuration=$Configuration" '/p:Platform=x64' '/m:1' '/v:minimal' *> $logPath
        $buildExitCode = $LASTEXITCODE
    }
} else {
    $buildExitCode = Invoke-CMake @(
        '--build', $resolvedBuildDirectory,
        '--config', $Configuration,
        '--target', 'texas_multiway_train',
        '--parallel', $Parallel.ToString()
    )
}
if ($buildExitCode -ne 0) {
    Get-Content -LiteralPath $logPath
    throw "CMake normal build failed with exit code $buildExitCode"
}

$binaryPath = Join-Path $resolvedBuildDirectory (Join-Path $Configuration 'texas_multiway_train.exe')
if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
    throw "CMake reported success, but the normal binary was not found: $binaryPath"
}
Write-Output "[normal-build] ready=$binaryPath"
Write-Output "[normal-build] log=$logPath"
