"""Reproduce the single-application 2026-10-11 QUIC trace comparison offline.

Path 1 is the application's IP path in this capture; QAD uses path 0.
Do not use this selection for arbitrary captures with multiple media connections.
Packet construction is not evidence of socket/NIC transmission.
"""

import gzip
import json
import re
from pathlib import Path


def analyze(evidence_directory: Path) -> dict:
    server_lines = gzip.decompress(
        (evidence_directory / "20261011_quic_boundary_server_trace.log.gz").read_bytes()
    ).decode("utf-8").splitlines()
    client_lines = gzip.decompress(
        (evidence_directory / "20261011_quic_boundary_client_trace.log.gz").read_bytes()
    ).decode("utf-8").splitlines()
    sent_packets = {}
    received_packets = set()
    for line in server_lines:
        packet_match = re.search(r"send\{space=Data pn=(\d+) path_id=1\}", line)
        if not packet_match:
            continue
        packet = sent_packets.setdefault(
            int(packet_match[1]), {"constructed_at": line[:27], "frames": []}
        )
        if "frame=" in line:
            packet["frames"].append(line.split("frame=", 1)[1])
        if "wrote packet size=" in line:
            packet["bytes"] = int(line.rsplit("=", 1)[1])
    for line in client_lines:
        packet_match = re.search(r"pkt\{path_id=1\}:recv\{space=Data pn=(\d+)\}", line)
        if packet_match:
            received_packets.add(int(packet_match[1]))
    assert received_packets and sent_packets, "Both application path traces are required"
    # Exclude setup and the trailing connection-close observation boundary.
    first_packet = min(received_packets) + 10
    last_packet = max(received_packets) - 200
    missing_packets = {
        packet_number: packet
        for packet_number, packet in sent_packets.items()
        if first_packet < packet_number < last_packet
        and packet_number not in received_packets
    }
    incident_ack = next(
        line for line in client_lines
        if "ranges: [24318..24452,24456..24472,24489..24494]" in line
    )
    # These are half-open ACK ranges, not inclusive upper bounds.
    assert set(missing_packets) == set(range(24452, 24456)) | set(range(24472, 24489))
    retransmitted_offsets = (85786, 85829)
    retransmit_receipts = [
        line for line in client_lines
        if "got frame STREAM" in line and " len=43 " in line
        and any(f"offset={offset} " in line for offset in retransmitted_offsets)
    ]
    assert len(retransmit_receipts) == 2
    return {
        "scope": "single media connection, Data packet space, IP path 1",
        "packet_bounds_exclusive": [first_packet, last_packet],
        "missing_packets": missing_packets,
        "missing_packet_count": len(missing_packets),
        "missing_datagram_frames": sum(
            frame.startswith("DATAGRAM")
            for packet in missing_packets.values() for frame in packet["frames"]
        ),
        "client_ack": incident_ack,
        "reliable_retransmit_receipts": retransmit_receipts,
        "receiver_queue_evictions": sum("dropping stale datagram" in line for line in client_lines),
        "sender_loss_events": [line for line in server_lines if "packets lost path_id=1" in line],
        "incident_congestion_blocks": sum(
            "02:45:49." in line and "blocked by congestion control" in line for line in server_lines
        ),
        "limitation": "QUIC construction-to-receive evidence; no socket/NIC capture in this batch",
    }


if __name__ == "__main__":
    evidence_directory = Path(__file__).resolve().parent
    report = analyze(evidence_directory)
    (evidence_directory / "20261011_quic_boundary_packets.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps({
        "missing_packets": report["missing_packet_count"],
        "missing_datagrams": report["missing_datagram_frames"],
        "queue_evictions": report["receiver_queue_evictions"],
        "reliable_retransmit_receipts": len(report["reliable_retransmit_receipts"]),
    }))
