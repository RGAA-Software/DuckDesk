import json
from pathlib import Path
import sys

packets = json.loads(Path(sys.argv[1]).read_text())
packets = [packet for packet in packets if packet['direction'] == 'receive' and packet['payload_bytes'] > 0]
expected_sequence = None
pending_ranges = []
hole_started = None
holes = []
for packet in packets:
    start = packet['seq']
    end = start + packet['payload_bytes']
    if expected_sequence is None:
        expected_sequence = start
    if end <= expected_sequence:
        continue
    if start > expected_sequence and hole_started is None:
        hole_started = {'time': packet['time'], 'missing_sequence': expected_sequence, 'first_later_sequence': start}
    pending_ranges.append((start, end))
    pending_ranges.sort()
    while pending_ranges and pending_ranges[0][0] <= expected_sequence:
        _, range_end = pending_ranges.pop(0)
        expected_sequence = max(expected_sequence, range_end)
    if hole_started and not pending_ranges:
        hole_started.update({'filled_at': packet['time'], 'delay_ms': round((packet['time'] - hole_started['time']) * 1000, 3)})
        holes.append(hole_started)
        hole_started = None
print(json.dumps({'holes': holes, 'unresolved': hole_started}, indent=2))
