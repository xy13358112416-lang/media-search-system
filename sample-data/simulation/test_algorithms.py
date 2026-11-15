"""Consume generated data with the actual C++ algorithms, without a server."""
import json
import subprocess
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path
from test_generate import HERE

DEMO = sys.argv.pop(1) if len(sys.argv) > 1 else "missing-care-simulation-demo"


class AlgorithmIntegrationTests(unittest.TestCase):
    def test_twenty_users_use_actual_events_not_scenario_labels(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / "fixture"
            subprocess.run([sys.executable, str(HERE / "generate.py"), "--output", str(directory)],
                           check=True, capture_output=True)
            source = directory / "algorithm_fixture.json"
            fixture = json.loads(source.read_text(encoding="utf-8"))
            # Deliberately remove all narrative labels. They must never be features.
            for user in fixture["users"]:
                for field in ("scenario", "earlier_preference", "recent_preference", "display_name"):
                    user.pop(field, None)
            source.write_text(json.dumps(fixture), encoding="utf-8")
            completed = subprocess.run([DEMO, str(source)], capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode(errors="replace"))
            report = json.loads(completed.stdout)
            self.assertTrue(report["synthetic"])
            self.assertEqual(report["user_count"], 20)
            self.assertEqual(report["event_count"], len(fixture["events"]))
            self.assertEqual(len(report["users"]), 20)
            views = Counter(e["user_ref"] for e in fixture["events"] if e["action"] == "view")
            searches = Counter(e["user_ref"] for e in fixture["events"] if e["action"] == "search")
            for user in report["users"]:
                name = user["user_ref"]
                self.assertEqual(int(user["footprint"]["views"]), views[name])
                self.assertEqual(int(user["footprint"]["searches"]), searches[name])
                self.assertEqual(user["footprint"]["day_count"], 90)
                self.assertEqual(len({r["work_id"] for r in user["temporal"]}), len(user["temporal"]))
                self.assertTrue(all(r["reason"] for r in user["temporal"]))
                if name in ("sim_user_15", "sim_user_16", "sim_user_17"):
                    old, recent = {"sim_user_15": ("电影", "音乐"), "sim_user_16": ("音乐", "电视剧"),
                                   "sim_user_17": ("电视剧", "电影")}[name]
                    self.assertEqual(user["cumulative_interest"][0]["category"], old)
                    self.assertEqual(user["recent_interest_only"][0]["category"], recent)
                    self.assertIn(recent, [r["category"] for r in user["temporal"]])
                    # Default mixed scoring need not overturn a large long-term
                    # majority in exactly fourteen days. Verify that distinction.
                    self.assertEqual(user["temporal"][0]["category"],
                                     old if name == "sim_user_17" else recent)
                if name == "sim_user_20":
                    self.assertEqual(int(user["footprint"]["views"]), 0)
                    self.assertTrue(user["temporal"])
            for item in report["trending"]:
                self.assertGreaterEqual(int(item["distinct_users"]), 3)
            self.assertTrue(report["trending"])
            self.assertTrue(report["trending_synthetic"])


if __name__ == "__main__":
    unittest.main()
