"""Correlate TCP receiver SACK acknowledgements with video gaps; no payload decoding."""
import argparse
from collections import defaultdict
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import re
import socket
import struct
import sys

sys.path.insert(0, str(Path('.cache/packet_analysis_deps').resolve()))
import dpkt

parser = argparse.ArgumentParser()
parser.add_argument('--capture', required=True)
parser.add_argument('--client-log', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--sample-metadata', required=True)
options = parser.parse_args()
sample = json.loads(Path(options.sample_metadata).read_text(encoding='utf-8'))
flows = defaultdict(lambda: {'packets': 0, 'receive_bytes': 0, 'sack_events': [], 'pending_sack': None})
capture_started = None
capture_ended = None
with Path(options.capture).open('rb') as capture_file:
    for timestamp, packet_bytes in dpkt.pcapng.Reader(capture_file):
        capture_started = float(timestamp) if capture_started is None else capture_started
        capture_ended = float(timestamp)
        try:
            network = dpkt.ethernet.Ethernet(packet_bytes).data
            if not isinstance(network, dpkt.ip.IP) or not isinstance(network.data, dpkt.tcp.TCP):
                continue
            segment = network.data
            if 4605 not in (segment.sport, segment.dport):
                continue
            receiving = segment.sport == 4605
            relay_address = socket.inet_ntoa(network.src if receiving else network.dst)
            local_port = segment.dport if receiving else segment.sport
            flow = flows[f'{relay_address}:4605-local:{local_port}']
            flow['packets'] += 1
            payload_size = max(0, network.len - network.hl * 4 - segment.off * 4)
            if receiving:
                flow['receive_bytes'] += payload_size
                continue
            if not segment.flags & dpkt.tcp.TH_ACK:
                continue
            pending = flow['pending_sack']
            if pending and (segment.ack - pending['first_sack_start']) % (1 << 32) < (1 << 31):
                pending.update(resumed=float(timestamp), duration_ms=(float(timestamp) - pending['started']) * 1000)
                flow['sack_events'].append(pending)
                flow['pending_sack'] = None
            for kind, option_bytes in dpkt.tcp.parse_opts(segment.opts):
                if kind != dpkt.tcp.TCP_OPT_SACK or len(option_bytes) < 8:
                    continue
                sack_start, sack_end = struct.unpack('!II', option_bytes[:8])
                missing_bytes = (sack_start - segment.ack) % (1 << 32)
                if 0 < missing_bytes < (1 << 31) and not flow['pending_sack']:
                    flow['pending_sack'] = {'started': float(timestamp), 'ack': segment.ack,
                                            'first_sack_start': sack_start, 'first_sack_end': sack_end,
                                            'missing_bytes': missing_bytes}
        except (ValueError, dpkt.UnpackError):
            continue

video_gaps = []
for line in Path(options.client_log).read_text(encoding='utf-8', errors='replace').splitlines():
    if 'event=iroh.received_frame phase=gap' not in line:
        continue
    matched = re.search(r'^\[([^]]+)\].*frame=(\d+) gap_ms=([\d.]+)', line)
    if not matched:
        continue
    ended = datetime.strptime(matched[1], '%Y-%m-%d %H:%M:%S.%f').replace(tzinfo=timezone(timedelta(hours=8))).timestamp()
    if max(capture_started, sample['sample_started_epoch']) <= ended <= min(capture_ended, sample['sample_ended_epoch']):
        video_gaps.append({'ended': ended, 'local_time': matched[1], 'frame': int(matched[2]), 'gap_ms': float(matched[3])})

for flow in flows.values():
    flow['correlations'] = []
    for gap in video_gaps:
        started = gap['ended'] - gap['gap_ms'] / 1000
        events = [event for event in flow['sack_events'] if event['started'] <= gap['ended'] and event['resumed'] >= started]
        if events:
            flow['correlations'].append({'gap': gap, 'sack_events': events})

receive_data_observed = any(flow['receive_bytes'] > 0 for flow in flows.values())
report = {'capture_started': capture_started, 'capture_ended': capture_ended,
          'receive_data_observed': receive_data_observed,
          'coverage': 'bidirectional_data_observed' if receive_data_observed else 'no_receive_data_observed; downlink loss conclusions are unavailable',
          'sample_started': sample['sample_started_epoch'], 'sample_ended': sample['sample_ended_epoch'],
          'limitations': '96-byte NIC headers. Receiver SACK proves a TCP sequence hole; it does not identify the physical device that lost or delayed bytes.',
          'video_gaps': video_gaps, 'flows': dict(flows)}
Path(options.output).write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'video_gaps': len(video_gaps), 'receive_data_observed': receive_data_observed, 'flows': {name: {'receive_bytes': flow['receive_bytes'],
                   'sack_events': len(flow['sack_events']),
                   'sack_max_ms': max((event['duration_ms'] for event in flow['sack_events']), default=0),
                   'correlated_gaps': len(flow['correlations'])} for name, flow in flows.items()}}, indent=2))
