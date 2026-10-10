"""Exercise capacity and release using two owned processes of the product Relay."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time

from run_iroh_relay_recovery import launch, stop_owned


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--relay", type=Path, required=True)
    parser.add_argument("--certificate-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18601)
    parser.add_argument("--release-denied-candidate", action="store_true")
    parser.add_argument("--allocation", action="store_true", help="Allocate four fresh endpoints from the shared candidate pool")
    arguments = parser.parse_args()
    if arguments.allocation and arguments.release_denied_candidate:
        parser.error("--allocation and --release-denied-candidate select different scenarios")
    repository = Path(__file__).resolve().parents[2]
    certificates = arguments.certificate_dir.resolve()
    relay_binary = arguments.relay.resolve()
    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    relays = []
    report = {"passed": False, "scope": "product Relay capacity and release; loopback TLS, not cross-host load",
              "relay_sha256": hashlib.sha256(relay_binary.read_bytes()).hexdigest()}
    try:
        with tempfile.TemporaryDirectory(prefix="pixels-relay-capacity-") as temporary_directory:
            temporary_root = Path(temporary_directory)
            ports = [arguments.port, arguments.port + 1]
            for port in ports:
                configuration = temporary_root / f"relay-{port}.json"
                configuration.write_text(json.dumps({"https_bind": f"127.0.0.1:{port}", "qad_bind": f"127.0.0.1:{port}",
                    "certificate_file": str(certificates / "server.pem"), "private_key_file": str(certificates / "server-key.pem"),
                    "max_connections": 2, "console_managed": False}), encoding="utf-8")
                relay, relay_lines = launch([str(relay_binary), "--iroh-config", str(configuration)])
                relays.append(relay)
                deadline = time.monotonic() + 10
                while True:
                    line = relay_lines.get(timeout=max(.01, deadline - time.monotonic()))
                    if line is None:
                        raise RuntimeError("Product Relay exited during startup")
                    if "Pixels iroh Relay started" in line:
                        break
            endpoint_configuration = temporary_root / "endpoint.json"
            endpoint_configuration.write_text(json.dumps({"relay_only": True,
                "relays": [{"url": f"https://127.0.0.1:{port}/", "qad_port": port} for port in ports],
                "ca_certificates_pem": [(certificates / "ca.pem").read_text(encoding="utf-8")]}), encoding="utf-8")
            command = [str(repository / "rust_transport/target/release/px_transport_probe.exe"),
                       "allocation-self-test" if arguments.allocation else "capacity-self-test",
                       "--endpoint-config", str(endpoint_configuration)]
            if arguments.release_denied_candidate:
                command.append("--release-denied-candidate")
            result = subprocess.run(command,
                capture_output=True, text=True, encoding="utf-8", timeout=40)
            output.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
            report["exit_code"] = result.returncode
            for line in result.stdout.splitlines():
                if line.startswith("CAPACITY_RECOVERED "):
                    report["recovery"] = json.loads(line.split(" ", 1)[1])
                if line.startswith("CAPACITY_DENIED "):
                    report["denied_relay"] = line.split(" ", 1)[1]
                if line.startswith("ENDPOINT_ALLOCATED "):
                    report.setdefault("allocations", []).append(json.loads(line.split(" ", 1)[1]))
                if line.startswith("ALLOCATION_VERIFIED "):
                    report["allocation"] = json.loads(line.split(" ", 1)[1])
            if result.returncode != 0:
                raise RuntimeError("Capacity probe failed; inspect " + str(output.with_suffix(".log")))
            report["passed"] = True
    finally:
        for relay in relays:
            stop_owned(relay)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
