param(
    [Parameter(Mandatory = $true)][string]$RunName
)

$ErrorActionPreference = 'Stop'
if ($RunName -notmatch '^pb12_diagnostic_[a-zA-Z0-9_-]{1,64}$') {
    throw 'RunName must start with pb12_diagnostic_ and contain only letters, digits, underscores, or hyphens.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'pb12_diagnostic_process.ps1')
$runRoot = Join-Path (Join-Path $repo 'tmp') $RunName
$template = Join-Path $repo 'tmp/pb12_stage94_20261029T081147Z_stable_checkpoint_sampler/training.conf'
$baselinePath = Join-Path $repo 'tmp/pb12_stage94_20261029T081147Z_stable_checkpoint_sampler/preflight.json'
$binaryPath = Get-Pb12DiagnosticTrainerExecutable $repo
$bucketPath = Join-Path $repo 'artifacts/f1_sizing_v1/buckets/buckets.bin'
if (-not (Test-Path -LiteralPath $template -PathType Leaf)) { throw 'Preserved Stage 9.4 configuration is missing.' }
foreach ($inputPath in @($baselinePath, $binaryPath, $bucketPath)) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Required input is missing: $inputPath" }
}
if (Test-Path -LiteralPath $runRoot) { throw "Refusing to reuse an existing directory: $runRoot" }
$lines = Get-Content -LiteralPath $template
$required = @{
    storage_backend = 'ExternalCompactInt32'
    deterministic_seed = '1'
    training_worker_count = '1'
    scheduler_mode = 'fixed'
    trajectories_per_batch = '1000'
    checkpoint_interval = '1'
    synchronous_durability = 'true'
    compaction_fan_in = '4'
    target_trajectories = '10000'
    batches = '10'
}
foreach ($key in $required.Keys) {
    $matchingLines = @($lines | Where-Object { $_ -match ('^' + [regex]::Escape($key) + '=') })
    if ($matchingLines.Count -ne 1 -or $matchingLines[0] -cne "$key=$($required[$key])") { throw "Template does not match the required $key contract." }
}
$overrides = @{
    storage_directory = (Join-Path $runRoot 'store')
    output_path = (Join-Path $runRoot 'policy.mwfb')
    report_path = (Join-Path $runRoot 'report.json')
    checkpoint_dir = (Join-Path $runRoot 'checkpoints')
    log_path = (Join-Path $runRoot 'training.log')
    target_trajectories = '3000'
    batches = '3'
}
$seen = @{}
$result = foreach ($line in $lines) {
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
# The directory and all artifacts must be fresh. Never overwrite a prior run.
$null = New-Item -ItemType Directory -Path $runRoot -ErrorAction Stop
$configPath = Join-Path $runRoot 'training.conf'
[IO.File]::WriteAllLines($configPath, [string[]]$result, [Text.UTF8Encoding]::new($false))
$plan = [ordered]@{
    purpose = 'bounded diagnostic, not a Stage 9.4 or 9.5 run'
    batches = 3
    trajectories_per_batch = 1000
    checkpoint_interval = 1
    hard_stops = [ordered]@{ working_set_bytes = [long]40GB; private_commit_bytes = [long]48GB; available_physical_bytes = [long]3GB; disk_amplification = 3; p99 = 'three increasing two-cycle cohort P99s, including an established lower bound' }
    template_sha256 = (Get-FileHash -LiteralPath $template -Algorithm SHA256).Hash
    config_sha256 = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash
}
[IO.File]::WriteAllText((Join-Path $runRoot 'plan.json'), ($plan | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
$binary = Get-Item -LiteralPath $binaryPath
$bucket = Get-Item -LiteralPath $bucketPath
$baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
$evidence = [ordered]@{
    observed_utc = [DateTime]::UtcNow.ToString('o')
    binary_path = $binary.FullName; binary_bytes = $binary.Length; binary_last_write_utc = $binary.LastWriteTimeUtc.ToString('o')
    binary_sha256 = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash
    bucket_path = $bucket.FullName; bucket_bytes = $bucket.Length; bucket_last_write_utc = $bucket.LastWriteTimeUtc.ToString('o')
    bucket_size_and_timestamp_match_stage94 = ($bucket.Length -eq $baseline.bucket_bytes -and $bucket.LastWriteTimeUtc -eq ([DateTime]$baseline.bucket_last_write_time_utc).ToUniversalTime())
    bucket_hash_measured = $false; prepared_config_sha256 = $plan.config_sha256; plan_config_sha256 = $plan.config_sha256
    verification = 'Read-only metadata and binary hash; no build, test, or solver job.'
}
[IO.File]::WriteAllText((Join-Path $runRoot 'preparation_evidence.json'), ($evidence | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
Write-Output "Prepared $runRoot. No solver process was started."
