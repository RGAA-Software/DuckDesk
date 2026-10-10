"""Summarize captured TCP headers without decrypting application payloads."""
from collections import defaultdict
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import socket
import sys

sys.path.insert(0, str(Path('.cache/packet_analysis_deps').resolve()))
import dpkt

arguments = argparse.ArgumentParser()
arguments.add_argument('--capture', default='.cache/iroh-priority-tcp-20261010/relay.pcapng')
arguments.add_argument('--prefix', default='20261010_dual_priority_tcp')
arguments.add_argument('--log-prefix')
arguments.add_argument('--gap-ms', type=float, default=100)
options = arguments.parse_args()
capture_path = Path(options.capture)
flows = defaultdict(list)
packet_count = 0
with capture_path.open('rb') as capture_file:
    for timestamp, packet_bytes in dpkt.pcapng.Reader(capture_file):
        packet_count += 1
        try:
            ethernet = dpkt.ethernet.Ethernet(packet_bytes)
            network = ethernet.data
            if not isinstance(network, dpkt.ip.IP) or not isinstance(network.data, dpkt.tcp.TCP):
                continue
            segment = network.data
            if 4605 not in (segment.sport, segment.dport):
                continue
            source = socket.inet_ntoa(network.src)
            destination = socket.inet_ntoa(network.dst)
            from_relay = segment.sport == 4605
            remote = source if from_relay else destination
            local_port = segment.dport if from_relay else segment.sport
            key = f'{remote}:4605-local:{local_port}'
            payload_size = network.len - network.hl * 4 - segment.off * 4
            flows[key].append({'time': float(timestamp), 'direction': 'receive' if from_relay else 'send',
                               'seq': segment.seq, 'ack': segment.ack, 'window': segment.win,
                               'flags': segment.flags, 'payload_bytes': payload_size})
        except (ValueError, dpkt.UnpackError):
            continue

client_log = Path('docs/validation/iroh_transport/' + (options.log_prefix or options.prefix) + '.log').read_text(encoding='utf-8')
stutters = []
for line in client_log.splitlines():
    if 'event=iroh.frame_transit' not in line:
        continue
    matched = re.search(r'^\[([^]]+)\].*receive_gap_us=(\d+)', line)
    if matched and int(matched.group(2)) >= options.gap_ms * 1000:
        timestamp = datetime.strptime(matched.group(1), '%Y-%m-%d %H:%M:%S.%f').timestamp()
        stutters.append({'time': timestamp, 'local_time': matched.group(1), 'receive_gap_us': int(matched.group(2)), 'client_log': line})

summary = {'capture': str(capture_path), 'captured_packets': packet_count,
           'limitations': 'NIC capture uses offload and 96-byte truncation. Overlap can be reordering, duplicate capture or retransmission; no TLS decryption. TCP gaps alone do not distinguish host scheduling from upstream network.',
           'flows': [], 'stutters': stutters}
for flow_name, packets in flows.items():
    packets.sort(key=lambda packet: packet['time'])
    flow_summary = {'flow': flow_name, 'packets': len(packets), 'directions': {}, 'stutter_windows': []}
    for direction in ('receive', 'send'):
        selected = [packet for packet in packets if packet['direction'] == direction]
        payload_packets = [packet for packet in selected if packet['payload_bytes'] > 0]
        maximum_end = None
        overlaps = []
        gaps = []
        previous_payload = None
        for packet in payload_packets:
            if maximum_end is not None and packet['seq'] < maximum_end:
                overlaps.append(packet)
            maximum_end = max(maximum_end or packet['seq'], packet['seq'] + packet['payload_bytes'])
            if previous_payload is not None and packet['time'] - previous_payload['time'] > .08:
                gaps.append({'start': previous_payload['time'], 'end': packet['time'],
                             'gap_ms': round((packet['time'] - previous_payload['time']) * 1000, 3),
                             'seq_jump': packet['seq'] - previous_payload['seq'] - previous_payload['payload_bytes']})
            previous_payload = packet
        flow_summary['directions'][direction] = {'payload_bytes': sum(packet['payload_bytes'] for packet in payload_packets),
            'overlap_count': len(overlaps), 'overlap_samples': overlaps[:20],
            'zero_window_count': sum(packet['window'] == 0 and not packet['flags'] & 4 for packet in selected),
            'gaps_over_80ms': gaps[:50]}
    for stutter in stutters:
        start = stutter['time'] - stutter['receive_gap_us'] / 1e6 - .05
        end = stutter['time'] + .05
        surrounding = [packet for packet in packets if start <= packet['time'] <= end]
        bins = defaultdict(lambda: {'receive_bytes': 0, 'send_bytes': 0, 'receive_packets': 0, 'send_packets': 0})
        for packet in surrounding:
            bucket = int((packet['time'] - start) / .01) * 10
            bins[bucket][packet['direction'] + '_bytes'] += packet['payload_bytes']
            bins[bucket][packet['direction'] + '_packets'] += 1
        flow_summary['stutter_windows'].append({'local_time': stutter['local_time'], 'start': start,
                                              'bins_10ms': dict(sorted(bins.items()))})
    summary['flows'].append(flow_summary)
    (capture_path.parent / (flow_name.replace(':', '_') + '.json')).write_text(json.dumps(packets), encoding='utf-8')
Path('docs/validation/iroh_transport/' + options.prefix + '_analysis.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'packets': packet_count, 'flows': [{ 'flow': flow['flow'], 'packets': flow['packets'],
    'receive_overlap_count': flow['directions']['receive']['overlap_count'],
    'send_overlap_count': flow['directions']['send']['overlap_count'],
    'receive_zero_windows': flow['directions']['receive']['zero_window_count'],
    'send_zero_windows': flow['directions']['send']['zero_window_count']} for flow in summary['flows']],
    'stutters': stutters}, indent=2))
