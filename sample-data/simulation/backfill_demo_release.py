"""Fill missing release metadata on the original works in an existing demo database."""

import argparse
import csv
import json
import sqlite3
from contextlib import closing
from datetime import date
from pathlib import Path


def backfill(database, catalog):
    dates = {}
    with catalog.open(encoding="utf-8-sig", newline="") as source:
        for row in csv.DictReader(source):
            value = row["release_date"]
            if not value:
                continue
            suffix = {4: "-01-01", 7: "-01", 10: ""}.get(len(value))
            if suffix is None or date.fromisoformat(value + suffix).isoformat() != value + suffix:
                raise ValueError(f"Invalid release date for {row['id']}.")
            dates[row["id"]] = value

    with closing(sqlite3.connect(database.resolve().as_uri() + "?mode=rw", uri=True)) as db, db:
        db.execute("BEGIN IMMEDIATE")
        if not db.execute("SELECT 1 FROM demo_import_metadata WHERE id=1 AND kind='media-demo-v1'").fetchone():
            raise ValueError("Expected a server demo database.")
        rows = db.execute(
            "SELECT m.media_id,m.warehouse_id,m.item_id,i.media FROM demo_media_map m "
            "JOIN warehouse_items i ON i.id=m.item_id AND i.warehouse_id=m.warehouse_id"
        ).fetchall()
        updated = 0
        for media_id, warehouse_id, item_id, original in rows:
            value = dates.get(media_id)
            if not value:
                continue
            record = json.loads(original)
            if record["id"] != media_id or record["release_date"] or record.get("release_period"):
                continue
            record["release_date" if len(value) == 10 else "release_period"] = value
            db.execute("UPDATE warehouse_items SET media=? WHERE id=? AND warehouse_id=?",
                       (json.dumps(record, ensure_ascii=False, separators=(",", ":")).encode("utf-8"),
                        item_id, warehouse_id))
            updated += 1
        return updated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("catalog", type=Path)
    args = parser.parse_args()
    try:
        print(f"Updated demo release metadata: {backfill(args.database, args.catalog)}")
    except (KeyError, OSError, ValueError, sqlite3.Error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
