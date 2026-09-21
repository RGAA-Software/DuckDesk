param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("cloud_node", "client", "remote")]
    [string]$ExpectedProduct,

    [Parameter(Mandatory = $true)]
    [ValidateSet("official", "customer")]
    [string]$ExpectedDistribution,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$PreviousReleaseDirectory,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$CurrentReleaseDirectory,

    [ValidateScript({ [string]::IsNullOrWhiteSpace($_) -or (Test-Path -LiteralPath $_ -PathType Container) })]
    [string]$ConflictReleaseDirectory = "",

    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern("^[0-9A-Fa-f]{64}$")]
    [string]$ApprovedPreviousSignerSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern("^[0-9A-Fa-f]{64}$")]
    [string]$ApprovedCurrentSignerSha256,

    [switch]$ExecuteLifecycle
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$releaseVerifier = Join-Path $PSScriptRoot "verify_windows_installer_release.py"
$packageAuditor = Join-Path $PSScriptRoot "audit_installed_package.ps1"
$resolvedReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$reportDirectory = Split-Path -Parent $resolvedReportPath
$matrixPath = Join-Path $reportDirectory "installer-release-matrix.json"

$productDefinitions = @{
    cloud_node = @{
        install_directory = "C:\Program Files\Pixels Cloud Node"
        uninstall_key = "PixelsCloudNode"
        expects_service = $true
    }
    client = @{
        install_directory = "C:\Program Files\Pixels Client"
        uninstall_key = "PixelsClient"
        expects_service = $false
    }
    remote = @{
        install_directory = "C:\Program Files\Pixels Remote"
        uninstall_key = "PixelsRemote"
        expects_service = $true
    }
}

function Write-LifecycleReport {
    param([System.Collections.IDictionary]$Report)

    $temporaryReportPath = "$resolvedReportPath.pending"
    [System.IO.File]::WriteAllText(
        $temporaryReportPath,
        (($Report | ConvertTo-Json -Depth 12) + "`n"),
        [System.Text.UTF8Encoding]::new($false)
    )
    Move-Item -LiteralPath $temporaryReportPath -Destination $resolvedReportPath -Force
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Operation
    )

    $process = Start-Process -FilePath $ExecutablePath -ArgumentList $ArgumentList -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "$Operation failed with exit code $($process.ExitCode)"
    }
}

function Invoke-ExpectedInstallerRejection {
    param(
        [Parameter(Mandatory = $true)][string]$InstallerPath,
        [Parameter(Mandatory = $true)][string]$Operation
    )

    $process = Start-Process -FilePath $InstallerPath -ArgumentList @("/S") -Wait -PassThru
    if ($process.ExitCode -ne 1638) {
        throw "$Operation must return the product-conflict code 1638, actual=$($process.ExitCode)"
    }
}

function Get-UninstallRegistration {
    param([Parameter(Mandatory = $true)][string]$UninstallKey)

    foreach ($registryPath in @(
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKey",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKey"
    )) {
        $registration = Get-ItemProperty -LiteralPath $registryPath -ErrorAction SilentlyContinue
        if ($null -ne $registration) {
            return $registration
        }
    }
    return $null
}

function Assert-CleanValidationMachine {
    foreach ($productName in @("cloud_node", "client", "remote")) {
        $productDefinition = $productDefinitions[$productName]
        if ($null -ne (Get-UninstallRegistration -UninstallKey $productDefinition.uninstall_key)) {
            throw "validation machine is not clean: $productName has an uninstall registration"
        }
        if (Test-Path -LiteralPath $productDefinition.install_directory) {
            throw "validation machine is not clean: product directory exists: $($productDefinition.install_directory)"
        }
    }
    if ($null -ne (Get-Service -Name "px_service" -ErrorAction SilentlyContinue)) {
        throw "validation machine is not clean: px_service already exists"
    }
}

function Assert-ProductAbsent {
    param([Parameter(Mandatory = $true)][string]$Product)

    $productDefinition = $productDefinitions[$Product]
    if ($null -ne (Get-UninstallRegistration -UninstallKey $productDefinition.uninstall_key)) {
        throw "$Product unexpectedly has an uninstall registration"
    }
    if (Test-Path -LiteralPath $productDefinition.install_directory) {
        throw "$Product unexpectedly has an installation directory"
    }
}

function Invoke-ProductUninstall {
    param([Parameter(Mandatory = $true)][string]$Product)

    $productDefinition = $productDefinitions[$Product]
    $uninstallPath = Join-Path $productDefinition.install_directory "Uninstall.exe"
    Invoke-CheckedProcess -ExecutablePath $uninstallPath -ArgumentList @("/S") -Operation "$Product uninstallation"
    Assert-ProductAbsent -Product $Product
    if ($productDefinition.expects_service -and $null -ne (Get-Service -Name "px_service" -ErrorAction SilentlyContinue)) {
        throw "$Product uninstall left px_service registered"
    }
}

