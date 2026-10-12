"""Summarize captured TCP ordering and ACK stalls; never infer physical loss from overlap alone."""

import argparse
import gzip
import json
import socket
import struct
from pathlib import Path


def packets(capture_path):
    opener = gzip.open if capture_path.suffix == ".gz" else open
    with opener(capture_path, "rb") as capture:
        header = capture.read(24)
        byte_orders = {b"\xd4\xc3\xb2\xa1": ("<", 1e6), b"\xa1\xb2\xc3\xd4": (">", 1e6),
                       b"\x4d\x3c\xb2\xa1": ("<", 1e9), b"\xa1\xb2\x3c\x4d": (">", 1e9)}
        if header[:4] not in byte_orders:
            raise ValueError("Expected classic pcap, including a complete capture header")
        byte_order, timestamp_scale = byte_orders[header[:4]]
        link_type = struct.unpack(byte_order + "I", header[20:24])[0]
        link_offsets = {1: (14, 12), 113: (16, 14), 276: (20, 0)}
        if link_type not in link_offsets:
            raise ValueError(f"Unsupported link type {link_type}")
        ip_offset, protocol_offset = link_offsets[link_type]
        while packet_header := capture.read(16):
            if len(packet_header) != 16:
                raise ValueError("Truncated packet header")
            seconds, fraction, captured_size, original_size = struct.unpack(byte_order + "IIII", packet_header)
            packet = capture.read(captured_size)
            if len(packet) != captured_size:
                raise ValueError("Truncated captured packet")
            if len(packet) < ip_offset + 40 or packet[protocol_offset:protocol_offset + 2] != b"\x08\x00":
                continue
            ip_header_size = (packet[ip_offset] & 15) * 4
            if packet[ip_offset + 9] != 6 or struct.unpack("!H", packet[ip_offset + 6:ip_offset + 8])[0] & 0x1FFF:
                continue
            tcp_offset = ip_offset + ip_header_size
            if len(packet) < tcp_offset + 20:
                continue
            source_port, destination_port, sequence, acknowledgement = struct.unpack("!HHII", packet[tcp_offset:tcp_offset + 12])
            tcp_header_size = (packet[tcp_offset + 12] >> 4) * 4
            if len(packet) < tcp_offset + tcp_header_size:
                continue
            total_size = struct.unpack("!H", packet[ip_offset + 2:ip_offset + 4])[0]
            source = socket.inet_ntoa(packet[ip_offset + 12:ip_offset + 16])
            destination = socket.inet_ntoa(packet[ip_offset + 16:ip_offset + 20])
            sack_blocks = []
            options = packet[tcp_offset + 20:tcp_offset + tcp_header_size]
            option_offset = 0
            while option_offset < len(options):
                option_kind = options[option_offset]
                if option_kind == 0:
                    break
                if option_kind == 1:
                    option_offset += 1
                    continue
                if option_offset + 1 >= len(options):
                    break
                option_size = options[option_offset + 1]
                if option_size < 2 or option_offset + option_size > len(options):
                    break
                if option_kind == 5:
                    sack_blocks.extend(struct.unpack("!II", options[block_offset:block_offset + 8])
                                       for block_offset in range(option_offset + 2, option_offset + option_size - 7, 8))
                option_offset += option_size
            yield {"epoch": seconds + fraction / timestamp_scale,
                   "source": f"{source}:{source_port}", "destination": f"{destination}:{destination_port}",
                   "sequence": sequence, "ack": acknowledgement, "flags": packet[tcp_offset + 13],
                   "window": struct.unpack("!H", packet[tcp_offset + 14:tcp_offset + 16])[0],
                   "payload_bytes": max(0, total_size - ip_header_size - tcp_header_size), "sacks": sack_blocks}


def extend_sequence(sequence, reference):
    if reference is None:
        return sequence
    return reference + ((sequence - reference + 2**31) % 2**32) - 2**31


def new_flow():
    return {"packets": 0, "payload_bytes": 0, "next_sequence": None, "pending": [], "hole": None,
            "holes": [], "sack_hole": None, "sack_stalls": [], "overlap_packets": 0, "zero_window_packets": 0,
            "last_payload_epoch": None, "payload_gaps": []}


