@echo off
rem 校验录像文件: 流信息/编码/分辨率/采样率/时长 + 首视频包是否关键帧
rem usage: verify_record_file.bat <mp4>
setlocal
if "%~1"=="" (
    echo usage: verify_record_file.bat ^<mp4^>
    exit /b 1
)
set "FFPROBE=C:\source\vcpkg\installed\x64-windows-static-release\tools\ffmpeg\ffprobe.exe"
if not exist "%FFPROBE%" (
    echo ERROR: ffprobe not found: %FFPROBE%
    exit /b 1
)

echo ===== streams =====
"%FFPROBE%" -v error -show_entries stream=index,codec_type,codec_name,width,height,sample_rate,channels,duration -of compact "%~1"
echo ===== format =====
"%FFPROBE%" -v error -show_entries format=duration,size,format_name -of compact "%~1"
echo ===== first video packet =====
"%FFPROBE%" -v error -select_streams v:0 -show_entries packet=pts_time,flags -of csv=p=0 "%~1" > "%TEMP%\px_pkt_flags.txt" 2>&1
set /p FIRST=<"%TEMP%\px_pkt_flags.txt"
echo first-video-packet: %FIRST%
endlocal
