#pragma once

#include "mediarecord.h"
#include <QList>
#include <QString>
#include <QStringList>

struct RecommendationOptions {
    QString preferredCategory;  // 所需类别，例如纪录片。留空时无类别偏好。
    QString preferredFileType;  // 偏好的文件扩展名，例如 MP4。忽略大小写及首尾空白，留空时无偏好。
    int limit = 5;               // 最多返回的条数。小于等于零时返回空列表。
};

// 从当前 records 中选出最多 options.limit 条记录，返回按推荐顺序排列的媒体 ID。
// 依次按匹配偏好项数降序、viewCount 降序和 ID 升序排列。
// 类别取自 MediaRecord::category，文件类型取自 MediaRecord::fileType。
QStringList recommendMedia(const QList<MediaRecord>& records,
                           const RecommendationOptions& options);
