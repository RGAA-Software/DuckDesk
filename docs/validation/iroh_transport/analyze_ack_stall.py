"""Rebuild the startup stall from saved traces and encrypted packet fingerprints.

This capture has one media connection on QUIC path 0. Protocol packet numbers
come from endpoint traces, not decryption of the NIC capture.
"""

import gzip
import json
import re
from datetime import datetime
from pathlib import Path


def trace_time(line):
    return datetime.fromisoformat(line[:27].replace('Z', '+00:00')).timestamp()


def main():
    evidence_root = Path(__file__).resolve().parent
    source_prefix = '20261011_socket_budget_'

    def read_log(side, category):
        return gzip.decompress((evidence_root / (source_prefix + side + '_' + category + '.log.gz')).read_bytes()).decode('utf-8').splitlines()

    server_lines = read_log('server', 'trace')
    client_lines = read_log('client', 'trace')
    server_packets = json.loads(gzip.decompress((evidence_root / (source_prefix + 'server_nic.json.gz')).read_bytes()))
    client_packets = json.loads(gzip.decompress((evidence_root / (source_prefix + 'client_nic.json.gz')).read_bytes()))
    reverse_matches = [
        {'fingerprint': fingerprint, 'sent': packet['time'], 'received': server_packets['reverse'][fingerprint]['time'], 'bytes': packet['bytes']}
        for fingerprint, packet in client_packets['reverse'].items() if fingerprint in server_packets['reverse']
    ]
    clock_baseline = min(packet['received'] - packet['sent'] for packet in reverse_matches)
    ack_pairs = []
    for packet_number in (88, 89, 90, 92, 93, 95, 98, 100):
        send_marker = f'send{{space=Data pn={packet_number} path_id=0}}'
        receive_marker = f'pkt{{path_id=0}}:recv{{space=Data pn={packet_number}}}'
        sent = next(line for line in client_lines if send_marker in line and 'frame=PATH_ACK_ECN' in line)
        received = next(line for line in server_lines if receive_marker in line and 'got frame PATH_ACK_ECN' in line)
        ack_pairs.append({'packet_number': packet_number, 'sent': sent, 'received': received,
                          'relative_trace_delay_ms': (trace_time(received) - trace_time(sent) - clock_baseline) * 1000})
    first_ack = next(pair for pair in ack_pairs if pair['packet_number'] == 89)
    ack_received = trace_time(first_ack['received'])
    prior_ack_received = trace_time(ack_pairs[0]['received'])
    block_lines = [line for line in server_lines if 'blocked by congestion control' in line
                   and 'path_id=0' in line and '03:23:45.' in line and prior_ack_received < trace_time(line) < ack_received]
    first_block = block_lines[0]
    resumed_packet = next(line for line in server_lines if 'send{space=Data pn=240 path_id=0}' in line and 'frame=DATAGRAM' in line)
    resumed_send = next(line for line in server_lines if '03:23:45.432' in line and 'sending 2475 bytes' in line)
    nic_batch = [dict(packet, relative_delay_ms=(packet['received'] - packet['sent'] - clock_baseline) * 1000)
                 for packet in reverse_matches if abs(packet['received'] - ack_received) < .001
                 and packet['received'] - packet['sent'] - clock_baseline > .1]
    video_lines = read_log('client', 'media')
    previous_frame = next(line for line in video_lines if 'event=iroh.diag_rx_result ' in line and ' wire_frame=11 ' in line and 'deliver=true' in line)
    resumed_frame = next(line for line in video_lines if 'event=iroh.diag_rx_result ' in line and ' wire_frame=12 ' in line and 'deliver=true' in line)
    field_value = lambda line, field: int(re.search(r'\b' + field + r'=(\d+)', line)[1])
    report = {
        'scope': 'Offline analysis only; existing single media connection, Data space/path 0',
        'clock_offset_plus_minimum_reverse_transit_seconds': clock_baseline,
        'ack_pairs': ack_pairs,
        'delayed_reverse_nic_batch': nic_batch,
        'first_congestion_block': first_block,
        'block_observations_before_ack': len(block_lines),
        'block_to_ack_ms': (ack_received - trace_time(first_block)) * 1000,
        'resumed_packet': resumed_packet,
        'resumed_send': resumed_send,
        'ack_to_packet_construction_us': (trace_time(resumed_packet) - ack_received) * 1e6,
        'ack_to_send_trace_us': (trace_time(resumed_send) - ack_received) * 1e6,
        'probe_timeouts': [line for line in server_lines if '03:23:45.' in line and 'PTO fired' in line and trace_time(line) < ack_received],
        'previous_video_frame': previous_frame,
        'resumed_video_frame': resumed_frame,
        'delivery_gap_ms': (field_value(resumed_frame, 'mono_us') - field_value(previous_frame, 'mono_us')) / 1000,
        'resumed_assembly_feed_us': field_value(resumed_frame, 'feed_us'),
        'limitation': 'Endpoint PN/ACK traces and NIC hash matches corroborate timing; NIC packets were not decrypted or assigned protocol packet numbers.',
    }
    assert 140 < report['block_to_ack_ms'] < 155
    assert 0 < report['ack_to_send_trace_us'] < 1000
    assert len(nic_batch) >= 8
    assert report['resumed_assembly_feed_us'] < 1000
    (evidence_root / '20261011_ack_stall.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({key: report[key] for key in ('block_observations_before_ack', 'block_to_ack_ms', 'ack_to_send_trace_us', 'delivery_gap_ms', 'resumed_assembly_feed_us')}, indent=2))


if __name__ == '__main__':
    main()
