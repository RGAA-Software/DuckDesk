"""Register, log in, log out, and remove one temporary Android Console account."""

from __future__ import annotations

import argparse
import json
import urllib.parse
import uuid
from pathlib import Path

from migrate_public_catalog_names_20260915 import AdminClient
from test_android_cloud_apps_public import ApiError, ConsoleClient, require_string


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="HTTPS PX Console base URL")
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
    client = ConsoleClient(arguments.endpoint, arguments.ca)
    guest = client.request(
        "/api/v1/session/guest",
        body={"client_nonce": f"android-registration-{suffix}", "client_type": "android"},
    )
    guest_token = require_string(guest, "access_token")
    registered = client.request(
        "/api/v1/user/register",
        guest_token,
        {"username": username, "password": password},
    )
    uid = require_string(registered, "uid")
    deleted = False
    try:
        login = client.request(
            "/api/v1/session/user/login",
            body={"username": username, "password": password, "client_type": "android"},
        )
        user_token = require_string(login, "access_token")
        client.request("/api/v1/session/user/logout", user_token, {})
    finally:
        admin = AdminClient(arguments.endpoint, arguments.ca)
        admin.login(admin_username, admin_password)
        try:
            query = urllib.parse.urlencode({"page": 1, "page_size": 100, "keyword": username})
            page = admin.request(f"/api/v1/admin/users?{query}")
            row = next((item for item in page.get("items", []) if item.get("uid") == uid), None)
            if row is None:
                raise ApiError("Registered user was not visible to the cleanup API")
            result = admin.request(
                f"/api/v1/admin/users/{urllib.parse.quote(uid, safe='')}",
                {"version": row.get("version")},
                write=True,
                method="DELETE",
            )
            deleted = result.get("uid") == uid and result.get("disabled") is True
            if not deleted:
                raise ApiError("Temporary user cleanup did not return a disabled user")
        finally:
            admin.logout()
    print(json.dumps({"registered": True, "android_login": True, "logout": True, "cleanup": deleted}))


if __name__ == "__main__":
    main()
