import importlib.util
import json
from contextlib import closing
import sqlite3
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("repair", Path(__file__).with_name("repair_demo_profiles.py"))
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)


class RepairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "source.sqlite"
        with closing(sqlite3.connect(self.source)) as db, db:
            db.executescript("""
                CREATE TABLE demo_import_metadata(id INTEGER,kind TEXT);
                INSERT INTO demo_import_metadata VALUES(1,'media-demo-v1');
                CREATE TABLE demo_media_map(media_id TEXT,work_id TEXT,warehouse_id INTEGER,item_id INTEGER);
                CREATE TABLE warehouse_items(id INTEGER,warehouse_id INTEGER,media BLOB);
                CREATE TABLE work_sources(work_id TEXT,warehouse_id INTEGER,item_id INTEGER);
                CREATE TABLE behavior_events(id INTEGER);
                INSERT INTO behavior_events VALUES(1);
                CREATE TABLE person_catalog(person_id TEXT PRIMARY KEY,display_name TEXT,identity_source TEXT);
                CREATE TABLE person_aliases(person_id TEXT,alias TEXT,PRIMARY KEY(person_id,alias));
                CREATE TABLE person_credits(person_id TEXT,work_id TEXT,role TEXT,credit_source TEXT,PRIMARY KEY(person_id,work_id,role));
            """)
            self.original = {"id":"tvmaze:1","file_name":"Real title","director":"演示导演 05"}
            db.execute("INSERT INTO demo_media_map VALUES('tvmaze:1','work:1',1,1)")
            db.execute("INSERT INTO work_sources VALUES('work:1',1,1)")
            db.execute("INSERT INTO warehouse_items VALUES(1,1,?)", (json.dumps(self.original),))

    def test_preview_does_not_change_source(self):
        before = self.source.read_bytes()
        self.assertEqual(repair.repair(self.source)["placeholder_directors_to_clear"], 1)
        self.assertEqual(self.source.read_bytes(), before)

    def test_new_copy_clears_only_placeholder_and_preserves_history(self):
        output = Path(self.temp.name) / "repaired.sqlite"
        repair.repair(self.source, output)
        with closing(sqlite3.connect(output)) as db, db:
            media = json.loads(db.execute("SELECT media FROM warehouse_items").fetchone()[0])
            self.assertEqual(media["director"], "")
            self.assertEqual(media["file_name"], "Real title")
            self.assertEqual(db.execute("SELECT COUNT(*) FROM behavior_events").fetchone()[0], 1)
        self.assertEqual(repair.repair(output)["placeholder_directors_to_clear"], 0)
        with closing(sqlite3.connect(self.source)) as db, db:
            self.assertEqual(json.loads(db.execute("SELECT media FROM warehouse_items").fetchone()[0]), self.original)

    def test_existing_output_is_not_overwritten(self):
        output = Path(self.temp.name) / "keep.txt"
        output.write_text("keep")
        with self.assertRaises(ValueError):
            repair.repair(self.source, output)
        self.assertEqual(output.read_text(), "keep")

    def test_user_edit_is_preserved(self):
        with closing(sqlite3.connect(self.source)) as db, db:
            media = dict(self.original, director="User corrected name")
            db.execute("UPDATE warehouse_items SET media=?", (json.dumps(media),))
        self.assertEqual(repair.repair(self.source)["placeholder_directors_to_clear"], 0)

    def test_source_path_cannot_be_output(self):
        with self.assertRaises(ValueError):
            repair.repair(self.source, self.source)


if __name__ == "__main__":
    unittest.main()
