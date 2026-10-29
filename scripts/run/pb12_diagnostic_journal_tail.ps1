# Bounded nonblocking journal tail. The caller retains State between polls.
function Read-Pb12DiagnosticJournalTail([string]$Path, [hashtable]$State) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        if ($stream.Length -lt [long]$State.Offset) { throw 'Diagnostic journal was truncated.' }
        $null = $stream.Seek([long]$State.Offset, [IO.SeekOrigin]::Begin)
        $buffer = [byte[]]::new(65536)
        $count = $stream.Read($buffer, 0, $buffer.Length)
        $State.Offset = [long]$State.Offset + $count
        # The journal schema contains only ASCII field names and numbers.
        for ($index = 0; $index -lt $count; $index++) {
            if ($buffer[$index] -gt 127) { throw 'Diagnostic journal contains non-ASCII data.' }
        }
        $text = [string]$State.Pending + [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
        $lines = $text.Split("`n")
        $State.Pending = $lines[-1]
        if ($State.Pending.Length -gt 65536) { throw 'Diagnostic journal record exceeds its bound.' }
        for ($index = 0; $index -lt $lines.Length - 1; $index++) {
            if ($lines[$index].Length -eq 0 -or $lines[$index].Length -gt 65536) {
                throw 'Diagnostic journal contains an empty or oversized record.'
            }
            try { $lines[$index] | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "Invalid complete diagnostic journal record: $($_.Exception.Message)" }
        }
    } finally { $stream.Dispose() }
}
