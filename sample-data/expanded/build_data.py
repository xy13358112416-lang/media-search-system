"""将本地 UCI 截断快照中可匹配的影片转为单独的演示 CSV。"""
import csv
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from lxml import html

ROOT = Path(__file__).resolve().parent
BASE = ROOT.parent / 'data' / 'media_records.csv'
MAIN = ROOT / 'source' / 'main.html'
CASTS = ROOT / 'source' / 'casts.html'
OUTPUT = ROOT / 'media_records_expanded.csv'
PROVENANCE = ROOT / 'provenance.csv'
HEADER = ['id', 'file_name', 'file_type', 'file_size', 'director', 'actors',
          'view_count', 'release_date', 'awards', 'category']
CAT = {'Dram':'剧情', 'Comd':'喜剧', 'Horr':'恐怖', 'ScFi':'科幻', 'Romt':'爱情',
       'Faml':'家庭', 'Hist':'历史', 'Musc':'音乐', 'Susp':'悬疑', 'Noir':'黑色电影',
       'BioP':'传记', 'Disa':'灾难'}

def clean(s):
    return re.sub(r'\s+', ' ', s).strip()

def rows(path):
    doc = html.fromstring(path.read_bytes())
    return [[clean(c.text_content()) for c in row.xpath('./td')] for row in doc.xpath('//tr[td]')][:-1]

casts = defaultdict(list)
for row in rows(CASTS):
    if len(row) < 3 or not row[1].startswith('T:'):
        continue
    key, title, actor = row[0], clean(row[1][2:]), row[2]
    # 原始库中 s a 等代表引用其他身份，不能当成演员姓名；动物角色也不计入人名列表。
    if (re.fullmatch(r'[A-Za-z][A-Za-z0-9._-]*', key) and actor and len(actor) < 100
            and actor.casefold() not in {'s a', 'sa', 'unknown', '?', 'unknown actor'}
            and not (len(row) > 3 and row[3].casefold() == r'\ani')):
        if actor not in casts[(key, title.casefold())]:
            casts[(key, title.casefold())].append(actor)

candidates = []
for row in rows(MAIN):
    if len(row) < 8 or not row[1].startswith('T:') or not row[3].startswith('D:'):
        continue
    key, title, year, director = row[0], clean(row[1][2:]), row[2], clean(row[3][2:])
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9._-]*', key) or not re.fullmatch(r'(18|19)\d{2}', year):
        continue
    actors = casts.get((key, title.casefold()), [])
    if not title or not director or len(actors) < 2:
        continue
    candidates.append((key, title, year, director, actors[:5], CAT.get(row[7], '')))

counts = Counter(c[0] for c in candidates)
candidates = sorted((c for c in candidates if counts[c[0]] == 1), key=lambda c: c[0])[:500]
if len(candidates) < 100:
    raise RuntimeError(f'可匹配的完整影片不足：{len(candidates)}')

with BASE.open(encoding='utf-8', newline='') as f:
    reader = csv.DictReader(f)
    if reader.fieldnames != HEADER:
        raise RuntimeError('原始样例的列顺序发生变化')
    baseline = list(reader)
if len(baseline) != 30 or len({r['id'] for r in baseline}) != 30:
    raise RuntimeError('原始样例不符合预期')

with OUTPUT.open('w', encoding='utf-8', newline='') as data, PROVENANCE.open('w', encoding='utf-8', newline='') as provenance:
    writer = csv.DictWriter(data, fieldnames=HEADER, lineterminator='\n')
    source = csv.writer(provenance, lineterminator='\n')
    writer.writeheader(); writer.writerows(baseline)
    source.writerow(['id', 'origin', 'uci_key', 'source_year', 'date_precision', 'field_notes'])
    for row in baseline:
        source.writerow([row['id'], 'synthetic', '', '', 'synthetic', 'original 30 synthetic records'])
    for index, (key, title, year, director, actors, category) in enumerate(candidates, start=31):
        ident = f'M{index:06d}'
        writer.writerow({'id':ident, 'file_name':title + '.mp4', 'file_type':'MP4', 'file_size':'0',
                         'director':director, 'actors':json.dumps(actors, ensure_ascii=False), 'view_count':'0',
                         'release_date':year + '-01-01', 'awards':'[]', 'category':category})
        source.writerow([ident, 'UCI Movie', key, year, 'year only', 'MP4/name/date day/size/views are placeholders'])

hashes = {p.name: {'bytes':p.stat().st_size, 'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
                   'complete':False} for p in (MAIN, CASTS)}
(ROOT / 'source_snapshot.json').write_text(json.dumps(hashes, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('original_synthetic=', len(baseline), 'uci_records=', len(candidates), 'total=', len(baseline)+len(candidates))
