"""Offline fixture contract tests; no database, network or real accounts."""
import csv
import hashlib
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
            self.assertEqual(manifest["event_count"], 6420)
            # JSON text writing follows the platform newline convention. Compare
            # the original fixture content without making this test Windows-only.
            normalized = (a / "algorithm_fixture.json").read_bytes().replace(b"\r\n", b"\n")
            self.assertEqual(hashlib.sha256(normalized).hexdigest(),
                             "f9ab74b6c2ab4900d9dc131c400908c1f149abddcbb0806db80969fcb063b481")
            self.assertEqual(manifest["sha256"]["events.csv"],
                             "eb813c5b7d4e8848b04561c3513d607aeaf0266be0edbfc2f8b4df4fc1c08c92")
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

    def test_200_users_reproducible_cohorts_and_recent_history(self):
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / "a", Path(temp) / "b"
            args = ("--users", "200", "--end-date", "2026-11-18")
            result = self.run_generator(a, *args)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
            self.assertEqual(self.run_generator(b, *args).returncode, 0)
            for path in a.iterdir():
                self.assertEqual(path.read_bytes(), (b / path.name).read_bytes(), path.name)
            fixture = json.loads((a / "algorithm_fixture.json").read_text(encoding="utf-8"))
            manifest = json.loads((a / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["user_count"], 200)
            self.assertEqual(manifest["end_exclusive"], "2026-11-18T00:00:00+08:00")
            self.assertLessEqual(manifest["event_count"], 100000)
            users = {u["user_ref"]: u for u in fixture["users"]}
            self.assertEqual(list(users), [f"sim_user_{i:02d}" for i in range(1, 201)])
            works = {w["work_id"]: w for w in fixture["works"]}
            by_user = {ref: [] for ref in users}
            event_ids = set()
            start, end = int(fixture["start_utc"]), int(fixture["now_utc"])
            self.assertEqual(end - start, 90 * 86400)
            for event in fixture["events"]:
                self.assertIn(event["user_ref"], users)
                self.assertNotIn(event["event_id"], event_ids)
                event_ids.add(event["event_id"])
                self.assertLessEqual(start, int(event["occurred_at_utc"]))
                self.assertLess(int(event["occurred_at_utc"]), end)
                self.assertTrue(event["synthetic"] and event["public_eligible"])
                if event["action"] == "view":
                    self.assertIn(event["work_id"], works)
                    self.assertFalse(event["query"])
                else:
                    self.assertEqual(event["action"], "search")
                    self.assertFalse(event["work_id"])
                    self.assertTrue(event["query"])
                by_user[event["user_ref"]].append(event)
            for number in range(1, 201):
                ref = f"sim_user_{number:02d}"
                slot = (number - 1) % 20 + 1
                profile = users[f"sim_user_{slot:02d}"]
                for field in ("scenario", "earlier_preference", "recent_preference"):
                    self.assertEqual(users[ref][field], profile[field])
                history = by_user[ref]
                if slot == 20:
                    self.assertFalse(history)
                    continue
                self.assertTrue(history)
                if slot == 19:
                    self.assertTrue(all(int(e["occurred_at_utc"]) >= end - 3 * 86400 for e in history))
                elif slot == 18:
                    self.assertLess(len(history), len(by_user[f"sim_user_{number - 17:02d}"]))
                else:
                    for recent in (False, True):
                        counts = Counter(works[e["work_id"]]["category"] for e in history
                                         if e["action"] == "view"
                                         and (int(e["occurred_at_utc"]) >= end - 14 * 86400) == recent)
                        self.assertEqual(counts.most_common(1)[0][0],
                                         users[ref]["recent_preference" if recent else "earlier_preference"])
            # Cohorts share preferences, not duplicate event histories.
            signatures = {tuple((e["occurred_at_utc"], e["work_id"], e["query"]) for e in history)
                          for history in by_user.values() if history}
            self.assertEqual(len(signatures), 190)
            with (a / "summary.csv").open(encoding="utf-8-sig", newline="") as stream:
                for row in csv.DictReader(stream):
                    counts = Counter(e["action"] for e in by_user[row["user_ref"]])
                    self.assertEqual(int(row["views"]), counts["view"])
                    self.assertEqual(int(row["searches"]), counts["search"])
            self.assertTrue(all(not w["people"] for w in works.values()))

    def test_user_and_date_bounds_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "out"
            for args in (("--users", "0"), ("--users", "201"), ("--users", "-1"),
                         ("--end-date", "2026-02-30"), ("--end-date", "2026-1-1"),
                         ("--end-date", "1970-01-01"), ("--end-date", "2026-11-18T00:00:00")):
                with self.subTest(args=args):
                    self.assertNotEqual(self.run_generator(out, *args).returncode, 0)
                    self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
