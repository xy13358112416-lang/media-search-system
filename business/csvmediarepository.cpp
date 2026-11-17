#include "csvmediarepository.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <utility>
#include <limits>

namespace {
RepositoryResult fail(const QString& message) { return {false, message}; }
const QStringList header = {"id", "file_name", "file_type", "file_size", "creator", "actors",
                            "view_count", "release_date", "awards", "category"};

// 将 CSV 解析为行；引号、逗号、换行和 CRLF 均按 CSV 规则处理。
bool parseCsv(const QString& text, QList<QStringList>& rows) {
    QString field; QStringList row;
    enum State { Start, Plain, Quoted, Closed } state = Start;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (state == Quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text.at(i+1) == '"') { field += c; ++i; }
                else state = Closed;
            } else field += c;
            continue;
        }
        if (c == ',') { row.append(field); field.clear(); state = Start; continue; }
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text.at(i+1) == '\n') ++i;
            row.append(field); field.clear(); rows.append(row); row.clear(); state = Start; continue;
        }
        if (state == Closed) return false;
        if (c == '"') { if (state != Start) return false; state = Quoted; }
        else { field += c; state = Plain; }
    }
    if (state == Quoted) return false;
    if (state != Start || !row.isEmpty()) { row.append(field); rows.append(row); }
    return true;
}

bool parseList(const QString& value, QStringList& result) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(value.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) return false;
    for (const auto& item : doc.array()) {
        if (!item.isString() || item.toString().trimmed().isEmpty()) return false;
        result.append(item.toString());
    }
    return true;
}
bool parseNumber(const QString& value, quint64& result) {
    if (value.isEmpty()) return false;
    for (const auto c : value) if (c < '0' || c > '9') return false;
    bool ok = false; result = value.toULongLong(&ok); return ok;
}
bool validRecord(const MediaRecord& r) {
    if (r.id.isEmpty() || r.id != r.id.trimmed() || r.fileName.trimmed().isEmpty() ||
        (!r.fileType.isEmpty() && !CsvMediaRepository::supportedFileTypes().contains(r.fileType))) return false;
    if (!r.releasePeriod.isEmpty() && (!r.releaseDate.isNull() || !validReleasePeriod(r.releasePeriod)))
        return false;
    if (!r.releaseDate.isValid()) {
        if (!r.releaseDate.isNull()) return false;
    } else {
        // ISO 日期必须能以四位年格式往返，否则写入后无法重新加载。
        const QString dateText = r.releaseDate.toString(Qt::ISODate);
        if (dateText.size() != 10 || QDate::fromString(dateText, Qt::ISODate) != r.releaseDate) return false;
    }
    for (const auto& s : r.actors) if (s.trimmed().isEmpty()) return false;
    for (const auto& s : r.awards) if (s.trimmed().isEmpty()) return false;
    return true;
}
QString quoted(QString value) {
    value.replace('"', "\"\"");
    return '"' + value + '"';
}
QString listJson(const QStringList& values) {
    QJsonArray a; for (const auto& value : values) a.append(value);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}
}

CsvMediaRepository::CsvMediaRepository(QString path) : path_(std::move(path)) {}

QStringList CsvMediaRepository::supportedFileTypes() {
    return {"AAC", "AVI", "FLAC", "FLV", "M4A", "MKV", "MOV", "MP3", "MP4", "OGG", "WAV", "WEBM", "WMV"};
}

RepositoryResult CsvMediaRepository::validateNewRecord(const MediaRecord& record) {
    if (!supportedFileTypes().contains(record.fileType))
        return fail(QStringLiteral("不支持的文件类型，请选择支持的音视频扩展名"));
    MediaRecord candidate = record;
    candidate.id = QStringLiteral("M000001"); // 新增时 ID 由业务层分配。
    if (!validRecord(candidate)) return fail(QStringLiteral("记录字段无效"));
    return {true, {}};
}

RepositoryResult CsvMediaRepository::ensureSequence(const QList<MediaRecord>& records) {
    return updateSequence(records, false, nullptr);
}

RepositoryResult CsvMediaRepository::reserveNextId(QString& id) {
    QList<MediaRecord> records;
    auto result = load(records);
    if (!result.success) return result;
    return updateSequence(records, true, &id);
}

