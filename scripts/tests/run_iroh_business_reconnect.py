"""Hold an owned Relay offline until QUIC closes, then verify business readmission."""
import argparse
import json
import os
import queue
from pathlib import Path
import time

from run_iroh_relay_recovery import launch, stop_owned, wait_for_line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--certificate-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18450)
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    certificates = arguments.certificate_dir.resolve()
    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    relay_address = "127.0.0.1:" + str(arguments.port)
    relay_command = [str(repository / "rust_transport/target/release/px_relay_probe.exe"), relay_address,
                     "--tls-cert", str(certificates / "server.pem"),
                     "--tls-key", str(certificates / "server-key.pem"), "--qad-bind", relay_address]
    environment = os.environ.copy()
    environment["PX_IROH_TEST_LONG_RELAY_OUTAGE"] = "1"
    environment["PX_IROH_TEST_ENDPOINT_CONFIG"] = json.dumps({
        "relay_only": True,
        "relays": [{"url": "https://" + relay_address, "qad_port": arguments.port}],
        "ca_certificates_pem": [(certificates / "ca.pem").read_text(encoding="utf-8")],
    })
    relay = None
    client_test = None
    try:
        relay, relay_lines = launch(relay_command)
        wait_for_line(relay_lines, "Private test relay listening", 10)
        client_test, test_lines = launch([
            str(repository / "build_official/cloud_node/cmake/src/px_render/tests/test_iroh_frontend.exe"),
            "--gtest_filter=IrohFrontendTest.NetClientReadmitsAfterRelayOutageClosesQuic",
            "--gtest_output=xml:" + str(output.with_suffix(".xml")),
        ], environment)
        wait_for_line(test_lines, "IROH_RECONNECT_READY", 15)
        interruption_started = time.monotonic()
        stop_owned(relay)
        print("Owned Relay stopped; waiting for actual QUIC disconnect", flush=True)
        wait_for_line(test_lines, "IROH_QUIC_DISCONNECTED", 45)
        disconnected_after_ms = round((time.monotonic() - interruption_started) * 1000)
        relay, relay_lines = launch(relay_command)
        wait_for_line(relay_lines, "Private test relay listening", 10)
        restored_at = time.monotonic()
        print("Owned Relay restarted; waiting for readmission and video", flush=True)
        completion_deadline = time.monotonic() + 85
        while True:
            line = test_lines.get(timeout=max(0.01, completion_deadline - time.monotonic()))
            if line is None:
                break
            print(line, flush=True)
        exit_code = client_test.wait(timeout=5)
        report = {
            "scenario": "owned private Relay unavailable until QUIC closes, followed by business readmission and video",
            "quic_disconnect_after_ms": disconnected_after_ms,
            "test_completion_after_relay_restart_ms": round((time.monotonic() - restored_at) * 1000),
            "exit_code": exit_code,
            "relay_only": True,
        }
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report), flush=True)
        if exit_code:
            raise RuntimeError("Business reconnect test failed; inspect the XML report")
    finally:
        stop_owned(client_test)
        stop_owned(relay)


if __name__ == "__main__":
    main()
