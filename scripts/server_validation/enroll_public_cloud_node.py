#!/usr/bin/env python3
"""Create a test Cloud Node identity in the freshly initialized public Console."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import requests


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ca", type=Path, required=True)
    parser.add_argument("--password-file", type=Path, required=True)
    parser.add_argument("--token-output", type=Path, required=True)
    arguments = parser.parse_args()
    origin = "https://39.71.45.66:4600"
    administrator_password = arguments.password_file.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9a-f]{48}", administrator_password):
        raise ValueError("Administrator password has an unexpected shape")
    session = requests.Session()
    session.verify = str(arguments.ca)
    headers = {"Origin": origin, "x-pixels-client-type": "admin_web"}
    login = session.post(f"{origin}/api/console/sessions", headers=headers,
                         json={"username": "admin", "password": administrator_password}, timeout=15)
    login.raise_for_status()
    headers["Authorization"] = f"Bearer {login.json()['token']}"
    try:
        device_response = session.post(
            f"{origin}/api/console/managed/devices", headers=headers,
            json={"name": "Public 90 Cloud Node", "platform": "windows"}, timeout=15,
        )
        device_response.raise_for_status()
        device = device_response.json()["device"]
        node_response = session.post(
            f"{origin}/api/console/managed/nodes", headers=headers,
            json={"device_id": device["id"], "product": "cloud_node", "max_instances": 4},
            timeout=15,
        )
        node_response.raise_for_status()
        node = node_response.json()
        node_token = node["node_token"]
        if not re.fullmatch(r"[0-9a-f]{64}", node_token):
            raise ValueError("Node token has an unexpected shape")
        arguments.token_output.write_text(node_token, encoding="ascii")
        print(json.dumps({"result": "NODE_CREATED", "device_id": device["id"],
                          "node_id": node["node"]["id"]}))
    finally:
        session.delete(f"{origin}/api/console/session", headers=headers, timeout=10)


if __name__ == "__main__":
    main()
