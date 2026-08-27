$vars = [Environment]::GetEnvironmentVariables('Process')

$paths = foreach ($entry in $vars.GetEnumerator()) {
    if ($entry.Key -ieq 'Path') {
        $entry.Value -split ';'
    }
}

$paths = $paths |
    Where-Object { $_ } |
    Select-Object -Unique

[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable(
    'Path',
    ($paths -join ';'),
    'Process'
)

& $args[0] $args[1..($args.Count - 1)]
exit $LASTEXITCODE