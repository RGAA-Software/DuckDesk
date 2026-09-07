$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$client = Join-Path $repo 'build_official/dist/px_client.exe'
$output = Join-Path $repo 'test-results'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$previous = $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES
$env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = '1'
try {
    foreach ($option in @('network_type', 'force_direct', 'enable_p2p', 'relay_host', 'relay_port', 'relay_appkey', 'signal_remote_device_id')) {
        $stdout = Join-Path $output "native-cli-$option.out"
        $stderr = Join-Path $output "native-cli-$option.err"
        $process = Start-Process -FilePath $client -ArgumentList "--$option=1" -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        try {
            if (-not $process.WaitForExit(5000)) {
                Stop-Process -Id $process.Id -Force
                throw "Retired option did not exit within 5 seconds: $option"
            }
            $message = (Get-Content -LiteralPath $stdout, $stderr -Raw) -join "`n"
            if ($process.ExitCode -eq 0 -or $message -notmatch ("Unknown option '" + $option + "'")) {
                throw "Retired option was not explicitly rejected: $option"
            }
            Write-Host "PASS: --$option rejected"
        }
        finally { $process.Dispose() }
    }
}
finally { $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = $previous }
