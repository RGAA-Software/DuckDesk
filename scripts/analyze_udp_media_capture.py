"""Compare bounded NIC captures by protected media identity, without decoding payloads.

Usage: python scripts/analyze_udp_media_capture.py <wire-directory>
Only Ethernet/IPv4 UDP video packets from the Pixels v2 transport are inspected.
Clock offsets are irrelevant to loss matching; timestamps measure each host locally.
"""

import collections
import json
import pathlib
import re
import struct
import sys


def packets(path):
    interfaces = []
    endian = "<"
    with path.open("rb") as source:
        while header := source.read(8):
            if len(header) != 8:
                raise ValueError("Truncated pcapng block header")
            if header[:4] == b"\x0a\x0d\x0d\x0a":
                magic = source.read(4)
                if magic not in (b"\x4d\x3c\x2b\x1a", b"\x1a\x2b\x3c\x4d"):
                    raise ValueError("Invalid pcapng byte order magic")
                endian = "<" if magic == b"\x4d\x3c\x2b\x1a" else ">"
                length = struct.unpack(endian + "I", header[4:])[0]
                if length < 28 or length > 16 * 1024 * 1024 or length % 4:
                    raise ValueError("Invalid pcapng section length")
                tail = source.read(length - 12)
                if len(tail) != length - 12 or struct.unpack(endian + "I", tail[-4:])[0] != length:
                    raise ValueError("Truncated or inconsistent pcapng section")
                interfaces = []
                continue
            kind, length = struct.unpack(endian + "II", header)
            if length < 12 or length > 16 * 1024 * 1024 or length % 4:
                raise ValueError("Invalid pcapng block length")
            body = source.read(length - 8)
            if len(body) != length - 8:
                raise ValueError("Truncated pcapng block")
            if struct.unpack(endian + "I", body[-4:])[0] != length:
                raise ValueError("Inconsistent pcapng block trailer")
            if kind == 1:
                if len(body) < 12:
                    raise ValueError("Truncated pcapng interface")
                link = struct.unpack_from(endian + "H", body)[0]
                resolution = 1e-6
                offset = 8
                while offset + 4 <= len(body) - 4:
                    code, size = struct.unpack_from(endian + "HH", body, offset)
                    offset += 4
                    if offset + ((size + 3) & ~3) > len(body) - 4:
                        raise ValueError("Truncated pcapng interface option")
                    if code == 0:
                        break
                    if code == 9 and size == 1:
                        power = body[offset]
                        resolution = 2 ** -(power & 127) if power & 128 else 10 ** -power
                    offset += (size + 3) & ~3
                interfaces.append((link, resolution))
            elif kind == 6:
                if len(body) < 24:
                    raise ValueError("Truncated pcapng packet header")
                interface, high, low, captured, original = struct.unpack_from(endian + "IIIII", body)
                if interface >= len(interfaces) or captured > original or 20 + ((captured + 3) & ~3) > len(body) - 4:
                    raise ValueError("Invalid pcapng packet interface or captured length")
                link, resolution = interfaces[interface]
                if link == 1:
                    yield ((high << 32) | low) * resolution, body[20:20 + captured], original


