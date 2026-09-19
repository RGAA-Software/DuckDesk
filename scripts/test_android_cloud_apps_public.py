"""Exercise current Android CloudApplication contracts against a public Console."""

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
    def __init__(self, message: str, status_code: int | None = None) -> None:
        super().__init__(message)
        self.status_code = status_code


class ConsoleClient:
    def __init__(self, endpoint: str, ca_file: Path, client_type: str = "android") -> None:
        self.endpoint = endpoint.rstrip("/")
        self.context = ssl.create_default_context(cafile=str(ca_file))
        # Python 3.13 enables OpenSSL strict mode, which rejects the current
        # private test CA solely because it predates the Authority Key
        # Identifier extension. Chain and hostname verification remain on.
        self.context.verify_flags &= ~ssl.VERIFY_X509_STRICT
        self.client_type = client_type

    def request(
        self,
        path: str,
        *,
        method: str = "GET",
        token: str = "",
        subject_kind: str = "",
        body: dict[str, Any] | None = None,
    ) -> Any:
        headers = {
            "Accept": "application/json",
            "X-Pixels-Client-Type": self.client_type,
        }
        payload = None
        if token:
            headers["Authorization"] = f"Bearer {token}"
        if subject_kind:
            headers["X-Pixels-Subject-Kind"] = subject_kind
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(
            self.endpoint + path,
            data=payload,
            headers=headers,
            method=method,
        )
        try:
            with urllib.request.urlopen(request, context=self.context, timeout=20) as response:
                content = response.read()
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            raise ApiError(f"{method} {path} returned HTTP {error.code}: {detail[:300]}", error.code) from error
        except (OSError, ValueError) as error:
            raise ApiError(f"{method} {path} failed: {error}") from error
        if not content:
            return None
        try:
            return json.loads(content)
        except json.JSONDecodeError as error:
            raise ApiError(f"{method} {path} returned invalid JSON") from error


def encoded(value: str) -> str:
    return urllib.parse.quote(value, safe="")


def require_string(record: dict[str, Any], key: str) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value:
        raise ApiError(f"Response is missing non-empty {key}")
    return value


