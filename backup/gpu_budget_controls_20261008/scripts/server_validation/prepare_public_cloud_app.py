#!/usr/bin/env python3
"""Prepare one public WebView application on the newly initialized 90 deployment."""

from __future__ import annotations

import argparse
import json
import re
import secrets
from pathlib import Path

import requests


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ca", type=Path, required=True)
    parser.add_argument("--password-file", type=Path, required=True)
    parser.add_argument("--credentials-output", type=Path, required=True)
    parser.add_argument("--node-id", required=True)
    arguments = parser.parse_args()
    if arguments.credentials_output.exists():
        raise ValueError("Refusing to replace existing test credentials")
    origin = "https://39.71.45.66:4600"
    administrator_password = arguments.password_file.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9a-f]{48}", administrator_password):
        raise ValueError("Administrator password has an unexpected shape")
    headers = {"Origin": origin, "x-pixels-client-type": "admin_web"}
    session = requests.Session()
    session.verify = str(arguments.ca)
    login = session.post(f"{origin}/api/console/sessions", headers=headers,
                         json={"username": "admin", "password": administrator_password}, timeout=15)
    login.raise_for_status()
    headers["Authorization"] = f"Bearer {login.json()['token']}"
    username = f"cloud-acceptance-{secrets.token_hex(6)}"
    password = secrets.token_hex(24)
    try:
        user_response = session.post(f"{origin}/api/console/users", headers=headers,
                                     json={"username": username, "password": password, "role": "user"},
                                     timeout=15)
        user_response.raise_for_status()
        application_response = session.post(
            f"{origin}/api/console/managed/applications", headers=headers,
            json={"name": "90 WebView acceptance", "access": "public",
                  "launch": {"kind": "webview", "entry_url": f"{origin}/",
                             "video": {"codec": "h264", "bitrate_kbps": 8000}},
                  "allow_observer": False, "allow_takeover": False, "disabled": False},
            timeout=15,
        )
        application_response.raise_for_status()
        application = application_response.json()
        deployment_response = session.post(
            f"{origin}/api/console/managed/deployments", headers=headers,
            json={"application_id": application["id"], "node_id": arguments.node_id,
                  "configuration": {"target": {"kind": "webview"}, "gpu_key": None,
                                    "gpu_profile": {"memory_bytes": 536870912,
                                                    "compute_per_mille": 100,
                                                    "encoder_per_mille": 100,
                                                    "memory_reserve_bytes": 536870912,
                                                    "compute_limit_per_mille": 900,
                                                    "encoder_limit_per_mille": 900},
                                    "capacity": 1, "disabled": False}},
            timeout=15,
        )
        deployment_response.raise_for_status()
        arguments.credentials_output.write_text(
            json.dumps({"username": username, "password": password}), encoding="utf-8"
        )
        print(json.dumps({"result": "CLOUD_APP_PREPARED", "application_id": application["id"],
                          "deployment_id": deployment_response.json()["id"]}))
    finally:
        session.delete(f"{origin}/api/console/session", headers=headers, timeout=10)


if __name__ == "__main__":
    main()
