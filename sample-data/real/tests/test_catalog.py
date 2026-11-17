import csv
import json
import unittest
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = ROOT.parent


def csv_rows(path):
    with path.open(encoding="utf-8", newline="") as source:
        return list(csv.DictReader(source))


class CatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = csv_rows(SAMPLES / "data" / "media_records.csv")
        cls.metadata = [
            json.loads(line)
            for line in (ROOT / "metadata.jsonl").read_text(encoding="utf-8").splitlines()
        ]

    def test_catalog_fields_and_unknown_values(self):
        self.assertEqual(len(self.records), 500)
        self.assertEqual(len({r["id"] for r in self.records}), 500)
        self.assertEqual(Counter(r["category"] for r in self.records),
                         {"电影": 200, "电视剧": 150, "音乐": 150})
        self.assertEqual(len(self.metadata), 500)
        for row, item in zip(self.records, self.metadata):
            self.assertEqual((row["id"], row["file_name"], row["category"]),
                             (item["id"], item["title"], item["kind"]))
            self.assertTrue(item["source_url"].startswith("https://"))
            self.assertEqual(json.loads(row["actors"]), item["people"])
            self.assertEqual(row["director"], item["creator"])
            self.assertEqual(row["release_date"], item["release_date"])
            self.assertEqual((row["file_type"], row["file_size"], row["view_count"],
                              json.loads(row["awards"])), ("", "0", "0", []))
            if row["release_date"]:
                self.assertEqual(row["release_date"], item["release_raw"])
                self.assertIn(len(row["release_date"]), (4, 7, 10))
        self.assertEqual(sum(bool(row["release_date"]) for row in self.records), 474)

    def test_source_ids_and_titles_match_snapshots(self):
        provenance = {r["uci_key"]: r for r in csv_rows(SAMPLES / "expanded" / "provenance.csv")
                      if r["origin"] == "UCI Movie"}
        legacy = {r["id"]: r for r in csv_rows(
            SAMPLES / "tests" / "fixtures" / "media_records_expanded_legacy.csv")}
        shows = {str(r["id"]): r for r in json.loads(
            (ROOT / "source" / "tvmaze_page0.json").read_text(encoding="utf-8"))}
        artist_ids = [
            "0383dadf-2a4e-4d10-a46a-e9e041da8eb3",
            "b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d",
            "a223958d-5c56-4b2c-a30a-87e357bc121b",
        ]
        recordings = {}
        for index, artist_id in enumerate(artist_ids):
            data = json.loads((ROOT / "source" / f"musicbrainz_{index}.json").read_text(encoding="utf-8"))
            for record in data["recordings"]:
                credits = record.get("artist-credit", [])
                if any(isinstance(c, dict) and c.get("artist", {}).get("id") == artist_id
                       for c in credits):
                    recordings[record["id"]] = record
        for item in self.metadata:
            key = item["source_id"]
            if item["source"] == "UCI Movie":
                original = legacy[provenance[key]["id"]]
                self.assertEqual(item["title"], original["file_name"].removesuffix(".mp4"))
                self.assertEqual(item["release_raw"], provenance[key]["source_year"])
            elif item["source"] == "TVmaze":
                self.assertEqual(item["title"], shows[key]["name"])
                self.assertIn(shows[key]["type"], ("Scripted", "Animation"))
            elif item["source"] == "MusicBrainz":
                self.assertEqual(item["title"], recordings[key]["title"])
            else:
                self.fail("Unexpected source " + item["source"])


if __name__ == "__main__":
    unittest.main()
