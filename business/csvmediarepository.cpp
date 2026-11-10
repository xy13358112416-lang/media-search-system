#include "csvmediarepository.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <utility>

namespace {
RepositoryResult fail(const QString& message) { return {false, message}; }
const QStringList header = {"id", "file_name", "file_type", "file_size", "director", "actors",
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
        r.fileType.isEmpty() || r.fileType != r.fileType.trimmed().toUpper() ||
        r.fileType.contains('.') || !r.releaseDate.isValid()) return false;
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
    const bool legacy = columns == header.mid(0, 9);
    if (!legacy && columns != header) return fail(QStringLiteral("CSV 表头不匹配"));
    QList<MediaRecord> next; QSet<QString> ids;
    for (const auto& row : rows) {
        if (row.size() != columns.size()) return fail(QStringLiteral("CSV 列数不匹配"));
        MediaRecord r; r.id=row[0]; r.fileName=row[1]; r.fileType=row[2]; r.director=row[4];
        r.releaseDate=QDate::fromString(row[7], Qt::ISODate);
        if (!legacy) r.category=row[9];
        if (!parseNumber(row[3], r.fileSize) || !parseNumber(row[6], r.viewCount) ||
            !parseList(row[5], r.actors) || !parseList(row[8], r.awards) ||
            !validRecord(r) || r.releaseDate.toString(Qt::ISODate) != row[7] || ids.contains(r.id))
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
        QStringList values{r.id, r.fileName, r.fileType, QString::number(r.fileSize), r.director,
                           listJson(r.actors), QString::number(r.viewCount), r.releaseDate.toString(Qt::ISODate),
                           listJson(r.awards), r.category};
        for (auto& value : values) value = quoted(value);
        output += values.join(',').toUtf8() + '\n';
    }
    if (file.write(output) != output.size()) { file.cancelWriting(); return fail(QStringLiteral("写入 CSV 失败：") + file.errorString()); }
    if (!file.commit()) return fail(QStringLiteral("替换 CSV 失败：") + file.errorString());
    return {true, {}};
}

RepositoryResult CsvMediaRepository::add(const MediaRecord& record) {
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
