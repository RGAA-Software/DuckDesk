"""Validate capture evidence against known ordering waits, sequence wrap and truncated headers."""

import socket
import struct
import tempfile
import unittest
from pathlib import Path

from analyze_relay_tcp_capture import summarize


def packet_bytes(sequence, payload_size, reverse=False, acknowledgement=0, sack=None):
    options = b"" if sack is None else b"\x01\x01\x05\x0a" + struct.pack("!II", *sack)
    source, destination = ("10.0.0.1", "10.0.0.2") if not reverse else ("10.0.0.2", "10.0.0.1")
    source_port, destination_port = (4605, 12345) if not reverse else (12345, 4605)
    ip_header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 40 + len(options) + payload_size, 0, 0, 64, 6, 0,
                            socket.inet_aton(source), socket.inet_aton(destination))
    tcp_header = struct.pack("!HHIIBBHHH", source_port, destination_port, sequence % 2**32,
                             acknowledgement % 2**32, ((20 + len(options)) // 4) << 4, 16, 65535, 0, 0)
    return ip_header + tcp_header + options + bytes(payload_size)


class CaptureEvidenceTests(unittest.TestCase):
    def test_known_hole_and_sack_wait_across_sequence_wrap_on_each_link_type(self):
        for link_type, link_header in ((1, bytes(12) + b"\x08\x00"), (113, bytes(14) + b"\x08\x00"),
                                       (276, b"\x08\x00" + bytes(18))):
            with self.subTest(link_type=link_type), tempfile.TemporaryDirectory() as workspace:
                capture_path = Path(workspace) / "sample.pcap"
                capture = struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 128, link_type)
                sequence = 2**32 - 50
                observations = [(0, packet_bytes(sequence, 30)), (10000, packet_bytes(sequence + 60, 30)),
                                (20000, packet_bytes(700, 0, True, sequence + 30,
                                                     ((sequence + 60) % 2**32, (sequence + 90) % 2**32))),
                                (210000, packet_bytes(sequence + 30, 30)),
                                (220000, packet_bytes(700, 0, True, sequence + 90))]
                for micros, payload in observations:
                    packet = link_header + payload
                    capture += struct.pack("<IIII", 100, micros, len(packet), len(packet)) + packet
                capture_path.write_bytes(capture)
                flow = summarize(capture_path, 50)["flows"]["10.0.0.1:4605>10.0.0.2:12345"]
                self.assertEqual(flow["payload_bytes"], 90)
                self.assertAlmostEqual(flow["holes"][0]["duration_ms"], 200)
                self.assertAlmostEqual(flow["sack_stalls"][0]["duration_ms"], 200)
                self.assertIsNone(flow["hole"])
                self.assertIsNone(flow["sack_hole"])

    def test_truncated_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as workspace:
            capture_path = Path(workspace) / "incomplete.pcap"
            capture_path.write_bytes(struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 128, 276) + b"\x00")
            with self.assertRaisesRegex(ValueError, "Truncated packet header"):
                summarize(capture_path, 50)


if __name__ == "__main__":
    unittest.main()
