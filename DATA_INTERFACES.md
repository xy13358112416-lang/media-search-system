# 媒体数据接口草案

此提交仅定义共享数据结构与存储接口，不包含 CSV 或增删实现。`MediaRecord` 可直接用于检索模块的样例数据。

字段类型：ID、文件名、类型、导演为 QString；演员和奖项为 QStringList；大小和浏览次数为无符号 64 位整数；日期为 QDate。文件类型采用不带点的大写扩展名，如 `MP4`。文件大小单位为字节。记录 ID 非空、唯一、创建后不变。

主程序与检索模块对接顺序：

1. `load(records)` 成功后调用 `rebuild(records)`。
2. `add(record)` 成功后调用 `addRecord(record)`，然后通知界面刷新。
3. `remove(id)` 成功后调用 `removeRecord(id)`，然后通知界面刷新。

操作失败时不调用检索接口，也不发送成功刷新通知。检索模块接口由组长实现；主程序负责调用及刷新信号。若检索更新失败或抛异常，主程序应重新加载已保存的数据并重建索引，恢复成功前不应显示旧索引作为最新结果。

CSV 具体编码、多值字段转义及原子替换实现将在后续实现中明确；当前接口不承诺已经支持存储。

样例：

```cpp
MediaRecord record;
record.id = "M000001";
record.fileName = QString::fromUtf8("示例.mp4");
record.fileType = "MP4";
record.fileSize = 1024;
record.director = QString::fromUtf8("导演甲");
record.actors = QStringList{QString::fromUtf8("演员甲"), QString::fromUtf8("演员乙")};
record.viewCount = 5;
record.releaseDate = QDate(2024, 1, 20);
record.awards = QStringList{QString::fromUtf8("示例奖项")};
QList<MediaRecord> records{record};
// search.rebuild(records);
```
