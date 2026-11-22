"""Preview demo cleanup, or create a NEW repaired DB. Never edit the source DB.

MusicBrainz artist/credit IDs and aliases come from the checked-in raw snapshots.
Unverified TV director placeholders become unknown; history is never rewritten.
"""
import argparse
import json
import re
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ARTISTS = ["0383dadf-2a4e-4d10-a46a-e9e041da8eb3", "b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d", "a223958d-5c56-4b2c-a30a-87e357bc121b"]


def plan(db):
    if not db.execute("SELECT 1 FROM demo_import_metadata WHERE id=1 AND kind='media-demo-v1'").fetchone():
        raise ValueError("Not a synthetic demo database")
    mapped = db.execute("SELECT m.media_id,m.work_id,m.warehouse_id,m.item_id,i.media FROM demo_media_map m "
                        "JOIN warehouse_items i ON i.id=m.item_id AND i.warehouse_id=m.warehouse_id "
                        "JOIN work_sources s ON s.work_id=m.work_id AND s.warehouse_id=m.warehouse_id AND s.item_id=m.item_id").fetchall()
    clean = []
    for ident, work, warehouse, item, payload in mapped:
        media = json.loads(payload)
        if ident.startswith("tvmaze:") and media.get("id") == ident and re.fullmatch(r"演示导演\s*\d+", media.get("director", "")):
            media["director"] = ""
            clean.append((json.dumps(media, ensure_ascii=False).encode(), item, warehouse))
    records = {row[0]: row for row in mapped}
    people, credits = {}, set()
    for index, artist_id in enumerate(ARTISTS):
        snapshot = json.loads((ROOT / f"sample-data/real/source/musicbrainz_{index}.json").read_text(encoding="utf-8"))
        for recording in snapshot["recordings"]:
            source = records.get("musicbrainz:" + recording["id"])
            if source is None:
                continue
            for credit in recording.get("artist-credit", []):
                if not isinstance(credit, dict):
                    continue
                artist = credit.get("artist", {})
                # Only the explicitly selected stable artist identity is imported.
                if artist.get("id") != artist_id:
                    continue
                person_id = "person:musicbrainz:" + artist_id
                name = artist["name"]
                aliases = {a["name"] for a in artist.get("aliases", []) if a.get("name")}
                aliases.add(credit.get("name") or name)
                current = people.setdefault(person_id, [name, "https://musicbrainz.org/artist/" + artist_id, set()])
                if current[0] != name:
                    raise ValueError("Conflicting artist names in source")
                current[2].update(aliases)
                credits.add((person_id, source[1], "musician", "https://musicbrainz.org/recording/" + recording["id"]))
    return clean, people, sorted(credits)


def repair(source, output=None):
    source = Path(source).resolve()
    with sqlite3.connect(source.as_uri() + "?mode=ro", uri=True) as original:
        clean, people, credits = plan(original)
        report = {"placeholder_directors_to_clear": len(clean), "source_artists": len(people), "source_credits": len(credits), "mode": "preview"}
        if output is None:
            return report
        output = Path(output).resolve()
        if output == source or output.exists():
            raise ValueError("Output must be a new path distinct from source")
        # Reserve the new path before SQLite opens it, so an existing file is never replaced.
        with output.open("xb"):
            pass
        db = sqlite3.connect(output)
        try:
            original.backup(db)
            before = db.execute("SELECT COUNT(*) FROM behavior_events").fetchone()[0]
            with db:
                for person_id, (name, evidence, aliases) in people.items():
                    existing = db.execute("SELECT display_name,identity_source FROM person_catalog WHERE person_id=?", (person_id,)).fetchone()
                    if existing and existing != (name, evidence):
                        raise ValueError("Existing person identity conflicts with source")
                    db.execute("INSERT OR IGNORE INTO person_catalog VALUES(?,?,?)", (person_id, name, evidence))
                    for alias in sorted(aliases):
                        db.execute("INSERT OR IGNORE INTO person_aliases(person_id,alias) VALUES(?,?)", (person_id, alias))
                for person_id, work, role, evidence in credits:
                    existing = db.execute("SELECT credit_source FROM person_credits WHERE person_id=? AND work_id=? AND role=?", (person_id, work, role)).fetchone()
                    if existing and existing[0] != evidence:
                        raise ValueError("Existing credit evidence conflicts with source")
                    db.execute("INSERT OR IGNORE INTO person_credits(person_id,work_id,role,credit_source) VALUES(?,?,?,?)", (person_id, work, role, evidence))
                db.executemany("UPDATE warehouse_items SET media=? WHERE id=? AND warehouse_id=?", clean)
            assert before == db.execute("SELECT COUNT(*) FROM behavior_events").fetchone()[0]
            report.update(mode="new repaired copy", output=str(output), history_events_preserved=before)
        except Exception:
            db.close()
            output.unlink(missing_ok=True)
            raise
        finally:
            db.close()
        return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("--output", type=Path, help="Create a repaired copy; omit to preview")
    args = parser.parse_args()
    print(json.dumps(repair(args.database, args.output), ensure_ascii=False, indent=2))
