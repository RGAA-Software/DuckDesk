from datetime import datetime
import json
from pathlib import Path

directory = Path('.cache/iroh-paired-tcp-20261010-c')
client_packets = json.loads((directory / '39.71.45.66_4605-local_10248.json').read_text())
relay_packets = json.loads((directory / '10.0.0.90_4605-local_10248.json').read_text())
holes = json.loads(Path('docs/validation/iroh_transport/20261010_paired_tcp_c_holes.json').read_text())['holes']
events = json.loads(Path('docs/validation/iroh_transport/20261010_paired_tcp_c_analysis.json').read_text())['stutters']
results = []
for event in events:
    preceding = [hole for hole in holes if hole['time'] <= event['time'] <= hole['filled_at'] + .1]
    for hole in preceding:
        missing = hole['missing_sequence']
        client_arrivals = [packet for packet in client_packets if packet['direction'] == 'receive'
                           and packet['seq'] <= missing < packet['seq'] + packet['payload_bytes']]
        relay_transmissions = [packet for packet in relay_packets if packet['direction'] == 'receive'
                               and packet['seq'] <= missing < packet['seq'] + packet['payload_bytes']]
        advances = [packet for packet in client_packets if packet['direction'] == 'send'
                    and packet['time'] >= hole['time'] and packet['ack'] > missing]
        duplicate_acks = [packet for packet in client_packets if packet['direction'] == 'send'
                          and hole['time'] <= packet['time'] <= hole['filled_at'] and packet['ack'] == missing]
        results.append({'frame_time': event['local_time'], 'receive_gap_us': event['receive_gap_us'],
                        'hole': hole, 'ack_stalled_ms': round((advances[0]['time'] - hole['time']) * 1000, 3) if advances else None,
                        'duplicate_acks': len(duplicate_acks), 'client_arrivals': client_arrivals,
                        'relay_transmissions': relay_transmissions})
report_path = Path('docs/validation/iroh_transport/20261010_paired_tcp_c_correlation.json')
report_path.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
for result in results:
    print(result['frame_time'], result['receive_gap_us'], 'ACK stall', result['ack_stalled_ms'],
          'duplicate ACKs', result['duplicate_acks'], 'relay transmissions', len(result['relay_transmissions']))
    if result['frame_time'] == '2026-10-10 21:29:47.072':
        print(json.dumps(result, indent=2))
