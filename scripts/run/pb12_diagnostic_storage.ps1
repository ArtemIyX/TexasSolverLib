# Derived from the preserved Stage 9.4 runner. Loading defines checksum helpers.
if (-not ('Pb12DiagnosticChecksum' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
public static class Pb12DiagnosticChecksum {
    public static ulong Compute(byte[] bytes, int count) {
        ulong hash = 14695981039346656037UL;
        unchecked { for (int i = 0; i < count; ++i) { hash ^= bytes[i]; hash *= 1099511628211UL; } }
        return hash == 0 ? 1UL : hash;
    }
    public static bool Valid(byte[] bytes) {
        return bytes.Length >= 8 && Compute(bytes, bytes.Length - 8) == BitConverter.ToUInt64(bytes, bytes.Length - 8);
    }
}
'@
}
function Read-U32([byte[]]$Bytes, [int]$Offset) { return [BitConverter]::ToUInt32($Bytes, $Offset) }
function Read-U64([byte[]]$Bytes, [int]$Offset) { return [BitConverter]::ToUInt64($Bytes, $Offset) }

function Get-Pb12CheckpointHeader([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $header = [byte[]]::new(88)
        $offset = 0
        while ($offset -lt $header.Length) {
            $read = $stream.Read($header, $offset, $header.Length - $offset)
            if ($read -eq 0) { throw 'Checkpoint header is truncated.' }
            $offset += $read
        }
        if ([Text.Encoding]::ASCII.GetString($header, 0, 8) -ne 'TXMWCP61' -or
            (Read-U32 $header 8) -ne 6 -or (Read-U32 $header 12) -ne 1) {
            throw 'Checkpoint header is invalid.'
        }
        return [pscustomobject]@{ training_generation = (Read-U64 $header 72); committed_batch = (Read-U64 $header 80) }
    } finally { $stream.Dispose() }
}
function Get-OptionalFileLength([string]$Path) {
    try {
        return [int64](Get-Item -LiteralPath $Path -ErrorAction Stop).Length
    } catch {
        if ($_.CategoryInfo.Category -eq [System.Management.Automation.ErrorCategory]::ObjectNotFound -or $_.Exception -is [IO.FileNotFoundException] -or $_.Exception -is [IO.DirectoryNotFoundException]) { return [int64]0 }
        throw
    }
}

function Get-ExternalStorageUsage([string]$Path, [hashtable]$OrphanReferences = @{}, [switch]$AllowAbsentComponents) {
    $liveBytes = [int64]0
    $retainedBytes = [int64]0
    $controlBytes = [int64]0
    $temporaryBytes = [int64]0
    $compactionTempBytes = [int64]0
    $liveGenerations = @{}
    $retainedGenerations = @{}
    $payloadByComponent = @{}
    foreach ($component in @('catalog', 'rows', 'pages')) {
        $componentPath = Join-Path $Path $component
        $manifestPath = Join-Path $componentPath 'CURRENT'
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            if ($AllowAbsentComponents) { continue }
            throw "Missing storage telemetry manifest: $manifestPath"
        }
        $bytes = [IO.File]::ReadAllBytes($manifestPath)
        if ($bytes.Length -lt 48 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'TXMWEXT1' -or (Read-U32 $bytes 8) -ne 1) {
            throw "Invalid storage telemetry manifest: $manifestPath"
        }
        if (-not [Pb12DiagnosticChecksum]::Valid($bytes)) { throw "Storage manifest checksum mismatch: $manifestPath" }
        $payloadBytes = [int64](Read-U32 $bytes 12)
        $payloadByComponent[$component] = $payloadBytes
        $controlBytes += $bytes.Length
        $referenceCount = [int64](Read-U64 $bytes 24)
        if ($referenceCount -gt 1000000 -or $bytes.Length -ne (48 + 24 * $referenceCount)) { throw "Invalid storage manifest reference count: $manifestPath" }
        for ($index = 0; $index -lt $referenceCount; $index++) {
            $offset = 40 + (24 * $index)
            $generation = Read-U64 $bytes $offset
            $recordCount = [int64](Read-U64 $bytes ($offset + 8))
            $expectedData = $recordCount * ($payloadBytes + 8)
            $expectedIndex = $recordCount * 8
            $base = Join-Path $componentPath ("run_{0}" -f $generation)
            $actualData = [int64](Get-Item -LiteralPath "$base.dat" -ErrorAction Stop).Length
            $actualIndex = [int64](Get-Item -LiteralPath "$base.idx" -ErrorAction Stop).Length
            if ($actualData -ne $expectedData -or $actualIndex -ne $expectedIndex) {
                throw "Referenced storage run has an unexpected length: $base"
            }
            $liveBytes += $expectedData + $expectedIndex
            $liveGenerations["$component`:$generation"] = $true
        }

        foreach ($pinPath in [IO.Directory]::EnumerateFiles($componentPath, 'CHECKPOINT_*.PIN', [IO.SearchOption]::TopDirectoryOnly)) {
            $pin = [IO.File]::ReadAllBytes($pinPath)
            if ($pin.Length -lt 28 -or [Text.Encoding]::ASCII.GetString($pin, 0, 8) -ne 'TXMWPIN1' -or (Read-U32 $pin 8) -ne 1) { throw "Invalid checkpoint pin manifest: $pinPath" }
            if (-not [Pb12DiagnosticChecksum]::Valid($pin)) { throw "Checkpoint pin checksum mismatch: $pinPath" }
            $pinCount = [int64](Read-U64 $pin 12)
            if ($pinCount -gt 1000000 -or $pin.Length -ne (28 + 24 * $pinCount)) { throw "Invalid checkpoint pin reference count: $pinPath" }
            $controlBytes += $pin.Length
            for ($pinIndex = 0; $pinIndex -lt $pinCount; $pinIndex++) {
                $pinOffset = 20 + (24 * $pinIndex)
                $generation = Read-U64 $pin $pinOffset
                $recordCount = [int64](Read-U64 $pin ($pinOffset + 8))
                $generationKey = "$component`:$generation"
                if ($liveGenerations.ContainsKey($generationKey) -or $retainedGenerations.ContainsKey($generationKey)) { continue }
                $base = Join-Path $componentPath ("run_{0}" -f $generation)
                $actualData = [int64](Get-Item -LiteralPath "$base.dat" -ErrorAction Stop).Length
                $actualIndex = [int64](Get-Item -LiteralPath "$base.idx" -ErrorAction Stop).Length
                if ($actualData -ne ($recordCount * ($payloadBytes + 8)) -or $actualIndex -ne ($recordCount * 8)) {
                    throw "Pinned storage run has an unexpected length: $base"
                }
                $retainedBytes += ($recordCount * ($payloadBytes + 8)) + ($recordCount * 8)
                $retainedGenerations[$generationKey] = $true
            }
        }
        $walPath = Join-Path $componentPath 'BATCH.wal'
        $controlBytes += Get-OptionalFileLength $walPath
        foreach ($tempPath in [IO.Directory]::EnumerateFiles($componentPath, '*.tmp', [IO.SearchOption]::TopDirectoryOnly)) {
            $size = Get-OptionalFileLength $tempPath
            $temporaryBytes += $size
            if ([IO.Path]::GetFileName($tempPath) -match '^run_\d+\.(dat|idx)\.tmp$') { $compactionTempBytes += $size }
        }
    }
    $orphanBytes = [int64]0
    foreach ($referenceKey in @($OrphanReferences.Keys)) {
        if ($liveGenerations.ContainsKey($referenceKey) -or $retainedGenerations.ContainsKey($referenceKey)) { continue }
        $reference = $OrphanReferences[$referenceKey]
        $componentPath = Join-Path $Path $reference.component
        $base = Join-Path $componentPath ("run_{0}" -f $reference.generation)
        $dataPath = "$base.dat"
        $indexPath = "$base.idx"
        $dataBytes = Get-OptionalFileLength $dataPath
        $indexBytes = Get-OptionalFileLength $indexPath
        if ($dataBytes -eq 0 -and $indexBytes -eq 0) { $OrphanReferences.Remove($referenceKey) | Out-Null; continue }
        $payloadBytes = [int64]$payloadByComponent[$reference.component]
        if (($dataBytes -gt 0 -and $dataBytes -ne ($reference.record_count * ($payloadBytes + 8))) -or ($indexBytes -gt 0 -and $indexBytes -ne ($reference.record_count * 8))) { throw "Orphaned compaction segment size mismatch: $base" }
        $orphanBytes += $dataBytes + $indexBytes
    }
    $trainingManifest = Join-Path $Path 'TRAINING_CURRENT'
    $controlBytes += Get-OptionalFileLength $trainingManifest
    foreach ($controlPath in [IO.Directory]::EnumerateFiles($Path, '*.wal', [IO.SearchOption]::TopDirectoryOnly)) {
        $controlBytes += Get-OptionalFileLength $controlPath
    }
    foreach ($tempPath in [IO.Directory]::EnumerateFiles($Path, '*.tmp', [IO.SearchOption]::TopDirectoryOnly)) { $temporaryBytes += Get-OptionalFileLength $tempPath }
    # Count every file, including unreferenced runs not present in checkpoint
    # history. Missing files during enumeration invalidate this sample.
    $storeBytes = [int64]0
    foreach ($filePath in [IO.Directory]::EnumerateFiles($Path, '*', [IO.SearchOption]::AllDirectories)) {
        $storeBytes += [int64](Get-Item -LiteralPath $filePath -ErrorAction Stop).Length
    }
    [pscustomobject]@{ live_bytes = $liveBytes; retained_bytes = $retainedBytes; orphan_bytes = $orphanBytes; store_bytes = $storeBytes; control_bytes = $controlBytes; temporary_bytes = $temporaryBytes; compaction_temp_bytes = $compactionTempBytes }
}

