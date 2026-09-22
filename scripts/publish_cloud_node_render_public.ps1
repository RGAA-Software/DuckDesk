#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ConsoleBase,
    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$arguments = @(
    (Join-Path $repository 'scripts\publish_windows_node_public.py'),
    '--product',
    'cloud_node',
    '--component',
    'render')
if ($ConsoleBase) {
    $arguments += @('--console-base', $ConsoleBase)
}
if ($PreflightOnly) {
    $arguments += '--preflight-only'
}
& python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Public Cloud Node Render deployment failed with exit code $LASTEXITCODE."
}
