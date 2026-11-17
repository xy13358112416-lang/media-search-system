"""生成完全虚构的联调用数据；不会读取或修改用户的媒体文件。"""
import csv
import json
from pathlib import Path

HEADERS = ['id', 'file_name', 'file_type', 'file_size', 'creator', 'actors',
           'view_count', 'release_date', 'awards', 'category']
directors = ['林川', '李宁', '赵岚', '周远']
actor_pairs = [('陈明', '王青'), ('陈明', '李悦'), ('王青', '孙晓'),
               ('李悦', '孙晓'), ('周宁', '陈明'), ('王青', '周宁')]
categories = ['纪录片', '剧情', '音乐', '科幻']
types = ['MP4', 'MP3', 'WAV']
names = ['晨光', '远山', '城市', '海风', '旅程', '星河', '归途', '夜曲']
out = Path(__file__).parent / 'tests' / 'fixtures' / 'media_records_synthetic.csv'
out.parent.mkdir(parents=True, exist_ok=True)
with out.open('w', encoding='utf-8', newline='') as f:
    writer = csv.writer(f, lineterminator='\n')
    writer.writerow(HEADERS)
    for i in range(30):
        director = directors[i % len(directors)]
        actors = actor_pairs[i % len(actor_pairs)]
        file_type = types[i % len(types)]
        if i < 2:
            director, actors, file_type = '林川', ('陈明', '王青'), 'MP4'
        elif director == '林川' and '陈明' in actors and file_type == 'MP4':
            actors = ('王青', '孙晓')
        filename = (names[i % len(names)] if i >= 2 else '同名作品') + '.' + file_type.lower()
        writer.writerow([f'M{i+1:06d}', filename, file_type, (i + 1) * 1048576,
                         director, json.dumps(list(actors), ensure_ascii=False),
                         (i * 37) % 501, f'{2010 + i % 15:04d}-{1 + i % 12:02d}-{1 + i % 28:02d}',
                         json.dumps(['示例奖项'] if i % 4 == 0 else [], ensure_ascii=False),
                         categories[i % len(categories)]])
