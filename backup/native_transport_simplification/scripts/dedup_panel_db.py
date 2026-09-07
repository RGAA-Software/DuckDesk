#!/usr/bin/env python3
"""Deduplicate panel SQLite audit tables before unique-index upgrade."""

import datetime
import os
import shutil
import sqlite3


def main() -> None:
    public = os.environ.get("PUBLIC", r"C:\Users\Public")
    db_path = os.path.join(public, "Pixels", "px_data", "px_data.db")
    if not os.path.exists(db_path):
        print("DB not found:", db_path)
        return

    backup = db_path + ".dedup." + datetime.datetime.now().strftime("%Y%m%d%H%M%S") + ".bak"
    shutil.copy2(db_path, backup)
    print("Backup:", backup)

    conn = sqlite3.connect(db_path)
    conn.execute(
        """
        DELETE FROM visit_record
        WHERE id NOT IN (
            SELECT MAX(id) FROM visit_record GROUP BY conn_id
        )
        """
    )
    conn.execute(
        """
        DELETE FROM file_transfer_record
        WHERE id NOT IN (
            SELECT MAX(id) FROM file_transfer_record GROUP BY the_file_id
        )
        """
    )
    conn.commit()
    conn.close()
    print("Dedup done.")


if __name__ == "__main__":
    main()
