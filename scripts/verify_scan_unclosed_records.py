#!/usr/bin/env python3
"""Seed or verify unclosed audit records for WsPanelServer::ScanAndFixUnclosedRecords.

Usage:
  python scripts/verify_scan_unclosed_records.py --seed
  # Start GammaRay.exe, wait ~30s, then:
  python scripts/verify_scan_unclosed_records.py --check
  python scripts/verify_scan_unclosed_records.py --cleanup
"""

from __future__ import annotations

import argparse
import os
import sqlite3
import time

TEST_CONN_ID = "audit-test-conn-id"
TEST_FILE_ID = "audit-test-file-id"


def db_path() -> str:
    public = os.environ.get("PUBLIC", r"C:\Users\Public")
    return os.path.join(public, "GoDesk", "gr_data", "gr_data.db")


def connect() -> sqlite3.Connection:
    path = db_path()
    if not os.path.exists(path):
        raise SystemExit(f"Database not found: {path}")
    return sqlite3.connect(path)


def seed() -> None:
    now = int(time.time() * 1000)
    old = now - 5 * 60 * 1000
    conn = connect()
    conn.execute("DELETE FROM visit_record WHERE conn_id = ?", (TEST_CONN_ID,))
    conn.execute("DELETE FROM file_transfer_record WHERE the_file_id = ?", (TEST_FILE_ID,))

    visit_id = conn.execute("SELECT COALESCE(MAX(id), 0) + 1 FROM visit_record").fetchone()[0]
    transfer_id = conn.execute("SELECT COALESCE(MAX(id), 0) + 1 FROM file_transfer_record").fetchone()[0]

    conn.execute(
        """
        INSERT INTO visit_record
        (id, stream_id, conn_id, conn_type, begin, end, duration, visitor_device, target_device)
        VALUES (?, ?, ?, ?, ?, 0, 0, ?, ?)
        """,
        (visit_id, "test-stream", TEST_CONN_ID, "Direct", old, "visitor-test", "target-test"),
    )
    conn.execute(
        """
        INSERT INTO file_transfer_record
        (id, the_file_id, begin, end, visitor_device, target_device, direction, file_detail, success, duration)
        VALUES (?, ?, ?, 0, ?, ?, ?, ?, 0, 0)
        """,
        (transfer_id, TEST_FILE_ID, old, "visitor-test", "target-test", "In", "audit-test.txt"),
    )
    conn.commit()
    conn.close()
    print(f"Seeded unclosed records in {db_path()}")
    print("Next: start GammaRay.exe, wait >= 30s, then run with --check")


def check() -> int:
    conn = connect()
    conn.row_factory = sqlite3.Row
    visit = conn.execute(
        "SELECT * FROM visit_record WHERE conn_id = ?", (TEST_CONN_ID,)
    ).fetchone()
    transfer = conn.execute(
        "SELECT * FROM file_transfer_record WHERE the_file_id = ?", (TEST_FILE_ID,)
    ).fetchone()
    conn.close()

    ok = True
    if not visit:
        print("FAIL: visit_record test row missing")
        ok = False
    elif visit["end"] == 0:
        print("FAIL: visit_record still unclosed (end == 0)")
        ok = False
    else:
        print(
            f"OK visit: end={visit['end']}, duration={visit['duration']}, "
            f"visitor={visit['visitor_device']}"
        )

    if not transfer:
        print("FAIL: file_transfer_record test row missing")
        ok = False
    elif transfer["end"] == 0:
        print("FAIL: file_transfer_record still unclosed (end == 0)")
        ok = False
    elif transfer["success"] != 0:
        print(f"WARN: file_transfer success={transfer['success']} (expected 0/false)")
    else:
        print(
            f"OK file transfer: end={transfer['end']}, duration={transfer['duration']}, "
            f"success={transfer['success']}"
        )

    if ok:
        print("ScanAndFixUnclosedRecords verification passed.")
        return 0
    print("Verification failed. Check godesk.log for ScanAndFixUnclosedRecords.")
    return 1


def cleanup() -> None:
    conn = connect()
    conn.execute("DELETE FROM visit_record WHERE conn_id = ?", (TEST_CONN_ID,))
    conn.execute("DELETE FROM file_transfer_record WHERE the_file_id = ?", (TEST_FILE_ID,))
    conn.commit()
    conn.close()
    print("Removed test audit rows.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify startup scan for audit records")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--seed", action="store_true", help="Insert unclosed test records")
    group.add_argument("--check", action="store_true", help="Verify records were closed")
    group.add_argument("--cleanup", action="store_true", help="Remove test records")
    args = parser.parse_args()

    if args.seed:
        seed()
    elif args.check:
        raise SystemExit(check())
    else:
        cleanup()


if __name__ == "__main__":
    main()