def video(path):
    result = {}
    sizes = {}
    blocks = {}
    for timestamp, packet, original in packets(path):
        if len(packet) < 42:
            continue
        ethernet = 14
        ether_type = int.from_bytes(packet[12:14], "big")
        while ether_type in (0x8100, 0x88A8):
            if len(packet) < ethernet + 4:
                break
            ether_type = int.from_bytes(packet[ethernet + 2:ethernet + 4], "big")
            ethernet += 4
        if ether_type != 0x800 or len(packet) < ethernet + 20 or packet[ethernet] >> 4 != 4 or packet[ethernet + 9] != 17:
            continue
        ip_header = (packet[ethernet] & 15) * 4
        if ip_header < 20 or len(packet) < ethernet + ip_header + 8:
            continue
        if int.from_bytes(packet[ethernet + 6:ethernet + 8], "big") & 0x1FFF:
            continue
        start = ethernet + ip_header + 8
        media = packet[start:]
        if len(media) < 40 or media[:5] != b"PXM\x02\x01":
            continue
        stream = media[5]
        frame = int.from_bytes(media[28:32], "little")
        block = (media[35] >> 4) & 3
        fec = int.from_bytes(media[36:40], "little")
        index = (fec >> 12) & 1023
        count = fec >> 22
        percent = (fec >> 4) & 255
        key = (stream, frame, block, index)
        result[key] = min(timestamp, result.get(key, timestamp))
        sizes[key] = original
        blocks[key[:3]] = (count, (count * percent + 99) // 100)
    return result, sizes, blocks


def summary(sent, received, sizes, blocks):
    if not sent or not received:
        raise ValueError("No video packets in one capture; check port, capture format and connection")
    # Circular capture and connection startup/shutdown can have unequal bounds.
    # Compare only the shared complete frame interval and report those bounds.
    streams = {key[0] for key in sent} & {key[0] for key in received}
    bounds = {}
    for stream in streams:
        left = [key[1] for key in sent if key[0] == stream]
        right = [key[1] for key in received if key[0] == stream]
        if max(left + right) - min(left + right) >= 2 ** 31:
            raise ValueError("Frame wrap or multiple sessions: split captures before comparing")
        first, last = max(min(left), min(right)) + 1, min(max(left), max(right)) - 1
        if first <= last:
            bounds[stream] = (first, last)
    sent = {key: value for key, value in sent.items() if key[0] in bounds and bounds[key[0]][0] <= key[1] <= bounds[key[0]][1]}
    received = {key: value for key, value in received.items() if key[0] in bounds and bounds[key[0]][0] <= key[1] <= bounds[key[0]][1]}
    if not sent or not received:
        raise ValueError("No shared interior frame interval; capture is too short or sessions differ")
    matched = sent.keys() & received.keys()
    if not matched:
        raise ValueError("No shared packet identities; cannot compare clock variation")
    missing = sent.keys() - received.keys()
    sent_blocks = collections.Counter(key[:3] for key in sent)
    received_blocks = collections.Counter(key[:3] for key in received)
    incomplete_sender_blocks = sum(sent_blocks[key] != sum(blocks[key]) for key in sent_blocks)
    unexpected_received = len(received.keys() - sent.keys())
    missing_frames = sorted({key[:2] for key in sent} - {key[:2] for key in received})
    unrecoverable = []
    for key in sorted(sent_blocks):
        data, parity = blocks[key]
        if received_blocks[key] < data:
            unrecoverable.append({"stream": key[0], "frame": key[1], "block": key[2], "data": data, "parity": parity,
                                  "sent": sent_blocks[key], "received": received_blocks[key]})
    times = sorted(received.values())
    gaps = sorted(((later - earlier) * 1000 for earlier, later in zip(times, times[1:])), reverse=True)
    send_times = sorted(sent.values())
    send_gaps = sorted(((b - a) * 1000 for a, b in zip(send_times, send_times[1:])), reverse=True)
    duration = max(sent.values()) - min(sent.values())
    rate = sum(sizes[key] for key in sent) * 8 / duration / 1e6 if duration > 0 else None
    delays = sorted(received[key] - sent[key] for key in matched)
    baseline = delays[0]
    return {"capture_complete_for_comparison": not incomplete_sender_blocks and not unexpected_received,
            "capture_completeness_proven": False,
            "evidence_limit": "Header consistency cannot rule out whole-frame capture loss. Check pktmon counters. "
                              "Relative delay includes clock drift and is not absolute one-way latency.",
            "sender_incomplete_blocks": incomplete_sender_blocks,
            "frame_intervals_by_stream": bounds, "sent_unique": len(sent), "received_unique": len(received),
            "missing_packets": len(missing), "missing_percent": round(100 * len(missing) / len(sent), 3),
            "unmatched_received": len(received.keys() - sent.keys()), "sender_wire_mbps": round(rate, 3) if rate is not None else None,
            "missing_whole_frames": missing_frames, "unrecoverable_blocks": unrecoverable,
            "largest_receive_gaps_ms": [round(value, 3) for value in gaps[:20]],
            "largest_send_gaps_ms": [round(value, 3) for value in send_gaps[:20]],
            "relative_transit_delay_ms": {str(p): round((delays[int((len(delays)-1)*p/100)]-baseline)*1000, 3)
                                           for p in (50, 95, 99, 100)}}


def timing_events(path, sent, received):
    """Join sparse application anomalies to full NIC identities, never subtract host steady clocks."""
    events = []
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        match = re.search(r"UDP timing (\w+): (.*)", line)
        if not match:
            continue
        fields = dict(re.findall(r"(\w+)=([^,\s]+)", match[2]))
        identity = fields.get("current", fields.get("trigger", ""))
        if not re.fullmatch(r"\d+/\d+/\d+", identity) or not fields.get("stream", "").isdigit():
            continue
        key = (int(fields["stream"]), *(int(part) for part in identity.split("/")))
        event = {"stage": match[1], "fields": fields, "key": key,
                 "seen_sender_nic": key in sent, "seen_receiver_nic": key in received}
        previous = fields.get("previous", "")
        if re.fullmatch(r"\d+/\d+/\d+", previous):
            before = (key[0], *(int(part) for part in previous.split("/")))
            for name, capture in (("sender", sent), ("receiver", received)):
                if before in capture and key in capture:
                    event[name + "_nic_pair_gap_us"] = round((capture[key] - capture[before]) * 1e6)
        events.append(event)
    return events


if __name__ == "__main__":
    directory = pathlib.Path(sys.argv[1])
    sent, sizes, blocks = video(directory / "send.pcapng")
    received, _, _ = video(directory / "receive.pcapng")
    result = summary(sent, received, sizes, blocks)
    result["application_timing"] = {
        name: timing_events(directory / name, sent, received)
        for name in ("client.log", "render.log") if (directory / name).exists()
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
