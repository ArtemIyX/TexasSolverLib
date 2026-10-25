[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',

    [ValidateRange(10, 900)]
    [int]$TimeoutSeconds = 120,

    [ValidateRange(1, 32)]
    [int]$Parallel = 4,

    [string]$OutputDirectory = 'tmp\focused-tests',

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $resolvedBuildDirectory = $BuildDirectory
} else {
    $resolvedBuildDirectory = Join-Path $repoRoot $BuildDirectory
}

if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $resolvedOutputDirectory = $OutputDirectory
} else {
    $resolvedOutputDirectory = Join-Path $repoRoot $OutputDirectory
}

if (-not (Test-Path -LiteralPath $resolvedBuildDirectory -PathType Container)) {
    throw "CMake build directory was not found: $resolvedBuildDirectory"
}

New-Item -ItemType Directory -Force -Path $resolvedOutputDirectory | Out-Null

$targets = @(
    'test_multiway_public_builder',
    'test_multiway_solver',
    'test_multiway_public_catalog',
    'test_multiway_terminal_adapter',
    'test_multiway_qualification_reports'
)

function Invoke-BoundedProcess {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments,
        [Parameter(Mandatory)] [string]$LogPath,
        [Parameter(Mandatory)] [string]$WorkingDirectory
    )

    $stdoutPath = "$LogPath.stdout"
    $stderrPath = "$LogPath.stderr"
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    Write-Output "[focused-tests] running $FilePath $($Arguments -join ' ')"
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try {
            $process.Kill()
        } catch {
        }
        $message = "Timed out after $TimeoutSeconds seconds: $FilePath"
        @($message, (Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue), (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue)) |
            Set-Content -LiteralPath $LogPath
        throw $message
    }

    $stdout = Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
    @($stdout, $stderr) | Set-Content -LiteralPath $LogPath
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $FilePath"
    }
}

if (-not $SkipBuild) {
    $buildLog = Join-Path $resolvedOutputDirectory "cmake-build-$Configuration.log"
    $buildArguments = @('--build', $resolvedBuildDirectory, '--config', $Configuration, '--parallel', $Parallel)
    foreach ($target in $targets) {
        $buildArguments += @('--target', $target)
    }
    $buildArguments += @('--', '/nologo', '/v:q', '/clp:ErrorsOnly;NoSummary')
    Invoke-BoundedProcess -FilePath 'cmake' -Arguments $buildArguments -LogPath $buildLog -WorkingDirectory $repoRoot
}

$testLog = Join-Path $resolvedOutputDirectory "ctest-$Configuration.log"
$testRegex = '^(test_multiway_public_builder|test_multiway_solver|test_multiway_public_catalog|test_multiway_terminal_adapter|test_multiway_qualification_reports)$'
$testArguments = @('--test-dir', $resolvedBuildDirectory, '-C', $Configuration, '--output-on-failure', '-R', $testRegex)
Invoke-BoundedProcess -FilePath 'ctest' -Arguments $testArguments -LogPath $testLog -WorkingDirectory $repoRoot

Write-Output "[focused-tests] passed configuration=$Configuration logs=$resolvedOutputDirectory"
