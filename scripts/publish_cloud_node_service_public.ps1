#requires -Version 5.1

[CmdletBinding()]
param(
    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$arguments = @(
    (Join-Path $repository 'scripts\publish_windows_node_public.py'),
    '--product',
    'cloud_node',
    '--component',
    'service')
if ($PreflightOnly) {
    $arguments += '--preflight-only'
}
& python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Public Cloud Node Service deployment failed with exit code $LASTEXITCODE."
}
