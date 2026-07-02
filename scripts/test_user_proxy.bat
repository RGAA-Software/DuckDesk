@echo off
setlocal
cd /d "%~dp0..\rust_client"
cargo test -p gr_user_proxy -p gr_service -p service_core -- --nocapture
exit /b %errorlevel%
