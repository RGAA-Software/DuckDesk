"""Replace private Relay candidates while holding a reliable stream and QUIC connection."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from run_iroh_relay_recovery import launch, stop_owned, wait_for_line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--certificate-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18555)
    parser.add_argument("--relay-count", type=int, default=2, choices=range(2, 33))
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    certificates = arguments.certificate_dir.resolve()
    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    relays = []
    report = {"passed": False, "relay_count": arguments.relay_count,
              "scope": "live candidate replacement, held stream/datagram and new peer; loopback TLS, not cross-host capacity"}
    try:
        ports = list(range(arguments.port, arguments.port + arguments.relay_count))
        for port in ports:
            relay, relay_lines = launch([
                str(repository / "rust_transport/target/release/px_relay_probe.exe"), f"127.0.0.1:{port}",
                "--tls-cert", str(certificates / "server.pem"), "--tls-key", str(certificates / "server-key.pem"),
                "--qad-bind", f"127.0.0.1:{port}"])
            relays.append(relay)
            wait_for_line(relay_lines, "Private test relay listening", 10)
        environment = os.environ.copy()
        environment["PX_IROH_TEST_ENDPOINT_CONFIG"] = json.dumps({"relay_only": True,
            "relays": [{"url": f"https://127.0.0.1:{port}/", "qad_port": port} for port in ports],
            "ca_certificates_pem": [(certificates / "ca.pem").read_text(encoding="utf-8")]})
        result = subprocess.run([
            str(repository / "build_official/cloud_node/cmake/src/px_transport/test_iroh_transport.exe"),
            "--gtest_filter=IrohTransport.Candidate*",
            "--gtest_output=xml:" + str(output.with_suffix(".xml"))],
            env=environment, capture_output=True, text=True, encoding="utf-8", timeout=60)
        output.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
        report["exit_code"] = result.returncode
        if result.returncode != 0:
            raise RuntimeError("Relay candidate test failed; inspect " + str(output.with_suffix(".log")))
        report["passed"] = True
    finally:
        for relay in relays:
            stop_owned(relay)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
