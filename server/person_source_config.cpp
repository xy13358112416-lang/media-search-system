#include "person_source_config.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace person_source_config {
using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
constexpr qsizetype MaxJsonBytes = 2 * 1024 * 1024;
constexpr int MaxEntries = 10000;

Status invalid(const QString& message) { return {ErrorCode::InvalidArgument, message}; }
Status error(const QString& message) { return {ErrorCode::ServerError, message}; }
bool validText(const QString& text) {
    if (text.simplified().isEmpty() || text.size() > MaxQueryUnits) return false;
    for (auto ch : text)
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
std::optional<QUrl> canonicalFeed(const QString& text) {
    if (text.size() > 2048) return std::nullopt;
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || !url.fragment().isEmpty())
        return std::nullopt;
    return QUrl(url.toString(QUrl::FullyEncoded), QUrl::StrictMode);
}
bool onlyKeys(const QJsonObject& object, const QSet<QString>& allowed) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
bool readDatabaseInteger(const QVariant& value, qint64& result) {
    if (value.isNull()) return false;
    const auto type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::UInt &&
        type != QMetaType::LongLong && type != QMetaType::ULongLong)
        return false;
    bool ok = false;
    result = value.toLongLong(&ok);
    return ok;
}
Result<Source> decodeSource(QSqlQuery& query) {
    const auto personId = query.value(0).toString();
    const auto feedText = query.value(1).toString();
    const auto feed = canonicalFeed(feedText);
    std::optional<qint64> lastSuccess;
    qint64 stale = 0, generation = 0;
    if (!validatePersonId(personId).ok() || !feed)
        return {error(QStringLiteral("Configured person source contains invalid identity or URL.")), {}};
    if (!query.value(2).isNull()) {
        qint64 parsed = 0;
        if (!readDatabaseInteger(query.value(2), parsed) || parsed < 0 || parsed >= MaxUtcExclusive)
            return {error(QStringLiteral("Configured person source has an invalid success time.")), {}};
        lastSuccess = parsed;
    }
    if (!readDatabaseInteger(query.value(3), stale) || (stale != 0 && stale != 1) ||
        !readDatabaseInteger(query.value(4), generation) || generation < 0)
        return {error(QStringLiteral("Configured person source has invalid refresh state.")), {}};
    return {{}, Source{personId, *feed, lastSuccess, stale == 1, generation}};
}
Result<Page<Source>> readPage(QSqlDatabase& db, const QString& personId,
                              const PageRequest& page) {
    if (!db.isOpen() || db.driverName() != "QSQLITE")
        return {error(QStringLiteral("Person source database is unavailable.")), {}};
    const auto status = validate(page);
    if (!status.ok()) return {status, {}};
    if (!personId.isEmpty()) {
        const auto idStatus = validatePersonId(personId);
        if (!idStatus.ok()) return {idStatus, {}};
        QSqlQuery exists(db);
        exists.prepare("SELECT 1 FROM person_catalog WHERE person_id=?");
        exists.addBindValue(personId);
        if (!exists.exec())
            return {error(QStringLiteral("Cannot verify configured person.")), {}};
        if (!exists.next())
            return {{ErrorCode::NotFound, QStringLiteral("Configured person does not exist.")}, {}};
    }
    QSqlQuery query(db);
    if (personId.isEmpty()) {
        query.prepare("SELECT person_id,source_url,last_success_utc,stale,refresh_generation "
                      "FROM person_sources ORDER BY person_id,source_url LIMIT ? OFFSET ?");
        query.addBindValue(page.limit + 1);
        query.addBindValue(page.offset);
    } else {
        query.prepare("SELECT person_id,source_url,last_success_utc,stale,refresh_generation "
                      "FROM person_sources WHERE person_id=? "
                      "ORDER BY person_id,source_url LIMIT ? OFFSET ?");
        query.addBindValue(personId);
        query.addBindValue(page.limit + 1);
        query.addBindValue(page.offset);
    }
    if (!query.exec())
        return {error(QStringLiteral("Cannot read configured person sources.")), {}};
    Page<Source> result;
    while (query.next()) {
        if (result.items.size() == page.limit) {
            result.nextOffset = page.offset + page.limit;
            break;
        }
        const auto source = decodeSource(query);
        if (!source.status.ok() || !source.value) return {source.status, {}};
        result.items.append(*source.value);
    }
    if (query.lastError().isValid())
        return {error(QStringLiteral("Reading configured person sources failed.")), {}};
    return {{}, result};
}
class Savepoint {
public:
    explicit Savepoint(QSqlDatabase& db)
        : db_(db), name_(QStringLiteral("person_source_") +
                         QUuid::createUuid().toString(QUuid::Id128)) {}
    bool start() {
        QSqlQuery query(db_);
        active_ = query.exec("SAVEPOINT " + name_);
        return active_;
    }
    bool release() {
        QSqlQuery query(db_);
        if (!query.exec("RELEASE SAVEPOINT " + name_)) return false;
        active_ = false;
        return true;
    }
    ~Savepoint() {
        if (!active_) return;
        QSqlQuery query(db_);
        query.exec("ROLLBACK TO SAVEPOINT " + name_);
        query.exec("RELEASE SAVEPOINT " + name_);
    }
private:
    QSqlDatabase& db_;
    QString name_;
    bool active_ = false;
};
}

