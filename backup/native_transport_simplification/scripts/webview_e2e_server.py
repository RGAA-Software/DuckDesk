#!/usr/bin/env python3
"""Tiny localhost-only static/event server for the WebView CEF smoke suite."""

from __future__ import annotations

import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import time


class Handler(SimpleHTTPRequestHandler):
    event_file: Path

    def do_GET(self) -> None:  # noqa: N802 - stdlib callback name
        if self.path.split("?", 1)[0] != "/streaming.html":
            super().do_GET()
            return

        # A document can become fully visible while its HTTP navigation stays
        # open for a long-lived response.  Keep the connection alive after
        # writing the normal E2E page so the smoke suite verifies that first
        # frame readiness is based on paint, not OnLoadEnd/network-idle.
        page = Path(self.directory or ".") / "index.html"
        self.send_response(200)
        self.send_header("content-type", "text/html; charset=utf-8")
        self.end_headers()
        try:
            self.wfile.write(page.read_bytes())
            self.wfile.write(b"\n<!-- keep main navigation open")
            self.wfile.flush()
            while True:
                time.sleep(1)
        except (BrokenPipeError, ConnectionResetError):
            return

    def do_POST(self) -> None:  # noqa: N802 - stdlib callback name
        if self.path != "/events":
            self.send_error(404)
            return
        try:
            size = min(int(self.headers.get("content-length", "0")), 16_384)
        except ValueError:
            size = 0
        payload = self.rfile.read(size)
        with self.event_file.open("ab") as output:
            output.write(payload.replace(b"\r", b"").replace(b"\n", b" ") + b"\n")
        self.send_response(204)
        self.end_headers()

    def log_message(self, format: str, *args: object) -> None:
        # URLs may contain test secrets. Keep the server silent by design.
        return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--events", type=Path, required=True)
    args = parser.parse_args()
    args.events.parent.mkdir(parents=True, exist_ok=True)
    args.events.write_bytes(b"")

    handler = lambda *a, **kw: Handler(*a, directory=str(args.root.resolve()), **kw)
    Handler.event_file = args.events.resolve()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    server.serve_forever()


if __name__ == "__main__":
    main()
