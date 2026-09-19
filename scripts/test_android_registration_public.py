"""Register, authenticate, log out, and remove a temporary Android account."""

from __future__ import annotations

import argparse
import json
import urllib.parse
import uuid
from pathlib import Path
from typing import Any

from test_android_cloud_apps_public import ApiError, ConsoleClient, require_string


def list_users(client: ConsoleClient, token: str) -> list[dict[str, Any]]:
    users: list[dict[str, Any]] = []
    after = ""
    for _ in range(100):
        query = urllib.parse.urlencode({"limit": 100, **({"after": after} if after else {})})
        page = client.request(f"/api/console/users?{query}", token=token)
        if not isinstance(page, list):
            raise ApiError("Managed user directory is not a list")
        users.extend(page)
        if len(page) < 100:
            return users
        after = require_string(page[-1], "id")
    raise ApiError("Managed user directory exceeds the supported test page window")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="HTTPS Pixels Console base URL")
    parser.add_argument("--ca", required=True, type=Path, help="CA or server certificate PEM")
    parser.add_argument("--admin-credentials", required=True, type=Path, help="Ignored JSON containing admin username and password")
    arguments = parser.parse_args()
    credentials = json.loads(arguments.admin_credentials.read_text(encoding="utf-8"))
    admin_username = credentials.get("username")
    admin_password = credentials.get("password")
    if not isinstance(admin_username, str) or not admin_username or not isinstance(admin_password, str) or not admin_password:
        raise ApiError("Admin credentials JSON does not contain username and password")

    suffix = uuid.uuid4().hex[:12]
    username = f"android_smoke_{suffix}"
    password = f"Aa1!{uuid.uuid4().hex}"
    android = ConsoleClient(arguments.endpoint, arguments.ca)
    registered = android.request(
        "/api/console/accounts",
        method="POST",
        body={"username": username, "password": password},
    )
    user_id = require_string(registered, "id")
    login = android.request(
        "/api/console/sessions",
        method="POST",
        body={"username": username, "password": password},
    )
    user_token = require_string(login, "token")
    android.request("/api/console/session", method="DELETE", token=user_token)

    administrator = ConsoleClient(arguments.endpoint, arguments.ca, client_type="admin_web")
    admin_login = administrator.request(
        "/api/console/sessions",
        method="POST",
        body={"username": admin_username, "password": admin_password},
    )
    admin_token = require_string(admin_login, "token")
    deleted = False
    try:
        managed_user = next(
            (item for item in list_users(administrator, admin_token) if item.get("id") == user_id),
            None,
        )
        if managed_user is None:
            raise ApiError("Registered Android user was not visible to the current management API")
        revision = managed_user.get("revision")
        if not isinstance(revision, int) or revision <= 0:
            raise ApiError("Registered Android user has an invalid revision")
        query = urllib.parse.urlencode({"revision": revision})
        administrator.request(
            f"/api/console/users/{urllib.parse.quote(user_id, safe='')}?{query}",
            method="DELETE",
            token=admin_token,
        )
        deleted = True
    finally:
        administrator.request("/api/console/session", method="DELETE", token=admin_token)
    print(json.dumps({"registered": True, "android_login": True, "logout": True, "cleanup": deleted}))


if __name__ == "__main__":
    main()
