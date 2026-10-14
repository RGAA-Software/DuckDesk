@echo off
rem Shared build-job ceiling. Called without setlocal so compiler environment is inherited.
set "PX_BUILD_JOBS=%~1"
if not defined PX_BUILD_JOBS set "PX_BUILD_JOBS=10"
set "PX_BUILD_JOBS_VALID="
for %%J in (1 2 3 4 5 6 7 8 9 10) do if "%PX_BUILD_JOBS%"=="%%J" set "PX_BUILD_JOBS_VALID=1"
if not defined PX_BUILD_JOBS_VALID (
    echo ERROR: build jobs must be an integer from 1 to 10.
    exit /b 2
)
set "CARGO_BUILD_JOBS=%PX_BUILD_JOBS%"
set "CMAKE_BUILD_PARALLEL_LEVEL=%PX_BUILD_JOBS%"
set "VCPKG_MAX_CONCURRENCY=%PX_BUILD_JOBS%"
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0limit_build_cpu.ps1" -ParentProcess
if errorlevel 1 exit /b %errorlevel%
exit /b 0