function New-ManualServiceConflictProbe {
    $probeDisplayName = "Pixels Installer Manual Service Conflict Probe"
    $probeCommand = Join-Path $env:SystemRoot "System32\cmd.exe"
    New-Service -Name "px_service" -BinaryPathName "`"$probeCommand`" /c exit 0" -DisplayName $probeDisplayName -StartupType Manual | Out-Null
    $registeredDisplayName = (Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\px_service").DisplayName
    if ([string]$registeredDisplayName -ne $probeDisplayName) {
        throw "manual px_service conflict probe was not registered with the expected identity"
    }
    return $probeDisplayName
}

function Remove-ManualServiceConflictProbe {
    param([Parameter(Mandatory = $true)][string]$ExpectedDisplayName)

    $serviceRegistryPath = "HKLM:\SYSTEM\CurrentControlSet\Services\px_service"
    $registeredService = Get-ItemProperty -LiteralPath $serviceRegistryPath -ErrorAction SilentlyContinue
    if ($null -eq $registeredService) {
        return
    }
    if ([string]$registeredService.DisplayName -ne $ExpectedDisplayName) {
        throw "refusing to delete px_service because the manual conflict probe identity changed"
    }
    Invoke-CheckedProcess -ExecutablePath "sc.exe" -ArgumentList @("delete", "px_service") -Operation "manual px_service conflict probe cleanup"
    for ($attemptIndex = 0; $attemptIndex -lt 20; $attemptIndex++) {
        if ($null -eq (Get-Service -Name "px_service" -ErrorAction SilentlyContinue)) {
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "manual px_service conflict probe remained registered after cleanup"
}

function Invoke-InstalledVerification {
    param(
        [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
        [Parameter(Mandatory = $true)][string]$InstallDirectory,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerSha256,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    Invoke-CheckedProcess -ExecutablePath "python" -ArgumentList @(
        $releaseVerifier,
        "installed",
        "--release-dir", $ReleaseDirectory,
        "--install-dir", $InstallDirectory,
        "--expected-signer-sha256", $ExpectedSignerSha256,
        "--output", $OutputPath
    ) -Operation "installed payload verification"
    return Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
}

function Assert-InstalledState {
    param(
        [Parameter(Mandatory = $true)][string]$Product,
        [Parameter(Mandatory = $true)][string]$ExpectedDistribution,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerSha256,
        [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
        [Parameter(Mandatory = $true)][string]$PhaseName
    )

    $productDefinition = $productDefinitions[$Product]
    $phaseAuditPath = Join-Path $reportDirectory "$PhaseName-audit.json"
    & powershell.exe -NoProfile -NonInteractive -File $packageAuditor `
        -OutputPath $phaseAuditPath `
        -Product $Product `
        -InstallRoot $productDefinition.install_directory
    if ($LASTEXITCODE -ne 0) {
        throw "$PhaseName package audit failed with exit code $LASTEXITCODE"
    }
    $packageAudit = Get-Content -LiteralPath $phaseAuditPath -Raw | ConvertFrom-Json
    if (-not $packageAudit.installed) {
        throw "$PhaseName did not create the product installation directory"
    }
    if ([string]$packageAudit.installed_version -ne $ExpectedVersion) {
        throw "$PhaseName installed version mismatch: expected=$ExpectedVersion actual=$($packageAudit.installed_version)"
    }
    if ([string]$packageAudit.installed_distribution -ne $ExpectedDistribution) {
        throw "$PhaseName installed distribution mismatch: expected=$ExpectedDistribution actual=$($packageAudit.installed_distribution)"
    }
    if ([string]$packageAudit.installed_publisher -ne "Pixels") {
        throw "$PhaseName installed publisher is not Pixels"
    }
    $servicePresent = [bool]$packageAudit.service.present
    if ($servicePresent -ne [bool]$productDefinition.expects_service) {
        throw "$PhaseName service presence does not match the product contract"
    }
    if ($productDefinition.expects_service -and [string]$packageAudit.service.status -ne "Running") {
        throw "$PhaseName px_service is not running"
    }
    $installedVerificationPath = Join-Path $reportDirectory "$PhaseName-installed.json"
    $installedVerification = Invoke-InstalledVerification `
        -ReleaseDirectory $ReleaseDirectory `
        -InstallDirectory $productDefinition.install_directory `
        -ExpectedSignerSha256 $ExpectedSignerSha256 `
        -OutputPath $installedVerificationPath
    return [ordered]@{
        package_audit = $packageAudit
        installed_verification = $installedVerification
    }
}

