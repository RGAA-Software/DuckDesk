"""Unit checks for capture matching; no client, network or packet monitor is started."""

import pathlib
import struct
import tempfile
import unittest

import analyze_udp_media_capture as analyzer


class CaptureComparisonTests(unittest.TestCase):
    def fixture(self):
        sent = {(0, frame, 0, shard): frame + shard / 1000
                for frame in range(1, 5) for shard in range(6)}
        sizes = {key: 1442 for key in sent}
        blocks = {key[:3]: (4, 2) for key in sent}
        return sent, sizes, blocks

    def test_single_lost_parity_does_not_lose_frame(self):
        sent, sizes, blocks = self.fixture()
        received = {key: value + 3 for key, value in sent.items() if key != (0, 2, 0, 5)}
        result = analyzer.summary(sent, received, sizes, blocks)
        self.assertTrue(result["capture_complete_for_comparison"])
        self.assertEqual(result["missing_packets"], 1)
        self.assertEqual(result["unrecoverable_blocks"], [])

    def test_burst_beyond_parity_is_unrecoverable(self):
        sent, sizes, blocks = self.fixture()
        received = {key: value + 3 for key, value in sent.items() if not (key[1] == 2 and key[3] < 3)}
        result = analyzer.summary(sent, received, sizes, blocks)
        self.assertEqual(len(result["unrecoverable_blocks"]), 1)
        self.assertEqual(result["unrecoverable_blocks"][0]["received"], 3)

    def test_truncated_sender_trace_is_not_valid_loss_evidence(self):
        sent, sizes, blocks = self.fixture()
        received = sent.copy()
        del sent[(0, 2, 0, 1)]
        result = analyzer.summary(sent, received, sizes, blocks)
        self.assertFalse(result["capture_complete_for_comparison"])
        self.assertEqual(result["unmatched_received"], 1)

    def test_short_and_disjoint_captures_fail_explicitly(self):
        sent, sizes, blocks = self.fixture()
        with self.assertRaisesRegex(ValueError, "No shared interior"):
            analyzer.summary({key: time for key, time in sent.items() if key[1] == 1}, sent, sizes, blocks)
        with self.assertRaisesRegex(ValueError, "No shared packet identities"):
            analyzer.summary(sent, {(key[0], key[1], 1, key[3]): time for key, time in sent.items()}, sizes, blocks)

    def test_streams_have_independent_bounds_and_missing_frames(self):
        sent, sizes, blocks = self.fixture()
        extra = {(9, key[1] + 100, key[2], key[3]): time for key, time in sent.items()}
        sent.update(extra)
        sizes.update({key: 1442 for key in extra})
        blocks.update({key[:3]: (4, 2) for key in extra})
        received = {key: time for key, time in sent.items() if key[:2] != (9, 102)}
        result = analyzer.summary(sent, received, sizes, blocks)
        self.assertEqual(result["missing_whole_frames"], [(9, 102)])
        self.assertEqual(result["frame_intervals_by_stream"], {0: (2, 3), 9: (102, 103)})

    def test_wrap_is_rejected_instead_of_matching_different_generations(self):
        sent, sizes, blocks = self.fixture()
        sent[(0, 0xffffffff, 0, 0)] = 0
        with self.assertRaisesRegex(ValueError, "Frame wrap"):
            analyzer.summary(sent, sent, sizes, blocks)

    def test_timing_join_uses_identity_and_host_local_intervals(self):
        sent = {(0, 1, 0, 0): 1.0, (0, 2, 0, 0): 1.03}
        received = {(0, 1, 0, 0): 500.0, (0, 2, 0, 0): 500.09}
        with tempfile.TemporaryDirectory(prefix="pixels-timing-test-") as directory:
            path = pathlib.Path(directory) / "client.log"
            path.write_text("UDP timing receive_gap: steady_us=1, gap_us=90000, stream=0, previous=1/0/0, current=2/0/0\n"
                            "UDP timing receive_gap: stream=broken, current=bad\n", encoding="utf-8")
            events = analyzer.timing_events(path, sent, received)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["sender_nic_pair_gap_us"], 30000)
        self.assertEqual(events[0]["receiver_nic_pair_gap_us"], 90000)

    def test_malformed_sections_raise_value_error(self):
        with tempfile.TemporaryDirectory(prefix="pixels-capture-test-") as directory:
            path = pathlib.Path(directory) / "bad.pcapng"
            for data in (b"bad", b"\x0a\x0d\x0d\x0a" + struct.pack("<I", 0) + b"oops",
                         b"\x0a\x0d\x0d\x0a" + struct.pack("<II", 28, 0x1a2b3c4d)):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    list(analyzer.packets(path))

    def test_pcapng_identity_and_duplicate_observation(self):
        def block(kind, payload):
            size = len(payload) + 12
            return struct.pack("<II", kind, size) + payload + struct.pack("<I", size)

        ethernet = bytearray(14 + 20 + 8)
        ethernet[12:14] = b"\x08\x00"
        ethernet[14] = 0x45
        ethernet[23] = 17
        media = bytearray(40)
        media[:8] = b"PXM\x02\x01\x07\x00\x00"
        struct.pack_into("<I", media, 28, 123)
        media[35] = 0x50  # Second of two FEC blocks.
        struct.pack_into("<I", media, 36, (3 << 12) | (4 << 22) | (50 << 4))
        packet = ethernet + media
        packet += bytes((-len(packet)) % 4)
        header = block(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1))
        interface = block(1, struct.pack("<HHI", 1, 0, 96))
        first = block(6, struct.pack("<IIIII", 0, 0, 1_000_000, 82, 1442) + packet)
        duplicate = block(6, struct.pack("<IIIII", 0, 0, 1_000_010, 82, 1442) + packet)
        with tempfile.TemporaryDirectory(prefix="pixels-capture-test-") as directory:
            path = pathlib.Path(directory) / "sample.pcapng"
            path.write_bytes(header + interface + first + duplicate)
            received, sizes, blocks = analyzer.video(path)
        self.assertEqual(received, {(7, 123, 1, 3): 1.0})
        self.assertEqual(sizes[(7, 123, 1, 3)], 1442)
        self.assertEqual(blocks[(7, 123, 1)], (4, 2))


if __name__ == "__main__":
    unittest.main()
