# Functions only. A cycle spans two successive checkpoint publications.
function Get-Pb12DiagnosticP99Stop([object[]]$Cycles) {
    $cohorts = [Collections.Generic.List[object]]::new()
    for ($index = 0; ($index + 1) -lt $Cycles.Count; $index += 2) {
        $left = $Cycles[$index]
        $right = $Cycles[$index + 1]
        $cohorts.Add([pscustomobject]@{
            first_cycle = $index + 1
            p99_ms = [Math]::Max([double]$left.duration_ms, [double]$right.duration_ms)
            live_bytes = [Math]::Max([long]$left.live_bytes, [long]$right.live_bytes)
        })
    }
    if ($cohorts.Count -ge 3) {
        for ($index = 2; $index -lt $cohorts.Count; $index++) {
            $first, $second, $third = $cohorts[$index - 2], $cohorts[$index - 1], $cohorts[$index]
            if ($first.p99_ms -lt $second.p99_ms -and $second.p99_ms -lt $third.p99_ms -and
                $first.live_bytes -lt $second.live_bytes -and $second.live_bytes -lt $third.live_bytes) {
                return [pscustomobject]@{ reason = 'three increasing P99 cohorts'; evidence = @($first, $second, $third) }
            }
        }
    }
    # The nearest-rank P99 of a two-cycle cohort is its maximum. One cycle
    # above the preceding P99 is already a strict lower bound on that cohort.
    if ($Cycles.Count -ge 5 -and ($Cycles.Count % 2) -eq 1 -and $cohorts.Count -ge 2) {
        $first, $second = $cohorts[$cohorts.Count - 2], $cohorts[$cohorts.Count - 1]
        $partial = $Cycles[$Cycles.Count - 1]
        if ($first.p99_ms -lt $second.p99_ms -and $second.p99_ms -lt [double]$partial.duration_ms -and
            $first.live_bytes -lt $second.live_bytes -and $second.live_bytes -lt [long]$partial.live_bytes) {
            return [pscustomobject]@{ reason = 'third P99 cohort lower bound increased'; evidence = @($first, $second, $partial) }
        }
    }
    return $null
}