New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$releaseVerificationArguments = [System.Collections.Generic.List[string]]::new()
$releaseVerificationArguments.Add($releaseVerifier)
$releaseVerificationArguments.Add("pair")
$releaseVerificationArguments.Add("--previous")
$releaseVerificationArguments.Add([System.IO.Path]::GetFullPath($PreviousReleaseDirectory))
$releaseVerificationArguments.Add("--current")
$releaseVerificationArguments.Add([System.IO.Path]::GetFullPath($CurrentReleaseDirectory))
$releaseVerificationArguments.Add("--expected-product")
$releaseVerificationArguments.Add($ExpectedProduct)
$releaseVerificationArguments.Add("--expected-distribution")
$releaseVerificationArguments.Add($ExpectedDistribution)
$releaseVerificationArguments.Add("--output")
$releaseVerificationArguments.Add($matrixPath)
$releaseVerificationArguments.Add("--previous-signer-sha256")
$releaseVerificationArguments.Add($ApprovedPreviousSignerSha256)
$releaseVerificationArguments.Add("--current-signer-sha256")
$releaseVerificationArguments.Add($ApprovedCurrentSignerSha256)
Invoke-CheckedProcess -ExecutablePath "python" -ArgumentList @(
    $releaseVerificationArguments.ToArray()
) -Operation "signed installer pair preflight"
$releaseMatrix = Get-Content -LiteralPath $matrixPath -Raw | ConvertFrom-Json
$conflictRelease = $null
if (-not [string]::IsNullOrWhiteSpace($ConflictReleaseDirectory)) {
    $conflictReleasePath = Join-Path $reportDirectory "installer-conflict-release.json"
    Invoke-CheckedProcess -ExecutablePath "python" -ArgumentList @(
        $releaseVerifier,
        "single",
        "--release-dir", ([System.IO.Path]::GetFullPath($ConflictReleaseDirectory)),
        "--expected-signer-sha256", $ApprovedCurrentSignerSha256,
        "--output", $conflictReleasePath
    ) -Operation "signed conflict installer preflight"
    $conflictRelease = (Get-Content -LiteralPath $conflictReleasePath -Raw | ConvertFrom-Json).release
    if ([string]$conflictRelease.product -eq [string]$releaseMatrix.product) {
        throw "conflict installer must belong to a different Pixels product"
    }
    if ([string]$conflictRelease.distribution -ne [string]$releaseMatrix.distribution) {
        throw "conflict installer must use the same distribution as the lifecycle pair"
    }
    if ([string]$conflictRelease.signer_certificate_sha256 -ne [string]$releaseMatrix.current.signer_certificate_sha256) {
        throw "conflict installer must use the current release signer certificate"
    }
}

$lifecycleReport = [ordered]@{
    schema_version = 1
    started_at = (Get-Date).ToUniversalTime().ToString("O")
    completed_at = $null
    status = if ($ExecuteLifecycle) { "running" } else { "preflight_passed" }
    execution_requested = [bool]$ExecuteLifecycle
    host = [ordered]@{
        computer_name = $env:COMPUTERNAME
        windows_version = [System.Environment]::OSVersion.VersionString
    }
    release_matrix = $releaseMatrix
    conflict_release = $conflictRelease
    phases = [System.Collections.Generic.List[object]]::new()
    failure = $null
}

if (-not $ExecuteLifecycle) {
    $lifecycleReport.completed_at = (Get-Date).ToUniversalTime().ToString("O")
    Write-LifecycleReport -Report $lifecycleReport
    Write-Host "Signed installer pair preflight passed. No installation or uninstallation was performed."
    exit 0
}

$windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$windowsPrincipal = [Security.Principal.WindowsPrincipal]::new($windowsIdentity)
if (-not $windowsPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "-ExecuteLifecycle requires an elevated PowerShell process"
}

$product = [string]$releaseMatrix.product
$previousRelease = $releaseMatrix.previous
$currentRelease = $releaseMatrix.current

