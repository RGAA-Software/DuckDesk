"""Observe an existing Relay-only QUIC connection without load or fault injection."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading
import time


def write_report(report_path, report):
    pending_path = report_path.with_suffix(".pending")
    pending_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    pending_path.replace(report_path)


def run(arguments):
    probe_path = arguments.probe.resolve(strict=True)
    configuration_path = arguments.configuration.resolve(strict=True)
    configuration = json.loads(configuration_path.read_text(encoding="utf-8-sig"))
    if not configuration.get("relay_only") or len(configuration.get("relays", [])) != 1:
        raise ValueError("The soak requires exactly one Relay-only candidate")
    evidence_directory = arguments.evidence_directory.resolve()
    evidence_directory.mkdir(parents=True, exist_ok=False)
    report_path = evidence_directory / "report.json"
    stdout_path = evidence_directory / "stdout.log"
    stderr_path = evidence_directory / "stderr.log"
    observations = queue.Queue()
    report = {
        "scope": "low-load held QUIC connection through a public Relay",
        "relay_url": configuration["relays"][0]["url"],
        "probe_sha256": hashlib.sha256(probe_path.read_bytes()).hexdigest(),
        "configuration_sha256": hashlib.sha256(configuration_path.read_bytes()).hexdigest(),
        "requested_seconds": arguments.duration_seconds,
        "interval_seconds": arguments.interval_seconds,
        "started_epoch": time.time(),
        "samples": [],
        "passed": False,
        "media_performance_test": False,
        "throughput_capacity_test": False,
        "fault_injection": False,
    }

    with stderr_path.open("wb") as stderr_file:
        probe = subprocess.Popen(
            [str(probe_path), "retention-self-test", "--endpoint-config", str(configuration_path),
             "--retention-timeout-seconds", str(arguments.duration_seconds + 60)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr_file,
            text=True, encoding="utf-8",
        )
        report["owned_pid"] = probe.pid

        def collect_output():
            with stdout_path.open("w", encoding="utf-8") as stdout_file:
                for log_line in probe.stdout:
                    stdout_file.write(log_line)
                    stdout_file.flush()
                    observations.put(log_line)

        output_thread = threading.Thread(target=collect_output, daemon=True)
        output_thread.start()

        def wait_marker(prefix):
            deadline = time.monotonic() + arguments.command_timeout_seconds
            while time.monotonic() < deadline:
                try:
                    log_line = observations.get(timeout=min(1, max(0.01, deadline - time.monotonic())))
                except queue.Empty:
                    if probe.poll() is not None:
                        raise RuntimeError("Owned probe exited before " + prefix)
                    continue
                if log_line.startswith(prefix):
                    encoded = log_line[len(prefix):].strip()
                    return json.loads(encoded) if encoded else None
            raise TimeoutError("Owned probe timed out before " + prefix)

        def send_command(command, marker):
            probe.stdin.write(json.dumps(command) + "\n")
            probe.stdin.flush()
            return wait_marker(marker)

        try:
            ready = wait_marker("RETENTION_READY ")
            report["endpoint_address"] = ready["endpoint_address"]
            started = time.monotonic()
            sample_index = 0
            while True:
                scheduled = started + sample_index * arguments.interval_seconds
                remaining = scheduled - time.monotonic()
                if remaining > 0:
                    time.sleep(remaining)
                sample_started = time.monotonic()
                result = send_command(
                    {"phase": "bandwidth", "relays": configuration["relays"]}, "RETENTION_PHASE ",
                )
                if not all(result.get(field) for field in (
                    "same_endpoint_identities", "same_quic_connections", "same_reliable_stream",
                )):
                    raise RuntimeError("Held connection identity or stream changed")
                if result["reliable_round_trips"] != 4 or result["datagrams_each_direction"] != 4:
                    raise RuntimeError("Unexpected verified exchange count")
                if result["home"] != report["relay_url"]:
                    raise RuntimeError("Held connection moved to another Relay")
                report["samples"].append({
                    "elapsed_seconds": time.monotonic() - started,
                    "exchange_seconds": time.monotonic() - sample_started,
                    "result": result,
                })
                report["observation_seconds"] = time.monotonic() - started
                write_report(report_path, report)
                if time.monotonic() - started >= arguments.duration_seconds:
                    break
                sample_index += 1
            send_command({"phase": "release"}, "RETENTION_RELEASED")
            send_command({"phase": "finish"}, "DISTRIBUTION_FINISHED")
            probe.stdin.close()
            if probe.wait(timeout=10) != 0:
                raise RuntimeError("Owned probe exited unsuccessfully")
            report["released"] = True
            report["passed"] = True
        except Exception as error:
            report["failure"] = str(error)
        finally:
            if probe.poll() is None:
                probe.terminate()
                try:
                    probe.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    probe.kill()
                    probe.wait(timeout=10)
            output_thread.join(timeout=5)
            probe.stdout.close()
            if not probe.stdin.closed:
                probe.stdin.close()
            report["exit_code"] = probe.poll()
            report["owned_probe_stopped"] = probe.poll() is not None
            report["completed_epoch"] = time.time()
            write_report(report_path, report)

    report["logs"] = []
    for log_path in (stdout_path, stderr_path):
        compressed_path = log_path.with_suffix(log_path.suffix + ".gz")
        compressed_path.write_bytes(gzip.compress(log_path.read_bytes(), mtime=0))
        report["logs"].append({
            "name": compressed_path.name,
            "bytes": compressed_path.stat().st_size,
            "sha256": hashlib.sha256(compressed_path.read_bytes()).hexdigest(),
        })
    sample_count = len(report["samples"])
    report["reliable_round_trips_verified"] = sample_count * 4
    report["datagrams_verified_each_direction"] = sample_count * 4
    report["maximum_exchange_seconds"] = max((sample["exchange_seconds"] for sample in report["samples"]), default=None)
    write_report(report_path, report)
    print(json.dumps({key: value for key, value in report.items() if key not in ("samples", "endpoint_address")}))
    if not report["passed"]:
        raise RuntimeError(report.get("failure", "Held-connection observation failed"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--configuration", type=Path, required=True)
    parser.add_argument("--evidence-directory", type=Path, required=True)
    parser.add_argument("--duration-seconds", type=int, default=600)
    parser.add_argument("--interval-seconds", type=int, default=5)
    parser.add_argument("--command-timeout-seconds", type=int, default=30)
    arguments = parser.parse_args()
    if not 30 <= arguments.duration_seconds <= 3600:
        parser.error("duration must be between 30 and 3600 seconds")
    if not 1 <= arguments.interval_seconds <= 60 or not 1 <= arguments.command_timeout_seconds <= 60:
        parser.error("interval and command timeout must be between 1 and 60 seconds")
    run(arguments)
