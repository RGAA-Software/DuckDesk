"""Check payload-free arrival traces for useful shards discarded at a frame boundary."""

import argparse
import json
import re
from pathlib import Path


def analyze(client_log: Path, source_log: Path) -> dict:
    offers = {}
    for line in source_log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "event=iroh.diag_offer " in line:
            fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
            offers[(int(fields["stream"]), int(fields["wire_frame"]))] = int(fields["encoder_frame"])
    frames = {}
    for line in client_log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "event=iroh.diag_rx_packets " not in line:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        frame_key = (int(fields["stream"]), int(fields["wire_frame"]))
        frame_trace = frames.setdefault(frame_key, {"arrivals": [], "truncated": False})
        frame_trace["truncated"] |= fields["truncated"] == "true"
        for sample in fields["arrivals"].split(";"):
            if not sample:
                continue
            sequence, block, last_block, shard, data_count, parity_count, relative_us = map(int, sample.split(":"))
            frame_trace["arrivals"].append({
                "block": block, "last_block": last_block, "shard": shard,
                "data_count": data_count, "parity_count": parity_count,
                "arrival_us": int(fields["first_us"]) + relative_us,
            })
    next_frame_arrival = {}
    earliest_higher = {}
    for frame_key, trace in sorted(frames.items(), reverse=True):
        stream, wire_frame = frame_key
        next_frame_arrival[frame_key] = earliest_higher.get(stream)
        first_arrival = min(sample["arrival_us"] for sample in trace["arrivals"])
        earliest_higher[stream] = min(first_arrival, earliest_higher.get(stream, first_arrival))
    incomplete = []
    for (stream, wire_frame), trace in sorted(frames.items()):
        later_first = next_frame_arrival[(stream, wire_frame)]
        blocks = {}
        for sample in trace["arrivals"]:
            blocks.setdefault(sample["block"], []).append(sample)
        for block, samples in blocks.items():
            needed = samples[0]["data_count"]
            unique = {sample["shard"] for sample in samples}
            before = {sample["shard"] for sample in samples
                      if later_first is None or sample["arrival_us"] < later_first}
            if len(before) >= needed:
                continue  # Later parity of an already completed frame is harmless.
            incomplete.append({
                "stream": stream, "wire_frame": wire_frame,
                "encoder_frame": offers.get((stream, wire_frame)), "block": block,
                "data_needed": needed, "parity_offered": samples[0]["parity_count"],
                "unique_before_next": len(before), "unique_total": len(unique),
                "late_recoverable": later_first is not None and len(unique) >= needed,
                "truncated": trace["truncated"],
            })
    return {"observed_frames": len(frames), "incomplete_blocks": incomplete,
            "late_recoverable_blocks": [block for block in incomplete if block["late_recoverable"]],
            "limits": "Metadata-only; absent traces do not prove packet loss, and codec payloads are not decoded."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("client_log", type=Path)
    parser.add_argument("source_log", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    report = analyze(arguments.client_log, arguments.source_log)
    arguments.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"observed_frames": report["observed_frames"],
                      "incomplete_blocks": len(report["incomplete_blocks"]),
                      "late_recoverable_blocks": report["late_recoverable_blocks"]}, indent=2))