def summarize(capture_path, minimum_gap_ms):
    flows = {}
    first_epoch = None
    last_epoch = None
    for packet in packets(capture_path):
        timestamp = packet["epoch"]
        if first_epoch is None:
            first_epoch = timestamp
        last_epoch = timestamp
        flow_key = packet["source"] + ">" + packet["destination"]
        reverse_key = packet["destination"] + ">" + packet["source"]
        flow = flows.setdefault(flow_key, new_flow())
        reverse = flows.setdefault(reverse_key, new_flow())
        flow["packets"] += 1
        if packet["flags"] & 16 and not packet["flags"] & 6:
            flow["zero_window_packets"] += packet["window"] == 0
            acknowledgement = extend_sequence(packet["ack"], reverse["next_sequence"])
            stalled_ack = reverse["sack_hole"]
            if stalled_ack is not None and acknowledgement > stalled_ack["ack"]:
                stalled_ack.update(ended_epoch=timestamp, duration_ms=(timestamp - stalled_ack["started_epoch"]) * 1000)
                if stalled_ack["duration_ms"] >= minimum_gap_ms:
                    reverse["sack_stalls"].append(stalled_ack)
                reverse["sack_hole"] = None
            if any(extend_sequence(lower, acknowledgement) > acknowledgement for lower, upper in packet["sacks"]):
                if reverse["sack_hole"] is None:
                    reverse["sack_hole"] = {"started_epoch": timestamp, "ack": acknowledgement}
        sequence = extend_sequence(packet["sequence"], flow["next_sequence"])
        if packet["flags"] & 2:
            flow["next_sequence"] = sequence + 1
        payload_bytes = packet["payload_bytes"]
        if payload_bytes <= 0:
            continue
        flow["payload_bytes"] += payload_bytes
        previous_payload_epoch = flow["last_payload_epoch"]
        if previous_payload_epoch is not None and (timestamp - previous_payload_epoch) * 1000 >= minimum_gap_ms:
            flow["payload_gaps"].append({"started_epoch": previous_payload_epoch, "ended_epoch": timestamp,
                                         "duration_ms": (timestamp - previous_payload_epoch) * 1000})
        flow["last_payload_epoch"] = timestamp
        expected_sequence = flow["next_sequence"]
        if expected_sequence is None:
            expected_sequence = sequence
        if sequence < expected_sequence:
            flow["overlap_packets"] += 1
        flow["pending"].append((sequence, sequence + payload_bytes))
        pending_ranges = []
        for lower, upper in sorted(flow["pending"]):
            if lower <= expected_sequence:
                expected_sequence = max(expected_sequence, upper)
            elif pending_ranges and lower <= pending_ranges[-1][1]:
                pending_ranges[-1] = (pending_ranges[-1][0], max(pending_ranges[-1][1], upper))
            else:
                pending_ranges.append((lower, upper))
        flow["next_sequence"] = expected_sequence
        flow["pending"] = pending_ranges
        pending_hole = flow["hole"]
        if pending_hole is not None and expected_sequence > pending_hole["missing_sequence"]:
            pending_hole.update(ended_epoch=timestamp, duration_ms=(timestamp - pending_hole["started_epoch"]) * 1000)
            if pending_hole["duration_ms"] >= minimum_gap_ms:
                flow["holes"].append(pending_hole)
            flow["hole"] = None
        if pending_ranges and flow["hole"] is None:
            flow["hole"] = {"started_epoch": timestamp, "missing_sequence": expected_sequence}
    excluded_fields = {"next_sequence", "pending", "last_payload_epoch"}
    return {"capture": str(capture_path), "first_epoch": first_epoch, "last_epoch": last_epoch,
            "minimum_gap_ms": minimum_gap_ms,
            "limitations": "Sequence holes and SACK prove ordering waits at this capture point, not a physical loss location. Payload gaps include idle periods. TLS prevents frame identification.",
            "flows": {flow_key: {field: value for field, value in flow.items() if field not in excluded_fields}
                      for flow_key, flow in flows.items()}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--minimum-gap-ms", type=float, default=50)
    arguments = parser.parse_args()
    report = summarize(arguments.capture, arguments.minimum_gap_ms)
    arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({flow_key: {"bytes": flow["payload_bytes"], "ordering_waits": len(flow["holes"]),
                               "maximum_ordering_wait_ms": max((event["duration_ms"] for event in flow["holes"]), default=0),
                               "ack_waits": len(flow["sack_stalls"]), "zero_window_packets": flow["zero_window_packets"]}
                      for flow_key, flow in report["flows"].items()}, indent=2))
