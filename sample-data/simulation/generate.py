"""Reproducible SYNTHETIC fixture. No accounts, credentials, DB or network access."""
import argparse
import csv
import hashlib
import io
import json
import os
import random
import re
from collections import Counter
from datetime import datetime, timedelta, timezone
from pathlib import Path

SEED = 20261115
TZ = timezone(timedelta(hours=8))  # This fixture covers modern dates, without DST.
END = datetime(2026, 11, 15, tzinfo=TZ)
START = END - timedelta(days=90)
DEFAULT_CATALOG = Path(__file__).resolve().parents[1] / "data" / "media_records.csv"
MAX_USERS = 200
MAX_EVENTS = 100000
MAX_FIXTURE_BYTES = 64 * 1024 * 1024


def read_catalog(path):
    if path.stat().st_size > MAX_FIXTURE_BYTES:
        raise ValueError("catalog exceeds 64 MiB")
    raw = path.read_bytes()
    reader = csv.DictReader(io.StringIO(raw.decode("utf-8-sig")))
    required = {"id", "file_name", "file_type", "file_size", "actors",
                "view_count", "release_date", "awards", "category"}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError("catalog is missing required columns")
    # 创作者字段新名为 creator，旧版目录为 director，二者皆可。
    if "creator" not in (reader.fieldnames or []) and "director" not in (reader.fieldnames or []):
        raise ValueError("catalog is missing creator column")
    rows = list(reader)
    if not 1 <= len(rows) <= 5000:
        raise ValueError("catalog must contain 1 to 5000 works")
    ids = set()
    for row in rows:
        key = row["id"]
        if not key or len(key.encode("utf-16-le")) // 2 > 128 or any(c.isspace() or ord(c) < 32 for c in key):
            raise ValueError("invalid catalog id")
        if key in ids:
            raise ValueError("duplicate catalog id: " + key)
        ids.add(key)
        if not row["file_name"].strip() or not row["category"].strip():
            raise ValueError("empty title/category")
        for field in ("file_size", "view_count"):
            value = row[field]
            if not value.isascii() or not value.isdecimal() or int(value) >= 2**64:
                raise ValueError("invalid unsigned catalog count")
        for field in ("actors", "awards"):
            value = json.loads(row[field])
            if not isinstance(value, list) or any(not isinstance(v, str) for v in value):
                raise ValueError("catalog list must be a JSON string array")
        if row["release_date"]:
            value = row["release_date"]
            suffix = {4: "-01-01", 7: "-01", 10: ""}.get(len(value))
            if suffix is None or not datetime.fromisoformat(value + suffix).date().isoformat().startswith(value):
                raise ValueError("invalid catalog release date")
    if not {"电影", "电视剧", "音乐"}.issubset({r["category"] for r in rows}):
        raise ValueError("demo needs movie, television and music categories")
    return raw, rows


