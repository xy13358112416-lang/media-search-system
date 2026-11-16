"""Offline fixture contract tests; no database, network or real accounts."""
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from collections import Counter
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
CATALOG = HERE.parent / "data" / "media_records.csv"


class FixtureTests(unittest.TestCase):
    def run_generator(self, output, *args):
        return subprocess.run([sys.executable, str(HERE / "generate.py"),
                               "--output", str(output), *args], capture_output=True)

    def test_reproducible_linked_histories_and_interest_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / "a", Path(temp) / "b"
            result = self.run_generator(a)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
            self.assertEqual(self.run_generator(b).returncode, 0)
            for file in a.iterdir():
                self.assertEqual(file.read_bytes(), (b / file.name).read_bytes(), file.name)
            manifest = json.loads((a / "manifest.json").read_text(encoding="utf-8"))
            self.assertTrue(manifest["synthetic"])
            self.assertEqual(manifest["user_count"], 20)
            fixture = json.loads((a / "algorithm_fixture.json").read_text(encoding="utf-8"))
            users = {u["user_ref"]: u for u in fixture["users"]}
            works = {w["work_id"]: w for w in fixture["works"]}
            self.assertEqual(len(works), 500)
            events = fixture["events"]
            self.assertEqual(len(events), manifest["event_count"])
            self.assertEqual(len({e["event_id"] for e in events}), len(events))
            self.assertEqual(events, sorted(events, key=lambda e: (int(e["occurred_at_utc"]), e["user_ref"], e["action"])))
            cutoff = int(fixture["now_utc"]) - 14 * 86400
            for e in events:
                self.assertIn(e["user_ref"], users)
                self.assertTrue(e["synthetic"])
                self.assertTrue(e["public_eligible"])
                self.assertLessEqual(int(fixture["start_utc"]), int(e["occurred_at_utc"]))
                self.assertLess(int(e["occurred_at_utc"]), int(fixture["now_utc"]))
                if e["action"] == "view":
                    self.assertIn(e["work_id"], works)
                    self.assertFalse(e["query"])
                else:
                    self.assertEqual(e["action"], "search")
                    self.assertFalse(e["work_id"])
                    self.assertTrue(e["query"])
            for user in ("sim_user_15", "sim_user_16", "sim_user_17"):
                for recent in (False, True):
                    counts = Counter(works[e["work_id"]]["category"] for e in events
                                     if e["user_ref"] == user and e["action"] == "view"
                                     and (int(e["occurred_at_utc"]) >= cutoff) == recent)
                    self.assertEqual(counts.most_common(1)[0][0],
                                     users[user]["recent_preference" if recent else "earlier_preference"])
            self.assertFalse(any(e["user_ref"] == "sim_user_20" for e in events))
            self.assertTrue(all(not w["people"] for w in works.values()))

    def test_seed_changes_events_not_catalog_and_no_silent_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / "a", Path(temp) / "b"
            self.assertEqual(self.run_generator(a).returncode, 0)
            self.assertEqual(self.run_generator(b, "--seed", "7").returncode, 0)
            self.assertEqual((a / "catalog.csv").read_bytes(), (b / "catalog.csv").read_bytes())
            self.assertNotEqual((a / "events.csv").read_bytes(), (b / "events.csv").read_bytes())
            self.assertNotEqual(self.run_generator(a).returncode, 0)

    def test_bad_catalog_rejected_before_writing_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            bad, out = Path(temp) / "bad.csv", Path(temp) / "out"
            with CATALOG.open(encoding="utf-8-sig", newline="") as stream:
                rows = list(csv.DictReader(stream))
            rows[1]["id"] = rows[0]["id"]
            with bad.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=rows[0])
                writer.writeheader()
                writer.writerows(rows)
            result = self.run_generator(out, "--catalog", str(bad))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate", result.stderr.decode(errors="replace").lower())
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