function Get-StableExternalStorageUsage([string]$Path, [hashtable]$OrphanReferences = @{}, [switch]$AllowAbsentComponents) {
    $lastError = $null
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        try {
            $manifests = @('TRAINING_CURRENT', 'catalog/CURRENT', 'rows/CURRENT', 'pages/CURRENT')
            $before = foreach ($name in $manifests) {
                $manifestPath = Join-Path $Path $name
                if ($AllowAbsentComponents -and $name -ne 'TRAINING_CURRENT' -and -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { 'ABSENT' }
                else { (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256 -ErrorAction Stop).Hash }
            }
            $usage = Get-ExternalStorageUsage $Path $OrphanReferences -AllowAbsentComponents:$AllowAbsentComponents
            $after = foreach ($name in $manifests) {
                $manifestPath = Join-Path $Path $name
                if ($AllowAbsentComponents -and $name -ne 'TRAINING_CURRENT' -and -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { 'ABSENT' }
                else { (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256 -ErrorAction Stop).Hash }
            }
            if (($before -join ',') -ne ($after -join ',')) { throw 'Storage manifests changed during sampling' }
            return $usage
        } catch {
            $lastError = $_
            if ($attempt -lt 2) { Start-Sleep -Milliseconds 25 }
        }
    }
    throw $lastError
}

function Get-CheckpointMetadata([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 196 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'TXMWCP61' -or (Read-U32 $bytes 8) -ne 6 -or (Read-U32 $bytes 12) -ne 1) {
        throw "Invalid external checkpoint manifest: $Path"
    }
    $segmentReferences = [Collections.Generic.List[object]]::new()
    $cursor = 196
    foreach ($component in @('catalog', 'rows', 'pages')) {
        if ($cursor + 8 -gt ($bytes.Length - 8)) { throw "Truncated checkpoint segment list: $Path" }
        $referenceCount = [int64](Read-U64 $bytes $cursor)
        $cursor += 8
        if ($referenceCount -gt 1000000 -or $referenceCount -gt (($bytes.Length - 8 - $cursor) / 24)) { throw "Invalid checkpoint segment count: $Path" }
        for ($index = 0; $index -lt $referenceCount; $index++) {
            $generation = Read-U64 $bytes $cursor
            $recordCount = Read-U64 $bytes ($cursor + 8)
            $segmentReferences.Add([pscustomobject]@{ key = "$component`:$generation"; component = $component; generation = $generation; record_count = $recordCount })
            $cursor += 24
        }
    }
    if ($cursor + 8 -gt ($bytes.Length - 8) -or (Read-U32 $bytes ($cursor + 4)) -ne 1) { throw "Invalid checkpoint training extension: $Path" }
    $cursor += 8 + (14 * 8) + 4
    if ($cursor + 8 -gt ($bytes.Length - 8)) { throw "Truncated checkpoint training extension: $Path" }
    $baselineCount = [int64](Read-U64 $bytes $cursor)
    $cursor += 8 + (8 * $baselineCount)
    if ($baselineCount -gt 1000000 -or $cursor -ne ($bytes.Length - 8)) { throw "Invalid checkpoint training extension length: $Path" }
    [pscustomobject]@{
        bytes = $bytes.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        training_generation = (Read-U64 $bytes 72)
        committed_batch = (Read-U64 $bytes 80)
        segment_references = @($segmentReferences)
    }
}
