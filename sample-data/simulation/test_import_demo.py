"""Exercise the demo importer against disposable SQLite databases only."""
import hashlib
from contextlib import closing
import json
import sqlite3
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

import generate

IMPORTER = sys.argv.pop(1)
SERVER = sys.argv.pop(1)


class DemoImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="media-demo-test-")
        cls.root = Path(cls.tmp.name)
        cls.fixture_dir = cls.root / "fixture"
        cls.manifest = generate.generate(cls.fixture_dir)
        cls.fixture = cls.fixture_dir / "algorithm_fixture.json"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_import(self, db, fixture=None):
        return subprocess.run([IMPORTER, "--demo-db", str(db), "--fixture", str(fixture or self.fixture),
                               "--demo-password-stdin"], input="synthetic-test-password\n", text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90,
                              encoding="utf-8", errors="replace")

    def test_repeat_import_maps_and_marks_every_event(self):
        self.assertEqual(self.manifest["catalog_count"], 500)
        self.assertEqual(self.manifest["event_count"], 6420)
        db = self.root / "mapped-demo.sqlite"
        first = self.run_import(db)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_import(db)
        self.assertEqual(second.returncode, 0, second.stderr)
        with closing(sqlite3.connect(db)) as connection:
            def scalar(query):
                return connection.execute(query).fetchone()[0]
            self.assertEqual(scalar("SELECT COUNT(*) FROM demo_user_map"), 20)
            self.assertEqual(scalar("SELECT COUNT(*) FROM demo_media_map"), self.manifest["catalog_count"])
            self.assertEqual(scalar("SELECT COUNT(*) FROM behavior_events"), self.manifest["event_count"])
            self.assertEqual(scalar("SELECT COUNT(*) FROM behavior_events WHERE action='view'"), 5033)
            self.assertEqual(scalar("SELECT COUNT(*) FROM behavior_events WHERE action='search'"), 1387)
            self.assertEqual(scalar("SELECT COUNT(*) FROM behavior_events WHERE synthetic != 1"), 0)
            self.assertEqual(scalar("SELECT COUNT(*) FROM behavior_events e JOIN demo_user_map u ON u.user_id=e.user_id WHERE u.user_ref='sim_user_20'"), 0)
            self.assertEqual(scalar("SELECT COUNT(*) FROM behavior_events e LEFT JOIN work_sources w ON w.warehouse_id=e.warehouse_id AND w.item_id=e.item_id AND w.work_id=e.work_id WHERE e.action='view' AND w.work_id IS NULL"), 0)
            self.assertEqual(scalar("SELECT COUNT(DISTINCT password_hash) FROM network_users"), 20)
        self.assert_demo_login(db)
        changed = json.loads(self.fixture.read_text(encoding="utf-8"))
        changed["events"][0]["event_id"] = "modified-fixture"
        altered = self.root / "altered.json"
        altered.write_text(json.dumps(changed, ensure_ascii=False), encoding="utf-8")
        self.assertNotEqual(self.run_import(db, altered).returncode, 0)
        with closing(sqlite3.connect(db)) as connection:
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM behavior_events").fetchone()[0], self.manifest["event_count"])
            connection.execute("UPDATE behavior_events SET query='changed' WHERE rowid=(SELECT rowid FROM behavior_events WHERE action='search' LIMIT 1)")
            connection.commit()
        before = hashlib.sha256(db.read_bytes()).digest()
        self.assertNotEqual(self.run_import(db).returncode, 0)
        self.assertEqual(hashlib.sha256(db.read_bytes()).digest(), before)

    def test_existing_unmarked_database_is_not_modified(self):
        db = self.root / "existing.sqlite"
        with closing(sqlite3.connect(db)) as connection:
            connection.execute("CREATE TABLE private_data(value TEXT)")
            connection.execute("INSERT INTO private_data VALUES ('preserve')")
            connection.commit()
        before = hashlib.sha256(db.read_bytes()).digest()
        result = self.run_import(db)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(hashlib.sha256(db.read_bytes()).digest(), before)
        with closing(sqlite3.connect(db)) as connection:
            self.assertEqual(connection.execute("SELECT value FROM private_data").fetchone()[0], "preserve")

    def test_invalid_reference_is_rejected_before_database_creation(self):
        db = self.root / "invalid-demo.sqlite"
        data = json.loads(self.fixture.read_text(encoding="utf-8"))
        next(e for e in data["events"] if e["action"] == "view")["work_id"] = "missing:work"
        path = self.root / "invalid-fixture.json"
        path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        self.assertNotEqual(self.run_import(db, path).returncode, 0)
        self.assertFalse(db.exists())

    def assert_demo_login(self, db):
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        server = subprocess.Popen([SERVER, "--db", str(db), "--port", str(port)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    client = socket.create_connection(("127.0.0.1", port), timeout=1)
                    break
                except OSError:
                    if server.poll() is not None or time.monotonic() >= deadline:
                        self.fail("demo server did not start")
                    time.sleep(0.05)
            with client:
                client.settimeout(5)
                request = {"type": "login", "req_id": "1", "payload": {"username": "sim_user_01", "password": "synthetic-test-password"}}
                data = json.dumps(request).encode("utf-8")
                client.sendall(struct.pack(">I", len(data)) + data)
                def read_exact(n):
                    chunks = b""
                    while len(chunks) < n:
                        block = client.recv(n - len(chunks))
                        if not block:
                            self.fail("server closed before complete response")
                        chunks += block
                    return chunks
                size = struct.unpack(">I", read_exact(4))[0]
                response = json.loads(read_exact(size).decode("utf-8"))
                self.assertEqual(response.get("type"), "login_resp")
                self.assertTrue(response["payload"]["token"])
        finally:
            server.terminate()
            server.wait(timeout=10)


if __name__ == "__main__":
    unittest.main()
