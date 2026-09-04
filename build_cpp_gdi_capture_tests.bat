@echo off
setlocal

rem Focused built-in GDI capture and Render linkage tests. This does not build
rem Rust, web assets, bump release versions, or produce a capture plug-in DLL.
call "%~dp0scripts\build_cpp_target.bat" cap_gdi test_render_builtin_linkage check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -R "^render_builtin_linkage$" --output-on-failure
exit /b %errorlevel%
