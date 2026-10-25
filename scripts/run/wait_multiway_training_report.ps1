[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RunDirectory,

    [ValidateRange(1, 300)]
    [int]$PollSeconds = 30,

    [ValidateRange(1, 86400)]
    [int]$TimeoutSeconds = 3600
)

$ErrorActionPreference = 'Stop'
$resolvedRunDirectory = (Resolve-Path -LiteralPath $RunDirectory -ErrorAction Stop).Path
$reportPath = Join-Path $resolvedRunDirectory 'report.json'
$stdoutPath = Join-Path $resolvedRunDirectory 'stdout.log'
$stderrPath = Join-Path $resolvedRunDirectory 'stderr.log'
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)

Write-Output "[wait-report] run_directory=$resolvedRunDirectory"
Write-Output "[wait-report] report_path=$reportPath"

while ($true) {
    if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
        $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
        Write-Output '[wait-report] report created'
        $report | Select-Object accepted_trajectories, failed, failed_batch_available,
            requested_worker_count, effective_worker_count,
            sampled_process_working_set_peak_bytes,
            memory_estimated_working_set_peak_bytes,
            deterministic_merge_fingerprint,
            policy_content_hash | Format-List
        exit 0
    }

    $trainer = Get-Process -Name 'texas_multiway_train' -ErrorAction SilentlyContinue
    if ($trainer) {
        Write-Output "[wait-report] trainer_active pid=$($trainer.Id) working_set_bytes=$($trainer.WorkingSet64)"
        if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) {
            Get-Content -LiteralPath $stdoutPath -Tail 3
        }
    } else {
        Write-Output '[wait-report] trainer is no longer active'
        if (Test-Path -LiteralPath $stderrPath -PathType Leaf) {
            $stderr = Get-Content -Raw -LiteralPath $stderrPath
            if (-not [string]::IsNullOrWhiteSpace($stderr)) {
                Write-Output $stderr
            }
        }
        throw "Training report was not created: $reportPath"
    }

    if ((Get-Date) -ge $deadline) {
        throw "Timed out waiting for training report: $reportPath"
    }

    Start-Sleep -Seconds $PollSeconds
}
