"""Correlate connection-scoped Windows TCP counters with valid video sampling bounds."""
import argparse
import csv
from collections import Counter
from datetime import datetime, timedelta, timezone
import gzip
import io
import json
from pathlib import Path
import re


COUNTERS = ("fast_retrans", "timeouts", "retrans_bytes", "dup_acks_in", "sacks_received", "send_stalls", "receive_dup_acks")


def read_measurements(filename, offset_seconds, sample):
    content = Path(filename).read_bytes()
    if filename.endswith(".gz"):
        content = gzip.decompress(content)
    measurements = []
    previous_by_flow = {}
    for row in csv.DictReader(io.StringIO(content.decode("utf-8-sig"))):
        timestamp = datetime.fromisoformat(row["utc"]).timestamp() - offset_seconds
        if not sample["sample_started_epoch"] <= timestamp <= sample["sample_ended_epoch"]:
            continue
        if any(row[column] != "0" for column in ("path_error", "send_error", "receive_error")):
            measurements.append({"timestamp": timestamp, "flow": row["flow"], "errors": [row[column] for column in ("path_error", "send_error", "receive_error")]})
            continue
        metrics = {column: int(row[column]) for column in COUNTERS}
        previous = previous_by_flow.get(row["flow"])
        deltas = {column: max(0, metrics[column] - previous["metrics"][column]) if previous else 0 for column in COUNTERS}
        measurement = {"timestamp": timestamp, "previous_timestamp": previous["timestamp"] if previous else timestamp,
                       "flow": row["flow"], "metrics": metrics, "deltas": deltas,
                       "send_retrans_queue": int(row["send_retrans_queue"]),
                       "send_application_queue": int(row["send_application_queue"]),
                       "receive_reassembly_queue": int(row["receive_reassembly_queue"]),
                       "receive_application_queue": int(row["receive_application_queue"]),
                       "smoothed_rtt_ms": int(row["smoothed_rtt_ms"]), "rto_ms": int(row["rto_ms"])}
        measurements.append(measurement)
        previous_by_flow[row["flow"]] = measurement
    return measurements


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-csv", required=True)
    parser.add_argument("--source-csv", required=True)
    parser.add_argument("--clock-metadata", required=True)
    parser.add_argument("--sample-metadata", required=True)
    parser.add_argument("--client-log", required=True)
    parser.add_argument("--output", required=True)
    options = parser.parse_args()
    sample = json.loads(Path(options.sample_metadata).read_text(encoding="utf-8"))
    clock = json.loads(Path(options.clock_metadata).read_text(encoding="utf-8"))
    observations = {"client": read_measurements(options.client_csv, 0, sample),
                    "source": read_measurements(options.source_csv, clock["clock_offset_estimate_ms"] / 1000, sample)}
    primary_flows = {}
    additional_flows = {}
    for location, measurements in observations.items():
        flow_counts = Counter(measurement["flow"] for measurement in measurements if "deltas" in measurement)
        primary_flow = flow_counts.most_common(1)[0][0] if flow_counts else None
        primary_flows[location] = primary_flow
        additional_flows[location] = dict(flow_counts)
        observations[location] = [measurement for measurement in measurements if measurement["flow"] == primary_flow]
    gaps = []
    for line in Path(options.client_log).read_text(encoding="utf-8-sig", errors="replace").splitlines():
        if "event=iroh.received_frame phase=gap" not in line:
            continue
        matched = re.search(r"^\[([^]]+)\].*frame=(\d+) gap_ms=([\d.]+)", line)
        if not matched:
            continue
        ended = datetime.strptime(matched[1], "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=timezone(timedelta(hours=8))).timestamp()
        if not sample["sample_started_epoch"] <= ended <= sample["sample_ended_epoch"]:
            continue
        gap = {"frame": int(matched[2]), "local_time": matched[1], "gap_ms": float(matched[3])}
        started = ended - gap["gap_ms"] / 1000
        for location, measurements in observations.items():
            margin_seconds = (clock["uncertainty_ms"] / 1000 if location == "source" else 0) + 0.1
            gap[location] = [measurement for measurement in measurements if "deltas" in measurement
                             and measurement["previous_timestamp"] <= ended + margin_seconds
                             and measurement["timestamp"] >= started - margin_seconds]
        gaps.append(gap)
    totals = {}
    for location, measurements in observations.items():
        valid = [measurement for measurement in measurements if "deltas" in measurement]
        totals[location] = {"valid_samples": len(valid), "error_samples": len(measurements) - len(valid),
                            "deltas": {column: sum(measurement["deltas"][column] for measurement in valid) for column in COUNTERS},
                            "max_send_application_queue": max((measurement["send_application_queue"] for measurement in valid), default=0),
                            "max_receive_application_queue": max((measurement["receive_application_queue"] for measurement in valid), default=0),
                            "max_receive_reassembly_queue": max((measurement["receive_reassembly_queue"] for measurement in valid), default=0),
                            "max_smoothed_rtt_ms": max((measurement["smoothed_rtt_ms"] for measurement in valid), default=0)}
    report = {"sample": sample, "clock": clock, "primary_flows": primary_flows, "observed_flow_samples": additional_flows,
              "totals": totals, "video_gaps": gaps,
              "limitations": ["Counters are connection-scoped, not packet captures; sampled queues can miss shorter waits.",
                               "Source clock alignment has the recorded uncertainty; correlations are not exact per-packet causality.",
                               "Zero unsupported counters do not prove absence of loss; use advancing retransmission/duplicate-ACK counters."]}
    Path(options.output).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"video_gaps": len(gaps), "totals": totals}))


if __name__ == "__main__":
    main()
