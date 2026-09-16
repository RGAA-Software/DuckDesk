#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('cloud_node', 'remote')][string]$Product,
    [switch]$PreflightOnly
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$arguments = @((Join-Path $repository 'scripts\publish_windows_node_public.py'), '--product', $Product, '--component', 'render')
if ($PreflightOnly) { $arguments += '--preflight-only' }
& python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Public Render deployment failed with exit code $LASTEXITCODE."
}
