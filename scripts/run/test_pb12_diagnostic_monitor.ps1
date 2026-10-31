# Focused monitor checks. Run only with separate authorization.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'pb12_diagnostic_process.ps1')
. (Join-Path $PSScriptRoot 'pb12_diagnostic_p99.ps1')
. (Join-Path $PSScriptRoot 'pb12_diagnostic_journal_tail.ps1')
. (Join-Path $PSScriptRoot 'pb12_diagnostic_storage.ps1')
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

$trainerFixture = Join-Path ([IO.Path]::GetTempPath()) ('pb12-trainer-path-' + [guid]::NewGuid().ToString('N'))
try {
    $exampleTrainer = Join-Path $trainerFixture 'build/examples/Debug/texas_multiway_train.exe'
    $rootTrainer = Join-Path $trainerFixture 'build/Debug/texas_multiway_train.exe'
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $exampleTrainer)
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $rootTrainer)
    [IO.File]::WriteAllBytes($exampleTrainer, [byte[]]@(1))
    [IO.File]::WriteAllBytes($rootTrainer, [byte[]]@(2))
    [IO.File]::SetLastWriteTimeUtc($rootTrainer, [DateTime]::UtcNow.AddMinutes(-2))
    [IO.File]::SetLastWriteTimeUtc($exampleTrainer, [DateTime]::UtcNow.AddMinutes(-1))
    Require ((Get-Pb12DiagnosticTrainerExecutable $trainerFixture) -eq [IO.Path]::GetFullPath($exampleTrainer)) 'The newest configured trainer output must be selected over a stale binary.'
    [IO.File]::SetLastWriteTimeUtc($rootTrainer, [DateTime]::UtcNow)
    Require ((Get-Pb12DiagnosticTrainerExecutable $trainerFixture) -eq [IO.Path]::GetFullPath($rootTrainer)) 'A newer root-level trainer output must remain selectable.'
} finally {
    if (Test-Path -LiteralPath $trainerFixture) { [IO.Directory]::Delete($trainerFixture, $true) }
}

