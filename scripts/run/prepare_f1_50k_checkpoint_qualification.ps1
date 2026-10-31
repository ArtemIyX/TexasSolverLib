param(
    [Parameter(Mandatory = $true)][string]$RunName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunName -notmatch '^pb12_diagnostic_f1_50k_[a-zA-Z0-9_-]{1,48}$') {
    throw 'RunName must start with pb12_diagnostic_f1_50k_ and contain only letters, digits, underscores, or hyphens.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'pb12_diagnostic_process.ps1')
$runRoot = Join-Path (Join-Path $repo 'tmp') $RunName
$template = Join-Path $repo 'configs/multiway/f1_checkpoint_qualification_50k_v1.cfg'
$binary = Get-Pb12DiagnosticTrainerExecutable $repo
$bucket = Join-Path $repo 'artifacts/f1_sizing_v1/buckets/buckets.bin'
foreach ($path in @($template, $binary, $bucket)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}
if (Test-Path -LiteralPath $runRoot) { throw "Refusing to reuse an existing directory: $runRoot" }
$requiredFreeBytes = [long](16GB + 320MB)
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($runRoot))
if ($drive.AvailableFreeSpace -lt $requiredFreeBytes) { throw 'Insufficient disk for the configured 50K reserve and bounded storage operations.' }

$overrides = @{
    storage_directory = (Join-Path $runRoot 'store')
    output_path = (Join-Path $runRoot 'policy.mwfb')
    report_path = (Join-Path $runRoot 'report.json')
    checkpoint_dir = (Join-Path $runRoot 'checkpoints')
    log_path = (Join-Path $runRoot 'training.log')
}
$seen = @{}
$configLines = foreach ($line in Get-Content -LiteralPath $template) {
    if ($line -match '^([^=]+)=') {
        $key = $Matches[1]
        if ($overrides.ContainsKey($key)) {
            if ($seen.ContainsKey($key)) { throw "Duplicate configuration key: $key" }
            $seen[$key] = $true
            "$key=$($overrides[$key])"
        } else { $line }
    } else { $line }
}
foreach ($key in $overrides.Keys) {
    if (-not $seen.ContainsKey($key)) { throw "Missing configuration key: $key" }
}

$null = New-Item -ItemType Directory -Path $runRoot
$configPath = Join-Path $runRoot 'training.conf'
[IO.File]::WriteAllLines($configPath, [string[]]$configLines, [Text.UTF8Encoding]::new($false))
$bucketItem = Get-Item -LiteralPath $bucket
$plan = [ordered]@{
    purpose = 'F1 12/12/12 bounded 50K checkpoint, resource, export, and resume qualification'
    profile_id = 'F1-QUAL-50K-12-v1'
    flop_buckets = 12
    turn_buckets = 12
    river_buckets = 12
    batches = 50
    trajectories_per_batch = 1000
    target_trajectories = 50000
    checkpoint_interval = 1
    observation_deadline_hours = 24
    required_free_disk_bytes = $requiredFreeBytes
    config_sha256 = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash
    hard_stops = [ordered]@{
        working_set_bytes = [long]40GB
        private_commit_bytes = [long]48GB
        available_physical_bytes = [long]3GB
        disk_amplification = 3
        p99 = 'three increasing two-cycle checkpoint-cycle P99 cohorts as live bytes grow'
    }
}
[IO.File]::WriteAllText((Join-Path $runRoot 'plan.json'), ($plan | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
$evidence = [ordered]@{
    observed_utc = [DateTime]::UtcNow.ToString('o')
    binary_path = [IO.Path]::GetFullPath($binary)
    binary_bytes = (Get-Item -LiteralPath $binary).Length
    binary_last_write_utc = (Get-Item -LiteralPath $binary).LastWriteTimeUtc.ToString('o')
    binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
    bucket_path = [IO.Path]::GetFullPath($bucket)
    bucket_bytes = [long]$bucketItem.Length
    bucket_last_write_utc = $bucketItem.LastWriteTimeUtc.ToString('o')
    bucket_sha256 = (Get-FileHash -LiteralPath $bucket -Algorithm SHA256).Hash
    config_sha256 = $plan.config_sha256
    verification = 'Read-only binary and bucket SHA-256; training startup validates artifact identity and every table bucket count.'
}
[IO.File]::WriteAllText((Join-Path $runRoot 'preparation_evidence.json'), ($evidence | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
Write-Output "Prepared $runRoot. No compiled program or training process was started."
