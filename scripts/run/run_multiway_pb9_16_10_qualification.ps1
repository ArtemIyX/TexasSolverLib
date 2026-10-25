[CmdletBinding()]
param(
    [ValidateRange(1, 100)]
    [int]$Batches = 50,

    [ValidateRange(1, [int]::MaxValue)]
    [int]$TrajectoriesPerBatch = 1000,

    [ValidateRange(5, 20)]
    [int]$Repeats = 5,

    [ValidateRange(0, 20)]
    [int]$WarmupRuns = 1,

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildConfiguration = 'RelWithDebInfo',

    [string]$OutputDirectory = '',

    [switch]$SkipBuildValidation
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$workerMatrix = Join-Path $PSScriptRoot 'run_multiway_worker_matrix.ps1'
$ramRunner = Join-Path $PSScriptRoot 'run_multiway_ram_1000.ps1'

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'tmp\pb9_16_10'
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,

        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [object[]]$Arguments
    )

    Write-Output "[pb9.16.10] $Label"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

$binary = Join-Path $repoRoot ("build\{0}\texas_multiway_train.exe" -f $BuildConfiguration)
$sourceConfig = Join-Path $repoRoot 'configs\multiway\f1_dev_v1.cfg'

if (-not $SkipBuildValidation) {
    Invoke-Checked 'Reconfigure CMake' 'cmake' @('-S', $repoRoot, '-B', (Join-Path $repoRoot 'build'))
    Invoke-Checked 'Build RelWithDebInfo test and training targets' 'cmake' @(
        '--build', (Join-Path $repoRoot 'build'),
        '--config', $BuildConfiguration,
        '--target', 'ALL_BUILD',
        '--', '/nologo', '/v:minimal', '/m:1', '/p:BuildInParallel=false'
    )
    Invoke-Checked 'Run RelWithDebInfo CTest' 'ctest' @(
        '--test-dir', (Join-Path $repoRoot 'build'),
        '-C', $BuildConfiguration,
        '--output-on-failure'
    )
}

foreach ($requiredPath in @($binary, $sourceConfig)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

function Run-WorkerMatrix {
    param(
        [string]$Name,
        [string]$Placement,
        [string]$Pinning,
        [int]$Seed
    )

    $output = Join-Path $OutputDirectory $Name
    Invoke-Checked "Worker matrix: $Name" 'powershell.exe' @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $workerMatrix,
        '-Workers', '4,8,12,16',
        '-Batches', $Batches.ToString(),
        '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
        '-Repeats', $Repeats.ToString(),
        '-Scheduling', 'both',
        '-Pinning', $Pinning,
        '-DynamicChunkSize', '16',
        '-PlacementPolicy', $Placement,
        '-CheckpointMode', 'disabled',
        '-WarmupRuns', $WarmupRuns.ToString(),
        '-WarmColdOrder', 'warm-first',
        '-RandomizeCaseOrder',
        '-CaseOrderSeed', $Seed.ToString(),
        '-BuildConfiguration', $BuildConfiguration,
        '-BinaryPath', $binary,
        '-SourceConfigPath', $sourceConfig,
        '-OutputDirectory', $output
    )
}

Run-WorkerMatrix 'worker-matrix-os-default' 'os-default' 'unpinned' 20261025
Run-WorkerMatrix 'worker-matrix-physical-compact' 'physical-compact' 'pinned' 20261026
Run-WorkerMatrix 'worker-matrix-physical-spread' 'physical-spread' 'pinned' 20261027

foreach ($placement in @(
    [pscustomobject]@{ Name = 'os-default'; Policy = 'os-default'; Pinning = 'unpinned' },
    [pscustomobject]@{ Name = 'physical-compact'; Policy = 'physical-compact'; Pinning = 'pinned' },
    [pscustomobject]@{ Name = 'physical-spread'; Policy = 'physical-spread'; Pinning = 'pinned' }
)) {
    foreach ($chunk in @(4, 8, 16, 32, 64)) {
        $output = Join-Path $OutputDirectory ("chunk-sweep-{0}-{1}" -f $placement.Name, $chunk)
        Invoke-Checked "Dynamic chunk sweep: $($placement.Name), chunk=$chunk" 'powershell.exe' @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $workerMatrix,
            '-Workers', '8,16',
            '-Batches', $Batches.ToString(),
            '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
            '-Repeats', $Repeats.ToString(),
            '-Scheduling', 'dynamic',
            '-Pinning', $placement.Pinning,
            '-DynamicChunkSize', $chunk.ToString(),
            '-PlacementPolicy', $placement.Policy,
            '-CheckpointMode', 'disabled',
            '-WarmupRuns', $WarmupRuns.ToString(),
            '-WarmColdOrder', 'warm-first',
            '-RandomizeCaseOrder',
            '-CaseOrderSeed', (20262000 + $chunk).ToString(),
            '-BuildConfiguration', $BuildConfiguration,
            '-BinaryPath', $binary,
            '-SourceConfigPath', $sourceConfig,
            '-OutputDirectory', $output
        )
    }
}

$configDirectory = Join-Path $OutputDirectory 'configs'
New-Item -ItemType Directory -Force -Path $configDirectory | Out-Null
$finalOnlyConfig = Join-Path $configDirectory 'checkpoint-final-only.cfg'
$periodicConfig = Join-Path $configDirectory 'checkpoint-periodic.cfg'

foreach ($config in @(
    [pscustomobject]@{ Path = $finalOnlyConfig; Interval = 1000000000 },
    [pscustomobject]@{ Path = $periodicConfig; Interval = 10 }
)) {
    Copy-Item -LiteralPath $sourceConfig -Destination $config.Path -Force
    $text = Get-Content -Raw -LiteralPath $config.Path
    if ($text -notmatch '(?im)checkpoint_interval\s*[:=]') {
        throw "checkpoint_interval not found in $($config.Path)"
    }
    $text = [regex]::Replace(
        $text,
        '(?im)^(\s*checkpoint_interval\s*[:=]\s*)\d+(\s*)$',
        "`${1}$($config.Interval)`${2}"
    )
    [System.IO.File]::WriteAllText($config.Path, $text)
}

foreach ($workers in @(8, 16)) {
    foreach ($checkpoint in @(
        [pscustomobject]@{ Name = 'final-only'; Config = $finalOnlyConfig },
        [pscustomobject]@{ Name = 'periodic'; Config = $periodicConfig }
    )) {
        $output = Join-Path $OutputDirectory ("checkpoint-{0}-{1}" -f $checkpoint.Name, $workers)
        Invoke-Checked "Checkpoint comparison: $($checkpoint.Name), workers=$workers" 'powershell.exe' @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $ramRunner,
            '-Workers', $workers.ToString(),
            '-Batches', $Batches.ToString(),
            '-TrajectoriesPerBatch', $TrajectoriesPerBatch.ToString(),
            '-Scheduler', 'fixed',
            '-DynamicChunkSize', '16',
            '-PlacementPolicy', 'os-default',
            '-CheckpointMode', 'enabled',
            '-BuildConfiguration', $BuildConfiguration,
            '-BinaryPath', $binary,
            '-SourceConfigPath', $checkpoint.Config,
            '-RunDirectory', $output
        )
    }
}

Write-Output '[pb9.16.10] qualification runs complete'
Write-Output "[pb9.16.10] results=$OutputDirectory"
Write-Warning 'Linear-vs-tournament replay is not selectable through the existing runners. Keep that comparison pending until a captured-stream replay harness is used.'
