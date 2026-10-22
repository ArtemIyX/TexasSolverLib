[CmdletBinding()]
param(
    [string]$BinaryPath = '',
    [string]$OutputDirectory = 'tmp\multiway-steady-state',
    [ValidateRange(1, 1000000)]
    [int]$BatchSize = 1000,
    [ValidateRange(1, 16)]
    [int]$WarmupBatches = 1,
    [ValidateRange(1, 16)]
    [int]$TimedBatches = 4,
    [ValidateRange(1, 100)]
    [int]$Repeats = 5,
    [UInt64]$Seed = 0x77
)

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot 'build\RelWithDebInfo\texas_solver_multiway_training_scaling.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot $BinaryPath
}

if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Benchmark binary not found: $BinaryPath"
}
if ($WarmupBatches + $TimedBatches -gt 16) {
    throw 'Warmup plus timed batches exceeds the benchmark capacity.'
}
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BinaryPath).Hash
$groups = @()

foreach ($scheduler in @('fixed', 'dynamic')) {
    foreach ($pinWorkers in @($true, $false)) {
        $pinValue = if ($pinWorkers) { 'true' } else { 'false' }
        $groupId = 'steady-{0}-{1}' -f $scheduler, $(if ($pinWorkers) { 'pinned' } else { 'unpinned' })
        $stdoutPath = Join-Path $OutputDirectory ($groupId + '.stdout.log')
        $stderrPath = Join-Path $OutputDirectory ($groupId + '.stderr.log')
        $arguments = @(
            '--workers', '1,2,4,8',
            '--batch-sizes', $BatchSize.ToString(),
            '--warmup-batches', $WarmupBatches.ToString(),
            '--timed-batches', $TimedBatches.ToString(),
            '--repeats', $Repeats.ToString(),
            '--seed', $Seed.ToString(),
            '--scheduler', $scheduler,
            '--pin-workers', $pinValue
        )

        Write-Output "[steady-state] group=$groupId"
        Write-Output "[steady-state] stdout=$stdoutPath"
        & $BinaryPath @arguments 1> $stdoutPath 2> $stderrPath
        if ($LASTEXITCODE -ne 0) {
            throw "Benchmark group failed: $groupId (exit code $LASTEXITCODE)"
        }

        $groups += [ordered]@{
            id = $groupId
            scheduler = $scheduler
            pin_workers = $pinWorkers
            arguments = $arguments
            stdout = $stdoutPath
            stderr = $stderrPath
        }
    }
}

$manifest = [ordered]@{
    schema_version = 1
    captured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    binary = [ordered]@{
        path = $BinaryPath
        sha256 = $binaryHash
    }
    protocol = [ordered]@{
        workers = @(1, 2, 4, 8)
        batch_size = $BatchSize
        warmup_batches = $WarmupBatches
        timed_batches = $TimedBatches
        repeats = $Repeats
        seed = $Seed
        checkpoint_output = 'disabled'
        blueprint_output = 'disabled'
    }
    groups = $groups
}
$manifestPath = Join-Path $OutputDirectory 'matrix_manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output "[steady-state] manifest=$manifestPath"
