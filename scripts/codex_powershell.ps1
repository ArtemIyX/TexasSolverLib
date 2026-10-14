if ($args.Count -eq 0) {
    throw 'A command is required.'
}

$pathValues = @(
    [Environment]::GetEnvironmentVariables('Process').GetEnumerator() |
        Where-Object { $_.Key -ieq 'Path' }
)
$paths = $pathValues.Value -split ';' |
    Where-Object { $_ } |
    Select-Object -Unique

# Keep one normalized PATH value for child processes. Environment variable
# names are case-insensitive on Windows, so deleting every case variant can
# loop indefinitely when the host exposes duplicate entries.
[Environment]::SetEnvironmentVariable('Path', ($paths -join ';'), 'Process')

$command = $args[0]
$commandArgs = @($args | Select-Object -Skip 1)
& $command @commandArgs
exit $LASTEXITCODE
