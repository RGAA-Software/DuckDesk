# Game-hook launcher: start GammaRayRender only (no browser / no CDP).
# Parameter reference — edit values below, then run start_render_hook.bat
#
# --app_game_path is Base64(UTF-8 of GamePath) so spaces/Chinese never hit argv code pages.

$ErrorActionPreference = 'Stop'

# ===== launch parameters =====
$Port = 32000
$DeviceId = 'debug1'
$AppMode = 'game-hook'
$GamePath = 'D:\1_test_games\CarGame  汽车\CarGame\Binaries\Win64\VehicleGame-Win64-Shipping.exe'
$CaptureVideo = 'true'
$CaptureVideoType = 'inner'
$CaptureAudio = 'true'
$CaptureAudioType = 'global'
$WebrtcEnabled = 'true'
$WebsocketEnabled = 'true'
$EncoderFps = '60'
$EncoderBitrate = '20'
$EncoderFormat = 'h264'
# =============================

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Dist = Join-Path $RepoRoot 'build_official\dist'
$BuiltExe = Join-Path $RepoRoot 'build_official\src\gr_render\GammaRayRender.exe'
$SrcToml = Join-Path $RepoRoot 'src\gr_render\settings.toml'
$Exe = Join-Path $Dist 'GammaRayRender.exe'
$WebUrl = "http://127.0.0.1:${Port}/web_client/?deviceId=${DeviceId}"
$LogPath = "C:\Users\Public\GoDesk\gr_logs\godesk_render_${Port}.log"

# Incremental cmake builds land under src/gr_render; sync into dist when newer.
if (Test-Path -LiteralPath $BuiltExe) {
    $needCopy = -not (Test-Path -LiteralPath $Exe) -or
        ((Get-Item -LiteralPath $BuiltExe).LastWriteTime -gt (Get-Item -LiteralPath $Exe).LastWriteTime)
    if ($needCopy) {
        Write-Host "Syncing newer GammaRayRender.exe from build tree -> dist"
        New-Item -ItemType Directory -Force -Path $Dist | Out-Null
        Copy-Item -LiteralPath $BuiltExe -Destination $Exe -Force
    }
}

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Error "ERROR: $Exe not found. Build GammaRayRender first."
    exit 1
}
if (-not (Test-Path -LiteralPath $SrcToml)) {
    Write-Error "ERROR: $SrcToml not found."
    exit 1
}
if (-not (Test-Path -LiteralPath $GamePath)) {
    Write-Error "ERROR: game not found: $GamePath"
    exit 1
}

$gamePathB64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($GamePath))

Write-Host '============================================'
Write-Host 'Game-hook: start GammaRayRender only'
Write-Host '============================================'
Write-Host "Dist : $Dist"
Write-Host "URL  : $WebUrl"
Write-Host ''
Write-Host 'Args:'
Write-Host "  --app_mode=$AppMode"
Write-Host "  --app_game_path=<Base64 UTF-8>"
Write-Host "  GAME_PATH=$GamePath"
Write-Host "  app_game_path(b64)=$gamePathB64"
Write-Host "  --capture_video=$CaptureVideo"
Write-Host "  --capture_video_type=$CaptureVideoType"
Write-Host "  --capture_audio=$CaptureAudio"
Write-Host "  --capture_audio_type=$CaptureAudioType"
Write-Host "  --webrtc_enabled=$WebrtcEnabled"
Write-Host "  --websocket_enabled=$WebsocketEnabled"
Write-Host "  --encoder_fps=$EncoderFps"
Write-Host "  --encoder_bitrate=$EncoderBitrate"
Write-Host "  --encoder_format=$EncoderFormat"
Write-Host "  --network_listen_port=$Port"
Write-Host '  --logfile'
Write-Host ''

Get-Process -Name 'GammaRayRender' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

Copy-Item -LiteralPath $SrcToml -Destination (Join-Path $Dist 'settings.toml') -Force

$argList = @(
    '--logfile',
    "--app_mode=$AppMode",
    "--app_game_path=$gamePathB64",
    "--capture_video=$CaptureVideo",
    "--capture_video_type=$CaptureVideoType",
    "--capture_audio=$CaptureAudio",
    "--capture_audio_type=$CaptureAudioType",
    "--webrtc_enabled=$WebrtcEnabled",
    "--websocket_enabled=$WebsocketEnabled",
    "--encoder_fps=$EncoderFps",
    "--encoder_bitrate=$EncoderBitrate",
    "--encoder_format=$EncoderFormat",
    "--network_listen_port=$Port"
)

Write-Host 'Starting GammaRayRender.exe ...'
Start-Process -FilePath $Exe -ArgumentList $argList -WorkingDirectory $Dist -WindowStyle Normal

$ready = $false
for ($i = 1; $i -le 60; $i++) {
    try {
        $r = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:${Port}/api/ping" -TimeoutSec 1
        if ($r.StatusCode -ge 200) {
            $ready = $true
            break
        }
    } catch {
        # keep waiting
    }
    Start-Sleep -Seconds 1
}

if (-not $ready) {
    Write-Error "ERROR: render not ready on port $Port within 60s. Log: $LogPath"
    exit 1
}

# Verify decoded path was accepted (new builds log app_game_path(b64) + decoded path).
Start-Sleep -Milliseconds 500
if (Test-Path -LiteralPath $LogPath) {
    $tail = Get-Content -LiteralPath $LogPath -Tail 80 -ErrorAction SilentlyContinue
    $decodedOk = $tail | Where-Object { $_ -like "*app_game_path: $GamePath*" -or $_ -like "*game_path=$GamePath*" }
    $rawB64Fail = $tail | Where-Object { $_ -like '*Exe not exists: RDpc*' -or ($_ -like '*StartProcessWithHook: game_path=RDpc*' ) }
    if ($rawB64Fail -and -not $decodedOk) {
        Write-Host ''
        Write-Host 'ERROR: Render treated app_game_path as raw Base64 (not decoded).'
        Write-Host 'Rebuild GammaRayRender (Base64 decode in rd_main) and retry.'
        Write-Host "Log: $LogPath"
        exit 1
    }
    if ($tail | Where-Object { $_ -like "*Exe not exists: $GamePath*" }) {
        Write-Host ''
        Write-Host "ERROR: Render reports exe not exists: $GamePath"
        Write-Host "Log: $LogPath"
        exit 1
    }
}

Write-Host ''
Write-Host 'Render is up. Open this URL yourself:'
Write-Host "  $WebUrl"
Write-Host "Log: $LogPath"
Write-Host ''
exit 0
