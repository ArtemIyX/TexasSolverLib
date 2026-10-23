[CmdletBinding()]
param(
    [string]$OutputRoot = 'tmp\tracy',

    [ValidateRange(1, 300)]
    [int]$CaptureSeconds = 60,

    [ValidateRange(2, 120)]
    [int]$ConnectionTimeoutSeconds = 30,

    [switch]$PinWorkers
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'run_multiway_tracy_training_capture.ps1'

if ([System.IO.Path]::IsPathRooted($OutputRoot)) {
    $resolvedOutputRoot = $OutputRoot
} else {
    $resolvedOutputRoot = Join-Path $repoRoot $OutputRoot
}

$captures = @(
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-fixed-1w-r1'; Scheduler = 'fixed' },
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-fixed-1w-r2'; Scheduler = 'fixed' },
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-dynamic-1w-r1'; Scheduler = 'dynamic' },
    [pscustomobject]@{ Id = 'ad7-admission-split-cold-dynamic-1w-r2'; Scheduler = 'dynamic' }
)

if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "Capture runner was not found: $runner"
}

New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
Write-Output "[cold] repository_root=$repoRoot"
Write-Output "[cold] output_root=$resolvedOutputRoot"
Write-Output "[cold] captures=$($captures.Count) workers=1 batches=1 trajectories_per_batch=1000 pin_workers=$($PinWorkers.IsPresent)"

foreach ($capture in $captures) {
    $outputDirectory = Join-Path $resolvedOutputRoot $capture.Id
    Write-Output "[cold] starting capture_id=$($capture.Id) scheduler=$($capture.Scheduler) output_directory=$outputDirectory"

    $runnerArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $runner,
        '-CaptureId', $capture.Id,
        '-OutputDirectory', $outputDirectory,
        '-Workers', '1',
        '-Batches', '1',
        '-TrajectoriesPerBatch', '1000',
        '-Scheduler', $capture.Scheduler,
        '-CaptureSeconds', $CaptureSeconds.ToString(),
        '-ConnectionTimeoutSeconds', $ConnectionTimeoutSeconds.ToString()
    )
    if ($PinWorkers.IsPresent) {
        $runnerArguments += '-PinWorkers'
    }

    & powershell.exe @runnerArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Cold capture failed: id=$($capture.Id) exit_code=$LASTEXITCODE"
    }

    Write-Output "[cold] completed capture_id=$($capture.Id)"
}

Write-Output '[cold] all captures completed successfully'
