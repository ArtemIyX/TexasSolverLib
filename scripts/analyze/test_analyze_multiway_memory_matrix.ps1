[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$analyzer = Join-Path $PSScriptRoot 'analyze_multiway_memory_matrix.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $analyzer -SelfTest
if ($LASTEXITCODE -ne 0) {
    throw "Memory matrix analyzer synthetic tests failed with exit code $LASTEXITCODE"
}
