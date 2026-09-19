"""Exercise Android device visibility across an explicit ACL grant and revocation."""

from __future__ import annotations

import argparse
import json
import urllib.parse
import uuid
from collections.abc import Callable
from pathlib import Path
from typing import Any

from test_android_cloud_apps_public import ApiError, ConsoleClient, require_string
from test_android_registration_public import list_users


def paged_records(client: ConsoleClient, path: str, token: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    after = ""
    for _ in range(100):
        query = urllib.parse.urlencode({"limit": 100, **({"after": after} if after else {})})
        page = client.request(f"{path}?{query}", token=token)
        if not isinstance(page, list):
            raise ApiError(f"{path} did not return a list")
        records.extend(page)
        if len(page) < 100:
            return records
        after = require_string(page[-1], "id")
    raise ApiError(f"{path} exceeds the supported test page window")


def selected_device(devices: list[dict[str, Any]], requested_id: str) -> dict[str, Any]:
    if requested_id:
        device = next((record for record in devices if record.get("id") == requested_id), None)
        if device is None:
            raise ApiError("The requested device is not present in the managed directory")
        return device
    device = next((record for record in devices if record.get("disabled") is False), None)
    if device is None:
        raise ApiError("The managed directory has no enabled device for the ACL test")
    return device


def expect_status(action: Callable[[], Any], expected_status: int) -> None:
    try:
        action()
    except ApiError as error:
        if error.status_code == expected_status:
            return
        raise
    raise ApiError(f"Request succeeded but HTTP {expected_status} was required")


def replace_access(
    administrator: ConsoleClient,
    admin_token: str,
    device_id: str,
    revision: int,
    users: list[str],
    groups: list[str],
) -> int:
    updated = administrator.request(
        f"/api/console/managed/devices/{urllib.parse.quote(device_id, safe='')}/access",
        method="PUT",
        token=admin_token,
        body={"revision": revision, "users": users, "groups": groups},
    )
    next_revision = updated.get("revision") if isinstance(updated, dict) else None
    if not isinstance(next_revision, int) or next_revision <= revision:
        raise ApiError("Device ACL update did not advance its revision")
    return next_revision


def delete_temporary_user(administrator: ConsoleClient, admin_token: str, user_id: str) -> None:
    user = next((record for record in list_users(administrator, admin_token) if record.get("id") == user_id), None)
    if user is None:
        return
    revision = user.get("revision")
    if not isinstance(revision, int) or revision <= 0:
        raise ApiError("Temporary user has an invalid revision")
    query = urllib.parse.urlencode({"revision": revision})
    administrator.request(
        f"/api/console/users/{urllib.parse.quote(user_id, safe='')}?{query}",
        method="DELETE",
        token=admin_token,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="HTTPS Pixels Console base URL")
    parser.add_argument("--ca", required=True, type=Path, help="CA or server certificate PEM")
    parser.add_argument("--admin-credentials", required=True, type=Path, help="Ignored JSON containing admin username and password")
    parser.add_argument("--device-id", default="", help="Existing managed device UUID; defaults to the first enabled device")
    arguments = parser.parse_args()
    if not arguments.ca.is_file():
        raise ApiError(f"CA file does not exist: {arguments.ca}")
    credentials = json.loads(arguments.admin_credentials.read_text(encoding="utf-8"))
    admin_username = credentials.get("username")
    admin_password = credentials.get("password")
    if not isinstance(admin_username, str) or not admin_username or not isinstance(admin_password, str) or not admin_password:
        raise ApiError("Admin credentials JSON does not contain username and password")

    administrator = ConsoleClient(arguments.endpoint, arguments.ca, client_type="admin_web")
    android = ConsoleClient(arguments.endpoint, arguments.ca)
    admin_login = administrator.request(
        "/api/console/sessions",
        method="POST",
        body={"username": admin_username, "password": admin_password},
    )
    admin_token = require_string(admin_login, "token")
    device = selected_device(paged_records(administrator, "/api/console/managed/devices", admin_token), arguments.device_id)
    device_id = require_string(device, "id")
    device_revision = device.get("revision")
    if not isinstance(device_revision, int) or device_revision <= 0:
        raise ApiError("Selected device has an invalid revision")
    access = administrator.request(
        f"/api/console/managed/devices/{urllib.parse.quote(device_id, safe='')}/access",
        token=admin_token,
    )
    original_users = access.get("users") if isinstance(access, dict) else None
    original_groups = access.get("groups") if isinstance(access, dict) else None
    if not isinstance(original_users, list) or not all(isinstance(value, str) for value in original_users):
        raise ApiError("Selected device has an invalid user ACL")
    if not isinstance(original_groups, list) or not all(isinstance(value, str) for value in original_groups):
        raise ApiError("Selected device has an invalid group ACL")

    suffix = uuid.uuid4().hex[:12]
    username = f"android_acl_{suffix}"
    password = f"Aa1!{uuid.uuid4().hex}"
    registered = android.request(
        "/api/console/accounts",
        method="POST",
        body={"username": username, "password": password},
    )
    user_id = require_string(registered, "id")
    acl_modified = False
    current_revision = device_revision
    try:
        login = android.request(
            "/api/console/sessions",
            method="POST",
            body={"username": username, "password": password},
        )
        user_token = require_string(login, "token")
        if any(record.get("id") == device_id for record in paged_records(android, "/api/console/devices", user_token)):
            raise ApiError("Temporary Android user unexpectedly saw the device before the ACL grant")
        expect_status(
            lambda: android.request(f"/api/console/devices/{urllib.parse.quote(device_id, safe='')}", token=user_token),
            403,
        )

        current_revision = replace_access(
            administrator,
            admin_token,
            device_id,
            current_revision,
            sorted(set(original_users + [user_id])),
            original_groups,
        )
        acl_modified = True
        login = android.request(
            "/api/console/sessions",
            method="POST",
            body={"username": username, "password": password},
        )
        user_token = require_string(login, "token")
        visible = paged_records(android, "/api/console/devices", user_token)
        if not any(record.get("id") == device_id for record in visible):
            raise ApiError("Granted device is absent from the Android device directory")
        detail = android.request(f"/api/console/devices/{urllib.parse.quote(device_id, safe='')}", token=user_token)
        if not isinstance(detail, dict) or detail.get("id") != device_id:
            raise ApiError("Android device detail did not match the granted device")

        current_revision = replace_access(
            administrator,
            admin_token,
            device_id,
            current_revision,
            original_users,
            original_groups,
        )
        acl_modified = False
        login = android.request(
            "/api/console/sessions",
            method="POST",
            body={"username": username, "password": password},
        )
        user_token = require_string(login, "token")
        if any(record.get("id") == device_id for record in paged_records(android, "/api/console/devices", user_token)):
            raise ApiError("Revoked device remained in the Android device directory")
        expect_status(
            lambda: android.request(f"/api/console/devices/{urllib.parse.quote(device_id, safe='')}", token=user_token),
            403,
        )
        android.request("/api/console/session", method="DELETE", token=user_token)
        print(json.dumps({"device_id": device_id, "grant_visible": True, "revocation_hidden": True, "acl_restored": True}))
    finally:
        cleanup_error: Exception | None = None
        if acl_modified:
            try:
                managed = selected_device(paged_records(administrator, "/api/console/managed/devices", admin_token), device_id)
                latest_revision = managed.get("revision")
                if not isinstance(latest_revision, int) or latest_revision <= 0:
                    raise ApiError("Unable to restore the original device ACL: current revision is invalid")
                replace_access(administrator, admin_token, device_id, latest_revision, original_users, original_groups)
            except Exception as error:  # Preserve cleanup attempts after the first failure.
                cleanup_error = error
        try:
            delete_temporary_user(administrator, admin_token, user_id)
        except Exception as error:
            cleanup_error = cleanup_error or error
        try:
            administrator.request("/api/console/session", method="DELETE", token=admin_token)
        except Exception as error:
            cleanup_error = cleanup_error or error
        if cleanup_error:
            raise cleanup_error


if __name__ == "__main__":
    main()
