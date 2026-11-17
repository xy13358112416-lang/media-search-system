# 默认媒体资料与测试数据

`data/media_records.csv` 是 500 条来自公开资料的作品元数据：200 部电影、150 部电视剧、150 条音乐录音。电影来自 UCI Movie，电视剧来自 TVmaze，音乐来自 MusicBrainz。来源说明、选择方法和每条记录的来源链接见 [real/README.md](real/README.md) 与 `real/metadata.jsonl`。

这些记录只包含作品元数据。`file_name` 保存作品名称，`file_type` 留空、`file_size` 为 0 表示实际格式和大小未知。`view_count` 为本系统初始浏览数。空演员和奖项列表表示来源未提供或尚未采集。`release_date` 按来源精度保存年份、年月或完整日期，未知时留空。

CSV 使用 UTF-8，`actors`、`awards` 是 JSON 字符串数组。ID 带来源前缀，旧用户自己的媒体库和编号状态文件不会因构建自动覆盖。应用已有的运行时 CSV 也不会自动替换；如需体验新样例，请在新目录首次运行或用 `--csv` 指向新样例的可写副本。

原来的 30 条虚构记录和旧 530 条混合数据仅保留在 `tests/fixtures/`，供既有自动化测试使用，不作为默认媒体库。`generate_sample.py` 只会重建测试夹具。
