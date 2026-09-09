@echo off
setlocal
rem Isolated upstream proxy verification only. No product build, version bump or dist publication.
set "RDP_PROBE_SOURCE=D:\dolit\rdp\FreeRDP"
set "RDP_PROBE_BUILD=%~dp0..\.cache\rdp_proxy_probe"
if not exist "%RDP_PROBE_SOURCE%\CMakeLists.txt" exit /b 2
cmake -S "%RDP_PROBE_SOURCE%" -B "%RDP_PROBE_BUILD%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/source/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_MODE=OFF -DWITH_SERVER=ON -DWITH_PROXY=ON -DWITH_SHADOW=OFF -DWITH_PLATFORM_SERVER=OFF -DWITH_CLIENT=ON -DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_COMMON=ON -DWITH_CHANNELS=ON -DWITH_CLIENT_CHANNELS=ON -DWITH_SERVER_CHANNELS=ON -DWITH_SAMPLE=OFF -DBUILD_TESTING=OFF -DWITH_MANPAGES=OFF -DWITH_DOCUMENTATION=OFF -DWITH_MEDIA_FOUNDATION=ON -DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF -DWITH_OPENH264=OFF -DWITH_OPUS=OFF -DWITH_JPEG=OFF -DWITH_WINPR_TOOLS=OFF -DWITH_WINPR_TOOLS_CLI=OFF -DWITH_PROXY_MODULES=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build "%RDP_PROBE_BUILD%" --config Release --target freerdp-proxy --parallel 6
exit /b %errorlevel%
