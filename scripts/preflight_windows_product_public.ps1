#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ComputerName,
    [Parameter(Mandatory)][ValidateSet('cloud_node', 'remote')][string]$Product,
    [string]$MachineFile = ''
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($MachineFile)) {
    $MachineFile = Join-Path $repository '.env\test_machine.md'
}
$machineText = Get-Content -LiteralPath $MachineFile -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) {
    throw 'Public test host machine-qualified credential is incomplete.'
}

$productDirectory = if ($Product -eq 'cloud_node') {
    'C:\Program Files\Pixels Cloud Node'
} else {
    'C:\Program Files\Pixels Remote'
}
$credential = [pscredential]::new(
    "$machineName\Administrator",
    (ConvertTo-SecureString $password -AsPlainText -Force)
)
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$canUpdateTrustedHosts = Test-Path -LiteralPath $trustedHostsPath
$previousTrustedHosts = if ($canUpdateTrustedHosts) { (Get-Item -LiteralPath $trustedHostsPath).Value } else { $null }
$session = $null
try {
    if ($canUpdateTrustedHosts) { Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force }
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    Invoke-Command -Session $session -ArgumentList $Product, $productDirectory -ScriptBlock {
        param($expectedProduct, $directory)
        $descriptorPath = Join-Path $directory 'product-manifest.json'
        if (-not (Test-Path -LiteralPath $descriptorPath -PathType Leaf)) {
            throw "Current $expectedProduct product is not installed at $directory"
        }
        $descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
        if ($descriptor.schema_version -ne 2 -or $descriptor.company -ne 'Pixels' -or $descriptor.product -ne $expectedProduct) {
            throw 'Installed public-node product descriptor does not match the requested product.'
        }
        [pscustomobject]@{
            product = $descriptor.product
            company = $descriptor.company
            product_version = $descriptor.product_version
            install_directory = $directory
            service_present = Test-Path -LiteralPath (Join-Path $directory 'px_service.exe') -PathType Leaf
            render_present = Test-Path -LiteralPath (Join-Path $directory 'px_render.exe') -PathType Leaf
        } | ConvertTo-Json -Compress
    }
} finally {
    if ($session) { Remove-PSSession $session }
    if ($canUpdateTrustedHosts) { Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force }
}
