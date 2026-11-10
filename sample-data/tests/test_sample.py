import csv
import json
import unittest
from collections import Counter
from datetime import date
from pathlib import Path

DATA = Path(__file__).resolve().parents[1] / 'data' / 'media_records.csv'
COLUMNS = ['id', 'file_name', 'file_type', 'file_size', 'director', 'actors',
           'view_count', 'release_date', 'awards', 'category']

class SampleDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with DATA.open(encoding='utf-8', newline='') as f:
            reader = csv.DictReader(f, strict=True)
            cls.columns = reader.fieldnames
            cls.rows = list(reader)

    def test_columns_and_unique_ids(self):
        self.assertEqual(self.columns, COLUMNS)
        self.assertTrue(20 <= len(self.rows) <= 50)
        ids = [r['id'] for r in self.rows]
        self.assertTrue(all(ids))
        self.assertEqual(len(ids), len(set(ids)))
        self.assertLess(len(set(r['file_name'] for r in self.rows)), len(self.rows))

    def test_values_and_list_roundtrip(self):
        for r in self.rows:
            self.assertNotIn(None, r)
            self.assertIn(r['file_type'], ('MP4', 'MP3', 'WAV'))
            self.assertGreaterEqual(int(r['file_size']), 0)
            self.assertGreaterEqual(int(r['view_count']), 0)
            self.assertEqual(date.fromisoformat(r['release_date']).isoformat(), r['release_date'])
            for key in ('actors', 'awards'):
                values = json.loads(r[key])
                self.assertIsInstance(values, list)
                self.assertTrue(all(isinstance(v, str) and v for v in values))
            self.assertTrue(r['director'] and r['category'])
        self.assertTrue(any(len(json.loads(r['actors'])) > 1 for r in self.rows))

    def test_query_intersections(self):
        directors = Counter(r['director'] for r in self.rows)
        actors = Counter(a for r in self.rows for a in json.loads(r['actors']))
        self.assertGreater(max(directors.values()), 1)
        self.assertGreater(max(actors.values()), 1)
        result = [r for r in self.rows if r['director'] == '林川'
                  and '陈明' in json.loads(r['actors']) and r['file_type'] == 'MP4']
        self.assertEqual([r['id'] for r in result], ['M000001', 'M000002'])

if __name__ == '__main__':
    unittest.main()
