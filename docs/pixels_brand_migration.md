# Pixels brand migration

Pixels is the only current product brand. New UI text, executable metadata, installers, package names, cache/configuration paths, certificates, service diagnostics, logs, and generated artifacts must use `Pixels` or an established `px_` technical name.

The following old identifiers may remain only as compatibility inputs and must never be emitted for a new installation:

- the retired Windows service name removed by the installer during upgrade;
- the former Console theme key and SysMonitor configuration directory, read once and migrated to Pixels;
- schema-1 RDP credential AAD and existing Windows-account comments;
- the former pinned RDP SDK manifest filename, accepted while upgrading a developer cache;
- the existing C++ boundary-lint annotation and historical reports/archives.

The supported Windows package is `Pixels_<version>_Setup.exe`. It installs to `C:\Program Files\PixelsRender`, updates and starts `px_service`, and removes the retired service during a covering upgrade. The Qt Installer Framework package is archived under `backup/qt_installer_retirement_20260915/` and is not a supported build or release path.
