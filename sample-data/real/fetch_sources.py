"""下载公开元数据快照；不会触碰现有媒体库和用户数据。"""
import json
import time
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'source'
USER_AGENT = 'MediaSearchCourse/1.0 (https://github.com/xy13358112416-lang/media-search-system)'

def fetch(url, name):
    req = Request(url, headers={'User-Agent': USER_AGENT, 'Accept': 'application/json'})
    with urlopen(req, timeout=35) as response:
        raw = response.read()
    json.loads(raw.decode('utf-8'))  # 不保存错误页面。
    (SOURCE / name).write_bytes(raw)
    print(name, len(raw), 'bytes', flush=True)

def main():
    SOURCE.mkdir(parents=True, exist_ok=True)
    fetch('https://api.tvmaze.com/shows?page=0', 'tvmaze_page0.json')
    for index, artist in enumerate(['Queen', 'The Beatles', '周杰倫']):
        query = urlencode({'query': 'artist:"' + artist + '"', 'fmt': 'json', 'limit': 100})
        fetch('https://musicbrainz.org/ws/2/recording/?' + query, 'musicbrainz_%d.json' % index)
        time.sleep(1.1)

if __name__ == '__main__':
    main()
