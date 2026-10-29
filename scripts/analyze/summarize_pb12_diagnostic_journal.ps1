param(
    [Parameter(Mandatory = $true)][string]$Path
)

# Use only after the writer exits; a live append can change the final-line boundary.
$ErrorActionPreference = 'Stop'
$journalPath = (Resolve-Path -LiteralPath $Path).Path
$stream = [IO.File]::Open($journalPath, [IO.FileMode]::Open, [IO.FileAccess]::Read,
    [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
try {
    $length = $stream.Length
    $terminated = $true
    if ($length -gt 0) {
        $null = $stream.Seek(-1, [IO.SeekOrigin]::End)
        $terminated = $stream.ReadByte() -eq 10
    }
    $null = $stream.Seek(0, [IO.SeekOrigin]::Begin)
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
    try {
        $lineNumber = 0
        $pending = [long[]]::new(3)
        $pendingRecords = [long[]]::new(3)
        $pendingBatch = $null
        while ($stream.Position -lt $length -or -not $reader.EndOfStream) {
            $line = $reader.ReadLine()
            if ($null -eq $line) { break }
            $lineNumber++
            if ($reader.EndOfStream -and -not $terminated) {
                Write-Warning "Ignoring incomplete final line $lineNumber."
                break
            }
            try { $event = $line | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "Invalid complete JSON record at line ${lineNumber}: $($_.Exception.Message)" }
            switch ($event.event) {
                'start' { Write-Output ("start workflow_batch={0} storage_generation={1} store_batch={2}" -f $event.workflow_batch, $event.storage_generation, $event.store_committed_batch) }
                'component_publication' {
                    $component = [int]$event.component
                    if ($component -lt 0 -or $component -gt 2 -or [long]$event.records -le 0) { throw "Invalid publication at line $lineNumber" }
                    if ($null -ne $pendingBatch -and $pendingBatch -ne $event.workflow_batch) { throw "Publication batch changed without a completed batch at line $lineNumber" }
                    $pendingBatch = $event.workflow_batch
                    $pending[$component]++
                    $pendingRecords[$component] += [long]$event.records
                }
                'batch' {
                    if ($null -ne $pendingBatch -and $pendingBatch -ne $event.workflow_batch) { throw "Batch does not match tentative publications at line $lineNumber" }
                    $summary = [ordered]@{
                        event = 'batch_summary'
                        workflow_batch = $event.workflow_batch
                        publication_events = [ordered]@{ catalog = $pending[0]; rows = $pending[1]; pages = $pending[2] }
                        publication_records = [ordered]@{ catalog = $pendingRecords[0]; rows = $pendingRecords[1]; pages = $pendingRecords[2] }
                        batch_record = $event
                    }
                    Write-Output ($summary | ConvertTo-Json -Compress -Depth 12)
                    $pending = [long[]]::new(3)
                    $pendingRecords = [long[]]::new(3)
                    $pendingBatch = $null
                }
                'checkpoint' { Write-Output ("checkpoint batch={0} generation={1} store_batch={2} duration_ns={3} bytes={4}" -f $event.workflow_batch, $event.storage_generation, $event.store_committed_batch, $event.checkpoint_ns, $event.checkpoint_bytes) }
                default { throw "Unknown event at line $lineNumber" }
            }
        }
        if ($null -ne $pendingBatch) {
            Write-Warning "Batch $pendingBatch has tentative component publications but no completed batch record; do not infer aggregate commit."
        }
        if ($stream.Length -ne $length) { throw 'Journal changed during reading; rerun after the writer exits.' }
    } finally { $reader.Dispose() }
} finally { $stream.Dispose() }
