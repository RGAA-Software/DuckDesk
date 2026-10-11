"""Summarize complete-frame deliveries inside each recorded live sample."""
import gzip
import json
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path

EVIDENCE = Path(__file__).resolve().parent


def inspect_run(route):
    metadata = json.loads((EVIDENCE / f"20261011_feedback_lag_{route}_live.json").read_text(encoding="utf-8"))
    reports = []
    for case in metadata["cases"]:
        log_path = EVIDENCE / f"20261011_feedback_lag_{route}_client_{case['kind']}.log.gz"
        lines = gzip.decompress(log_path.read_bytes()).decode("utf-8-sig").splitlines()
        deliveries = []
        receive_windows = []
        for line in lines:
            timestamp = re.match(r"\[([\d-]+ [\d:.]+)\]", line)
            if not timestamp:
                continue
            epoch = datetime.strptime(timestamp[1], "%Y-%m-%d %H:%M:%S.%f").replace(
                tzinfo=timezone(timedelta(hours=8))
            ).timestamp()
            if not case["sample_started_epoch"] <= epoch <= case["sample_ended_epoch"]:
                continue
            if "iroh media receive window:" in line:
                receive_windows.append(line)
            if "event=iroh.diag_rx_result" in line and "deliver=true" in line:
                fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
                deliveries.append((int(fields["mono_us"]), int(fields["wire_frame"])))
        gaps = sorted((following[0] - previous[0]) / 1000 for previous, following in zip(deliveries, deliveries[1:]))
        reports.append({
            "kind": case["kind"], "state": case["state"], "grace_stopped": case.get("grace_stopped"),
            "sample_seconds": case["sample_ended_epoch"] - case["sample_started_epoch"],
            "delivered_frames": len(deliveries),
            "fps_between_deliveries": (len(deliveries) - 1) * 1000000 / (deliveries[-1][0] - deliveries[0][0]) if len(deliveries) > 1 else None,
            "max_gap_ms": max(gaps, default=None),
            "p99_gap_ms": gaps[min(len(gaps) - 1, int(len(gaps) * .99))] if gaps else None,
            "gaps_over_100ms": sum(gap > 100 for gap in gaps),
            "receive_windows": receive_windows,
        })
    return reports


if __name__ == "__main__":
    report = {route: inspect_run(route) for route in ("direct", "relay")}
    (EVIDENCE / "20261011_feedback_lag_summary.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