Require ($null -eq (Get-Pb12DiagnosticMemoryStop 40GB 48GB 3GB)) 'Limits must be strict.'
Require ((Get-Pb12DiagnosticMemoryStop (40GB + 1) 0 3GB) -eq 'working set exceeded 40 GiB') 'Working-set stop missing.'
Require ((Get-Pb12DiagnosticMemoryStop 0 (48GB + 1) 3GB) -eq 'private commit exceeded 48 GiB') 'Private-commit stop missing.'
Require ((Get-Pb12DiagnosticMemoryStop 0 0 (3GB - 1)) -eq 'available physical memory fell below 3 GiB') 'Available-memory stop missing.'
Require ((Get-Pb12DiagnosticCompletionStop 1 $true $false $false $false) -eq 'trainer exited unsuccessfully') 'A nonzero trainer exit must remain the primary completion reason.'
Require ((Get-Pb12DiagnosticCompletionStop 0 $true $true $false $true) -eq 'incomplete disk or checkpoint observations; no completed-run conclusion') 'Missing observations must remain inconclusive.'
Require ($null -eq (Get-Pb12DiagnosticDiskStop 100 300 0 $false)) 'Equal disk amplification must not stop.'
Require ($null -ne (Get-Pb12DiagnosticDiskStop 100 301 0 $false)) 'Excess disk amplification must stop.'
Require ($null -ne (Get-Pb12DiagnosticDiskStop 100 301 128MB $false)) 'An unconfirmed temporary cannot waive the stop.'
Require ($null -eq (Get-Pb12DiagnosticDiskStop 100 301 128MB $true)) 'Confirmed bounded compaction temporary is allowed.'
Require ($null -ne (Get-Pb12DiagnosticDiskStop 100 301 (128MB + 1) $true)) 'Oversized compaction temporary must stop.'
$diskJobDecision = Get-Pb12DiagnosticDiskJobDecision ([pscustomobject]@{ State = 'Completed' }) 0.65
Require ($diskJobDecision.Action -eq 'Complete' -and $diskJobDecision.State -eq 'Completed') 'A completed disk telemetry job must be consumed.'
$diskJobDecision = Get-Pb12DiagnosticDiskJobDecision ([pscustomobject]@{ State = 'Running' }) 0.65
Require ($diskJobDecision.Action -eq 'Wait') 'A running disk telemetry job must remain observable.'
$diskJobDecision = Get-Pb12DiagnosticDiskJobDecision ([pscustomobject]@{ State = 'Running' }) 31
Require ($diskJobDecision.Action -eq 'Stop') 'A disk telemetry job past its deadline must stop.'
$diskJobDecision = Get-Pb12DiagnosticDiskJobDecision ([pscustomobject]@{ State = 'Failed' }) 0.65
Require ($diskJobDecision.Action -eq 'Stop') 'A failed disk telemetry job must stop.'
$transitioningJob = [pscustomobject]@{ StateReads = 0 }
$transitioningJob | Add-Member -MemberType ScriptProperty -Name State -Value {
    $this.StateReads++
    if ($this.StateReads -eq 1) { 'Running' } else { 'Completed' }
}
$diskJobDecision = Get-Pb12DiagnosticDiskJobDecision $transitioningJob 0.65
Require ($diskJobDecision.Action -eq 'Wait' -and $diskJobDecision.State -eq 'Running' -and $transitioningJob.StateReads -eq 1) 'A job state transition between property reads must not create a false stop.'
Require ($null -eq (Get-Pb12ManifestAvailabilityStop 0 $false $false 3600)) 'An empty store may initialize while the bucket artifact loads.'
Require ($null -eq (Get-Pb12ManifestAvailabilityStop 0 $true $false 30)) 'The training-manifest grace boundary is inclusive.'
Require ($null -ne (Get-Pb12ManifestAvailabilityStop 0 $true $false 31)) 'Persistent files without a training manifest must stop.'
Require ($null -ne (Get-Pb12ManifestAvailabilityStop 1 $true $true 0)) 'A previously observed manifest must not disappear.'
Require ($null -eq (Get-Pb12ManifestAvailabilityStop 0 $true $true 31)) 'Unpublished component manifests may be absent.'
$cycles = @(
    [pscustomobject]@{duration_ms=10;live_bytes=100}, [pscustomobject]@{duration_ms=9;live_bytes=110},
    [pscustomobject]@{duration_ms=20;live_bytes=200}, [pscustomobject]@{duration_ms=19;live_bytes=210},
    [pscustomobject]@{duration_ms=21;live_bytes=220}
)
Require ($null -eq (Get-Pb12DiagnosticP99Stop $cycles[0..3])) 'Two cohorts cannot trigger the P99 stop.'
Require ((Get-Pb12DiagnosticP99Stop $cycles).reason -eq 'third P99 cohort lower bound increased') 'Third-cohort lower bound must stop.'
$cycles[4].duration_ms = 20
Require ($null -eq (Get-Pb12DiagnosticP99Stop $cycles)) 'Equal P99 must not trigger the stop.'
$retry2Cycles = @(
    [pscustomobject]@{duration_ms=61735.817;live_bytes=95219697},
    [pscustomobject]@{duration_ms=69640.4928;live_bytes=143844948},
    [pscustomobject]@{duration_ms=28253.2422;live_bytes=161110818},
    [pscustomobject]@{duration_ms=65828.6434;live_bytes=206790582},
    [pscustomobject]@{duration_ms=75226.4279;live_bytes=251849651},
    [pscustomobject]@{duration_ms=77450.4109;live_bytes=302007021},
    [pscustomobject]@{duration_ms=81176.363;live_bytes=345943612}
)
Require ($null -eq (Get-Pb12DiagnosticP99Stop $retry2Cycles[0..5])) 'Retry2 first three complete cohorts do not increase.'
$retry2Stop = Get-Pb12DiagnosticP99Stop $retry2Cycles
Require ($retry2Stop.reason -eq 'third P99 cohort lower bound increased' -and $retry2Stop.evidence[0].first_cycle -eq 3 -and $retry2Stop.evidence[1].first_cycle -eq 5) 'Retry2 sliding cohorts must stop on the seventh cycle.'

