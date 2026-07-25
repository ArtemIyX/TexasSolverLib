param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Debug",
    [int]$MaxReasonLength = 300
)

$ErrorActionPreference = "Stop"

function Limit-Text {
    param(
        [string]$Text,
        [int]$MaximumLength
    )

    $Text = ($Text -replace '\s+', ' ').Trim()

    if ($Text.Length -gt $MaximumLength) {
        return $Text.Substring(0, $MaximumLength - 3) + "..."
    }

    return $Text
}

function Parse-SourceLocation {
    param([string]$Text)

    $extensions = 'c|cc|cpp|cxx|h|hh|hpp|hxx|inl|py|cs|rs'

    # MSVC:
    # C:\project\test.cpp(42): expected true
    $match = [regex]::Match(
        $Text,
        "^(?<file>.+?\.(?:$extensions))\((?<line>\d+)(?:,\d+)?\)\s*:?\s*(?<reason>.*)$",
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
    )

    if (-not $match.Success) {
        # GCC/Clang/custom:
        # C:\project\test.cpp:42 expected true
        # /project/test.cpp:42: error: expected true
        $match = [regex]::Match(
            $Text,
            "^(?<file>.+\.(?:$extensions)):(?<line>\d+)(?::\d+)?(?:\s*:\s*|\s+)?(?<reason>.*)$",
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
        )
    }

    if (-not $match.Success) {
        return $null
    }

    $reason = $match.Groups["reason"].Value
    $reason = $reason -replace '^(?:fatal\s+)?(?:error|failure)\s*:\s*', ''

    return [PSCustomObject]@{
        Location = "{0}:{1}" -f `
            [System.IO.Path]::GetFileName($match.Groups["file"].Value),
            $match.Groups["line"].Value

        Reason = $reason.Trim()
    }
}

function Get-FailureRecords {
    param(
        [System.Xml.XmlElement]$TestCase,
        [int]$MaximumLength
    )

    $parts = [System.Collections.Generic.List[string]]::new()

    foreach ($node in @(
        $TestCase.SelectSingleNode("failure"),
        $TestCase.SelectSingleNode("error"),
        $TestCase.SelectSingleNode("system-out"),
        $TestCase.SelectSingleNode("system-err")
    )) {
        if ($null -eq $node) {
            continue
        }

        if ($node.Attributes["message"]) {
            $parts.Add($node.Attributes["message"].Value)
        }

        if ($node.InnerText) {
            $parts.Add($node.InnerText)
        }
    }

    $lines = @(
        (($parts -join "`n") -split "`r?`n") |
            ForEach-Object { $_.Trim() } |
            Where-Object {
                $_ -and

                # Never use successful internal tests as failure reasons.
                $_ -notmatch '^\[PASS\]\s*' -and
                $_ -notmatch '^\[\s*(RUN|OK)\s*\]' -and

                # Remove CTest/framework summary noise.
                $_ -notmatch '^\[[-=]+\]$' -and
                $_ -notmatch '^Running \d+ tests?' -and
                $_ -notmatch '^\d+% tests passed' -and
                $_ -notmatch '^The following tests FAILED' -and
                $_ -notmatch '^\d+\/\d+ Test' -and
                $_ -notmatch '^\s*Start \d+:'
            }
    )

    $records = [System.Collections.Generic.List[object]]::new()

    # Preferred format produced by the test executable:
    #
    # [FAIL] test_name: C:\project\test.cpp:42 expected value
    foreach ($line in $lines) {
        $match = [regex]::Match(
            $line,
            '^\[FAIL\]\s*(?<name>.*?)(?:\s*:\s*|\s+-\s+)(?<details>.+)$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
        )

        if (-not $match.Success) {
            continue
        }

        $internalName = $match.Groups["name"].Value.Trim()
        $details = $match.Groups["details"].Value.Trim()
        $location = Parse-SourceLocation -Text $details

        if ($null -ne $location) {
            $reason = $location.Reason

            if (-not $reason) {
                $reason = "test assertion failed"
            }

            $records.Add([PSCustomObject]@{
                Name     = $internalName
                Location = $location.Location
                Reason   = Limit-Text $reason $MaximumLength
            })
        }
        else {
            $records.Add([PSCustomObject]@{
                Name     = $internalName
                Location = $null
                Reason   = Limit-Text $details $MaximumLength
            })
        }
    }

    if ($records.Count -gt 0) {
        return $records
    }

    # No explicit [FAIL] line. Search for a source location.
    foreach ($line in $lines) {
        $location = Parse-SourceLocation -Text $line

        if ($null -ne $location) {
            $reason = $location.Reason

            if (-not $reason) {
                $reason = "test assertion failed"
            }

            $records.Add([PSCustomObject]@{
                Name     = $TestCase.name
                Location = $location.Location
                Reason   = Limit-Text $reason $MaximumLength
            })

            return $records
        }
    }

    # Look for an error-like line rather than arbitrary program output.
    $errorLine = $lines |
        Where-Object {
            $_ -match '(?i)\b(error|failed|failure|assert|expected|exception|abort|crash|segmentation|access violation)\b'
        } |
        Select-Object -Last 1

    if (-not $errorLine) {
        $errorLine = "test process returned a non-zero exit code"
    }

    $records.Add([PSCustomObject]@{
        Name     = $TestCase.name
        Location = $null
        Reason   = Limit-Text $errorLine $MaximumLength
    })

    return $records
}

try {
    $resolvedBuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
    $resultsFile = Join-Path $resolvedBuildDir "ctest-results.xml"

    Remove-Item `
        -LiteralPath $resultsFile `
        -Force `
        -ErrorAction SilentlyContinue

    & ctest `
        --test-dir $resolvedBuildDir `
        -C $Configuration `
        -Q `
        --no-tests=error `
        --output-junit $resultsFile *> $null

    $ctestExitCode = $LASTEXITCODE

    if (-not (Test-Path -LiteralPath $resultsFile)) {
        Write-Output "[FAIL] CTest - result XML was not created"
        exit $(if ($ctestExitCode -ne 0) { $ctestExitCode } else { 1 })
    }

    [xml]$results = Get-Content -LiteralPath $resultsFile -Raw

    $failedTests = @(
        $results.SelectNodes("//testcase[failure or error]")
    )

    if ($failedTests.Count -eq 0 -and $ctestExitCode -eq 0) {
        Write-Output "ALL TESTS PASSED"
        exit 0
    }

    if ($failedTests.Count -eq 0) {
        Write-Output (
            "[FAIL] CTest - exited with code {0} without failure details" -f
            $ctestExitCode
        )

        exit $(if ($ctestExitCode -ne 0) { $ctestExitCode } else { 1 })
    }

    foreach ($testCase in $failedTests) {
        $records = Get-FailureRecords `
            -TestCase $testCase `
            -MaximumLength $MaxReasonLength

        foreach ($record in $records) {
            if ($record.Location) {
                Write-Output (
                    "[FAIL] {0} : {1} - {2}" -f
                    $record.Name,
                    $record.Location,
                    $record.Reason
                )
            }
            else {
                Write-Output (
                    "[FAIL] {0} - {1}" -f
                    $record.Name,
                    $record.Reason
                )
            }
        }
    }

    exit $(if ($ctestExitCode -ne 0) { $ctestExitCode } else { 1 })
}
catch {
    $message = Limit-Text $_.Exception.Message $MaxReasonLength
    Write-Output "[FAIL] CTest runner - $message"
    exit 1
}