"""Exercise the Android guest Cloud Apps workflow against an explicit Console endpoint."""

from __future__ import annotations

import argparse
import json
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Any


class ApiError(RuntimeError):
    pass


class ConsoleClient:
    def __init__(self, endpoint: str, ca_file: Path) -> None:
        self.endpoint = endpoint.rstrip("/")
        self.context = ssl.create_default_context(cafile=str(ca_file))

    def request(self, path: str, token: str = "", body: dict[str, Any] | None = None) -> Any:
        headers = {"Accept": "application/json"}
        payload = None
        if token:
            headers["Authorization"] = f"Bearer {token}"
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(self.endpoint + path, data=payload, headers=headers, method="POST" if body is not None else "GET")
        try:
            with urllib.request.urlopen(request, context=self.context, timeout=10) as response:
                envelope = json.load(response)
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            raise ApiError(f"{path} returned HTTP {error.code}: {detail[:300]}") from error
        except (OSError, ValueError) as error:
            raise ApiError(f"{path} failed: {error}") from error
        if envelope.get("code") != 200:
            raise ApiError(f"{path} returned Console code {envelope.get('code')}: {envelope.get('error') or envelope.get('message')}")
        return envelope.get("data")


def encoded(value: str) -> str:
    return urllib.parse.quote(value, safe="")


def require_string(record: dict[str, Any], key: str) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value:
        raise ApiError(f"Response is missing non-empty {key}")
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="HTTPS PX Console base URL")
    parser.add_argument("--ca", required=True, type=Path, help="CA or server certificate PEM")
    parser.add_argument("--timeout", type=int, default=90, help="Seconds to wait for the application instance")
    parser.add_argument("--user-credentials", type=Path, help="Optional ignored JSON containing username and password")
    arguments = parser.parse_args()
    if not arguments.ca.is_file():
        raise ApiError(f"CA file does not exist: {arguments.ca}")

    client = ConsoleClient(arguments.endpoint, arguments.ca)
    guest = client.request(
        "/api/v1/session/guest",
        body={"client_nonce": f"android-smoke-{uuid.uuid4()}", "client_type": "android"},
    )
    token = require_string(guest, "access_token")
    applications = client.request("/api/v1/public/apps")
    application = next(
        (item for item in applications if item.get("app_type") in {"game-hook", "webview"}),
        None,
    )
    if application is None:
        raise ApiError("The public catalog has no Android-supported application")

    app_id = require_string(application, "app_id")
    instance_id = ""
    stop_error: Exception | None = None
    result: dict[str, Any] | None = None
    try:
        started = client.request(
            f"/api/v1/public/apps/{encoded(app_id)}/start",
            token,
            {"client_nonce": f"android-instance-{uuid.uuid4()}"},
        )
        instance_id = require_string(started, "instance_id")
        deadline = time.monotonic() + arguments.timeout
        instance: dict[str, Any] | None = None
        while time.monotonic() < deadline:
            instances = client.request("/api/v1/public/instances", token)
            instance = next((item for item in instances if item.get("instance_id") == instance_id), None)
            if instance is not None and instance.get("state") in {"failed", "stopped"}:
                raise ApiError(f"Application instance became {instance.get('state')}")
            if instance is not None and instance.get("state") == "running" and instance.get("reconnectable") is True:
                break
            time.sleep(1)
        else:
            raise ApiError(f"Application instance did not become reconnectable within {arguments.timeout} seconds")

        descriptor = client.request(
            f"/api/v1/public/instances/{encoded(instance_id)}/native-connection",
            token,
            {"view_only": False, "client_capability": "android-native-v1"},
        )
        require_string(descriptor, "host")
        require_string(descriptor, "device_id")
        require_string(descriptor, "password_hash")
        require_string(descriptor, "signal_device_id")
        if descriptor.get("instance_id") != instance_id:
            raise ApiError("Native connection descriptor returned a different instance")
        if descriptor.get("app_type") not in {"game-hook", "webview"}:
            raise ApiError("Native connection descriptor returned an unsupported application type")
        port = descriptor.get("port")
        if not isinstance(port, int) or port not in range(4613, 4999):
            raise ApiError("Native connection descriptor returned an invalid dynamic Render port")
        result = {
            "app_id": app_id,
            "instance_id": instance_id,
            "state": instance.get("state"),
            "render_port": port,
            "native_descriptor_valid": True,
        }
    finally:
        if instance_id:
            try:
                stopped = client.request(
                    f"/api/v1/public/instances/{encoded(instance_id)}/stop",
                    token,
                    {"reason": "Android Cloud Apps public smoke test completed"},
                )
                if stopped.get("state") not in {"stopping", "stopped"}:
                    raise ApiError("Stop response did not enter a terminal transition")
            except Exception as error:
                stop_error = error
    if stop_error is not None:
        raise stop_error
    if result is None:
        raise ApiError("Cloud application smoke test did not produce a result")
    result["stop_requested"] = True

    if arguments.user_credentials is not None:
        if not arguments.user_credentials.is_file():
            raise ApiError(f"User credentials file does not exist: {arguments.user_credentials}")
        credentials = json.loads(arguments.user_credentials.read_text(encoding="utf-8"))
        username = credentials.get("username")
        password = credentials.get("password")
        if not isinstance(username, str) or not username or not isinstance(password, str) or not password:
            raise ApiError("User credentials JSON does not contain username and password")
        login = client.request(
            "/api/v1/session/user/login",
            body={"username": username, "password": password, "client_type": "android"},
        )
        user_token = require_string(login, "access_token")
        try:
            user_apps = client.request("/api/v1/user/apps", user_token)
            if not isinstance(user_apps, list):
                raise ApiError("User application catalog is not a list")
            result["user_login_valid"] = True
            result["user_catalog_size"] = len(user_apps)
        finally:
            client.request("/api/v1/session/user/logout", user_token, {})

    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
