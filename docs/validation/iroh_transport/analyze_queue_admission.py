"""Compare bounded normal video runs; wall-clock sampling excludes disconnect/grace."""
import gzip
import json
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path

EVIDENCE = Path(__file__).resolve().parent


def inspect_run(label):
    metadata = json.loads((EVIDENCE / f"20261011_queue_{label}_live.json").read_text(encoding="utf-8"))
    reports = []
    for case in metadata["cases"]:
        log_path = EVIDENCE / f"20261011_queue_{label}_client_{case['kind']}.log.gz"
        lines = gzip.decompress(log_path.read_bytes()).decode("utf-8-sig").splitlines()
        delivered = []
        receive_windows = []
        for line in lines:
            timestamp = re.match(r"\[([\d-]+ [\d:.]+)\]", line)
            if not timestamp:
                continue
            # Source logs and this Windows host use Asia/Shanghai local time.
            epoch = datetime.strptime(timestamp[1], "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=timezone(timedelta(hours=8))).timestamp()
            if not case["sample_started_epoch"] <= epoch <= case["sample_ended_epoch"]:
                continue
            if "iroh media receive window:" in line:
                receive_windows.append(line)
            if "event=iroh.diag_rx_result" in line and "deliver=true" in line:
                fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
                delivered.append((int(fields["mono_us"]), int(fields["wire_frame"])))
        gaps = sorted((following[0] - previous[0]) / 1000 for previous, following in zip(delivered, delivered[1:]))
        reports.append({
            "kind": case["kind"], "state": case["state"], "grace_stopped": case.get("grace_stopped"),
            "sample_seconds": case["sample_ended_epoch"] - case["sample_started_epoch"],
            "delivered_frames": len(delivered),
            "fps_between_deliveries": (len(delivered) - 1) * 1000000 / (delivered[-1][0] - delivered[0][0]) if len(delivered) > 1 else None,
            "max_gap_ms": max(gaps, default=None),
            "p99_gap_ms": gaps[min(len(gaps) - 1, int(len(gaps) * .99))] if gaps else None,
            "gaps_over_100ms": sum(gap > 100 for gap in gaps),
            "receive_windows": receive_windows,
        })
    return reports


if __name__ == "__main__":
    report = {label: inspect_run(label) for label in ("baseline", "candidate")}
    (EVIDENCE / "20261011_queue_comparison.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