RepositoryResult CsvMediaRepository::updateSequence(const QList<MediaRecord>& records, bool reserve, QString* id) {
    const QString statePath = path_ + QStringLiteral(".id-sequence");
    QLockFile lock(statePath + QStringLiteral(".lock"));
    if (!lock.tryLock(1000)) return fail(QStringLiteral("编号状态正被其他进程使用"));
    quint64 maximum = 0;
    const QRegularExpression pattern(QStringLiteral("^M([0-9]{6,})$"));
    for (const auto& record : records) {
        const auto match = pattern.match(record.id);
        if (!match.hasMatch()) continue; // 现有 UUID 和其他旧 ID 原样保留。
        bool ok = false;
        const quint64 value = match.captured(1).toULongLong(&ok);
        if (!ok) return fail(QStringLiteral("已有编号超出支持范围"));
        maximum = qMax(maximum, value);
    }
    QFile current(statePath);
    const bool exists = current.exists();
    quint64 previous = 0;
    if (exists) {
        if (!current.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取编号状态：") + current.errorString());
        const QByteArray bytes = current.readAll();
        if (current.error() != QFileDevice::NoError) return fail(QStringLiteral("读取编号状态失败"));
        const QString text = QString::fromLatin1(bytes).trimmed();
        if (!parseNumber(text, previous)) return fail(QStringLiteral("编号状态损坏，拒绝分配编号"));
        current.close();
    }
    maximum = qMax(maximum, previous);
    if (reserve) {
        if (maximum == std::numeric_limits<quint64>::max()) return fail(QStringLiteral("编号已用尽"));
        ++maximum;
    }
    if (!exists || maximum != previous) {
        QSaveFile state(statePath);
        if (!state.open(QIODevice::WriteOnly)) return fail(QStringLiteral("无法保存编号状态：") + state.errorString());
        const QByteArray bytes = QByteArray::number(maximum) + '\n';
        if (state.write(bytes) != bytes.size() || !state.commit()) return fail(QStringLiteral("保存编号状态失败：") + state.errorString());
    }
    if (reserve && id) *id = QStringLiteral("M%1").arg(maximum, 6, 10, QLatin1Char('0'));
    return {true, {}};
}

RepositoryResult CsvMediaRepository::load(QList<MediaRecord>& records) {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法打开 CSV：") + file.errorString());
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(QStringLiteral("读取 CSV 失败：") + file.errorString());
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes) return fail(QStringLiteral("CSV 不是有效的 UTF-8"));
    QList<QStringList> rows;
    if (!parseCsv(text, rows) || rows.isEmpty()) return fail(QStringLiteral("CSV 格式错误"));
    const auto columns = rows.takeFirst();
    // 兼容旧版 CSV 的 director 表头；现统一为 creator（创作者）。
    QStringList normalized = columns;
    if (normalized.size() > 4 && normalized.at(4) == QStringLiteral("director")) normalized[4] = QStringLiteral("creator");
    const bool legacy = normalized == header.mid(0, 9);
    if (!legacy && normalized != header) return fail(QStringLiteral("CSV 表头不匹配"));
    QList<MediaRecord> next; QSet<QString> ids;
    for (const auto& row : rows) {
        if (row.size() != columns.size()) return fail(QStringLiteral("CSV 列数不匹配"));
        MediaRecord r; r.id=row[0]; r.fileName=row[1]; r.fileType=row[2]; r.creator=row[4];
        if (!legacy) r.category=row[9];
        if (!parseNumber(row[3], r.fileSize) || !parseNumber(row[6], r.viewCount) ||
            !parseList(row[5], r.actors) || !parseList(row[8], r.awards) ||
            !parseMediaRelease(row[7], r) || !validRecord(r) || ids.contains(r.id))
            return fail(QStringLiteral("CSV 包含无效记录或重复 ID：") + r.id);
        ids.insert(r.id); next.append(r);
    }
    records = std::move(next); return {true, {}};
}

RepositoryResult CsvMediaRepository::save(const QList<MediaRecord>& records) {
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("无法保存 CSV：") + file.errorString());
    QByteArray output = header.join(',').toUtf8() + '\n';
    for (const auto& r : records) {
        QStringList values{r.id, r.fileName, r.fileType, QString::number(r.fileSize), r.creator,
                           listJson(r.actors), QString::number(r.viewCount), mediaReleaseText(r),
                           listJson(r.awards), r.category};
        for (auto& value : values) value = quoted(value);
        output += values.join(',').toUtf8() + '\n';
    }
    if (file.write(output) != output.size()) { file.cancelWriting(); return fail(QStringLiteral("写入 CSV 失败：") + file.errorString()); }
    if (!file.commit()) return fail(QStringLiteral("替换 CSV 失败：") + file.errorString());
    return {true, {}};
}

RepositoryResult CsvMediaRepository::add(const MediaRecord& record) {
    if (!supportedFileTypes().contains(record.fileType))
        return fail(QStringLiteral("不支持的文件类型，请选择支持的音视频扩展名"));
    if (!validRecord(record)) return fail(QStringLiteral("记录字段无效"));
    QList<MediaRecord> all; auto result = load(all); if (!result.success) return result;
    for (const auto& r : all) if (r.id == record.id) return fail(QStringLiteral("ID 已存在"));
    all.append(record); return save(all);
}

RepositoryResult CsvMediaRepository::remove(const QString& id) {
    QList<MediaRecord> all; auto result = load(all); if (!result.success) return result;
    for (qsizetype i=0; i<all.size(); ++i) if (all[i].id == id) { all.removeAt(i); return save(all); }
    return fail(QStringLiteral("未找到该 ID"));
}
