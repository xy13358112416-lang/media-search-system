#include "media_json.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace media_json {
QJsonObject toJson(const MediaRecord& r) {
    QJsonObject result{{"id",r.id},{"file_name",r.fileName},{"file_type",r.fileType},
            {"file_size",QString::number(r.fileSize)},{"director",r.director},
            {"actors",QJsonArray::fromStringList(r.actors)},{"view_count",QString::number(r.viewCount)},
            {"release_date",r.releaseDate.toString(Qt::ISODate)},{"awards",QJsonArray::fromStringList(r.awards)},
            {"category",r.category}};
    if (!r.releasePeriod.isEmpty())
        result.insert("release_period", r.releasePeriod);
    return result;
}
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
        for (const auto& item : value.toArray()) { if (!item.isString()) return false; dest.append(item.toString()); }
        return true;
    };
    const auto numberField = [&j](const char* key, quint64& dest) {
        const auto value = j.value(QLatin1String(key)); if (!value.isString()) return false;
        const auto text = value.toString();
        static const QRegularExpression decimal(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
        if (!decimal.match(text).hasMatch()) return false;
        bool ok = false; dest = text.toULongLong(&ok); return ok;
    };
    if (!stringField("id",r.id) || r.id.isEmpty()) return invalid("id");
    if (!stringField("file_name",r.fileName) || r.fileName.isEmpty()) return invalid("file_name");
    if (!stringField("file_type",r.fileType)) return invalid("file_type");
    if (!numberField("file_size",r.fileSize)) return invalid("file_size");
    if (!stringField("director",r.director)) return invalid("director");
    if (!listField("actors",r.actors)) return invalid("actors");
    if (!numberField("view_count",r.viewCount)) return invalid("view_count");
    QString date; if (!stringField("release_date",date)) return invalid("release_date");
    r.releaseDate = QDate::fromString(date,Qt::ISODate);
    if (!date.isEmpty() && (!r.releaseDate.isValid() || r.releaseDate.toString(Qt::ISODate) != date))
        return invalid("release_date");
    const auto period = j.value("release_period");
    if (!period.isUndefined()) {
        if (!date.isEmpty() || !period.isString() || !validReleasePeriod(period.toString()))
            return invalid("release_period");
        r.releasePeriod = period.toString();
    }
    if (!listField("awards",r.awards)) return invalid("awards");
    if (!stringField("category",r.category)) return invalid("category");
    output = r; return true;
}
}