def write_csv(path, fields, rows):
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def generate(output, catalog_path=DEFAULT_CATALOG, seed=SEED, user_count=20, end_date="2026-11-15"):
    if type(user_count) is not int or not 1 <= user_count <= MAX_USERS:
        raise ValueError("users must be an integer from 1 to 200")
    if not isinstance(end_date, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", end_date):
        raise ValueError("end-date must use YYYY-MM-DD")
    end = datetime.strptime(end_date, "%Y-%m-%d").replace(tzinfo=TZ)
    start = end - timedelta(days=90)
    if start.timestamp() < 0:
        raise ValueError("the complete 90-day window must be on or after the Unix epoch")
    raw, catalog = read_catalog(catalog_path)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError("output must be a new or empty directory")
    rng = random.Random(seed)
    categories = sorted({row["category"] for row in catalog})
    by_category = {c: [r for r in catalog if r["category"] == c] for c in categories}
    users = []
    for number in range(1, user_count + 1):
        slot = (number - 1) % 20 + 1
        old = "电影" if slot <= 5 else "电视剧" if slot <= 10 else "音乐"
        recent, scenario = old, "稳定兴趣"
        if slot in (15, 16, 17):
            old, recent = {15: ("电影", "音乐"), 16: ("音乐", "电视剧"), 17: ("电视剧", "电影")}[slot]
            scenario = "最后14天兴趣转移"
        elif slot in (18, 20):
            old = recent = ""
            scenario = "低活跃混合兴趣" if slot == 18 else "无历史新用户"
        elif slot == 19:
            old = recent = "音乐"
            scenario = "最近3天的新用户"
        users.append(dict(user_ref=f"sim_user_{number:02d}", display_name=f"模拟用户{number:02d}",
                          scenario=scenario, earlier_preference=old, recent_preference=recent))

    events = []
    for number, user in enumerate(users, 1):
        slot = (number - 1) % 20 + 1
        if slot == 20:
            continue
        for day in range(90):
            is_new = slot == 19
            if is_new and day < 87:
                continue
            probability = 1 if is_new else 0.22 if slot == 18 else 0.72
            if rng.random() > probability:
                continue
            preference = user["recent_preference"] if day >= 76 else user["earlier_preference"]
            for _ in range(rng.randint(2, 7)):
                category = preference if preference and rng.random() < 0.85 else rng.choice(categories)
                work = rng.choice(by_category[category])
                stamp = start + timedelta(days=day, hours=rng.randint(8, 22), minutes=rng.randrange(60), seconds=rng.randrange(60))
                base = dict(user_ref=user["user_ref"], occurred_at=stamp.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"))
                events.append(dict(base, action="view", media_id=work["id"], query=""))
                if rng.random() < 0.28:
                    before = stamp - timedelta(seconds=8)
                    events.append(dict(base, action="search", media_id="", query=work["file_name"],
                                       occurred_at=before.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")))
                if len(events) > MAX_EVENTS:
                    raise ValueError("fixture exceeds 100000 events")
    events.sort(key=lambda e: (e["occurred_at"], e["user_ref"], e["action"]))
    events = [dict(event_id=f"sim_event_{i:06d}", **e) for i, e in enumerate(events, 1)]
    counts = Counter((e["user_ref"], e["action"]) for e in events)
    summary = [dict(u, views=counts[u["user_ref"], "view"], searches=counts[u["user_ref"], "search"]) for u in users]

    # Offline snapshot, NOT wire payload. IDs/synthetic/eligibility must be mapped
    # by the separate server demo importer; real clients cannot assert these.
    works = []
    for i, row in enumerate(catalog, 1):
        work = dict(row, work_id=row["id"], source={"warehouse_id": "1", "item_id": str(i)}, people=[])
        if len(work["release_date"]) in (4, 7):
            work["release_period"] = work["release_date"]
            work["release_date"] = ""
        works.append(work)
    fixture = dict(schema_version=1, synthetic=True, seed=seed, now_utc=str(int(end.timestamp())),
                   start_utc=str(int(start.timestamp())), users=users,
                   works=works,
                   events=[dict(event_id=e["event_id"], user_ref=e["user_ref"], action=e["action"],
                                work_id=e["media_id"], query=e["query"], synthetic=True, public_eligible=True,
                                occurred_at_utc=str(int(datetime.fromisoformat(e["occurred_at"].replace("Z", "+00:00")).timestamp()))) for e in events])
    fixture_text = json.dumps(fixture, ensure_ascii=False, indent=2) + "\n"
    fixture_bytes = fixture_text.replace("\n", os.linesep).encode("utf-8")
    if len(fixture_bytes) > MAX_FIXTURE_BYTES:
        raise ValueError("fixture exceeds 64 MiB")
    output.mkdir(parents=True, exist_ok=True)
    (output / "catalog.csv").write_bytes(raw)
    write_csv(output / "users.csv", list(users[0]), users)
    write_csv(output / "events.csv", ["event_id", "user_ref", "action", "media_id", "query", "occurred_at"], events)
    write_csv(output / "summary.csv", list(summary[0]), summary)
    (output / "algorithm_fixture.json").write_bytes(fixture_bytes)
    manifest = dict(schema_version=1, synthetic=True, seed=seed, catalog_count=len(catalog), user_count=user_count,
                    event_count=len(events), actions=dict(Counter(e["action"] for e in events)),
                    start_inclusive=start.isoformat(), end_exclusive=end.isoformat(),
                    sha256={f: hashlib.sha256((output / f).read_bytes()).hexdigest()
                            for f in ("catalog.csv", "users.csv", "events.csv", "summary.csv", "algorithm_fixture.json")})
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--users", type=int, default=20, help="Synthetic users, from 1 to 200 (default: 20)")
    parser.add_argument("--end-date", default="2026-11-15", help="Exclusive Beijing midnight, YYYY-MM-DD")
    args = parser.parse_args()
    try:
        result = generate(args.output, args.catalog, args.seed, args.users, args.end_date)
    except (ValueError, OSError, KeyError, TypeError, OverflowError) as error:
        parser.exit(1, "fixture error: " + str(error) + "\n")
    print(json.dumps({k: result[k] for k in ("synthetic", "user_count", "event_count", "actions")}, ensure_ascii=False))