$fixture = Join-Path ([IO.Path]::GetTempPath()) ('pb12_monitor_' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixture
$journal = Join-Path $fixture 'journal.jsonl'
$checkpoint = Join-Path $fixture 'checkpoint.bin'
$store = Join-Path $fixture 'store'
$child = $null
$decoy = $null
try {
    $published = Join-Path $fixture 'published'
    $replacement = Join-Path $fixture 'replacement'
    $backup = Join-Path $fixture 'backup'
    [IO.File]::WriteAllText($published, 'old')
    [IO.File]::WriteAllText($replacement, 'new')
    $reader = Open-Pb12SharedRead $published
    try {
        [IO.File]::Replace($replacement, $published, $backup)
        $old = [IO.StreamReader]::new($reader)
        try { Require ($old.ReadToEnd() -eq 'old') 'An open reader must retain the old generation.' }
        finally { $old.Dispose() }
        Require ([Text.Encoding]::UTF8.GetString((Read-Pb12SharedBytes $published)) -eq 'new') 'A new reader must observe the replacement.'
        Require ((Get-Pb12SharedSha256 $published) -eq (Get-FileHash -LiteralPath $published -Algorithm SHA256).Hash) 'Shared hashing must preserve SHA-256.'
    } finally { $reader.Dispose() }
    Remove-Item -LiteralPath $published
    [IO.File]::WriteAllText($journal, "{`"event`":`"start`"}`n{`"event`":`"batch`"", [Text.Encoding]::ASCII)
    $state = @{ Offset = [long]0; Pending = '' }
    $events = @(Read-Pb12DiagnosticJournalTail $journal $state)
    Require ($events.Count -eq 1 -and $events[0].event -eq 'start') 'Only complete journal lines may be read.'
    Require ($state.Pending.Length -gt 0) 'Incomplete journal line was not retained.'
    [IO.File]::AppendAllText($journal, "}`n", [Text.Encoding]::ASCII)
    $events = @(Read-Pb12DiagnosticJournalTail $journal $state)
    Require ($events.Count -eq 1 -and $events[0].event -eq 'batch' -and $state.Pending.Length -eq 0) 'Completed journal line was not emitted exactly once.'
    [IO.File]::WriteAllText($journal, '', [Text.Encoding]::ASCII)
    $failed = $false
    try { $null = Read-Pb12DiagnosticJournalTail $journal $state } catch { $failed = $true }
    Require $failed 'Journal truncation must fail closed.'
    [IO.File]::WriteAllText($journal, (('x' * 65537) + "`n"), [Text.Encoding]::ASCII)
    $state = @{ Offset = [long]0; Pending = '' }
    $null = Read-Pb12DiagnosticJournalTail $journal $state
    $failed = $false
    try { $null = Read-Pb12DiagnosticJournalTail $journal $state } catch { $failed = $true }
    Require $failed 'An oversized complete journal record must fail closed.'

    $header = [byte[]]::new(88)
    [Text.Encoding]::ASCII.GetBytes('TXMWCP61').CopyTo($header, 0)
    [BitConverter]::GetBytes([uint32]6).CopyTo($header, 8)
    [BitConverter]::GetBytes([uint32]1).CopyTo($header, 12)
    [BitConverter]::GetBytes([uint64]14).CopyTo($header, 72)
    [BitConverter]::GetBytes([uint64]12).CopyTo($header, 80)
    [IO.File]::WriteAllBytes($checkpoint, $header)
    $parsed = Get-Pb12CheckpointHeader $checkpoint
    Require ($parsed.training_generation -eq 14 -and $parsed.committed_batch -eq 12) 'Checkpoint generation and store batch must remain distinct.'
    [IO.File]::WriteAllBytes($checkpoint, [byte[]]::new(8))
    $failed = $false
    try { $null = Get-Pb12CheckpointHeader $checkpoint } catch { $failed = $true }
    Require $failed 'Truncated checkpoint header must fail closed.'

    $null = New-Item -ItemType Directory -Path $store
    foreach ($component in @('catalog', 'rows', 'pages')) {
        $directory = Join-Path $store $component
        $null = New-Item -ItemType Directory -Path $directory
        $manifest = [byte[]]::new(72)
        [Text.Encoding]::ASCII.GetBytes('TXMWEXT1').CopyTo($manifest, 0)
        [BitConverter]::GetBytes([uint32]1).CopyTo($manifest, 8)
        [BitConverter]::GetBytes([uint32]8).CopyTo($manifest, 12)
        [BitConverter]::GetBytes([uint64]1).CopyTo($manifest, 16)
        [BitConverter]::GetBytes([uint64]1).CopyTo($manifest, 24)
        [BitConverter]::GetBytes([uint64]1).CopyTo($manifest, 40)
        [BitConverter]::GetBytes([uint64]1).CopyTo($manifest, 48)
        [BitConverter]::GetBytes([Pb12DiagnosticChecksum]::Compute($manifest, 64)).CopyTo($manifest, 64)
        [IO.File]::WriteAllBytes((Join-Path $directory 'CURRENT'), $manifest)
        [IO.File]::WriteAllBytes((Join-Path $directory 'run_1.dat'), [byte[]]::new(16))
        [IO.File]::WriteAllBytes((Join-Path $directory 'run_1.idx'), [byte[]]::new(8))
    }
    [IO.File]::WriteAllBytes((Join-Path $store 'unreferenced.dat'), [byte[]]::new(300))
    $usage = Get-ExternalStorageUsage $store
    Require ($usage.live_bytes -eq 72 -and $usage.store_bytes -ge 372) 'Disk accounting must include live and unreferenced files.'
    $pageManifest = Join-Path $store 'pages/CURRENT'
    $corrupt = [IO.File]::ReadAllBytes($pageManifest)
    $corrupt[32] = $corrupt[32] -bxor 1
    [IO.File]::WriteAllBytes($pageManifest, $corrupt)
    $failed = $false
    try { $null = Get-ExternalStorageUsage $store } catch { $failed = $true }
    Require $failed 'Corrupt manifest checksum must fail closed.'
    $corrupt[32] = $corrupt[32] -bxor 1
    [IO.File]::WriteAllBytes($pageManifest, $corrupt)
    Remove-Item -LiteralPath (Join-Path $store 'pages/run_1.idx')
    $failed = $false
    try { $null = Get-ExternalStorageUsage $store } catch { $failed = $true }
    Require $failed 'A missing referenced segment must fail closed.'
    Remove-Item -LiteralPath (Join-Path $store 'pages/CURRENT')
    $usage = Get-ExternalStorageUsage $store -AllowAbsentComponents
    Require ($usage.live_bytes -eq 48 -and $usage.store_bytes -ge 348) 'A never-published component must be counted conservatively.'

    $stableStore = Join-Path $fixture 'stable-store'
    $null = New-Item -ItemType Directory -Path $stableStore
    [IO.File]::WriteAllText((Join-Path $stableStore 'TRAINING_CURRENT'), 'stable')
    $hashImplementation = (Get-Item Function:\Get-Pb12SharedSha256).ScriptBlock
    $script:stableHashCalls = 0
    Set-Item Function:\Get-Pb12SharedSha256 -Force -Value {
        $script:stableHashCalls++
        if ($script:stableHashCalls -eq 1) { return 'before-publication' }
        if ($script:stableHashCalls -eq 2) { return 'during-publication' }
        return 'stable-generation'
    }
    try {
        $stableUsage = Get-StableExternalStorageUsage $stableStore -AllowAbsentComponents -RetryWindowMilliseconds 500
        Require ($stableUsage.live_bytes -eq 0 -and $script:stableHashCalls -eq 4) 'Transient manifest publication must retry to a stable storage sample.'
        $script:stableHashCalls = 0
        Set-Item Function:\Get-Pb12SharedSha256 -Force -Value {
            $script:stableHashCalls++
            return "generation-$script:stableHashCalls"
        }
        $failed = $false
        try { $null = Get-StableExternalStorageUsage $stableStore -AllowAbsentComponents -RetryWindowMilliseconds 0 }
        catch { $failed = $true }
        Require ($failed -and $script:stableHashCalls -eq 2) 'A telemetry sample that never stabilizes must still fail closed.'
    } finally {
        Set-Item Function:\Get-Pb12SharedSha256 -Force -Value $hashImplementation
    }

    $shell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    foreach ($expectedExit in @(0, 7)) {
        $exitChild = Start-Process -FilePath $shell -ArgumentList "-NoProfile -NonInteractive -Command `"Start-Sleep -Seconds 1; exit $expectedExit`"" -PassThru -WindowStyle Hidden
        $null = $exitChild.Handle
        $exitChild.WaitForExit()
        $exitChild.Refresh()
        Require ($exitChild.ExitCode -eq $expectedExit) 'The retained process handle must preserve the exit code after refresh.'
        $exitChild.Dispose()
    }
    $arguments = '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 30"'
    $child = Start-Process -FilePath $shell -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $decoy = Start-Process -FilePath $shell -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $null = $child.Handle
    $null = $decoy.Handle
    Stop-Pb12DiagnosticProcess $child
    $decoy.Refresh()
    Require (-not $decoy.HasExited) 'Stopping the diagnostic process must not stop another process.'
} finally {
    foreach ($name in @('published', 'replacement', 'backup')) {
        $file = Join-Path $fixture $name
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    if ($null -ne $child) { Stop-Pb12DiagnosticProcess $child }
    if ($null -ne $decoy) { Stop-Pb12DiagnosticProcess $decoy }
    if (Test-Path -LiteralPath $journal) { Remove-Item -LiteralPath $journal }
    if (Test-Path -LiteralPath $checkpoint) { Remove-Item -LiteralPath $checkpoint }
    foreach ($component in @('catalog', 'rows', 'pages')) {
        $directory = Join-Path $store $component
        foreach ($name in @('CURRENT', 'run_1.dat', 'run_1.idx')) {
            $file = Join-Path $directory $name
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
        }
        if (Test-Path -LiteralPath $directory) { Remove-Item -LiteralPath $directory }
    }
    if (Test-Path -LiteralPath (Join-Path $store 'unreferenced.dat')) { Remove-Item -LiteralPath (Join-Path $store 'unreferenced.dat') }
    if (Test-Path -LiteralPath $store) { Remove-Item -LiteralPath $store }
    $stableStore = Join-Path $fixture 'stable-store'
    if (Test-Path -LiteralPath $stableStore) { [IO.Directory]::Delete($stableStore, $true) }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture }
}
Write-Output 'PB-12 diagnostic monitor checks passed.'
