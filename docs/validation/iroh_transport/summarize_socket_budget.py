import gzip
import json
import re
from datetime import datetime
from pathlib import Path

evidence_root = Path('docs/validation/iroh_transport')
prefix = '20261011_socket_budget_'
packet_report = json.loads((evidence_root / (prefix + 'packets.json')).read_text())
window_start, window_end = packet_report['client_window']
media_lines = Path('.cache/socket-budget-client-direct.log').read_text(encoding='utf-8').splitlines()
delivered_frames = []
for line in media_lines:
    if 'event=iroh.diag_rx_result' not in line or 'deliver=true' not in line:
        continue
    wall_time = datetime.strptime(line[1:24], '%Y-%m-%d %H:%M:%S.%f').timestamp()
    if window_start <= wall_time <= window_end:
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        delivered_frames.append({'time': wall_time, 'mono_us': int(fields['mono_us']), 'wire_frame': int(fields['wire_frame'])})
gaps = [{'gap_ms': (current['mono_us'] - previous['mono_us']) / 1000,
         'previous': previous['wire_frame'], 'current': current['wire_frame'], 'time': current['time']}
        for previous, current in zip(delivered_frames, delivered_frames[1:])]
server_packets = json.loads(gzip.decompress((evidence_root / (prefix + 'server_nic.json.gz')).read_bytes()))['forward']
send_start, send_end = packet_report['directions']['forward']['active_send_range']
active_packets = sorted((packet for packet in server_packets.values() if send_start <= packet['time'] <= send_end), key=lambda packet: packet['time'])
bursts = {}
for window_ms in [1, 5, 10, 50, 100]:
    first_index = 0
    window_bytes = 0
    maximum_bytes = 0
    for packet_index, packet in enumerate(active_packets):
        window_bytes += packet['bytes']
        while packet['time'] - active_packets[first_index]['time'] > window_ms / 1000:
            window_bytes -= active_packets[first_index]['bytes']
            first_index += 1
        maximum_bytes = max(maximum_bytes, window_bytes)
    bursts[str(window_ms)] = {'bytes': maximum_bytes, 'mbps': maximum_bytes * 8 / window_ms / 1000}
report = {
    'window_seconds': window_end - window_start,
    'delivered_frames': len(delivered_frames),
    'fps': len(delivered_frames) / (window_end - window_start),
    'largest_gaps': sorted(gaps, key=lambda gap: gap['gap_ms'], reverse=True)[:10],
    'udp_payload_mbps': sum(packet['bytes'] for packet in active_packets) * 8 / (send_end - send_start) / 1e6,
    'udp_payload_plus_66_byte_wire_estimate_mbps': sum(packet['bytes'] + 66 for packet in active_packets) * 8 / (send_end - send_start) / 1e6,
    'maximum_sliding_bursts_ms': bursts,
    'receive_windows': [line for line in media_lines if 'iroh media receive window:' in line],
}
(evidence_root / (prefix + 'performance.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
