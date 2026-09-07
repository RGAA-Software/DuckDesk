@echo off
setlocal
cd /d "%~dp0..\rust_client"
cargo test -p px_user_proxy -p px_service -p service_core -- --nocapture
exit /b %errorlevel%
