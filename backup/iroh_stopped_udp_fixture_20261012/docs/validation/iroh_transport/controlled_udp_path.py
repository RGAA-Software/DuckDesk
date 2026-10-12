"""Low-rate owned path switch; no host firewall or NIC configuration changes."""
import copy
import json
from pathlib import Path
import select
import socket
import subprocess
import sys
import threading
import time


class OwnedUdpPath:
    def __init__(self, peer_address):
        self.peer_address = peer_address
        self.ingress_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.ingress_socket.bind(('127.0.0.1', 0))
        self.peer_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.peer_socket.bind(('0.0.0.0', 0))
        self.client_address = None
        self.blocked = threading.Event()
        self.stopped = threading.Event()
        self.forwarded_uplink = 0
        self.forwarded_downlink = 0
        self.dropped_uplink = 0
        self.dropped_downlink = 0
        self.reset_notifications = 0
        self.failure = None
        self.worker = threading.Thread(target=self.run, name='owned-nat-path-forwarder')
        self.worker.start()

    def run(self):
        try:
            while not self.stopped.is_set():
                readable_sockets, _, _ = select.select([self.ingress_socket, self.peer_socket], [], [], .05)
                for readable_socket in readable_sockets:
                    try:
                        payload_bytes, sender_address = readable_socket.recvfrom(65535)
                    except ConnectionResetError:
                        self.reset_notifications += 1
                        continue
                    if readable_socket is self.ingress_socket:
                        if self.client_address is None:
                            self.client_address = sender_address
                        if sender_address != self.client_address:
                            raise RuntimeError('Unexpected second local client')
                        if self.blocked.is_set():
                            self.dropped_uplink += 1
                        else:
                            self.peer_socket.sendto(payload_bytes, self.peer_address)
                            self.forwarded_uplink += 1
                    elif sender_address == self.peer_address and self.client_address is not None:
                        if self.blocked.is_set():
                            self.dropped_downlink += 1
                        else:
                            self.ingress_socket.sendto(payload_bytes, self.client_address)
                            self.forwarded_downlink += 1
        except Exception as failure:
            self.failure = str(failure)

    def close(self):
        self.stopped.set()
        self.worker.join(timeout=2)
        if self.worker.is_alive():
            raise RuntimeError('Owned UDP path worker did not stop')
        self.ingress_socket.close()
        self.peer_socket.close()


def main():
    address_path = Path(sys.argv[1]).resolve()
    endpoint_configuration_path = Path(sys.argv[2]).resolve()
    evidence_prefix = Path(sys.argv[3]).resolve()
    server_address = json.loads(address_path.read_text(encoding='utf-8'))
    public_address = next(address['Ip'] for address in server_address['addrs'] if 'Ip' in address and not address['Ip'].startswith('10.'))
    peer_host, peer_port = public_address.rsplit(':', 1)
    forwarder = OwnedUdpPath((peer_host, int(peer_port)))
    ingress_host, ingress_port = forwarder.ingress_socket.getsockname()
    ingress_address = f'{ingress_host}:{ingress_port}'
    routed_address = copy.deepcopy(server_address)
    routed_address['addrs'] = [address for address in routed_address['addrs'] if 'Relay' in address] + [{'Ip': ingress_address}]
    routed_address_path = evidence_prefix.with_suffix('.routed-address.json')
    routed_address_path.write_text(json.dumps(routed_address, indent=2)+'\n', encoding='utf-8')
    configuration = json.loads(endpoint_configuration_path.read_text(encoding='utf-8'))
    configuration['bind_address'] = '127.0.0.1:0'
    configuration_path = Path('.cache/nat-switch-loopback-20261012.json').resolve()
    configuration_path.write_text(json.dumps(configuration, indent=2)+'\n', encoding='utf-8')
    process = None
    blocked_at = None
    unblocked = False
    events = []
    started = time.monotonic()
    try:
        with evidence_prefix.with_suffix('.client.log').open('w', encoding='utf-8') as stdout_file, evidence_prefix.with_suffix('.client_error.log').open('w', encoding='utf-8') as stderr_file:
            process = subprocess.Popen([str(Path('rust_transport/target/release/px_nat_probe.exe').resolve()), 'connect', str(configuration_path), str(routed_address_path), '60'], stdout=stdout_file, stderr=stderr_file)
            while process.poll() is None:
                records = []
                for line in evidence_prefix.with_suffix('.client.log').read_text(encoding='utf-8').splitlines():
                    try:
                        records.append(json.loads(line))
                    except json.JSONDecodeError:
                        continue
                ip_paths = [record for record in records if record.get('selected_is_ip') is True]
                assert all(record['selected'] == {'Ip': ingress_address} for record in ip_paths), 'Owned forwarding bypassed'
                elapsed = time.monotonic()-started
                if blocked_at is None and elapsed >= 8 and ip_paths:
                    forwarder.blocked.set()
                    blocked_at = time.monotonic()
                    events.append({'event':'peer_udp_blocked','elapsed_seconds':blocked_at-started})
                    print(json.dumps(events[-1]), flush=True)
                if blocked_at is not None and not unblocked and time.monotonic()-blocked_at >= 14:
                    forwarder.blocked.clear()
                    unblocked = True
                    events.append({'event':'peer_udp_restored','elapsed_seconds':time.monotonic()-started})
                    print(json.dumps(events[-1]), flush=True)
                if forwarder.failure:
                    raise RuntimeError(forwarder.failure)
                if elapsed > 100:
                    raise RuntimeError('Owned probe deadline exceeded')
                time.sleep(.1)
            assert process.returncode == 0, f'Probe exited {process.returncode}'
            assert blocked_at is not None and unblocked, 'UDP block and restore segment incomplete'
            assert forwarder.dropped_uplink + forwarder.dropped_downlink > 0, 'No packets actually dropped'
    finally:
        forwarder.blocked.clear()
        forwarder.close()
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        report = {'events':events,'ingress':ingress_address,'peer':public_address,'forwarded_uplink':forwarder.forwarded_uplink,'forwarded_downlink':forwarder.forwarded_downlink,'dropped_uplink':forwarder.dropped_uplink,'dropped_downlink':forwarder.dropped_downlink,'reset_notifications':forwarder.reset_notifications,'worker_failure':forwarder.failure,'worker_stopped':not forwarder.worker.is_alive(),'exit_code':process.returncode if process else None,'scope':'Controlled path migration, not new two-ended NAT evidence. Native UDP binds loopback to prevent bypass; private Relay TCP remains real.'}
        evidence_prefix.with_suffix('.proxy.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report),flush=True)


if __name__ == '__main__':
    main()
