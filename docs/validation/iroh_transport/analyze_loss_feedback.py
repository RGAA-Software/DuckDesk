"""Keep valid sampling bounds and distinguish delivery from graphics presentation."""
import gzip
import json
import re
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

EVIDENCE = Path(__file__).resolve().parent


def summarize(prefix):
    metadata = json.loads((EVIDENCE / (prefix + "_live.json")).read_text(encoding="utf-8"))
    compressed = EVIDENCE / (prefix + "_client.log.gz")
    original = Path(".cache") / (prefix + "_client.log")
    if not compressed.exists():
        compressed.write_bytes(gzip.compress(original.read_bytes(), mtime=0))
    lines = gzip.decompress(compressed.read_bytes()).decode("utf-8-sig").splitlines()
    receive_windows = []
    present_windows = []
    receive_gaps = []
    present_gaps = []
    deliveries = []
    ui_operations = []
    ui_loops = []
    native_messages = []
    log_overruns = []
    decoder_output_windows = []
    decoder_recoveries = []
    upload_rejections = []
    for line in lines:
        timestamp = re.match(r"\[([\d-]+ [\d:.]+)\]", line)
        if not timestamp:
            continue
        epoch = datetime.strptime(timestamp[1], "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=timezone(timedelta(hours=8))).timestamp()
        if not metadata["sample_started_epoch"] <= epoch <= metadata["sample_ended_epoch"]:
            continue
        fields = dict(re.findall(r"(\w+)=([^ ,]+)", line))
        if "[LAT-decode]" in line and all(name in fields for name in ("attempts", "no_output", "errors")):
            decoder_output_windows.append({"timestamp": timestamp[1], **fields})
        if "event=video.decoder_output_stalled" in line:
            decoder_recoveries.append({"timestamp": timestamp[1], **fields})
        if "event=video.upload_rejected" in line:
            upload_rejections.append({"timestamp": timestamp[1], **fields})
        if "log_overruns" in fields:
            log_overruns.append(int(fields["log_overruns"]))
        if "event=video.ui_operation phase=slow" in line:
            ui_operations.append({"timestamp": timestamp[1], **fields})
        if "event=video.ui_loop phase=slow" in line:
            ui_loops.append({"timestamp": timestamp[1], **fields})
        if "event=video.native_message phase=slow" in line:
            native_messages.append({"timestamp": timestamp[1], **fields})
        if "iroh media receive window:" in line:
            receive_windows.append({"timestamp": timestamp[1], **fields})
        if "event=iroh.received_frame phase=gap" in line:
            receive_gaps.append({"timestamp": timestamp[1], **fields})
        if "event=video.present phase=window" in line and epoch - int(fields["elapsed_us"]) / 1_000_000 >= metadata["sample_started_epoch"]:
            present_windows.append({"timestamp": timestamp[1], **fields})
        if "event=video.present phase=gap" in line:
            present_gaps.append({"timestamp": timestamp[1], **fields})
        # Recovery diagnostics continue after the initial detailed window. Their
        # sparse timestamps must not be treated as ordinary delivery intervals.
        if ("event=iroh.diag_rx_result" in line and fields.get("deliver") == "true"
                and epoch <= metadata["sample_started_epoch"] + 90):
            deliveries.append(int(fields["mono_us"]))
    early_gaps = sorted((following - previous) / 1000 for previous, following in zip(deliveries, deliveries[1:]))
    present_seconds = sum(int(window["elapsed_us"]) for window in present_windows) / 1_000_000
    bounded_receive_windows = [window for window in receive_windows
                               if "elapsed_us" in window and "gaps_gt_50ms" in window
                               and datetime.strptime(window["timestamp"], "%Y-%m-%d %H:%M:%S.%f")
                               .replace(tzinfo=timezone(timedelta(hours=8))).timestamp()
                               - int(window["elapsed_us"]) / 1_000_000 >= metadata["sample_started_epoch"]]
    measured_intervals = sum(int(window["frames"]) for window in bounded_receive_windows)
    intervals_over_50ms = sum(int(window["gaps_gt_50ms"]) for window in bounded_receive_windows)
    report = {
        "metadata": metadata,
        "receive_windows": receive_windows,
        "receive_gaps": receive_gaps,
        "present_windows": present_windows,
        "present_gaps": present_gaps,
        "ui_slow_operations": ui_operations,
        "ui_slow_loops": ui_loops,
        "native_slow_messages": native_messages,
        "maximum_log_overruns": max(log_overruns) if log_overruns else None,
        "weighted_present_fps": sum(int(window["frames"]) for window in present_windows) / present_seconds if present_seconds else None,
        "early_delivery_detail_seconds": (deliveries[-1] - deliveries[0]) / 1_000_000 if len(deliveries) > 1 else 0,
        "early_delivery_p99_ms": early_gaps[min(len(early_gaps) - 1, int(len(early_gaps) * .99))] if early_gaps else None,
        "aggregate_delivery_coverage_seconds": sum(int(window["elapsed_us"]) for window in bounded_receive_windows) / 1_000_000,
        "aggregate_delivery_intervals": measured_intervals,
        "aggregate_delivery_intervals_over_50ms": intervals_over_50ms,
        "aggregate_delivery_p99_at_most_50ms": intervals_over_50ms / measured_intervals <= .01 if measured_intervals else None,
        "decoder_output_diagnostic_totals": {
            name: sum(int(window[name]) for window in decoder_output_windows)
            for name in ("frames", "attempts", "no_output", "errors")
        } if decoder_output_windows else None,
        "decoder_recovery_events": decoder_recoveries,
        "upload_rejection_events": upload_rejections,
        "maximum_present_gap_ms": max((int(gap["gap_us"]) / 1000 for gap in present_gaps), default=0),
        "note": "Detail is limited to the initial diagnostic window. Aggregate presentation counts newly uploaded frames accepted by the API, not scanout.",
    }
    (EVIDENCE / (prefix + "_summary.json")).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return {"prefix": prefix, "receive_gaps": len(receive_gaps), "maximum_receive_gap_ms": max((float(gap["gap_ms"]) for gap in receive_gaps), default=0),
            "present_gaps": len(present_gaps), "maximum_present_gap_ms": report["maximum_present_gap_ms"],
            "decoder_output_diagnostic_totals": report["decoder_output_diagnostic_totals"],
            "decoder_recoveries": len(decoder_recoveries), "upload_rejections": len(upload_rejections),
            "weighted_present_fps": report["weighted_present_fps"],
            "maximum_log_overruns": report["maximum_log_overruns"], "ui_slow_operations": len(ui_operations),
            "early_delivery_p99_ms": report["early_delivery_p99_ms"], "drag": metadata["drag"]}


if __name__ == "__main__":
    for prefix in sys.argv[1:]:
        print(json.dumps(summarize(prefix), ensure_ascii=False))
