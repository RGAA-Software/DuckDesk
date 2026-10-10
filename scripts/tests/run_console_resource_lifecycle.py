"""Exercise the real Console C++ HTTP adapter against a local HTTPS fault fixture."""

import argparse
import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import uuid

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


def server_context(directory):
    private_key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    current_time = datetime.datetime.now(datetime.timezone.utc)
    certificate = (
        x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
        .public_key(private_key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(current_time - datetime.timedelta(minutes=1))
        .not_valid_after(current_time + datetime.timedelta(days=1)).sign(private_key, hashes.SHA256())
    )
    certificate_path = directory / "server.crt"
    key_path = directory / "server.key"
    certificate_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(private_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                                   serialization.NoEncryption()))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate_path, key_path)
    return context


class ResourceFixture(ThreadingHTTPServer):
    def __init__(self, context):
        super().__init__(("127.0.0.1", 0), ResourceHandler)
        self.socket = context.wrap_socket(self.socket, server_side=True)
        self.scenarios = {}
        self.failures = []
        self.opens = {}


class ResourceHandler(BaseHTTPRequestHandler):
    def log_message(self, format_string, *arguments):
        pass

    def respond(self, status, payload):
        response = json.dumps(payload).encode() if not isinstance(payload, bytes) else payload
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def do_GET(self):
        self.dispatch(None)

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
        self.dispatch(payload)

    def dispatch(self, payload):
        scenario = self.headers.get("Authorization", "").removeprefix("Bearer ")
        if self.headers.get("X-Pixels-Client-Type") != "panel" or self.headers.get("X-Pixels-Subject-Kind") != "guest":
            self.server.failures.append("Request lost its client/subject identity")
        if self.path == "/api/console/resource-sessions" and self.command == "POST":
            self.server.opens[scenario] = self.server.opens.get(scenario, 0) + 1
            session = {"id": str(uuid.uuid4()), "revision": 1, "state": "pending", "client_type": "panel",
                       "access_role": "controller", "target": payload["target"], "owner": {"kind": "guest"}}
            self.server.scenarios[scenario] = {"session": session, "closes": 0, "reads": 0, "descriptors": 0}
            opened = dict(session)
            if scenario == "invalid_open_metadata":
                opened["client_type"] = "android"
            self.respond(201, opened)
            return
        scenario_state = self.server.scenarios[scenario]
        session = scenario_state["session"]
        if not self.path.startswith("/api/console/resource-sessions/" + session["id"]):
            self.server.failures.append("Cleanup targeted another session")
        if self.path.endswith("/descriptor"):
            scenario_state["descriptors"] += 1
            if scenario == "iroh_wait":
                if payload["revision"] != 1:
                    self.server.failures.append("Endpoint readiness retry changed the reservation revision")
                if scenario_state["descriptors"] < 3:
                    self.respond(503, {"code": "transport_not_ready"})
                    return
            session["revision"] += 1
            if scenario in ("descriptor_failure", "cleanup_rejected"):
                self.respond(503, {"code": "unavailable", "message": "descriptor unavailable"})
            elif scenario == "malformed_descriptor":
                self.respond(200, b"{broken")
            else:
                descriptor = {"session": dict(session), "host": "127.0.0.1", "port": 4613, "transport": "native"}
                if scenario == "invalid_descriptor":
                    descriptor["port"] = 0
                if scenario == "iroh_wait":
                    descriptor["iroh"] = {"endpoint_address": {"id": "a" * 64, "addrs": [{"Ip": "127.0.0.1:4613"}]},
                                          "endpoint_configuration": {}}
                self.respond(200, {"descriptor": descriptor, "token": "fixture-token"})
            if scenario in ("stale_close", "concurrent_close", "already_closed"):
                session["revision"] += 1
                session["state"] = "closed" if scenario == "already_closed" else "connected"
        elif self.command == "GET":
            scenario_state["reads"] += 1
            self.respond(200, session)
        elif self.path.endswith("/close"):
            scenario_state["closes"] += 1
            if scenario == "concurrent_close" and scenario_state["closes"] == 1:
                session["revision"] += 1
            if payload["revision"] != session["revision"] or scenario in ("close_rejected", "cleanup_rejected"):
                self.respond(403, {"code": "rejected", "message": "close rejected"})
            else:
                session["revision"] += 1
                session["state"] = "closing"
                self.respond(200, session)
        else:
            self.server.failures.append("Unexpected endpoint")
            self.respond(404, {})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-executable", type=Path, required=True)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pixels-resource-lifecycle-") as temporary_directory:
        context = server_context(Path(temporary_directory))
        with ResourceFixture(context) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                environment = dict(os.environ, PIXELS_RESOURCE_TEST_PORT=str(server.server_port), NO_PROXY="127.0.0.1")
                completed = subprocess.run([str(arguments.test_executable.resolve()), "--gtest_filter=ConsoleResourceLifecycle.*"],
                                           env=environment, timeout=90, check=False)
                assert completed.returncode == 0, "C++ Console API assertions failed"
                assert len(server.scenarios) == 11, "Some fault scenarios were not exercised"
                assert server.opens["iroh_wait"] == 1, "Endpoint readiness retry reserved another session"
                assert server.scenarios["iroh_wait"]["descriptors"] == 3
                for scenario, observed in server.scenarios.items():
                    expected_closes = 0 if scenario == "already_closed" else 2 if scenario == "concurrent_close" else 1
                    assert observed["closes"] == expected_closes, (scenario, observed)
                    expected_state = "closed" if scenario == "already_closed" else "pending" if scenario in (
                        "cleanup_rejected", "close_rejected") else "closing"
                    assert observed["session"]["state"] == expected_state, (scenario, observed)
                assert not server.failures, server.failures
                print("PASS: 11 HTTPS reservation cleanup/revision/readiness scenarios")
            finally:
                server.shutdown()
                worker.join(timeout=5)


if __name__ == "__main__":
    main()
