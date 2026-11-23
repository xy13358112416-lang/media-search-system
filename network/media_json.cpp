// >>> MAIN-DEFENSE | 主逻辑文件导读
// MediaRecord与JSON转换：统一网络字段和类型校验，客户端和服务端复用；JSON解析成功不代表媒体字段合法。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "media_json.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace media_json {
// >>> MAIN-DEFENSE | 函数入口｜toJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject toJson(const MediaRecord& r) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject toJson(const MediaRecord& r) {
    QJsonObject result{{"id",r.id},{"file_name",r.fileName},{"file_type",r.fileType},
            {"file_size",QString::number(r.fileSize)},{"director",r.director},
            {"actors",QJsonArray::fromStringList(r.actors)},{"view_count",QString::number(r.viewCount)},
            {"release_date",r.releaseDate.toString(Qt::ISODate)},{"awards",QJsonArray::fromStringList(r.awards)},
            {"category",r.category}};
    // >>> MAIN-DEFENSE | toJson｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!r.releasePeriod.isEmpty())
        result.insert("release_period", r.releasePeriod);
    return result;
}
// >>> MAIN-DEFENSE | 函数入口｜fromJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool fromJson(const QJsonObject& j, MediaRecord& output, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool fromJson(const QJsonObject& j, MediaRecord& output, QString& error) {
    error.clear();
    const auto invalid = [&error](const char* key) { error = QStringLiteral("无效媒体字段：") + QString::fromLatin1(key); return false; };
    MediaRecord r;
    const auto stringField = [&j](const char* key, QString& dest) {
        const auto value = j.value(QLatin1String(key)); if (!value.isString()) return false;
        dest = value.toString(); return true;
    };
    const auto listField = [&j](const char* key, QStringList& dest) {
        const auto value = j.value(QLatin1String(key)); if (!value.isArray()) return false;
        // >>> MAIN-DEFENSE | fromJson｜处理阶段 01
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& item : value.toArray()) { if (!item.isString()) return false; dest.append(item.toString()); }
        return true;
    };
    const auto numberField = [&j](const char* key, quint64& dest) {
        const auto value = j.value(QLatin1String(key)); if (!value.isString()) return false;
        const auto text = value.toString();
        static const QRegularExpression decimal(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
        // >>> MAIN-DEFENSE | fromJson｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!decimal.match(text).hasMatch()) return false;
        bool ok = false; dest = text.toULongLong(&ok); return ok;
    };
    if (!stringField("id",r.id) || r.id.isEmpty()) return invalid("id");
    // >>> MAIN-DEFENSE | fromJson｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!stringField("file_name",r.fileName) || r.fileName.isEmpty()) return invalid("file_name");
    if (!stringField("file_type",r.fileType)) return invalid("file_type");
    if (!numberField("file_size",r.fileSize)) return invalid("file_size");
    if (!stringField("director",r.director)) return invalid("director");
    // >>> MAIN-DEFENSE | fromJson｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!listField("actors",r.actors)) return invalid("actors");
    if (!numberField("view_count",r.viewCount)) return invalid("view_count");
    QString date; if (!stringField("release_date",date)) return invalid("release_date");
    r.releaseDate = QDate::fromString(date,Qt::ISODate);
    // >>> MAIN-DEFENSE | fromJson｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!date.isEmpty() && (!r.releaseDate.isValid() || r.releaseDate.toString(Qt::ISODate) != date))
        return invalid("release_date");
    const auto period = j.value("release_period");
    if (!period.isUndefined()) {
        // >>> MAIN-DEFENSE | fromJson｜处理阶段 06
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!date.isEmpty() || !period.isString() || !validReleasePeriod(period.toString()))
            return invalid("release_period");
        r.releasePeriod = period.toString();
    }
    // >>> MAIN-DEFENSE | fromJson｜处理阶段 07
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!listField("awards",r.awards)) return invalid("awards");
    if (!stringField("category",r.category)) return invalid("category");
    output = r; return true;
}
}
