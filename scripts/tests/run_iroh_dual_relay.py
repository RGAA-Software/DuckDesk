"""Stop the selected owned Relay, then dial the same endpoint via its updated address."""
import argparse
import json
from pathlib import Path
import re
import tempfile
import time

from run_iroh_relay_recovery import launch, stop_owned, wait_for_line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--certificate-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18551)
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    binaries = repository / "rust_transport/target/release"
    certificates = arguments.certificate_dir.resolve()
    relays = {}
    probe = None
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "same_quic_connection": False,
              "scope": "home Relay reselection and fresh-address reconnect; not Console/Panel address refresh"}
    try:
        with tempfile.TemporaryDirectory(prefix="pixels-dual-relay-") as temporary_directory:
            for port in (arguments.port, arguments.port + 1):
                relay, relay_lines = launch([str(binaries / "px_relay_probe.exe"), f"127.0.0.1:{port}",
                    "--tls-cert", str(certificates / "server.pem"), "--tls-key", str(certificates / "server-key.pem"),
                    "--qad-bind", f"127.0.0.1:{port}"])
                relays[port] = relay
                wait_for_line(relay_lines, "Private test relay listening", 10)
            configuration = Path(temporary_directory) / "endpoint.json"
            configuration.write_text(json.dumps({
                "relays": [{"url": f"https://127.0.0.1:{port}", "qad_port": port} for port in relays],
                "relay_only": True, "ca_certificates_pem": [(certificates / "ca.pem").read_text()],
            }), encoding="utf-8")
            probe, probe_lines = launch([str(binaries / "px_transport_probe.exe"), "relay-failover-self-test",
                                        "--endpoint-config", str(configuration)])
            initial_line = wait_for_line(probe_lines, "RECOVERY_READY", 15)
            selected_port = int(re.search(r"127\.0\.0\.1:(\d+)", initial_line).group(1))
            report["initial_path"] = initial_line
            time.sleep(2)
            stop_owned(relays[selected_port])
            report["stopped_relay_port"] = selected_port
            report["failover"] = wait_for_line(probe_lines, "RELAY_FAILOVER", 25)
            report["transfer"] = json.loads(wait_for_line(probe_lines, "{", 15))
            exit_code = probe.wait(timeout=10)
            remaining_port = next(port for port in relays if port != selected_port)
            if exit_code != 0 or f"127.0.0.1:{remaining_port}/" not in report["transfer"]["paths"]:
                raise RuntimeError("Probe did not complete through the surviving Relay")
            report["passed"] = True
    finally:
        stop_owned(probe)
        for relay in relays.values():
            stop_owned(relay)
        arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
