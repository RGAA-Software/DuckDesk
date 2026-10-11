"""Match full encrypted UDP payloads across this bounded pair of captures."""
import gzip
import hashlib
import json
import socket
import statistics
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path('.cache/packet_analysis_deps').resolve()))
import dpkt

capture_directory = Path('.cache/socket-budget-20261011')

def read_capture(filename, remote):
    capture_path = capture_directory / filename
    opener = gzip.open if capture_path.suffix == '.gz' else open
    packets = {'forward': {}, 'reverse': {}}
    flow_counts = Counter()
    truncated_packets = 0
    duplicates = 0
    with opener(capture_path, 'rb') as capture:
        for timestamp, packet_bytes in dpkt.pcapng.Reader(capture):
            network = dpkt.ethernet.Ethernet(packet_bytes).data
            if not isinstance(network, dpkt.ip.IP) or not isinstance(network.data, dpkt.udp.UDP):
                continue
            datagram = network.data
            source = socket.inet_ntoa(network.src)
            destination = socket.inet_ntoa(network.dst)
            if not remote and '39.71.45.66' not in (source, destination):
                continue
            # Actual peer observed in this capture. Exclude NAT candidate and QAD probes.
            if remote and '39.82.198.244' not in (source, destination):
                continue
            if datagram.sport != 4613 and datagram.dport != 4613:
                continue
            flow_counts[(source, datagram.sport, destination, datagram.dport)] += 1
            payload_bytes = bytes(datagram.data)
            if len(payload_bytes) != datagram.ulen - 8:
                truncated_packets += 1
                continue
            fingerprint = hashlib.sha256(payload_bytes).hexdigest()
            direction = 'forward' if datagram.sport == 4613 else 'reverse'
            if fingerprint in packets[direction]:
                duplicates += 1
            else:
                packets[direction][fingerprint] = {'time': timestamp, 'bytes': len(payload_bytes)}
    return packets, {'truncated': truncated_packets, 'duplicates': duplicates,
                     'flows': [{'flow': list(flow), 'packets': count} for flow, count in flow_counts.most_common()]}

captures = {}
metadata = {}
evidence_directory = Path('docs/validation/iroh_transport')
for label, filename, remote in (
    ('client_nic', 'local1-nic.pcapng', False),
    ('client_tcpip', 'local1-tcpip.pcapng', False),
    ('server_nic', 'remote1-10.pcapng.gz', True),
    ('server_tcpip', 'remote1-51.pcapng.gz', True),
):
    captures[label], metadata[label] = read_capture(filename, remote)
    assert metadata[label]['truncated'] == 0
    # Preserve normalized observations in-repo; full encrypted captures remain
    # at their recorded workspace locations with separate SHA-256 identities.
    (evidence_directory / ('20261011_socket_budget_' + label + '.json.gz')).write_bytes(
        gzip.compress(json.dumps(captures[label], separators=(',', ':')).encode(), mtime=0))

media_lines = Path('.cache/socket-budget-client-direct.log').read_text().splitlines()
first_frame_line = next(line for line in media_lines if 'event=iroh.diag_rx_result' in line and 'deliver=true' in line)
first_frame_time = datetime.strptime(first_frame_line[1:24], '%Y-%m-%d %H:%M:%S.%f').timestamp()
# The old harness calls PrintWindow after its 20-second sample. Exclude that
# potentially disruptive observation and subsequent close from performance analysis.
client_window = (first_frame_time + .5, first_frame_time + 19.5)
report = {'capture_metadata': metadata, 'client_window': client_window, 'layers': {}, 'directions': {}}
for direction in ('forward', 'reverse'):
    report['layers'][direction] = {
        side + '_' + boundary: len(captures[side + '_' + first][direction].keys() - captures[side + '_' + second][direction].keys())
        for side in ('client', 'server')
        for boundary, first, second in (('nic_not_tcpip', 'nic', 'tcpip'), ('tcpip_not_nic', 'tcpip', 'nic'))
    }
    source_side, destination_side = ('server_nic', 'client_nic') if direction == 'forward' else ('client_nic', 'server_nic')
    sent = captures[source_side][direction]
    received = captures[destination_side][direction]
    matches = [{'fingerprint': fingerprint, 'sent': packet['time'], 'received': received[fingerprint]['time'], 'bytes': packet['bytes']}
               for fingerprint, packet in sent.items() if fingerprint in received]
    # Restrict missing counts to the overlapping active capture, excluding close/start boundaries.
    baseline = min(packet['received'] - packet['sent'] for packet in matches)
    first_send, last_send = client_window if direction == 'reverse' else (client_window[0] - baseline, client_window[1] - baseline)
    active_sent = {fingerprint: packet for fingerprint, packet in sent.items() if first_send <= packet['time'] <= last_send}
    missing = [{'fingerprint': fingerprint, **packet} for fingerprint, packet in active_sent.items() if fingerprint not in received]
    active_matches = [packet for packet in matches if first_send <= packet['sent'] <= last_send]
    growth = sorted((packet['received'] - packet['sent'] - baseline) * 1000 for packet in active_matches)
    internal_layers = {}
    for side in ('client', 'server'):
        nic_packets = captures[side + '_nic'][direction]
        tcpip_packets = captures[side + '_tcpip'][direction]
        capture_delays = [(tcpip_packets[fingerprint]['time'] - packet['time']) * 1000
                          for fingerprint, packet in nic_packets.items() if fingerprint in tcpip_packets]
        internal_layers[side] = {'max_abs_ms': max(abs(delay) for delay in capture_delays),
                                 'median_abs_ms': statistics.median(abs(delay) for delay in capture_delays)}
    report['directions'][direction] = {
        'active_send_range': [first_send, last_send], 'active_sent': len(active_sent), 'matched': len(active_matches),
        'missing': len(missing), 'unmatched_received_all_capture': len(received.keys() - sent.keys()),
        'clock_offset_plus_minimum_transit_seconds': baseline,
        'host_boundary_delays': internal_layers,
        'relative_delay_ms': {'median': statistics.median(growth), 'p99': growth[int(len(growth) * .99)], 'max': max(growth)},
    }
    (capture_directory / (direction + '-missing.json')).write_text(json.dumps(missing, indent=2))
    (capture_directory / (direction + '-matched.json')).write_text(json.dumps(active_matches))
    (evidence_directory / ('20261011_socket_budget_' + direction + '_missing.json')).write_text(json.dumps(missing, indent=2))

Path('docs/validation/iroh_transport/20261011_socket_budget_packets.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
