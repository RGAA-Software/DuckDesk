@echo off
setlocal

rem Focused Console web build. It publishes only static assets to the PG development layout.
cd /d "%~dp0.." || exit /b 1
set "REPO_ROOT=%cd%"
set "WEB_ROOT=%REPO_ROOT%\web\px_console"
set "STATIC_OUTPUT=%REPO_ROOT%\output\px_console\dev\static"

where npm.cmd >nul 2>nul || (echo ERROR: npm.cmd is required.& exit /b 1)
if not exist "%WEB_ROOT%\node_modules" (
    call npm.cmd --prefix "%WEB_ROOT%" ci --no-audit --no-fund
    if errorlevel 1 exit /b 1
)
call npm.cmd --prefix "%WEB_ROOT%" run build
if errorlevel 1 exit /b 1

if exist "%STATIC_OUTPUT%" rmdir /S /Q "%STATIC_OUTPUT%"
mkdir "%STATIC_OUTPUT%"
xcopy /E /I /Y "%WEB_ROOT%\dist\*" "%STATIC_OUTPUT%\" >nul
if errorlevel 1 exit /b 1

pwsh.exe -NoProfile -Command "$source='%WEB_ROOT%\dist'; $copy='%STATIC_OUTPUT%'; Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object { $relative=[IO.Path]::GetRelativePath($source,$_.FullName); $target=Join-Path $copy $relative; if((Get-FileHash -LiteralPath $_.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash){throw ('Web hash mismatch: ' + $relative)} }; Write-Host 'HASH OK Console static assets'"
if errorlevel 1 exit /b 1

echo Console web development build: %STATIC_OUTPUT%
exit /b 0
