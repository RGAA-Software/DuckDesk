`build_setup.bat` will:

1. build `GrSysMonitor` and `GrSysMonitorHost` in release mode
2. stage the two executables into a temporary payload directory
3. compress the payload as `app.7z`
4. invoke NSIS to create the installer

Output location:

`rust_client/gr_sysinfo/build/setup_output/<version>/`

Installer behavior:

- installs to `%ProgramFiles%\GammaRayPremium\GrSysMonitorSuite`
- creates Start Menu shortcuts
- registers both executables in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
- launches `GrSysMonitor.exe` automatically when installation completes
- removes those entries on uninstall