def wait_for_instance(
    client: ConsoleClient,
    token: str,
    subject_kind: str,
    instance_id: str,
    timeout_seconds: int,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    path = f"/api/console/instances/{encoded(instance_id)}"
    while time.monotonic() < deadline:
        instance = client.request(path, token=token, subject_kind=subject_kind)
        if instance.get("state") == "running":
            return instance
        if instance.get("state") in {"failed", "stopped"}:
            raise ApiError(f"Application instance became {instance.get('state')}")
        time.sleep(0.5)
    raise ApiError(f"Application instance did not become running within {timeout_seconds} seconds")


def stop_instance(client: ConsoleClient, token: str, subject_kind: str, instance_id: str) -> None:
    path = f"/api/console/instances/{encoded(instance_id)}"
    current = client.request(path, token=token, subject_kind=subject_kind)
    if current.get("state") in {"stopped", "failed"}:
        return
    revision = current.get("revision")
    if not isinstance(revision, int) or revision <= 0:
        raise ApiError("Application instance has an invalid revision")
    client.request(
        f"{path}/stop",
        method="POST",
        token=token,
        subject_kind=subject_kind,
        body={"revision": revision},
    )


def close_resource_session(client: ConsoleClient, token: str, subject_kind: str, session_id: str) -> None:
    path = f"/api/console/resource-sessions/{encoded(session_id)}"
    current = client.request(path, token=token, subject_kind=subject_kind)
    if current.get("state") == "ended":
        return
    revision = current.get("revision")
    if not isinstance(revision, int) or revision <= 0:
        raise ApiError("Resource session has an invalid revision")
    client.request(
        f"{path}/close",
        method="POST",
        token=token,
        subject_kind=subject_kind,
        body={"revision": revision},
    )


def exercise_cloud_application(
    client: ConsoleClient,
    token: str,
    subject_kind: str,
    application: dict[str, Any],
    timeout_seconds: int,
) -> dict[str, Any]:
    application_id = require_string(application, "id")
    instance_id = ""
    resource_session_id = ""
    try:
        started = client.request(
            "/api/console/instances",
            method="POST",
            token=token,
            subject_kind=subject_kind,
            body={
                "request_id": str(uuid.uuid4()),
                "application_id": application_id,
                "deployment_id": None,
            },
        )
        instance_id = require_string(started, "id")
        running = wait_for_instance(client, token, subject_kind, instance_id, timeout_seconds)
        opened = client.request(
            "/api/console/resource-sessions",
            method="POST",
            token=token,
            subject_kind=subject_kind,
            body={
                "request_id": str(uuid.uuid4()),
                "target": {
                    "kind": "cloud_application",
                    "application_id": application_id,
                    "instance_id": instance_id,
                },
                "access": "controller",
            },
        )
        resource_session_id = require_string(opened, "id")
        revision = opened.get("revision")
        if not isinstance(revision, int) or revision <= 0:
            raise ApiError("Resource session has an invalid revision")
        descriptor_response = client.request(
            f"/api/console/resource-sessions/{encoded(resource_session_id)}/descriptor",
            method="POST",
            token=token,
            subject_kind=subject_kind,
            body={"revision": revision},
        )
        descriptor_token = require_string(descriptor_response, "token")
        descriptor = descriptor_response.get("descriptor")
        if not isinstance(descriptor, dict):
            raise ApiError("Resource descriptor is missing")
        session = descriptor.get("session")
        target = session.get("target") if isinstance(session, dict) else None
        if (
            not isinstance(session, dict)
            or not isinstance(target, dict)
            or session.get("id") != resource_session_id
            or session.get("client_type") != "android"
            or session.get("access_role") != "controller"
            or target.get("kind") != "cloud_application"
            or target.get("application_id") != application_id
            or target.get("instance_id") != instance_id
            or descriptor.get("transport") != "native"
            or not isinstance(descriptor.get("port"), int)
            or descriptor.get("port") not in range(4613, 4999)
            or len(descriptor_token) != 64
        ):
            raise ApiError("Console returned an invalid Android CloudApplication descriptor")
        return {
            "application_id": application_id,
            "application_kind": application.get("kind"),
            "instance_id": instance_id,
            "instance_state": running.get("state"),
            "resource_session_id": resource_session_id,
            "render_port": descriptor.get("port"),
            "descriptor_valid": True,
        }
    finally:
        if resource_session_id:
            close_resource_session(client, token, subject_kind, resource_session_id)
        if instance_id:
            stop_instance(client, token, subject_kind, instance_id)


def supported_application(applications: list[dict[str, Any]]) -> dict[str, Any]:
    application = next(
        (item for item in applications if item.get("kind") in {"webview", "game_hook"}),
        None,
    )
    if application is None:
        raise ApiError("The catalog has no Android-supported application")
    return application


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="HTTPS Pixels Console base URL")
    parser.add_argument("--ca", required=True, type=Path, help="CA or server certificate PEM")
    parser.add_argument("--timeout", type=int, default=90, help="Seconds to wait for each application instance")
    parser.add_argument("--user-credentials", type=Path, help="Ignored JSON containing username and password")
    arguments = parser.parse_args()
    if not arguments.ca.is_file():
        raise ApiError(f"CA file does not exist: {arguments.ca}")

    client = ConsoleClient(arguments.endpoint, arguments.ca)
    guest_response = client.request("/api/console/guest-sessions", method="POST", body={})
    guest_token = require_string(guest_response, "token")
    result: dict[str, Any] = {}
    try:
        guest_apps = client.request("/api/console/guest/applications?limit=100", token=guest_token)
        if not isinstance(guest_apps, list):
            raise ApiError("Guest application catalog is not a list")
        result["guest"] = exercise_cloud_application(
            client,
            guest_token,
            "guest",
            supported_application(guest_apps),
            arguments.timeout,
        )
    finally:
        client.request("/api/console/guest-session", method="DELETE", token=guest_token)

    if arguments.user_credentials is not None:
        credentials = json.loads(arguments.user_credentials.read_text(encoding="utf-8"))
        username = credentials.get("username")
        password = credentials.get("password")
        if not isinstance(username, str) or not username or not isinstance(password, str) or not password:
            raise ApiError("User credentials JSON does not contain username and password")
        login = client.request(
            "/api/console/sessions",
            method="POST",
            body={"username": username, "password": password},
        )
        user_token = require_string(login, "token")
        try:
            user_apps = client.request("/api/console/applications?limit=100", token=user_token)
            if not isinstance(user_apps, list):
                raise ApiError("User application catalog is not a list")
            result["user"] = exercise_cloud_application(
                client,
                user_token,
                "user",
                supported_application(user_apps),
                arguments.timeout,
            )
        finally:
            client.request("/api/console/session", method="DELETE", token=user_token)

    print(json.dumps(result, ensure_ascii=False, separators=(",", ":")))


if __name__ == "__main__":
    main()
