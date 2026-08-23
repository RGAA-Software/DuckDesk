@echo off
rem Client-only build: everything the installer (setup\make_setup.py) packages,
rem without any server-side content (px_console/px_auth/px_desk rust servers).
rem Full flow lives in build_official.bat; this wrapper just skips the
rem rust server build steps via PX_SKIP_SERVERS.
rem Usage: build_client.bat [full|reconfigure]   (args are passed through)
setlocal
set PX_SKIP_SERVERS=1
call "%~dp0build_official.bat" %*
exit /b %errorlevel%
