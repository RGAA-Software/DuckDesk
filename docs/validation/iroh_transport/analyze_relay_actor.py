"""Locate video gaps relative to upstream Relay actor reception, without packet payloads."""
import argparse
from bisect import bisect_left, bisect_right
from datetime import datetime, timedelta, timezone
import gzip
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", required=True)
    parser.add_argument("--client-log", required=True)
    parser.add_argument("--sample-metadata", required=True)
    parser.add_argument("--output", required=True)
    options = parser.parse_args()
    sample = json.loads(Path(options.sample_metadata).read_text(encoding="utf-8"))
    actor_times = []
    large_message_times = []
    trace_path = Path(options.trace)
    trace_bytes = trace_path.read_bytes()
    if trace_path.suffix == ".gz":
        trace_bytes = gzip.decompress(trace_bytes)
    for line in trace_bytes.decode("utf-8-sig", errors="replace").splitlines():
        if "received msg len=" not in line:
            continue
        timestamp = datetime.fromisoformat(line.split(" ", 1)[0].replace("Z", "+00:00")).timestamp()
        actor_times.append(timestamp)
        length = re.search(r"received msg len=(\d+)", line)
        if length and int(length[1]) >= 1100:
            large_message_times.append(timestamp)
    actor_times.sort()
    large_message_times.sort()
    video_gaps = []
    transit_by_frame = {}
    local_zone = timezone(timedelta(hours=8))
    for line in Path(options.client_log).read_text(encoding="utf-8-sig", errors="replace").splitlines():
        timestamp = re.match(r"\[([^]]+)\]", line)
        if not timestamp:
            continue
        ended = datetime.strptime(timestamp[1], "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=local_zone).timestamp()
        if not sample["sample_started_epoch"] <= ended <= sample["sample_ended_epoch"]:
            continue
        fields = dict(re.findall(r"(\w+)=([^ ,]+)", line))
        if "event=iroh.frame_transit" in line:
            transit_by_frame[int(fields["frame"])] = fields
        if "event=iroh.received_frame phase=gap" in line:
            video_gaps.append({"frame": int(fields["frame"]), "local_time": timestamp[1],
                               "ended": ended, "gap_ms": float(fields["gap_ms"])})
    for gap in video_gaps:
        started = gap["ended"] - gap["gap_ms"] / 1000
        gap["transit"] = transit_by_frame.get(gap["frame"])
        for label, received_times in (("actor", actor_times), ("large_message", large_message_times)):
            first_index = bisect_left(received_times, started)
            end_index = bisect_right(received_times, gap["ended"] + 0.002)
            neighboring_times = received_times[max(0, first_index - 1):min(len(received_times), end_index + 1)]
            intervals = [(following - previous) * 1000
                         for previous, following in zip(neighboring_times, neighboring_times[1:])
                         if previous <= gap["ended"] and following >= started]
            gap[label] = {"messages_inside": max(0, end_index - first_index),
                          "max_receive_gap_ms": max(intervals, default=None)}
    report = {"sample": sample, "actor_messages": len(actor_times),
              "large_messages": len(large_message_times), "video_gaps": video_gaps,
              "limitations": ["Opt-in upstream tracing writes synchronously; this is diagnostic evidence, not a performance acceptance run.",
                              "Actor messages include QUIC acknowledgements, audio and video; length alone does not identify a video shard.",
                              "A gap before actor reception does not distinguish TCP loss, Relay forwarding or source submission."]}
    Path(options.output).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"actor_messages": len(actor_times), "video_gaps": len(video_gaps),
                      "max_video_gap_ms": max((gap["gap_ms"] for gap in video_gaps), default=0),
                      "gaps_with_actor_pause": sum((gap["actor"]["max_receive_gap_ms"] or 0) >= 80 for gap in video_gaps)}))


if __name__ == "__main__":
    main()
