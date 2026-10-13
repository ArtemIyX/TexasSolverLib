if ($args.Count -eq 0) {
    throw 'A command is required.'
}

$pathEntries = @(
    [Environment]::GetEnvironmentVariables('Process').GetEnumerator() |
        Where-Object { $_.Key -ieq 'Path' }
)
$paths = $pathEntries.Value -split ';' |
    Where-Object { $_ } |
    Select-Object -Unique

# A malformed inherited environment can contain both Path and PATH. Delete
# every copy before adding one canonical entry for child processes.
while ($true) {
    $duplicates = @(
        [Environment]::GetEnvironmentVariables('Process').GetEnumerator() |
            Where-Object { $_.Key -ieq 'Path' }
    )
    if ($duplicates.Count -eq 0) {
        break
    }
    foreach ($entry in $duplicates) {
        [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
    }
}

[Environment]::SetEnvironmentVariable('Path', ($paths -join ';'), 'Process')

$command = $args[0]
$commandArgs = @($args | Select-Object -Skip 1)
& $command @commandArgs
exit $LASTEXITCODE
