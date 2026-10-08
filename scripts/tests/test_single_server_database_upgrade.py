"""Focused Windows upgrade orchestration and atomic Backup configuration tests."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[2]


class SingleServerDatabaseUpgradeTests(unittest.TestCase):
    def test_preflight_backup_migration_and_failure_are_ordered(self):
        installer = (REPOSITORY / "deploy/single_server/windows/install.ps1").read_text(encoding="utf-8")
        preflight = installer.index("preflight-single-server-database $resolvedConfig")
        stop_services = installer.index("Stop-InstalledTray -ExecutablePath (Join-Path $currentPath")
        snapshot = installer.index("Invoke-ConsoleDatabasePreUpgradeBackup -PackageRoot $stagePath")
        migration_attempt = installer.index("$databaseUpgradeAttempted = $true")
        migration = installer.index("upgrade-single-server-database $resolvedConfig")
        swap = installer.index("$swapAttempted = $true")
        self.assertLess(preflight, stop_services)
        self.assertLess(stop_services, snapshot)
        self.assertLess(snapshot, migration_attempt)
        self.assertLess(migration_attempt, migration)
        self.assertLess(migration, swap)
        self.assertIn("if ($databaseUpgradeAttempted) {", installer)
        self.assertIn("old binaries will not be restarted", installer)
        self.assertIn("-not $databaseUpgradeAttempted -and", installer)
        self.assertIn("if ($RecoverDatabaseOwner) {", installer)
        self.assertIn("PIXELS_SETUP_DATABASE_URL", installer)
        setup_script = (REPOSITORY / "setup/single_server.nsi").read_text(encoding="utf-8")
        self.assertLess(setup_script.index("FileSeek $1 0 END"), setup_script.index('FileWrite $1 "Pixels Server'))

    @unittest.skipUnless(os.name == "nt", "Windows upgrade uses PowerShell 5.1 and NTFS ACLs")
    def test_only_retired_guest_lifetime_is_removed_and_repeat_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            config_root = Path(temporary_directory)
            environment_path = config_root / "console.env"
            retained_environment = "PIXELS_CONSOLE_PUBLIC_ORIGIN=https://private.example:4600\nUSER_LOGIN_LIFETIME=3600\n"
            environment_path.write_text(retained_environment + "PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS=3600\n", encoding="utf-8")
            helper_path = REPOSITORY / "deploy/single_server/windows/upgrade_console_database.ps1"
            verification_script = f"""
$ErrorActionPreference='Stop'
. '{str(helper_path)}'
Remove-ObsoleteGuestLifetimeSetting -ConfigRoot '{str(config_root)}'
$firstContents=[Convert]::ToBase64String([IO.File]::ReadAllBytes('{str(environment_path)}'))
Remove-ObsoleteGuestLifetimeSetting -ConfigRoot '{str(config_root)}'
if ($firstContents -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes('{str(environment_path)}'))) {{ throw 'Repeated cleanup changed the file' }}
"""
            result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", verification_script],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(environment_path.read_text(encoding="utf-8"), retained_environment)
            self.assertEqual(list(config_root.glob(".guest-lifetime-retired-*")), [])

    @unittest.skipUnless(os.name == "nt", "Windows upgrade uses PowerShell 5.1 and NTFS ACLs")
    def test_backup_schema_is_updated_atomically_without_changing_business_fields(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            config_root = Path(temporary_directory)
            backup_path = config_root / "backup.json"
            original_configuration = {
                "deployment_id": "11111111-1111-4111-8111-111111111111",
                "control": {"token": "synthetic-control-secret"},
                "plan": {"targets": [
                    {"state": "required", "database": {
                        "service": "console", "database": "pixels_console", "schema_version": 33,
                        "username": "pixels_console_backup", "host": "localhost", "port": 5432,
                    }},
                    {"state": "not_applicable", "service": "auth"},
                    {"state": "not_applicable", "service": "desk"},
                ]},
            }
            backup_path.write_text(json.dumps(original_configuration), encoding="utf-8")
            helper_path = REPOSITORY / "deploy/single_server/windows/upgrade_console_database.ps1"
            verification_script = f"""
$ErrorActionPreference='Stop'
. '{str(helper_path)}'
Set-ConsoleBackupSchemaVersion -ConfigRoot '{str(config_root)}' -SchemaVersion 34
$successfulBytes=[IO.File]::ReadAllBytes('{str(backup_path)}')
try {{
    Set-ConsoleBackupSchemaVersion -ConfigRoot '{str(config_root)}' -SchemaVersion 0
    throw 'Invalid schema was accepted'
}} catch {{
    if ($_.Exception.Message -ne 'Backup schema version is invalid.') {{ throw }}
}}
if ([Convert]::ToBase64String($successfulBytes) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes('{str(backup_path)}'))) {{ throw 'Rejected update changed configuration' }}
Write-Output 'ATOMIC_SCHEMA_UPDATE_PASS'
"""
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", verification_script],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            expected_configuration = original_configuration
            expected_configuration["plan"]["targets"][0]["database"]["schema_version"] = 34
            self.assertEqual(json.loads(backup_path.read_text(encoding="utf-8")), expected_configuration)
            self.assertEqual(list(config_root.glob(".backup-schema-*")), [])


if __name__ == "__main__":
    unittest.main()
