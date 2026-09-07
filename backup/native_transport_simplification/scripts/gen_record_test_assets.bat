@echo off
rem 生成确定性测试素材(H264 / Opus)到 %TEMP%\px_record_test_assets
rem 供共享录制核心联调与人工验证使用(单测自身用 avcodec 内存内生成, 不依赖本脚本)
setlocal
set "FF=C:\source\vcpkg\installed\x64-windows-static-release\tools\ffmpeg\ffmpeg.exe"
set "OUT=%TEMP%\px_record_test_assets"
if not exist "%FF%" (
    echo ERROR: ffmpeg not found: %FF%
    exit /b 1
)
if not exist "%OUT%" mkdir "%OUT%"

"%FF%" -y -f lavfi -i testsrc=duration=10:size=640x360:rate=30 -c:v libx264 -preset ultrafast -g 30 -keyint_min 30 -pix_fmt yuv420p "%OUT%\test_10s.h264"
if errorlevel 1 exit /b %errorlevel%

"%FF%" -y -f lavfi -i sine=frequency=440:duration=10 -c:a libopus -ar 48000 -ac 2 "%OUT%\test_10s.opus"
if errorlevel 1 exit /b %errorlevel%

echo Assets generated: %OUT%
endlocal
