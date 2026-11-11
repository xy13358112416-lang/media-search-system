#pragma once

#include "mediarecord.h"
#include <QList>
#include <QString>
#include <QStringList>

struct RecommendationOptions {
    QString preferredCategory;  // 所需类别，例如纪录片。去除首尾空白后精确匹配，留空时无偏好。
    QString preferredFileType;  // 偏好的文件扩展名，例如 MP4。忽略大小写及首尾空白，留空时无偏好。
    int limit = 5;               // 最多返回的条数。小于等于零时返回空列表。
};

// 从当前 records 中选出最多 options.limit 条记录，返回按推荐顺序排列的媒体 ID。
// 类别匹配加 2 分，文件类型匹配加 1 分，热度为 viewCount 除以全表最大浏览次数。
// 最大浏览次数为零时热度为零。同分时按 ID 升序。
QStringList recommendMedia(const QList<MediaRecord>& records,
                           const RecommendationOptions& options);
