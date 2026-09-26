#!/usr/bin/env python3
"""Import a CN Auth test license through the public Console with pinned CA validation."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import requests


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", required=True)
    parser.add_argument("--ca", required=True, type=Path)
    parser.add_argument("--license", required=True, type=Path)
    parser.add_argument("--password-file", required=True, type=Path)
    parser.add_argument("--verify-only", action="store_true")
    arguments = parser.parse_args()
    administrator_password = arguments.password_file.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9a-f]{48}", administrator_password):
        raise ValueError("Administrator password file has an unexpected shape")
    origin = arguments.origin.rstrip("/")
    if origin != "https://39.71.45.66:4600":
        raise ValueError("Unexpected public Console origin")
    signed_wire = arguments.license.read_text(encoding="utf-8").strip()
    if not signed_wire.startswith("PXLIC2."):
        raise ValueError("Unexpected signed license format")
    headers = {"Origin": origin, "x-pixels-client-type": "admin_web"}
    session = requests.Session()
    session.verify = str(arguments.ca)
    login_response = session.post(
        f"{origin}/api/console/sessions",
        headers=headers,
        json={"username": "admin", "password": administrator_password},
        timeout=15,
    )
    if login_response.status_code != 200:
        raise ValueError(f"Console login returned HTTP {login_response.status_code}: "
                         f"{login_response.text[:300]}")
    token = login_response.json().get("token")
    if not isinstance(token, str) or not token:
        raise ValueError("Console administrator session token is missing")
    headers["Authorization"] = f"Bearer {token}"
    try:
        if not arguments.verify_only:
            license_response = session.put(
                f"{origin}/api/console/managed/license",
                headers=headers,
                json={"wire": signed_wire},
                timeout=15,
            )
            license_response.raise_for_status()
            license_status = license_response.json()
            if license_status.get("max_streams") != 1 or license_status.get("services") != ["cloud_applications"]:
                raise ValueError("Console accepted a license with unexpected terms")
        relay_response = session.get(f"{origin}/api/console/managed/relays?limit=100",
                                     headers=headers, timeout=10)
        relay_response.raise_for_status()
        relays = relay_response.json()
        ready_relays = [relay for relay in relays if relay.get("state") == "ready"
                        and relay.get("fresh") is True]
        node_response = session.get(f"{origin}/api/console/managed/nodes?limit=100",
                                    headers=headers, timeout=10)
        node_response.raise_for_status()
        nodes = node_response.json()
        readiness = session.get(f"{origin}/health/ready", timeout=10)
        if readiness.status_code != 204:
            raise ValueError(f"Console readiness stayed at HTTP {readiness.status_code}")
        print(json.dumps({"result": "LICENSE_ACTIVE" if not arguments.verify_only else "BUSINESS_STATUS",
                          "max_streams": 1, "services": ["cloud_applications"],
                          "ready_http": 204, "relay_count": len(relays),
                          "ready_relay_count": len(ready_relays), "node_count": len(nodes),
                          "nodes": [{"id": node.get("id"), "state": node.get("state"),
                                     "fresh": node.get("fresh"), "public_host": node.get("public_host")}
                                    for node in nodes]}))
    finally:
        session.delete(f"{origin}/api/console/session", headers=headers, timeout=10)


if __name__ == "__main__":
    main()
