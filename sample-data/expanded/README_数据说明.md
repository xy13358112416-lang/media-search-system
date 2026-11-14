# 历史测试数据来源

旧的 530 条混合数据已移至 `../tests/fixtures/media_records_expanded_legacy.csv`，仅供回归测试使用，其中 30 条是虚构记录；它不是默认媒体库。其余 500 条从 UCI Movie 的不完整源文件片段中提取，带有虚构的文件扩展名和日期占位值，不能将这些值当作真实信息。

UCI Movie（https://archive.ics.uci.edu/dataset/132/movie，CC BY 4.0）为旧影片资料；源文件不完整，可能有缺失和原始错误，未逐条人工核实。`source_snapshot.json` 记录片段的哈希；`provenance.csv` 记录来源编号与年份。`build_data.py` 使用 `lxml`，重新运行会覆盖测试夹具和来源表。新的默认数据只从这些条目提取来源提供的元数据，不使用占位的扩展名或完整日期。
