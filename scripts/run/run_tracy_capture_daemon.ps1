[CmdletBinding()]
param(
    [string]$CaptureToolPath = '',
    [string]$OutputDirectory = '',
    [ValidateRange(1, 65535)]
    [int]$Port = 8086,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$CaptureId = ''
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

if ([string]::IsNullOrWhiteSpace($CaptureId)) {
    $CaptureId = 'tracy-daemon-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot ("tmp\tracy\{0}" -f $CaptureId)
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

$CaptureToolPath = Resolve-RepoPath $CaptureToolPath 'build-tracy-capture\RelWithDebInfo\tracy-capture-daemon.exe'

if (-not (Test-Path -LiteralPath $CaptureToolPath -PathType Leaf)) {
    throw "Tracy capture daemon not found: $CaptureToolPath"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Write-Output "[tracy-daemon] executable=$CaptureToolPath"
Write-Output "[tracy-daemon] output_directory=$OutputDirectory"
Write-Output "[tracy-daemon] endpoint=127.0.0.1:$Port"
Write-Output '[tracy-daemon] waiting for Tracy clients; press Ctrl+C after the trainer exits'

$daemonArguments = @(
    '-o', $OutputDirectory,
    '-p', $Port.ToString()
)

& $CaptureToolPath @daemonArguments
$exitCode = $LASTEXITCODE

Write-Output "[tracy-daemon] stopped with exit_code=$exitCode; inspect $OutputDirectory for .tracy files"
