#pragma once
#include "mediarecord.h"
#include <QJsonObject>

namespace media_json {
// 转换十个字段；两个 quint64 使用十进制字符串保证无损。
QJsonObject toJson(const MediaRecord& record);
// 校验失败时返回 false、填写 error，不修改 record。
bool fromJson(const QJsonObject& json, MediaRecord& record, QString& error);
}
