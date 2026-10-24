[CmdletBinding()]
param(
    [ValidateRange(1, 20)]
    [int]$Repeats = 3,

    [ValidateRange(1, 1000)]
    [int]$TrajectoriesPerBatch = 1000,

    [string]$OutputDirectory = '',

    [string]$NormalBinaryPath = '',

    [string]$SourceConfigPath = '',

    [string]$TracyCaptureScriptPath = '',

    [switch]$SkipTracy
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$normalRunner = Join-Path $PSScriptRoot 'run_multiway_ram_1000.ps1'
$tracyRunner = if ([string]::IsNullOrWhiteSpace($TracyCaptureScriptPath)) {
    Join-Path $PSScriptRoot 'run_multiway_tracy_training_capture.ps1'
} elseif ([System.IO.Path]::IsPathRooted($TracyCaptureScriptPath)) {
    $TracyCaptureScriptPath
} else {
    Join-Path $repoRoot $TracyCaptureScriptPath
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'tmp\memory-attribution\baseline'
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}
if ([string]::IsNullOrWhiteSpace($NormalBinaryPath)) {
    $NormalBinaryPath = Join-Path $repoRoot 'build\RelWithDebInfo\texas_multiway_train.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($NormalBinaryPath)) {
    $NormalBinaryPath = Join-Path $repoRoot $NormalBinaryPath
}
if (-not [string]::IsNullOrWhiteSpace($SourceConfigPath) -and -not [System.IO.Path]::IsPathRooted($SourceConfigPath)) {
    $SourceConfigPath = Join-Path $repoRoot $SourceConfigPath
}

if (-not (Test-Path -LiteralPath $normalRunner -PathType Leaf)) {
    throw "Normal bounded runner not found: $normalRunner"
}
if (-not $SkipTracy -and -not (Test-Path -LiteralPath $tracyRunner -PathType Leaf)) {
    throw "Tracy bounded runner not found: $tracyRunner"
}

$cases = @(
    [pscustomobject]@{ id = 'A1'; workers = 1; batches = 1; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A2'; workers = 1; batches = 5; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A3'; workers = 4; batches = 1; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A4'; workers = 4; batches = 5; scheduler = 'fixed' },
    [pscustomobject]@{ id = 'A5'; workers = 4; batches = 5; scheduler = 'dynamic' }
)

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$gitCommit = (& git -C $repoRoot rev-parse HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($gitCommit)) {
    throw 'Could not determine git commit for matrix provenance'
}

Write-Output "[memory-matrix] output_directory=$OutputDirectory"
Write-Output "[memory-matrix] repeats=$Repeats trajectories_per_batch=$TrajectoriesPerBatch normal_configuration=RelWithDebInfo"

foreach ($case in $cases) {
    for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
        $runDirectory = Join-Path $OutputDirectory (Join-Path $case.id $repeat.ToString())
        Write-Output "[memory-matrix] normal case=$($case.id) repeat=$repeat workers=$($case.workers) batches=$($case.batches) scheduler=$($case.scheduler)"
        $arguments = @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $normalRunner,
            '-Workers', $case.workers.ToString(),
            '-Batches', $case.batches.ToString(),
            '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
            '-Scheduler', $case.scheduler,
            '-BinaryKind', 'normal',
            '-BuildConfiguration', 'RelWithDebInfo',
            '-BinaryPath', $NormalBinaryPath,
            '-RunDirectory', $runDirectory
        )
        if (-not [string]::IsNullOrWhiteSpace($SourceConfigPath)) {
            $arguments += @('-SourceConfigPath', $SourceConfigPath)
        }
        & powershell.exe @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Normal matrix run failed for case=$($case.id) repeat=$repeat with exit code $LASTEXITCODE"
        }
    }

    if (-not $SkipTracy) {
        $tracyDirectory = Join-Path $OutputDirectory (Join-Path $case.id 'tracy')
        $captureId = "memory-d-$($case.id.ToLowerInvariant())"
        Write-Output "[memory-matrix] Tracy case=$($case.id) workers=$($case.workers) batches=$($case.batches) scheduler=$($case.scheduler)"
        $tracyArguments = @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $tracyRunner,
            '-Workers', $case.workers.ToString(),
            '-Batches', $case.batches.ToString(),
            '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
            '-Scheduler', $case.scheduler,
            '-CaptureId', $captureId,
            '-OutputDirectory', $tracyDirectory
        )
        if (-not [string]::IsNullOrWhiteSpace($SourceConfigPath)) {
            $tracyArguments += @('-SourceConfigPath', $SourceConfigPath)
        }
        & powershell.exe @tracyArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Tracy matrix run failed for case=$($case.id) with exit code $LASTEXITCODE"
        }
    }
}

$matrixManifestPath = Join-Path $OutputDirectory 'matrix_manifest.json'
[ordered]@{
    schema_version = 1
    git_commit = $gitCommit
    normal_build_configuration = 'RelWithDebInfo'
    tracy_build_configuration = 'RelWithDebInfo'
    tracy_version = if ($SkipTracy) { $null } else { '0.14.1' }
    trajectories_per_batch = $TrajectoriesPerBatch
    repeats = $Repeats
    pin_workers = $false
    cases = @($cases | ForEach-Object {
        [ordered]@{
            id = $_.id
            workers = $_.workers
            batches = $_.batches
            scheduler = $_.scheduler
        }
    })
    normal_binary = $NormalBinaryPath
    source_config = $SourceConfigPath
    tracy_capture_required = (-not $SkipTracy)
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $matrixManifestPath -Encoding UTF8

Write-Output "[memory-matrix] manifest=$matrixManifestPath"
Write-Output '[memory-matrix] PASS: bounded matrix execution completed'
