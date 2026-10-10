"""Verify SDK readmission and video after losing the selected of two owned Relays."""
import argparse
import json
import os
from pathlib import Path
import time
from urllib.parse import urlparse

from run_iroh_relay_recovery import launch, stop_owned, wait_for_line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--certificate-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18553)
    parser.add_argument("--server-auto", action="store_true")
    parser.add_argument("--max-recovery-ms", type=int, default=15000)
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    certificates = arguments.certificate_dir.resolve()
    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    relays = {}
    client_test = None
    report = {"passed": False, "scope": "SDK fresh-address readmission and video; two loopback TLS Relays"}
    try:
        for port in (arguments.port, arguments.port + 1):
            relay, relay_lines = launch([str(repository / "rust_transport/target/release/px_relay_probe.exe"), f"127.0.0.1:{port}",
                "--tls-cert", str(certificates / "server.pem"), "--tls-key", str(certificates / "server-key.pem"),
                "--qad-bind", f"127.0.0.1:{port}"])
            relays[port] = relay
            wait_for_line(relay_lines, "Private test relay listening", 10)
        environment = os.environ.copy()
        environment["PX_IROH_TEST_LONG_RELAY_OUTAGE"] = "1"
        environment["PX_IROH_TEST_ENDPOINT_CONFIG"] = json.dumps({"relay_only": not arguments.server_auto,
            "relays": [{"url": f"https://127.0.0.1:{port}", "qad_port": port} for port in relays],
            "ca_certificates_pem": [(certificates / "ca.pem").read_text(encoding="utf-8")]})
        client_test, test_lines = launch([
            str(repository / "build_official/cloud_node/cmake/src/px_render/tests/test_iroh_frontend.exe"),
            "--gtest_filter=IrohFrontendTest.NetClientReadmitsAfterRelayOutageClosesQuic",
            "--gtest_output=xml:" + str(output.with_suffix(".xml")),
        ], environment)
        ready_line = wait_for_line(test_lines, "IROH_RECONNECT_READY ", 20)
        original_address = json.loads(ready_line.split(" ", 1)[1])
        selected_url = next(address["Relay"] for address in original_address["addrs"] if "Relay" in address)
        selected_port = urlparse(selected_url).port
        report["initial_address"] = original_address
        interruption_started = time.monotonic()
        stop_owned(relays[selected_port])
        print("Selected Relay stopped; it stays offline through readmission", flush=True)
        refresh_deadline = time.monotonic() + 65
        while True:
            refreshed_line = wait_for_line(test_lines, "IROH_REFRESHED_ADDRESS ", max(.01, refresh_deadline - time.monotonic()))
            refreshed_address = json.loads(refreshed_line.split(" ", 1)[1])
            if selected_url not in [address.get("Relay") for address in refreshed_address["addrs"]]:
                break
        report["refreshed_address"] = refreshed_address
        report["refresh_after_interruption_ms"] = round((time.monotonic() - interruption_started) * 1000)
        if refreshed_address["id"] != original_address["id"] or selected_url in [address.get("Relay") for address in refreshed_address["addrs"]]:
            raise RuntimeError("Reconnect did not use the same endpoint at a surviving Relay")
        while True:
            line = test_lines.get(timeout=60)
            if line is None:
                break
            print(line, flush=True)
        if client_test.wait(timeout=5) != 0:
            raise RuntimeError("SDK readmission/video test failed")
        report["recovery_after_interruption_ms"] = round((time.monotonic() - interruption_started) * 1000)
        report["max_recovery_ms"] = arguments.max_recovery_ms
        if report["recovery_after_interruption_ms"] > arguments.max_recovery_ms:
            raise RuntimeError("SDK video recovery exceeded its interruption budget")
        report["passed"] = True
    finally:
        stop_owned(client_test)
        for relay in relays.values():
            stop_owned(relay)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
