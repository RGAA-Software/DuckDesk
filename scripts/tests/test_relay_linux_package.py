import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from deploy.relay.deploy import PACKAGE_FILES, deploy, sha256, verify_package


class RelayLinuxPackageTests(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory()
        self.addCleanup(self.workspace.cleanup)
        self.package_directory = Path(self.workspace.name) / "release-one"
        self.package_directory.mkdir()
        for file_name in PACKAGE_FILES:
            (self.package_directory / file_name).write_text("fixture " + file_name, encoding="utf-8")
        self.manifest = {
            "product": "pixels-relay", "platform": "linux-x86_64", "image_tag": "pixels-relay:test",
            "image_id": "sha256:" + "1" * 64, "runtime_sha256": "2" * 64,
            "files": {file_name: sha256(self.package_directory / file_name) for file_name in PACKAGE_FILES},
        }
        self.write_manifest()
        self.config_directory = Path(self.workspace.name) / "persistent-config"
        self.config_directory.mkdir()
        (self.config_directory / "relay.json").write_text(json.dumps({
            "https_bind": "0.0.0.0:4605", "qad_bind": "0.0.0.0:4605",
        }), encoding="utf-8")
        (self.config_directory / "relay-control.env").write_text("fixture configuration", encoding="utf-8")
        self.docker_commands = []
        self.observed_environments = []

    def write_manifest(self):
        (self.package_directory / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def simulate_docker(self, arguments, environment=None, timeout=60):
        self.docker_commands.append(arguments)
        if environment:
            self.observed_environments.append(environment)
        if arguments[:2] == ["image", "inspect"]:
            return json.dumps([{"Id": self.manifest["image_id"], "Os": "linux", "Architecture": "amd64"}])
        if arguments[0] in ("run", "exec"):
            return self.manifest["runtime_sha256"] + "  /opt/pixels/bin/px_relay"
        if arguments[-4:] == ["ps", "--all", "--quiet", "relay"]:
            return "fixture-container"
        if arguments[0] == "inspect":
            return json.dumps([{"Image": self.manifest["image_id"], "State": {"Status": "running"}}])
        return ""

    def run_deploy(self):
        return deploy(self.package_directory, self.config_directory, "existing-relay", 5605, 5606)

    def test_corrupt_package_fails_before_any_docker_call(self):
        (self.package_directory / "relay-image.tar").write_bytes(b"corrupt")
        with patch("deploy.relay.deploy.docker") as docker_call:
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                self.run_deploy()
            docker_call.assert_not_called()

    def test_manifest_requires_all_packaged_files(self):
        del self.manifest["files"]["compose.yaml"]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            verify_package(self.package_directory)

    def test_missing_config_preserves_running_service(self):
        (self.config_directory / "relay-control.env").unlink()
        with patch("deploy.relay.deploy.docker") as docker_call:
            with self.assertRaisesRegex(ValueError, "Missing external configuration"):
                self.run_deploy()
            docker_call.assert_not_called()

    def test_image_mismatch_does_not_replace_running_service(self):
        def mismatched_image(arguments, environment=None, timeout=60):
            if arguments[:2] == ["image", "inspect"]:
                return json.dumps([{"Id": "sha256:" + "3" * 64, "Os": "linux", "Architecture": "amd64"}])
            return self.simulate_docker(arguments, environment, timeout)

        with patch("deploy.relay.deploy.docker", side_effect=mismatched_image):
            with self.assertRaisesRegex(ValueError, "identity/platform"):
                self.run_deploy()
        self.assertFalse(any("up" in command for command in self.docker_commands))

    def test_upgrade_from_new_directory_reuses_project_and_external_config(self):
        initial_config = {file_path.name: file_path.read_bytes() for file_path in self.config_directory.iterdir()}
        with patch("deploy.relay.deploy.docker", side_effect=self.simulate_docker):
            first_report = self.run_deploy()
            next_directory = self.package_directory.with_name("release-two")
            self.package_directory.rename(next_directory)
            self.package_directory = next_directory
            second_report = self.run_deploy()
        self.assertEqual(first_report["project"], second_report["project"])
        self.assertEqual(second_report["state"], "running")
        self.assertEqual(second_report["console_readiness"], "not_checked")
        for command in self.docker_commands:
            if command[0] == "compose":
                self.assertEqual(command[command.index("--project-name") + 1], "existing-relay")
                self.assertNotIn("down", command)
        for environment in self.observed_environments:
            self.assertEqual(environment["PIXELS_RELAY_CONFIG_DIR"], str(self.config_directory.resolve()))
            self.assertEqual(environment["PIXELS_RELAY_HTTPS_PORT"], "5605")
            self.assertEqual(environment["PIXELS_RELAY_QAD_PORT"], "5606")
        self.assertEqual(initial_config, {file_path.name: file_path.read_bytes() for file_path in self.config_directory.iterdir()})

    def test_wrong_runtime_hash_does_not_replace_running_service(self):
        def mismatched_runtime(arguments, environment=None, timeout=60):
            if arguments[0] == "run":
                return "3" * 64 + "  /opt/pixels/bin/px_relay"
            return self.simulate_docker(arguments, environment, timeout)

        with patch("deploy.relay.deploy.docker", side_effect=mismatched_runtime):
            with self.assertRaisesRegex(ValueError, "runtime SHA-256 mismatch"):
                self.run_deploy()
        self.assertFalse(any("up" in command for command in self.docker_commands))

    def test_invalid_compose_stops_before_image_load(self):
        with patch("deploy.relay.deploy.docker", side_effect=RuntimeError("Invalid Compose configuration")) as docker_call:
            with self.assertRaisesRegex(RuntimeError, "Invalid Compose"):
                self.run_deploy()
        self.assertEqual(docker_call.call_count, 1)
        self.assertEqual(docker_call.call_args.args[0][-2:], ["config", "--quiet"])

    def test_exited_container_is_not_reported_as_success(self):
        def exited_container(arguments, environment=None, timeout=60):
            if arguments[0] == "inspect":
                return json.dumps([{"Image": self.manifest["image_id"], "State": {"Status": "exited"}}])
            return self.simulate_docker(arguments, environment, timeout)

        with patch("deploy.relay.deploy.docker", side_effect=exited_container):
            with self.assertRaisesRegex(RuntimeError, "not running"):
                self.run_deploy()


if __name__ == "__main__":
    unittest.main()
