"""Build a reviewed Alan Alda mapping for a database created by media_demo_import."""

import argparse
import json
import sqlite3
from contextlib import closing
from pathlib import Path


PERSON_ID = "person:afi:8664"
IDENTITY = "https://catalog.afi.com/Catalog/PersonDetails/8664"
FILMS = {
    "uci:AAl6": "https://catalog.afi.com/Film/56615-THE-FOUR-SEASONS",
    "uci:AAl10": "https://catalog.afi.com/Catalog/moviedetails/57486",
    "uci:AAl20": "https://catalog.afi.com/Film/58431-BETSYS-WEDDING",
}


def build_mapping(database):
    with closing(sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True)) as db:
        if not db.execute("SELECT 1 FROM demo_import_metadata WHERE id = 1").fetchone():
            raise ValueError("Expected a database created by media_demo_import.")
        credits = []
        for media_id, credit_source in FILMS.items():
            source = db.execute(
                "SELECT m.work_id, m.warehouse_id, m.item_id FROM demo_media_map m "
                "JOIN work_sources s ON s.work_id = m.work_id AND s.warehouse_id = m.warehouse_id "
                "AND s.item_id = m.item_id WHERE m.media_id = ?", (media_id,)
            ).fetchone()
            if source is None:
                raise ValueError(f"The demo database is missing {media_id}.")
            work_id, warehouse, item = source
            for role in ("actor", "director"):
                credits.append({
                    "person_id": PERSON_ID, "role": role, "credit_source": credit_source,
                    "resource": {"work_id": work_id, "source": {
                        "warehouse_id": str(warehouse), "item_id": str(item)
                    }},
                })
    return {
        "schema": "care-person-import-v1",
        "synthetic": False,
        "provenance": "AFI Catalog person and film credits checked against the fixed UCI media IDs.",
        "people": [{"person_id": PERSON_ID, "name": "Alan Alda", "identity_source": IDENTITY, "aliases": []}],
        "credits": credits,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        mapping = build_mapping(args.database)
        with args.output.open("x", encoding="utf-8") as output:
            json.dump(mapping, output, ensure_ascii=False, indent=2)
            output.write("\n")
    except (OSError, ValueError, sqlite3.Error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
