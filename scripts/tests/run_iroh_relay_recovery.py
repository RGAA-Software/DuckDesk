"""Restart only an owned loopback Relay and verify held QUIC streams/datagrams recover."""
import argparse
import json
from pathlib import Path
import queue
import subprocess
import threading
import time


def read_lines(process, pending_lines):
    for line in process.stdout:
        pending_lines.put(line.rstrip())
    pending_lines.put(None)


def launch(command, environment=None):
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", env=environment)
    pending_lines = queue.Queue()
    threading.Thread(target=read_lines, args=(process, pending_lines), daemon=True).start()
    return process, pending_lines


def wait_for_line(pending_lines, prefix, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while True:
        line = pending_lines.get(timeout=max(0.01, deadline - time.monotonic()))
        if line is None:
            raise RuntimeError("Probe exited before " + prefix)
        if line.startswith(prefix):
            return line


def stop_owned(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--certificate-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18449)
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    binaries = repository / "rust_transport/target/release"
    certificate_dir = arguments.certificate_dir.resolve()
    relay_address = "127.0.0.1:" + str(arguments.port)
    relay_command = [str(binaries / "px_relay_probe.exe"), relay_address,
                     "--tls-cert", str(certificate_dir / "server.pem"),
                     "--tls-key", str(certificate_dir / "server-key.pem"),
                     "--qad-bind", relay_address]
    relay = None
    probe = None
    try:
        relay, relay_lines = launch(relay_command)
        wait_for_line(relay_lines, "Private test relay listening", 10)
        probe, probe_lines = launch([str(binaries / "px_transport_probe.exe"), "recovery-self-test",
                                    "--relay", "https://" + relay_address, "--relay-only",
                                    "--qad-port", str(arguments.port), "--ca-root", str(certificate_dir / "ca.pem")])
        wait_for_line(probe_lines, "RECOVERY_READY", 15)
        time.sleep(2)
        interruption_started = time.monotonic()
        stop_owned(relay)
        time.sleep(1)
        relay, relay_lines = launch(relay_command)
        wait_for_line(relay_lines, "Private test relay listening", 10)
        interruption_ms = round((time.monotonic() - interruption_started) * 1000)
        report = json.loads(wait_for_line(probe_lines, "{", 30))
        if probe.wait(timeout=10) != 0:
            raise RuntimeError("Recovery probe failed after reporting")
        report.update({"scenario": "loopback private HTTPS Relay restarted; same endpoint and QUIC connection",
                       "relay_process_interruption_ms": interruption_ms, "relay_only": True,
                       "scope": "transport recovery, not business re-admission or physical network migration"})
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report), flush=True)
    finally:
        stop_owned(probe)
        stop_owned(relay)


if __name__ == "__main__":
    main()
