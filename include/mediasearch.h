#pragma once

#include "mediarecord.h"
#include <QList>
#include <QString>
#include <QStringList>

// 各项去除首尾空白后，非空条件按 AND 组合。导演和演员按完整姓名匹配。
struct SearchQuery {
    QString director;  // 导演姓名，留空时不限导演。
    QString actor;     // 一名演员的完整姓名，留空时不限演员。
    QString fileType;  // 文件扩展名，例如 MP4。忽略大小写，留空时不限类型。
};

class MediaSearch {
public:
    // 用 records 中的全部记录替换现有索引。主程序在加载成功后调用。
    void rebuild(const QList<MediaRecord>& records);

    // 将已保存的 record 加入索引。record.id 必须非空且未被占用。
    void addRecord(const MediaRecord& record);

    // 从索引移除已删除的记录。id 是主程序成功删除的媒体 ID。
    void removeRecord(const QString& id);

    // 返回与 query 匹配的媒体 ID，按 ID 升序排列，无匹配时返回空列表。
    // 全部条件留空时返回所有 ID。列表长度即命中数量。
    QStringList search(const SearchQuery& query) const;
};
