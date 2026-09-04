param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("was-default", "process-loopback")]
    [string]$Kind,

    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = "Stop"
$skipCode = 125

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    Write-Error "Hardware test executable was not built: $Executable"
}

$arguments = @()
switch ($Kind) {
    "was-default" {
        if ($env:RENDER_TEST_WAS_HARDWARE -ne "1") {
            Write-Host "SKIP: set RENDER_TEST_WAS_HARDWARE=1 after confirming an active Windows playback device."
            exit $skipCode
        }
    }
    "process-loopback" {
        [uint32]$processId = 0
        if (-not [uint32]::TryParse(
                $env:RENDER_TEST_AUDIO_PID,
                [ref]$processId) -or $processId -eq 0) {
            Write-Host "SKIP: set RENDER_TEST_AUDIO_PID to an active audio-producing process id."
            exit $skipCode
        }
        $arguments += $processId.ToString()
    }
}

& $Executable @arguments
exit $LASTEXITCODE
