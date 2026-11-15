"""Reproducible SYNTHETIC fixture. No accounts, credentials, DB or network access."""
import argparse
import csv
import hashlib
import io
import json
import random
from collections import Counter
from datetime import datetime, timedelta, timezone
from pathlib import Path

SEED = 20261115
TZ = timezone(timedelta(hours=8))  # This fixture covers modern dates, without DST.
END = datetime(2026, 11, 15, tzinfo=TZ)
START = END - timedelta(days=90)
DEFAULT_CATALOG = Path(__file__).resolve().parents[1] / "data" / "media_records.csv"


def read_catalog(path):
    raw = path.read_bytes()
    reader = csv.DictReader(io.StringIO(raw.decode("utf-8-sig")))
    required = {"id", "file_name", "file_type", "file_size", "director", "actors",
                "view_count", "release_date", "awards", "category"}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError("catalog is missing required columns")
    rows = list(reader)
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
            datetime.strptime(row["release_date"], "%Y-%m-%d")
    if not {"电影", "电视剧", "音乐"}.issubset({r["category"] for r in rows}):
        raise ValueError("demo needs movie, television and music categories")
    return raw, rows


def write_csv(path, fields, rows):
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def generate(output, catalog_path=DEFAULT_CATALOG, seed=SEED):
    raw, catalog = read_catalog(catalog_path)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError("output must be a new or empty directory")
    rng = random.Random(seed)
    categories = sorted({row["category"] for row in catalog})
    by_category = {c: [r for r in catalog if r["category"] == c] for c in categories}
    users = []
    for number in range(1, 21):
        old = "电影" if number <= 5 else "电视剧" if number <= 10 else "音乐"
        recent, scenario = old, "稳定兴趣"
        if number in (15, 16, 17):
            old, recent = {15: ("电影", "音乐"), 16: ("音乐", "电视剧"), 17: ("电视剧", "电影")}[number]
            scenario = "最后14天兴趣转移"
        elif number in (18, 20):
            old = recent = ""
            scenario = "低活跃混合兴趣" if number == 18 else "无历史新用户"
        elif number == 19:
            old = recent = "音乐"
            scenario = "最近3天的新用户"
        users.append(dict(user_ref=f"sim_user_{number:02d}", display_name=f"模拟用户{number:02d}",
                          scenario=scenario, earlier_preference=old, recent_preference=recent))

    events = []
    for user in users:
        if user["user_ref"] == "sim_user_20":
            continue
        for day in range(90):
            is_new = user["user_ref"] == "sim_user_19"
            if is_new and day < 87:
                continue
            probability = 1 if is_new else 0.22 if user["user_ref"] == "sim_user_18" else 0.72
            if rng.random() > probability:
                continue
            preference = user["recent_preference"] if day >= 76 else user["earlier_preference"]
            for _ in range(rng.randint(2, 7)):
                category = preference if preference and rng.random() < 0.85 else rng.choice(categories)
                work = rng.choice(by_category[category])
                stamp = START + timedelta(days=day, hours=rng.randint(8, 22), minutes=rng.randrange(60), seconds=rng.randrange(60))
                base = dict(user_ref=user["user_ref"], occurred_at=stamp.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"))
                events.append(dict(base, action="view", media_id=work["id"], query=""))
                if rng.random() < 0.28:
                    before = stamp - timedelta(seconds=8)
                    events.append(dict(base, action="search", media_id="", query=work["file_name"],
                                       occurred_at=before.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")))
    events.sort(key=lambda e: (e["occurred_at"], e["user_ref"], e["action"]))
    events = [dict(event_id=f"sim_event_{i:06d}", **e) for i, e in enumerate(events, 1)]
    summary = [dict(u, views=sum(e["action"] == "view" and e["user_ref"] == u["user_ref"] for e in events),
                    searches=sum(e["action"] == "search" and e["user_ref"] == u["user_ref"] for e in events)) for u in users]

    # Offline snapshot, NOT wire payload. IDs/synthetic/eligibility must be mapped
    # by the separate server demo importer; real clients cannot assert these.
    fixture = dict(schema_version=1, synthetic=True, seed=seed, now_utc=str(int(END.timestamp())),
                   start_utc=str(int(START.timestamp())), users=users,
                   works=[dict(r, work_id=r["id"], source={"warehouse_id": "1", "item_id": str(i)},
                               people=[]) for i, r in enumerate(catalog, 1)],
                   events=[dict(event_id=e["event_id"], user_ref=e["user_ref"], action=e["action"],
                                work_id=e["media_id"], query=e["query"], synthetic=True, public_eligible=True,
                                occurred_at_utc=str(int(datetime.fromisoformat(e["occurred_at"].replace("Z", "+00:00")).timestamp()))) for e in events])
    output.mkdir(parents=True, exist_ok=True)
    (output / "catalog.csv").write_bytes(raw)
    write_csv(output / "users.csv", list(users[0]), users)
    write_csv(output / "events.csv", ["event_id", "user_ref", "action", "media_id", "query", "occurred_at"], events)
    write_csv(output / "summary.csv", list(summary[0]), summary)
    (output / "algorithm_fixture.json").write_text(json.dumps(fixture, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    manifest = dict(schema_version=1, synthetic=True, seed=seed, catalog_count=len(catalog), user_count=20,
                    event_count=len(events), actions=dict(Counter(e["action"] for e in events)),
                    start_inclusive=START.isoformat(), end_exclusive=END.isoformat(),
                    sha256={f: hashlib.sha256((output / f).read_bytes()).hexdigest()
                            for f in ("catalog.csv", "users.csv", "events.csv", "summary.csv", "algorithm_fixture.json")})
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--seed", type=int, default=SEED)
    args = parser.parse_args()
    try:
        result = generate(args.output, args.catalog, args.seed)
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, "fixture error: " + str(error) + "\n")
    print(json.dumps({k: result[k] for k in ("synthetic", "user_count", "event_count", "actions")}, ensure_ascii=False))
