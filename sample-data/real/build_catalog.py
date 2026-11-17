"""从固定快照生成 500 条真实作品元数据，不读取或覆盖用户运行中的媒体库。"""
import csv
import json
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
SOURCE = ROOT / 'source'
FIELDS = ['id', 'file_name', 'file_type', 'file_size', 'creator', 'actors',
          'view_count', 'release_date', 'awards', 'category']

def read_csv(path):
    with path.open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream))

def release_info(value):
    try:
        suffix = {4: '-01-01', 7: '-01', 10: ''}.get(len(value))
        if suffix is not None and date.fromisoformat(value + suffix).isoformat().startswith(value):
            return value
        return ''
    except (ValueError, TypeError):
        return ''

def main():
    records = []
    provenance = read_csv(PROJECT / 'expanded' / 'provenance.csv')
    expanded = read_csv(PROJECT / 'tests' / 'fixtures' / 'media_records_expanded_legacy.csv')
    by_id = {row['id']: row for row in expanded}
    for entry in [p for p in provenance if p['origin'] == 'UCI Movie'][:200]:
        old = by_id[entry['id']]
        key = entry['uci_key']
        title = old['file_name'][:-4] if old['file_name'].endswith('.mp4') else old['file_name']
        records.append(dict(id='uci:' + key, title=title, kind='电影', creator=old['director'],
                            people=json.loads(old['actors']), release_date=release_info(entry['source_year']),
                            release_raw=entry['source_year'],
                            genres=[old['category']] if old['category'] else [],
                            source='UCI Movie', source_id=key, source_url='https://archive.ics.uci.edu/dataset/132/movie'))

    shows = json.loads((SOURCE / 'tvmaze_page0.json').read_text(encoding='utf-8'))
    shows = [show for show in shows if show.get('type') in ('Scripted', 'Animation') and show.get('name')]
    for show in shows[:150]:
        raw = show.get('premiered') or ''
        records.append(dict(id='tvmaze:' + str(show['id']), title=show['name'], kind='电视剧', creator='',
                            people=[], release_date=release_info(raw), release_raw=raw, genres=show.get('genres') or [],
                            source='TVmaze', source_id=str(show['id']), source_url=show['url']))

    artists = ['0383dadf-2a4e-4d10-a46a-e9e041da8eb3',
               'b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d',
               'a223958d-5c56-4b2c-a30a-87e357bc121b']
    seen = set()
    for index, artist_id in enumerate(artists):
        data = json.loads((SOURCE / ('musicbrainz_%d.json' % index)).read_text(encoding='utf-8'))
        count = 0
        for item in data['recordings']:
            credits = [c for c in item.get('artist-credit', []) if isinstance(c, dict)]
            if not any(c.get('artist', {}).get('id') == artist_id for c in credits):
                continue
            key = item['id']
            if key in seen or not item.get('title'):
                continue
            seen.add(key)
            names = [c.get('name') or c.get('artist', {}).get('name', '') for c in credits]
            names = [name for name in names if name]
            raw = item.get('first-release-date') or ''
            records.append(dict(id='musicbrainz:' + key, title=item['title'], kind='音乐', creator=(names[0] if names else ''),
                                people=names, release_date=release_info(raw), release_raw=raw, genres=[],
                                source='MusicBrainz', source_id=key,
                                source_url='https://musicbrainz.org/recording/' + key))
            count += 1
            if count == 50:
                break
        if count != 50:
            raise ValueError('not enough verified recordings for artist ' + artist_id)

    if len(records) != 500 or len({r['id'] for r in records}) != 500:
        raise ValueError('expected 500 unique real-source records')
    with (ROOT / 'metadata.jsonl').open('w', encoding='utf-8', newline='\n') as stream:
        for row in records:
            stream.write(json.dumps(row, ensure_ascii=False, separators=(',', ':')) + '\n')
    with (PROJECT / 'data' / 'media_records.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator='\n')
        writer.writeheader()
        for row in records:
            writer.writerow(dict(id=row['id'], file_name=row['title'], file_type='', file_size=0,
                                 creator=row['creator'], actors=json.dumps(row['people'], ensure_ascii=False),
                                 view_count=0, release_date=row['release_date'], awards='[]', category=row['kind']))
    print('Wrote 500 records: 200 movies, 150 TV shows, 150 recordings')

if __name__ == '__main__':
    main()