Result<Batch> parse(const QByteArray& json) {
    if (json.size() > MaxJsonBytes)
        return {invalid(QStringLiteral("Person source configuration exceeds 2 MiB.")), {}};
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {invalid(QStringLiteral("Person source configuration must be a JSON object.")), {}};
    const auto root = document.object();
    if (!onlyKeys(root, {"schema", "provenance", "sources"}) ||
        root.value("schema") != QJsonValue("care-person-source-config-v1") ||
        !root.value("provenance").isString() ||
        !validText(root.value("provenance").toString()) ||
        !root.value("sources").isArray())
        return {invalid(QStringLiteral("Person source configuration has an invalid root.")), {}};
    const auto rows = root.value("sources").toArray();
    if (rows.size() > MaxEntries)
        return {invalid(QStringLiteral("Person source configuration has too many entries.")), {}};
    Batch batch;
    batch.provenance = root.value("provenance").toString();
    for (const auto& value : rows) {
        if (!value.isObject())
            return {invalid(QStringLiteral("Person source entry must be an object.")), {}};
        const auto object = value.toObject();
        if (!onlyKeys(object, {"person_id", "feed_url"}) ||
            !object.value("person_id").isString() || !object.value("feed_url").isString())
            return {invalid(QStringLiteral("Person source entry has invalid fields or types.")), {}};
        const auto personId = object.value("person_id").toString();
        const auto idStatus = validatePersonId(personId);
        if (!idStatus.ok()) return {invalid(QStringLiteral("Person source person_id is invalid.")), {}};
        const auto feed = canonicalFeed(object.value("feed_url").toString());
        if (!feed)
            return {invalid(QStringLiteral("Person source feed_url must be a credential-free HTTPS URL without a fragment.")), {}};
        batch.entries.append({personId, *feed});
    }
    return {{}, batch};
}

Result<Report> registerBatch(QSqlDatabase& db, const Batch& batch) {
    if (!validText(batch.provenance) || batch.entries.size() > MaxEntries)
        return {invalid(QStringLiteral("Person source batch is invalid or too large.")), {}};
    if (!db.isOpen() || db.driverName() != "QSQLITE")
        return {error(QStringLiteral("Person source database is unavailable.")), {}};
    Savepoint savepoint(db);
    if (!savepoint.start())
        return {error(QStringLiteral("Cannot start person source registration.")), {}};
    Report report;
    QSet<QString> seen;
    for (const auto& entry : batch.entries) {
        const auto idStatus = validatePersonId(entry.personId);
        const auto feed = canonicalFeed(entry.feedUrl.toString());
        if (!idStatus.ok()) return {invalid(QStringLiteral("Person source person_id is invalid.")), {}};
        if (!feed) return {invalid(QStringLiteral("Person source feed_url is invalid.")), {}};
        const auto key = entry.personId + QChar(0x1f) + feed->toString();
        if (seen.contains(key)) { ++report.unchanged; continue; }
        seen.insert(key);

        QSqlQuery person(db);
        person.prepare("SELECT 1 FROM person_catalog WHERE person_id=?");
        person.addBindValue(entry.personId);
        if (!person.exec())
            return {error(QStringLiteral("Cannot verify person source identity.")), {}};
        if (!person.next())
            return {{ErrorCode::NotFound, QStringLiteral("Person source references an unknown person.")}, {}};

        QSqlQuery existing(db);
        existing.prepare("SELECT 1 FROM person_sources WHERE person_id=? AND source_url=?");
        existing.addBindValue(entry.personId);
        existing.addBindValue(feed->toString());
        if (!existing.exec())
            return {error(QStringLiteral("Cannot check person source configuration.")), {}};
        if (existing.next()) { ++report.unchanged; continue; }

        QSqlQuery insert(db);
        insert.prepare("INSERT INTO person_sources"
                       "(person_id,source_url,last_success_utc,stale,refresh_generation) "
                       "VALUES(?,?,NULL,0,0)");
        insert.addBindValue(entry.personId);
        insert.addBindValue(feed->toString());
        if (!insert.exec())
            return {error(QStringLiteral("Cannot register person source configuration.")), {}};
        ++report.inserted;
    }
    if (!savepoint.release())
        return {error(QStringLiteral("Cannot commit person source registration.")), {}};
    return {{}, report};
}

Result<Report> registerFile(QSqlDatabase& db, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxJsonBytes)
        return {invalid(QStringLiteral("Cannot read person source configuration, or it exceeds 2 MiB.")), {}};
    const auto batch = parse(file.readAll());
    if (!batch.status.ok() || !batch.value) return {batch.status, {}};
    return registerBatch(db, *batch.value);
}

Result<Page<Source>> listSources(QSqlDatabase& db, const PageRequest& page) {
    return readPage(db, {}, page);
}

Result<Page<Source>> listSourcesForPerson(
    QSqlDatabase& db, const QString& personId, const PageRequest& page) {
    if (personId.isEmpty())
        return {invalid(QStringLiteral("Person identifier is required.")), {}};
    return readPage(db, personId, page);
}
} // namespace person_source_config
