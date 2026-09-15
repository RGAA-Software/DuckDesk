"""Rename current public catalog entries through the authenticated Console admin API."""

from __future__ import annotations

import argparse
import http.cookiejar
import json
import ssl
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


CATALOG_NAMES = {
    "app-10-ac0adf25": "2dAdventure",
    "app-16-7cb36d81": "心脏医学动态展示",
    "app-1-d0ca6494": "WebView 演示",
}


class ApiError(RuntimeError):
    pass


class AdminClient:
    def __init__(self, endpoint: str, ca_file: Path) -> None:
        self.endpoint = endpoint.rstrip("/")
        self.origin = self.endpoint
        context = ssl.create_default_context(cafile=str(ca_file))
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=context),
            urllib.request.HTTPCookieProcessor(self.cookies),
        )
        self.csrf_token = ""

    def request(
        self,
        path: str,
        body: dict[str, Any] | None = None,
        write: bool = False,
        method: str | None = None,
    ) -> Any:
        headers = {"Accept": "application/json", "Origin": self.origin}
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, separators=(",", ":")).encode("utf-8")
        if write and self.csrf_token:
            headers["X-CSRF-Token"] = self.csrf_token
        request = urllib.request.Request(
            self.endpoint + path,
            data=payload,
            headers=headers,
            method=method or ("POST" if body is not None else "GET"),
        )
        try:
            with self.opener.open(request, timeout=10) as response:
                envelope = json.load(response)
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            raise ApiError(f"{path} returned HTTP {error.code}: {detail[:300]}") from error
        except (OSError, ValueError) as error:
            raise ApiError(f"{path} failed: {error}") from error
        if envelope.get("code") != 200:
            raise ApiError(f"{path} returned Console code {envelope.get('code')}: {envelope.get('message')}")
        return envelope.get("data")

    def login(self, username: str, password: str) -> None:
        data = self.request("/api/v1/session/admin/login", {"username": username, "password": password})
        token = data.get("csrf_token")
        if not isinstance(token, str) or not token:
            raise ApiError("Admin login response did not contain a CSRF token")
        self.csrf_token = token

    def logout(self) -> None:
        if self.csrf_token:
            self.request("/api/v1/session/admin/logout", {}, write=True)
            self.csrf_token = ""


def save_request(row: dict[str, Any], name: str) -> dict[str, Any]:
    return {
        "app_id": row["app_id"],
        "name": name,
        "app_type": row["app_type"],
        "entry_url": row.get("entry_url"),
        "game_path": row.get("game_path", ""),
        "default_game_args": row.get("default_game_args"),
        "encoder_fps": row.get("encoder_fps"),
        "encoder_bitrate": row.get("encoder_bitrate"),
        "encoder_format": row.get("encoder_format"),
        "access_mode": row.get("access_mode"),
        "allow_observer": row.get("allow_observer"),
        "allow_takeover": row.get("allow_takeover"),
        "version": row.get("version"),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="HTTPS PX Console base URL")
    parser.add_argument("--ca", required=True, type=Path, help="CA or server certificate PEM")
    parser.add_argument("--admin-credentials", required=True, type=Path, help="Ignored JSON containing username and password")
    arguments = parser.parse_args()
    if not arguments.ca.is_file() or not arguments.admin_credentials.is_file():
        raise ApiError("CA or admin credentials file is missing")
    credentials = json.loads(arguments.admin_credentials.read_text(encoding="utf-8"))
    username = credentials.get("username")
    password = credentials.get("password")
    if not isinstance(username, str) or not username or not isinstance(password, str) or not password:
        raise ApiError("Admin credentials JSON does not contain username and password")

    client = AdminClient(arguments.endpoint, arguments.ca)
    updated: list[str] = []
    client.login(username, password)
    try:
        rows = client.request("/api/v1/app/control/app/rows")
        by_id = {row.get("app_id"): row for row in rows}
        missing = sorted(set(CATALOG_NAMES) - set(by_id))
        if missing:
            raise ApiError(f"Catalog is missing expected application IDs: {','.join(missing)}")
        for app_id, name in CATALOG_NAMES.items():
            row = by_id[app_id]
            if row.get("name") == name:
                continue
            saved = client.request("/api/v1/app/control/app/save", save_request(row, name), write=True)
            if saved.get("app_id") != app_id or saved.get("name") != name:
                raise ApiError(f"Console returned an invalid save result for {app_id}")
            updated.append(app_id)
    finally:
        client.logout()
    print(json.dumps({"updated": updated, "catalog_size": len(rows)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
