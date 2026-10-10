"""Exercise a formal Relay process with the existing mixed stream/datagram probe."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time


def available_port(socket_type):
    with socket.socket(socket.AF_INET, socket_type) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--relay-binary", required=True, type=Path)
    parser.add_argument("--probe-binary", required=True, type=Path)
    parser.add_argument("--certificate", required=True, type=Path)
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--ca-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    https_port = available_port(socket.SOCK_STREAM)
    qad_port = available_port(socket.SOCK_DGRAM)
    report = {
        "relay_sha256": hashlib.sha256(arguments.relay_binary.read_bytes()).hexdigest(),
        "platform": os.name,
        "https_port": https_port,
        "qad_port": qad_port,
        "installation_test": False,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    log_path = arguments.output.with_suffix(".relay.log")
    with tempfile.TemporaryDirectory(prefix="pixels-relay-smoke-") as temporary_directory:
        config_path = Path(temporary_directory) / "relay.json"
        config_path.write_text(json.dumps({
            "https_bind": f"127.0.0.1:{https_port}",
            "qad_bind": f"127.0.0.1:{qad_port}",
            "certificate_file": str(arguments.certificate.resolve()),
            "private_key_file": str(arguments.private_key.resolve()),
            "max_connections": 4,
        }), encoding="utf-8")
        with log_path.open("wb") as relay_log:
            relay_process = subprocess.Popen(
                [str(arguments.relay_binary.resolve()), "--iroh-config", str(config_path)],
                stdout=relay_log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            try:
                deadline = time.monotonic() + 15
                while True:
                    if relay_process.poll() is not None:
                        raise RuntimeError("Relay exited during startup; see " + str(log_path))
                    try:
                        with socket.create_connection(("127.0.0.1", https_port), timeout=0.2):
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise TimeoutError("Relay did not listen within 15 seconds")
                        time.sleep(0.1)
                completed = subprocess.run([
                    str(arguments.probe_binary.resolve()), "self-test",
                    "--relay", f"https://127.0.0.1:{https_port}",
                    "--qad-port", str(qad_port), "--relay-only",
                    "--ca-root", str(arguments.ca_root.resolve()),
                ], capture_output=True, text=True, timeout=45)
                report["probe_stdout"] = completed.stdout
                report["probe_stderr"] = completed.stderr
                report["probe_exit_code"] = completed.returncode
                if completed.returncode != 0:
                    raise RuntimeError("Relay probe failed: " + completed.stderr)
                measurements = [json.loads(line) for line in completed.stdout.splitlines() if line.startswith("{")]
                if len(measurements) != 1:
                    raise RuntimeError("Expected one probe measurement")
                report["measurements"] = measurements[0]
                if measurements[0]["reliable_bytes_verified"] != 6 * 1024 * 1024 + 64:
                    raise RuntimeError("Incomplete stream payload verification")
                if measurements[0]["datagrams_echoed"] == 0:
                    raise RuntimeError("No datagrams crossed the Relay")
                if "relay:https://" not in measurements[0]["paths"]:
                    raise RuntimeError("Probe did not select the Relay path")
                # Datagram delivery is best effort; preserve losses in the result.
                report["datagrams_missing"] = 120 - measurements[0]["datagrams_echoed"]
                # Allow the production five-second counter log to observe disconnects.
                time.sleep(5.5)
            finally:
                relay_process.terminate()
                try:
                    report["relay_exit_code"] = relay_process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    relay_process.kill()
                    relay_process.wait()
                    raise RuntimeError("Relay failed to stop within 15 seconds")
                report["shutdown_method"] = "owned-process termination; lifecycle covered by unit tests" if os.name == "nt" else "SIGTERM"
                arguments.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        relay_output = re.sub(r"\x1b\[[0-9;]*m", "", log_path.read_text(encoding="utf-8"))
        if os.name != "nt" and (report["relay_exit_code"] != 0 or "Pixels iroh Relay stopped" not in relay_output):
            raise RuntimeError("Linux Relay did not shut down gracefully")
        if "forwarded_bytes=" not in relay_output:
            raise RuntimeError("Missing Relay traffic counters")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
