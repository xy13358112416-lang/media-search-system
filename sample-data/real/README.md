# 500 条作品元数据的来源

本批样例是公开来源记录的摘录，生成于 2026-11-14，未逐条人工核实。它不包含媒体文件，不能据此断定用户拥有或能播放作品。

| 类别 | 数量 | 来源及选取 |
|---|---:|---|
| 电影 | 200 | [UCI Movie](https://archive.ics.uci.edu/dataset/132/movie)，从已有的不完整历史快照片段里取前 200 条可匹配记录。数据是旧影片，导演名可能是简称。来源只给年份时不伪造完整日期。数据集标注 CC BY 4.0。 |
| 电视剧 | 150 | [TVmaze API](https://www.tvmaze.com/api)，从 shows 第 0 页选择有名称的 Scripted 或 Animation 条目。源链接保存在逐条元数据中；作品类型遵循来源分类，并非人工逐条判断。TVmaze 要求署名与链接，使用需遵循其 CC BY-SA 条款。 |
| 音乐 | 150 | [MusicBrainz](https://musicbrainz.org/doc/MusicBrainz_API)，根据艺术家的 MusicBrainz ID 各选 50 条录音：Queen、The Beatles、周杰倫。同一首歌的不同录音版本可能各占一条。 |

这不是随机抽样：电影较老，电视剧集中在 API 首页，音乐仅来自三位艺人，不应据此声称覆盖均衡或计算真实流行度。MusicBrainz 的核心元数据通常为 CC0，其他数据可能适用不同许可，参见 [MusicBrainz 数据许可](https://musicbrainz.org/doc/MusicBrainz_Database/Download)。TVmaze 样例需保留来源及链接；若后续分发，核对相关使用条款。

`metadata.jsonl` 为每条作品记录来源 ID、来源 URL、原始日期和来源类别；当前程序尚未展示这份侧车文件。CSV 的 `category` 是电影、电视剧或音乐，原来源的具体题材不参与当前推荐。空类型、0 大小和空奖项都是未知占位，初始浏览量 0 只表示系统内尚无浏览数据。仅在来源给出合法的年、月、日时填写完整发行日期。

`source/` 是本次下载快照；`fetch_sources.py` 可重新下载（可能改变记录，需联网），`build_catalog.py` 用当前快照和历史 UCI 夹具生成默认 CSV 与元数据，运行会覆盖这两个仓库文件，不会修改运行时媒体库。音乐候选通过来源的艺术家 ID 筛选。数据核对：`python -m unittest discover -s sample-data/real/tests -v`。