try {
    Assert-CleanValidationMachine
    $lifecycleReport.phases.Add([ordered]@{ name = "clean_machine_precondition"; status = "passed" })
    Write-LifecycleReport -Report $lifecycleReport

    if ($null -ne $conflictRelease) {
        $conflictProduct = [string]$conflictRelease.product
        Invoke-CheckedProcess -ExecutablePath ([string]$conflictRelease.installer_path) -ArgumentList @("/S") -Operation "conflicting product installation"
        $conflictStateBeforeRejection = Assert-InstalledState `
            -Product $conflictProduct `
            -ExpectedDistribution ([string]$conflictRelease.distribution) `
            -ExpectedVersion ([string]$conflictRelease.product_version) `
            -ExpectedSignerSha256 $ApprovedCurrentSignerSha256 `
            -ReleaseDirectory ([string]$conflictRelease.directory) `
            -PhaseName "conflict_product_before_rejection"
        Invoke-ExpectedInstallerRejection `
            -InstallerPath ([string]$currentRelease.installer_path) `
            -Operation "cross-product installation rejection"
        Assert-ProductAbsent -Product $product
        $conflictStateAfterRejection = Assert-InstalledState `
            -Product $conflictProduct `
            -ExpectedDistribution ([string]$conflictRelease.distribution) `
            -ExpectedVersion ([string]$conflictRelease.product_version) `
            -ExpectedSignerSha256 $ApprovedCurrentSignerSha256 `
            -ReleaseDirectory ([string]$conflictRelease.directory) `
            -PhaseName "conflict_product_after_rejection"
        $lifecycleReport.phases.Add([ordered]@{
            name = "cross_product_rejection"
            status = "passed"
            before = $conflictStateBeforeRejection
            after = $conflictStateAfterRejection
        })
        Write-LifecycleReport -Report $lifecycleReport
        Invoke-ProductUninstall -Product $conflictProduct
        Assert-CleanValidationMachine
    }

    $manualServiceProbeName = New-ManualServiceConflictProbe
    try {
        Invoke-ExpectedInstallerRejection `
            -InstallerPath ([string]$currentRelease.installer_path) `
            -Operation "manual px_service conflict rejection"
        Assert-ProductAbsent -Product $product
        $lifecycleReport.phases.Add([ordered]@{ name = "manual_service_rejection"; status = "passed" })
        Write-LifecycleReport -Report $lifecycleReport
    } finally {
        Remove-ManualServiceConflictProbe -ExpectedDisplayName $manualServiceProbeName
    }
    Assert-CleanValidationMachine

    Invoke-CheckedProcess -ExecutablePath ([string]$previousRelease.installer_path) -ArgumentList @("/S") -Operation "previous version installation"
    $previousState = Assert-InstalledState `
        -Product $product `
        -ExpectedDistribution ([string]$releaseMatrix.distribution) `
        -ExpectedVersion ([string]$previousRelease.product_version) `
        -ExpectedSignerSha256 $ApprovedPreviousSignerSha256 `
        -ReleaseDirectory ([string]$previousRelease.directory) `
        -PhaseName "previous_install"
    $lifecycleReport.phases.Add([ordered]@{ name = "previous_install"; status = "passed"; evidence = $previousState })
    Write-LifecycleReport -Report $lifecycleReport

    Invoke-CheckedProcess -ExecutablePath ([string]$currentRelease.installer_path) -ArgumentList @("/S") -Operation "same-channel upgrade"
    $upgradeState = Assert-InstalledState `
        -Product $product `
        -ExpectedDistribution ([string]$releaseMatrix.distribution) `
        -ExpectedVersion ([string]$currentRelease.product_version) `
        -ExpectedSignerSha256 $ApprovedCurrentSignerSha256 `
        -ReleaseDirectory ([string]$currentRelease.directory) `
        -PhaseName "upgrade"
    $lifecycleReport.phases.Add([ordered]@{ name = "upgrade"; status = "passed"; evidence = $upgradeState })
    Write-LifecycleReport -Report $lifecycleReport

    Invoke-CheckedProcess -ExecutablePath ([string]$currentRelease.installer_path) -ArgumentList @("/S") -Operation "same-version covering installation"
    $coveringState = Assert-InstalledState `
        -Product $product `
        -ExpectedDistribution ([string]$releaseMatrix.distribution) `
        -ExpectedVersion ([string]$currentRelease.product_version) `
        -ExpectedSignerSha256 $ApprovedCurrentSignerSha256 `
        -ReleaseDirectory ([string]$currentRelease.directory) `
        -PhaseName "same_version_cover"
    $lifecycleReport.phases.Add([ordered]@{ name = "same_version_cover"; status = "passed"; evidence = $coveringState })
    Write-LifecycleReport -Report $lifecycleReport

    Invoke-ProductUninstall -Product $product
    $lifecycleReport.phases.Add([ordered]@{ name = "uninstall"; status = "passed" })
    $lifecycleReport.status = "passed"
} catch {
    $lifecycleReport.status = "failed"
    $lifecycleReport.failure = $_.Exception.Message
    throw
} finally {
    $lifecycleReport.completed_at = (Get-Date).ToUniversalTime().ToString("O")
    Write-LifecycleReport -Report $lifecycleReport
}

Write-Host "Windows installer lifecycle passed: $product $($previousRelease.product_version) -> $($currentRelease.product_version)"
