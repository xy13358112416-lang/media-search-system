#include "warehouse_server.h"
#include "media_json.h"
#include "behavior_contract.h"
#include "care_footprint.h"
#include "person_schema.h"
#include "person_store_sqlite.h"
#include "care_trending.h"
#include "care_temporal.h"
#include "person_snapshot_json.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUuid>
#include <memory>
#include <sodium.h>

using namespace warehouse_contract;

namespace {

QString userKey(const QString& name)
{
    return name.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded()
        .normalized(QString::NormalizationForm_C);
}

bool readUtc(const QJsonValue& value, qint64& result)
{
    if (!value.isString()) return false;
    static const QRegularExpression format(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
    const auto text = value.toString();
    bool ok = false; result = text.toLongLong(&ok);
    return ok && format.match(text).hasMatch();
}
bool readRange(const QJsonValue& value, care_contract::TimeRange& range)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    return readUtc(o.value("begin_utc"), range.beginUtc) && readUtc(o.value("end_utc"), range.endUtc) &&
           care_contract::validate(range).ok();
}
QJsonObject rangeJson(const care_contract::TimeRange& range)
{
    return {{"begin_utc", QString::number(range.beginUtc)}, {"end_utc", QString::number(range.endUtc)}};
}
bool readPreference(const QJsonValue& value, recommendation_contract::PreferenceSelection& preference)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (!object.value("mode").isString() || !object.value("value").isString()) return false;
    const auto mode = object.value("mode").toString();
    if (mode == "auto") preference.mode = recommendation_contract::PreferenceMode::Auto;
    else if (mode == "any") preference.mode = recommendation_contract::PreferenceMode::Any;
    else if (mode == "explicit") preference.mode = recommendation_contract::PreferenceMode::Explicit;
    else return false;
    preference.value = object.value("value").toString();
    return true;
}
QJsonObject workJson(const care_contract::ResourceSummary& work)
{
    QJsonArray people;
    for (const auto& person : work.people) {
        QJsonArray roles;
        for (const auto role : person.roles)
            roles.append(role == care_contract::PersonRole::Actor ? "actor" : role == care_contract::PersonRole::Director ? "director" : "musician");
        people.append(QJsonObject{{"person_id", person.personId}, {"name", person.name}, {"roles", roles}, {"followed", person.followed}});
    }
    return {{"resource", QJsonObject{{"work_id", work.resource.workId}, {"source", QJsonObject{
        {"warehouse_id", QString::number(work.resource.source.warehouseId)}, {"item_id", QString::number(work.resource.source.itemId)}}}}},
        {"media", media_json::toJson(work.media)}, {"people", people}};
}
QJsonObject periodJson(const care_contract::PeriodFootprint& period)
{
    QJsonArray categories, people, days;
    for (const auto& item : period.categories)
        categories.append(QJsonObject{{"key", item.key}, {"label", item.label}, {"count", QString::number(item.count)}});
    for (const auto& item : period.people) {
        const QString role = item.role == care_contract::PersonRole::Actor ? "actor" :
                             item.role == care_contract::PersonRole::Director ? "director" : "musician";
        people.append(QJsonObject{{"person_id", item.personId}, {"name", item.name}, {"role", role}, {"count", QString::number(item.count)}});
    }
    for (const auto& item : period.days)
        days.append(QJsonObject{{"local_date", item.localDate}, {"views", QString::number(item.views)}, {"searches", QString::number(item.searches)}});
    QJsonObject result{{"range", rangeJson(period.range)}, {"views", QString::number(period.views)},
        {"searches", QString::number(period.searches)}, {"unique_works", QString::number(period.uniqueWorks)},
        {"active_days", QString::number(period.activeDays)}, {"categories", categories}, {"people", people}, {"days", days}};
    if (period.coverageStartUtc) result.insert("coverage_start_utc", QString::number(*period.coverageStartUtc));
    return result;
}

qint64 readId(const QJsonValue& value)
{
    const QString text = value.toString();
    static const QRegularExpression format(QStringLiteral("\\A[1-9][0-9]*\\z"));
    return format.match(text).hasMatch() ? text.toLongLong() : 0;
}
bool readResource(const QJsonValue& value, care_contract::ResourceRef& result)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject(); const auto source = object.value("source");
    if (!object.value("work_id").isString() || !source.isObject()) return false;
    result = {object.value("work_id").toString(), {readId(source.toObject().value("warehouse_id")), readId(source.toObject().value("item_id"))}};
    return care_contract::validate(result).ok();
}

QJsonObject summaryJson(const WarehouseSummary& warehouse)
{
    return {{"warehouse_id", QString::number(warehouse.warehouseId)}, {"name", warehouse.name},
            {"owner_id", QString::number(warehouse.ownerId)},
            {"visibility", warehouse.visibility == Visibility::Public ? "public" : "private"}};
}

QJsonObject itemJson(const WarehouseItem& item)
{
    return {{"item_id", QString::number(item.itemId)}, {"media", media_json::toJson(item.media)}};
}

WarehouseSummary readSummary(const QSqlQuery& query)
{
    return {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(),
            query.value(3).toBool() ? Visibility::Public : Visibility::Private};
}

struct SnapshotBudget {
    int items = 0;
    qint64 bytes = 0;
};

std::optional<QList<WarehouseItem>> listItems(const QSqlDatabase& db, qint64 warehouseId,
                                              SnapshotBudget* budget = nullptr)
{
    QSqlQuery query(db);
    query.prepare("SELECT id,media FROM warehouse_items WHERE warehouse_id=? ORDER BY id");
    query.addBindValue(warehouseId);
    if (!query.exec())
        return std::nullopt;
    QList<WarehouseItem> items;
    while (query.next()) {
        const auto snapshot = query.value(1).toByteArray();
        if (budget) {
            ++budget->items;
            budget->bytes += snapshot.size();
            if (budget->items > 1000 || budget->bytes > warehouse_protocol::MaxFrameSize)
                return std::nullopt;
        }
        MediaRecord media;
        QString error;
        if (!media_json::fromJson(QJsonDocument::fromJson(snapshot).object(), media, error))
            return std::nullopt;
        items.append({query.value(0).toLongLong(), media});
    }
    return items;
}

} // namespace

WarehouseServer::WarehouseServer(llm_recommendation::Config config)
    : recommender_(config), trendSummarizer_(config), sceneRecommender_(std::move(config)), dbName_(QUuid::createUuid().toString())
{
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = nextPendingConnection()) {
            auto session = std::make_shared<Session>();
            socket->setReadBufferSize(64 * 1024);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, session] {
                readRequests(socket, session);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, session, socket] {
                cancelPending(*session, socket, false);
                session->token.clear();
                session->userId = 0;
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

WarehouseServer::~WarehouseServer()
{
    close();
    // Finish accepted connections while both async clients and the database still live.
    // disconnected cancels their pending jobs before callbacks can outlive this server.
    for (auto* socket : findChildren<QTcpSocket*>()) socket->abort();
    personContextLoader_ = {};
    personStore_.reset();
    db_ = {};
    QSqlDatabase::removeDatabase(dbName_);
}

bool WarehouseServer::openDatabase(const QString& path, QString& error)
{
    error.clear();
    if (db_.isValid() || path.trimmed().isEmpty() || sodium_init() < 0) {
        error = QStringLiteral("无法初始化仓库数据库。");
        return false;
    }
    db_ = QSqlDatabase::addDatabase("QSQLITE", dbName_);
    db_.setDatabaseName(path);
    QSqlQuery query(db_);
    if (!db_.open() || !query.exec("PRAGMA foreign_keys=ON") || !db_.transaction()) {
        error = QStringLiteral("无法打开仓库 SQLite 数据库。");
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS network_users "
                    "(id INTEGER PRIMARY KEY, username TEXT NOT NULL, username_key TEXT NOT NULL UNIQUE, "
                    "password_hash TEXT NOT NULL)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS warehouses "
                    "(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                    "owner_id INTEGER NOT NULL REFERENCES network_users(id), "
                    "is_public INTEGER NOT NULL CHECK(is_public IN (0,1)))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS warehouse_items "
                    "(id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "warehouse_id INTEGER NOT NULL REFERENCES warehouses(id), media BLOB NOT NULL)") ||
        !query.exec("CREATE INDEX IF NOT EXISTS warehouse_items_warehouse ON warehouse_items(warehouse_id,id)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS work_sources "
                    "(warehouse_id INTEGER NOT NULL, item_id INTEGER NOT NULL, work_id TEXT NOT NULL UNIQUE, "
                    "PRIMARY KEY(warehouse_id,item_id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS behavior_events "
                    "(user_id INTEGER NOT NULL REFERENCES network_users(id), event_id TEXT NOT NULL, "
                    "action TEXT NOT NULL, query TEXT NOT NULL, occurred_at_utc INTEGER NOT NULL, "
                    "synthetic INTEGER NOT NULL DEFAULT 0, public_eligible INTEGER NOT NULL DEFAULT 0, "
                    "work_id TEXT, warehouse_id INTEGER, item_id INTEGER, media_snapshot BLOB, "
                    "PRIMARY KEY(user_id,event_id))") ||
        !query.exec("CREATE INDEX IF NOT EXISTS behavior_events_user_time ON behavior_events(user_id,occurred_at_utc)") ||
        !query.exec("CREATE INDEX IF NOT EXISTS behavior_events_public_time ON behavior_events(public_eligible,action,occurred_at_utc)") ||
        !query.exec("CREATE INDEX IF NOT EXISTS behavior_events_public_source ON behavior_events(warehouse_id,item_id,work_id,public_eligible,action,occurred_at_utc,user_id)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS behavior_settings "
                    "(user_id INTEGER PRIMARY KEY REFERENCES network_users(id), history_enabled INTEGER NOT NULL, "
                    "personalization_enabled INTEGER NOT NULL, contribute_public_trends INTEGER NOT NULL)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS work_feedback "
                    "(user_id INTEGER NOT NULL REFERENCES network_users(id), work_id TEXT NOT NULL, "
                    "warehouse_id INTEGER NOT NULL, item_id INTEGER NOT NULL, PRIMARY KEY(user_id,work_id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS server_schema_migrations(version INTEGER PRIMARY KEY)") ||
        !query.exec("INSERT OR IGNORE INTO server_schema_migrations(version) VALUES(1)") ||
        !person_schema::migrate(db_, error) ||
        !db_.commit()) {
        db_.rollback();
        if (error.isEmpty()) error = QStringLiteral("无法创建仓库表。");
        return false;
    }
    personStore_ = std::make_unique<person_store::SqliteStore>(db_);
    if (!personContextLoader_) {
        personContextLoader_ = [this](qint64 userId, const QList<care_contract::ResourceRef>& resources) {
            return personStore_->contextFor(userId, resources);
        };
    }
    return true;
}

bool WarehouseServer::addUser(const QString& username, const QString& password, QString& error)
{
    const auto key = userKey(username);
    const auto bytes = password.toUtf8();
    if (key.isEmpty() || username.size() > 64 || bytes.size() < 6 || bytes.size() > 1024) {
        error = QStringLiteral("用户名最多 64 个字符，密码长度为 6–1024 字节。");
        return false;
    }
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash, bytes.constData(), bytes.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        error = QStringLiteral("无法生成密码哈希。");
        return false;
    }
    QSqlQuery query(db_);
    query.prepare("INSERT INTO network_users(username,username_key,password_hash) VALUES (?,?,?)");
    query.addBindValue(username.trimmed());
    query.addBindValue(key);
    query.addBindValue(QString::fromLatin1(hash));
    if (!query.exec()) {
        error = QStringLiteral("无法添加网络账号，请检查用户名是否已存在。");
        return false;
    }
    return true;
}

std::optional<QList<WarehouseDetail>> WarehouseServer::readableCandidates(qint64 userId)
{
    if (userId <= 0)
        return std::nullopt;
    QSqlQuery query(db_);
    query.prepare("SELECT id,name,owner_id,is_public FROM warehouses "
                  "WHERE EXISTS(SELECT 1 FROM network_users WHERE id=?) AND (owner_id=? OR is_public=1) ORDER BY id");
    query.addBindValue(userId);
    query.addBindValue(userId);
    if (!query.exec())
        return std::nullopt;
    QList<WarehouseDetail> rows;
    SnapshotBudget budget;
    while (query.next()) {
        if (rows.size() >= 1000) return std::nullopt;
        const auto warehouse = readSummary(query);
        auto items = listItems(db_, warehouse.warehouseId, &budget);
        if (!items)
            return std::nullopt;
        rows.append({warehouse, std::move(*items)});
    }
    return rows;
}

bool WarehouseServer::sendResponse(QTcpSocket* socket, const QJsonObject& response)
{
    QString error;
    auto frame = warehouse_protocol::encodeFrame(response, error);
    if (frame.isEmpty())
        frame = warehouse_protocol::encodeFrame({{"type", "error_resp"}, {"req_id", response.value("req_id")},
                {"code", "ServerError"}, {"message", QStringLiteral("响应超过帧大小限制。")}}, error);
    if (socket->state() != QAbstractSocket::ConnectedState) return false;
    if (frame.isEmpty() || socket->bytesToWrite() > warehouse_protocol::MaxFrameSize || socket->write(frame) != frame.size()) {
        socket->abort();
        return false;
    }
    return true;
}

std::optional<QList<StoredBehavior>> WarehouseServer::queryBehavior(qint64 userId, care_contract::TimeRange range)
{
    if (userId <= 0 || !care_contract::validate(range).ok()) return std::nullopt;
    QSqlQuery exists(db_);
    exists.prepare("SELECT 1 FROM network_users WHERE id=?");
    exists.addBindValue(userId);
    if (!exists.exec() || !exists.next()) return std::nullopt;
    QSqlQuery query(db_);
    query.prepare("SELECT event_id,action,query,occurred_at_utc,synthetic,work_id,warehouse_id,item_id,media_snapshot,public_eligible "
                  "FROM behavior_events WHERE user_id=? AND occurred_at_utc>=? AND occurred_at_utc<? "
                  "ORDER BY occurred_at_utc,event_id");
    query.addBindValue(userId); query.addBindValue(range.beginUtc); query.addBindValue(range.endUtc);
    if (!query.exec()) return std::nullopt;
    QList<StoredBehavior> result;
    qint64 snapshotBytes = 0;
    while (query.next()) {
        if (result.size() >= 10000) return std::nullopt;
        StoredBehavior stored;
        care_contract::BehaviorRecord record;
        record.event.eventId = query.value(0).toString();
        const auto action = query.value(1).toString();
        if (action == "view") {
            record.event.action = care_contract::BehaviorAction::View;
            record.event.resource = care_contract::ResourceRef{query.value(5).toString(),
                {query.value(6).toLongLong(), query.value(7).toLongLong()}};
            const auto bytes = query.value(8).toByteArray();
            snapshotBytes += bytes.size();
            if (snapshotBytes > 16 * 1024 * 1024) return std::nullopt;
            MediaRecord media; QString error;
            const auto object = QJsonDocument::fromJson(bytes).object();
            if (!media_json::fromJson(object, media, error)) return std::nullopt;
            const auto people = person_snapshot_json::decode(object, *record.event.resource, media);
            if (!people) return std::nullopt;
            stored.peopleSnapshot = *people;
            stored.mediaSnapshot = media;
        } else if (action == "search") {
            record.event.action = care_contract::BehaviorAction::Search;
            record.event.query = query.value(2).toString();
        } else return std::nullopt;
        record.occurredAtUtc = query.value(3).toLongLong();
        record.synthetic = query.value(4).toBool();
        if (!care_contract::validate(record.event).ok()) return std::nullopt;
        stored.record = record;
        stored.publicEligible = query.value(9).toBool();
        result.append(stored);
    }
    if (query.lastError().isValid()) return std::nullopt;
    return result;
}

care_contract::Result<care_algorithms::PersonContext> WarehouseServer::loadPersonContext(
    qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks)
{
    if (!personContextLoader_) return {{}, care_algorithms::PersonContext{}};
    QList<care_contract::ResourceRef> refs;
    refs.reserve(authorizedWorks.size());
    for (const auto& work : authorizedWorks) refs.append(work.resource);
    return personContextLoader_(userId, refs);
}

care_contract::Result<care_algorithms::CatalogSnapshot> WarehouseServer::personCatalog(
    qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks)
{
    const auto context = loadPersonContext(userId, authorizedWorks);
    if (!context.status.ok() || !context.value)
        return {{ErrorCode::ServerError, QStringLiteral("人物目录读取失败。")}, {}};
    care_algorithms::CatalogSnapshot catalog;
    catalog.readableWorks = authorizedWorks;
    return care_algorithms::attachConfirmedPeople(catalog, *context.value);
}

void WarehouseServer::readRequests(QTcpSocket* socket, const std::shared_ptr<Session>& session)
{
    QList<QJsonObject> requests;
    QString error;
    const bool valid = session->decoder.feed(socket->readAll(), requests, error);
    for (const auto& request : requests) {
        if (socket->state() != QAbstractSocket::ConnectedState) return;
        if (!request.value("req_id").isString() || request.value("req_id").toString().trimmed().isEmpty() ||
            request.value("req_id").toString().size() > 128) {
            socket->disconnectFromHost();
            return;
        }
        const QString id = request.value("req_id").toString();
        if (session->pending.contains(id) || session->summaries.contains(id) || session->scenes.contains(id)) {
            socket->abort();
            return;
        }
        const auto type = request.value("type").toString();
        const auto scene = request.value("payload").toObject().value("scene").toString();
        if (type == "recommend_temporal" && (scene == "relax" || scene == "focus" || scene == "explore")) {
            startSceneRecommendation(socket, session, request);
            continue;
        }
        if (type == "get_trending" && request.value("payload").toObject().value("summary_mode") == QJsonValue("model")) {
            startTrendingSummary(socket, session, request);
            continue;
        }
        if (type == "recommend_resources" || type == "cancel_recommendation") {
            startRecommendation(socket, session, request);
            continue;
        }
        if (type == "logout" && session->userId && request.value("token").toString() == session->token &&
            request.value("payload").isObject())
            cancelPending(*session, socket, true);
        if (socket->state() != QAbstractSocket::ConnectedState) return;
        if (!sendResponse(socket, dispatch(*session, request))) return;
    }
    if (!valid)
        socket->disconnectFromHost();
}

QJsonObject WarehouseServer::dispatch(Session& session, const QJsonObject& request)
{
    const auto id = request.value("req_id");
    const QString type = request.value("type").toString();
    const QJsonObject payload = request.value("payload").toObject();
    const auto fail = [&](const char* code, const QString& message) {
        return QJsonObject{{"type", "error_resp"}, {"req_id", id}, {"code", code}, {"message", message}};
    };
    const auto success = [&](const QJsonObject& result = {}) {
        return QJsonObject{{"type", type + "_resp"}, {"req_id", id}, {"payload", result}};
    };
    const auto invalid = [&] { return fail("InvalidArgument", QStringLiteral("请求参数无效。")); };
    const auto dbError = [&] { return fail("ServerError", QStringLiteral("仓库数据库操作失败。")); };
    if (type.isEmpty() || !request.value("payload").isObject())
        return invalid();

    QSqlQuery query(db_);
    if (type == "login") {
        if (!request.value("token").isUndefined() || !payload.value("username").isString() ||
            !payload.value("password").isString() || payload.value("username").toString().size() > 64)
            return invalid();
        if (session.userId)
            return invalid();
        const auto username = userKey(payload.value("username").toString());
        const auto password = payload.value("password").toString().toUtf8();
        if (username.isEmpty() || password.isEmpty() || password.size() > 1024)
            return invalid();
        query.prepare("SELECT id,username,password_hash FROM network_users WHERE username_key=?");
        query.addBindValue(username);
        if (!query.exec())
            return dbError();
        if (!query.next() || crypto_pwhash_str_verify(query.value(2).toString().toLatin1().constData(),
                                                     password.constData(), password.size()) != 0)
            return fail("AuthenticationFailed", QStringLiteral("用户名或密码不正确。"));
        session.userId = query.value(0).toLongLong();
        QByteArray token(32, Qt::Uninitialized);
        randombytes_buf(token.data(), token.size());
        session.token = QString::fromLatin1(token.toHex());
        return success({{"token", session.token}, {"user", QJsonObject{
            {"user_id", QString::number(session.userId)}, {"username", query.value(1).toString()}}}});
    }

    if (!session.userId || request.value("token").toString() != session.token)
        return fail("NotAuthenticated", QStringLiteral("请在当前连接登录。"));
    if (type == "logout") {
        session.userId = 0;
        session.token.clear();
        return success();
    }
    if (const auto person = dispatchPersonQuery(session.userId, type, payload)) {
        if (person->status.ok() && person->value)
            return success(*person->value);
        switch (person->status.code) {
        case ErrorCode::InvalidArgument: return fail("InvalidArgument", person->status.message);
        case ErrorCode::PermissionDenied: return fail("PermissionDenied", person->status.message);
        case ErrorCode::NotFound: return fail("NotFound", person->status.message);
        default: return fail("ServerError", person->status.message);
        }
    }
    if (type == "set_work_feedback") {
        care_contract::ResourceRef ref;
        if (!readResource(payload.value("resource"), ref) || !payload.value("feedback").isString()) return invalid();
        const auto feedback = payload.value("feedback").toString();
        QSqlQuery query(db_);
        if (feedback == "neutral") {
            // Removing one's own stored feedback remains possible after source deletion.
            query.prepare("DELETE FROM work_feedback WHERE user_id=? AND work_id=?");
            query.addBindValue(session.userId); query.addBindValue(ref.workId);
            if (!query.exec()) return dbError();
            return success();
        }
        if (feedback != "not_interested") return invalid();
        query.prepare("SELECT w.owner_id,w.is_public,s.work_id FROM warehouses w JOIN warehouse_items i ON i.warehouse_id=w.id "
                      "JOIN work_sources s ON s.warehouse_id=w.id AND s.item_id=i.id WHERE w.id=? AND i.id=?");
        query.addBindValue(ref.source.warehouseId); query.addBindValue(ref.source.itemId);
        if (!query.exec()) return dbError();
        if (!query.next()) return query.lastError().isValid() ? dbError() : fail("NotFound", QStringLiteral("资源不存在。"));
        if (query.value(0).toLongLong() != session.userId && !query.value(1).toBool())
            return fail("PermissionDenied", QStringLiteral("没有此资源的访问权限。"));
        if (query.value(2).toString() != ref.workId) return invalid();
        query.finish();
        query.prepare("INSERT INTO work_feedback(user_id,work_id,warehouse_id,item_id) VALUES(?,?,?,?) "
                      "ON CONFLICT(user_id,work_id) DO UPDATE SET warehouse_id=excluded.warehouse_id,item_id=excluded.item_id");
        query.addBindValue(session.userId); query.addBindValue(ref.workId);
        query.addBindValue(ref.source.warehouseId); query.addBindValue(ref.source.itemId);
        if (!query.exec()) return dbError();
        return success();
    }
    if (type == "list_work_feedback") {
        qint64 offset = 0;
        if (!readUtc(payload.value("offset"), offset) || !payload.value("limit").isDouble()) return invalid();
        const double limit = payload.value("limit").toDouble();
        if (limit < 1 || limit > care_contract::MaxPageSize || limit != static_cast<int>(limit)) return invalid();
        const care_contract::PageRequest page{offset, static_cast<int>(limit)};
        if (!care_contract::validate(page).ok()) return invalid();
        QSqlQuery query(db_);
        query.prepare("SELECT f.work_id,s.warehouse_id,s.item_id,i.media FROM work_feedback f "
                      "JOIN work_sources s ON s.work_id=f.work_id "
                      "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                      "JOIN warehouses w ON w.id=s.warehouse_id WHERE f.user_id=? AND (w.owner_id=? OR w.is_public=1) "
                      "ORDER BY f.work_id,s.warehouse_id,s.item_id LIMIT ? OFFSET ?");
        query.addBindValue(session.userId); query.addBindValue(session.userId);
        query.addBindValue(page.limit + 1); query.addBindValue(page.offset);
        if (!query.exec()) return dbError();
        QJsonArray items; bool more = false;
        while (query.next()) {
            if (items.size() == page.limit) { more = true; break; }
            MediaRecord media; QString error;
            if (!media_json::fromJson(QJsonDocument::fromJson(query.value(3).toByteArray()).object(), media, error)) return dbError();
            care_contract::ResourceSummary work{{query.value(0).toString(), {query.value(1).toLongLong(), query.value(2).toLongLong()}}, media, {}};
            items.append(QJsonObject{{"work", workJson(work)}, {"feedback", "not_interested"}});
        }
        if (query.lastError().isValid()) return dbError();
        QJsonObject result{{"items", items}};
        if (more) result.insert("next_offset", QString::number(page.offset + page.limit));
        return success({{"page", result}});
    }
    if (type == "recommend_temporal") {
        care_contract::TemporalRecommendationRequest input;
        if (!readPreference(payload.value("category"), input.category) || !readPreference(payload.value("file_type"), input.fileType) ||
            !payload.value("scene").isString() || !payload.value("limit").isDouble()) return invalid();
        const auto scene = payload.value("scene").toString();
        if (scene == "neutral") input.scene = care_contract::BrowseScene::Neutral;
        else if (scene == "relax") input.scene = care_contract::BrowseScene::Relax;
        else if (scene == "focus") input.scene = care_contract::BrowseScene::Focus;
        else if (scene == "explore") input.scene = care_contract::BrowseScene::Explore;
        else return invalid();
        const double limit = payload.value("limit").toDouble();
        if (limit < 1 || limit > care_contract::MaxRecommendationLimit || limit != static_cast<int>(limit)) return invalid();
        input.limit = static_cast<int>(limit);
        if (!care_contract::validate(input).ok()) return invalid();
        const auto candidates = readableCandidates(session.userId);
        const auto history = queryBehavior(session.userId, {0, care_contract::MaxUtcExclusive});
        if (!candidates || !history) return dbError();
        care_algorithms::PersonalSnapshot snapshot;
        snapshot.userKey = QString::number(session.userId);
        snapshot.nowUtc = QDateTime::currentSecsSinceEpoch();
        QSqlQuery exclusions(db_);
        exclusions.prepare("SELECT f.work_id FROM work_feedback f "
            "JOIN work_sources s ON s.work_id=f.work_id "
            "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
            "JOIN warehouses w ON w.id=s.warehouse_id WHERE f.user_id=? AND (w.owner_id=? OR w.is_public=1)");
        exclusions.addBindValue(session.userId); exclusions.addBindValue(session.userId);
        if (!exclusions.exec()) return dbError();
        while (exclusions.next()) {
            if (snapshot.notInterestedWorkIds.size() >= 10000) return dbError();
            snapshot.notInterestedWorkIds.insert(exclusions.value(0).toString());
        }
        if (exclusions.lastError().isValid()) return dbError();
        QSqlQuery settings(db_);
        settings.prepare("SELECT history_enabled,personalization_enabled,contribute_public_trends FROM behavior_settings WHERE user_id=?");
        settings.addBindValue(session.userId);
        if (!settings.exec()) return dbError();
        if (settings.next()) snapshot.settings = {settings.value(0).toBool(), settings.value(1).toBool(), settings.value(2).toBool()};
        else if (settings.lastError().isValid()) return dbError();
        // One transaction avoids one disk commit per newly discovered candidate.
        if (!db_.transaction()) return dbError();
        QSqlQuery insertSource(db_), findSource(db_);
        insertSource.prepare("INSERT OR IGNORE INTO work_sources(warehouse_id,item_id,work_id) VALUES(?,?,?)");
        findSource.prepare("SELECT work_id FROM work_sources WHERE warehouse_id=? AND item_id=?");
        for (const auto& warehouse : *candidates) {
            for (const auto& item : warehouse.items) {
                findSource.bindValue(0, warehouse.warehouse.warehouseId); findSource.bindValue(1, item.itemId);
                if (!findSource.exec()) { db_.rollback(); return dbError(); }
                if (!findSource.next()) {
                    if (findSource.lastError().isValid()) { db_.rollback(); return dbError(); }
                    findSource.finish();
                    insertSource.bindValue(0, warehouse.warehouse.warehouseId); insertSource.bindValue(1, item.itemId);
                    insertSource.bindValue(2, QStringLiteral("srv:") + QUuid::createUuid().toString(QUuid::WithoutBraces));
                    if (!insertSource.exec() || !findSource.exec() || !findSource.next()) { db_.rollback(); return dbError(); }
                }
                snapshot.readableWorks.append({{findSource.value(0).toString(), {warehouse.warehouse.warehouseId, item.itemId}}, item.media, {}});
                findSource.finish();
            }
        }
        if (!db_.commit()) { db_.rollback(); return dbError(); }
        for (const auto& stored : *history) {
            care_algorithms::ObservedBehavior event;
            event.userKey = snapshot.userKey; event.record = stored.record; event.publicEligible = stored.publicEligible;
            if (stored.record.event.resource && stored.mediaSnapshot)
                event.workSnapshot = care_contract::ResourceSummary{*stored.record.event.resource, *stored.mediaSnapshot, stored.peopleSnapshot};
            snapshot.events.append(event);
        }
        // The recommendation uses the complete seven-day public heat window, not the truncated UI top list.
        const QTimeZone zone("Asia/Shanghai");
        if (!zone.isValid()) return dbError();
        QMap<QString, QMap<qint64, QSet<qint64>>> heatDays;
        QSet<QString> syntheticHeat;
        QSqlQuery heat(db_);
        heat.prepare("SELECT e.work_id,e.user_id,e.occurred_at_utc,e.synthetic FROM behavior_events e "
            "JOIN behavior_settings b ON b.user_id=e.user_id JOIN work_sources s ON s.work_id=e.work_id "
            "AND s.warehouse_id=e.warehouse_id AND s.item_id=e.item_id "
            "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
            "JOIN warehouses w ON w.id=s.warehouse_id WHERE w.is_public=1 AND e.public_eligible=1 "
            "AND b.contribute_public_trends=1 AND e.action='view' AND e.occurred_at_utc>=? AND e.occurred_at_utc<=?");
        heat.addBindValue(qMax<qint64>(0, snapshot.nowUtc - 7 * 86400)); heat.addBindValue(snapshot.nowUtc);
        if (!heat.exec()) return dbError();
        int heatRows = 0;
        while (heat.next()) {
            if (++heatRows > 100000) return dbError();
            const auto workId = heat.value(0).toString(); const auto userId = heat.value(1).toLongLong();
            const auto date = QDateTime::fromSecsSinceEpoch(heat.value(2).toLongLong(), zone).date();
            if (workId.isEmpty() || userId <= 0 || !date.isValid()) return dbError();
            heatDays[workId][userId].insert(date.toJulianDay());
            if (heat.value(3).toBool()) syntheticHeat.insert(workId);
        }
        if (heat.lastError().isValid()) return dbError();
        for (auto it = heatDays.cbegin(); it != heatDays.cend(); ++it) {
            if (it.value().size() < 3) continue;
            qint64 effective = 0;
            for (const auto& days : it.value()) effective += days.size();
            snapshot.publicHeat.insert(it.key(), effective);
            if (syntheticHeat.contains(it.key())) snapshot.publicHeatContainsSyntheticData = true;
        }
        const auto context = loadPersonContext(session.userId, snapshot.readableWorks);
        if (!context.status.ok() || !context.value) return dbError();
        const auto enriched = care_algorithms::attachConfirmedPeople(snapshot, *context.value);
        if (!enriched.status.ok() || !enriched.value) return dbError();
        const auto recommended = care_algorithms::recommendTemporal(input, *enriched.value);
        if (!recommended.status.ok() || !recommended.value) return fail("ServerError", recommended.status.message);
        QJsonArray rows;
        for (const auto& item : recommended.value->items) rows.append(QJsonObject{{"work", workJson(item.work)}, {"reason", item.reason}});
        const QString note = personContextLoader_ ? QStringLiteral("人物和关注只使用服务端确认的关联。") :
            QStringLiteral("人物目录尚未配置，当前使用历史、公开热度、发行日期及不感兴趣设置。");
        return success({{"result", QJsonObject{{"items", rows}, {"message", recommended.value->message + note},
            {"fallback", recommended.value->fallback}, {"engine", "temporal"},
            {"generated_at_utc", QString::number(recommended.value->generatedAtUtc)},
            {"contains_synthetic_data", recommended.value->containsSyntheticData}}}});
    }
    if (type == "get_trending") {
        care_contract::TrendingRequest input;
        if (!parseTrendingRequest(payload, input).ok()) return invalid();
        const auto result = currentTrending(input);
        if (!result.status.ok() || !result.value) return dbError();
        return success({{"result", trendingJson(*result.value)}});
    }
    if (type == "get_footprint") {
        care_contract::FootprintRequest input;
        if (!readRange(payload.value("range"), input.range)) return invalid();
        const auto comparison = payload.value("comparison");
        if (!comparison.isUndefined() && !comparison.isNull()) {
            care_contract::TimeRange range;
            if (!readRange(comparison, range)) return invalid();
            input.comparison = range;
        }
        const auto valid = care_contract::validate(input);
        if (!valid.ok()) return fail("InvalidArgument", valid.message);
        const auto history = queryBehavior(session.userId, {0, care_contract::MaxUtcExclusive});
        if (!history) return dbError();
        care_algorithms::PersonalSnapshot snapshot;
        snapshot.userKey = QString::number(session.userId);
        snapshot.nowUtc = QDateTime::currentSecsSinceEpoch();
        for (const auto& item : *history) {
            care_algorithms::ObservedBehavior event;
            event.userKey = snapshot.userKey;
            event.record = item.record;
            event.publicEligible = item.publicEligible;
            if (item.record.event.resource && item.mediaSnapshot) {
                care_contract::ResourceSummary work;
                work.resource = *item.record.event.resource;
                work.media = *item.mediaSnapshot;
                work.people = item.peopleSnapshot;
                event.workSnapshot = work;
            }
            snapshot.events.append(event);
        }
        const auto report = care_algorithms::buildFootprint(input, snapshot);
        if (!report.status.ok() || !report.value)
            return fail(report.status.code == ErrorCode::InvalidArgument ? "InvalidArgument" : "ServerError", report.status.message);
        QJsonObject output{{"current", periodJson(report.value->current)}, {"generated_at_utc", QString::number(report.value->generatedAtUtc)},
            {"message", report.value->message + QStringLiteral(" 未确认的人物关联不纳入人物统计。")},
            {"contains_synthetic_data", report.value->containsSyntheticData}};
        if (report.value->comparison) output.insert("comparison", periodJson(*report.value->comparison));
        return success({{"report", output}});
    }
    if (type == "get_resource") {
        if (!payload.value("source").isObject()) return invalid();
        const auto source = payload.value("source").toObject();
        const qint64 warehouseId = readId(source.value("warehouse_id"));
        const qint64 itemId = readId(source.value("item_id"));
        if (!warehouseId || !itemId) return invalid();
        query.prepare("SELECT w.owner_id,w.is_public,i.media FROM warehouses w "
                      "JOIN warehouse_items i ON i.warehouse_id=w.id WHERE w.id=? AND i.id=?");
        query.addBindValue(warehouseId); query.addBindValue(itemId);
        if (!query.exec()) return dbError();
        if (!query.next()) return fail("NotFound", QStringLiteral("资源不存在。"));
        if (query.value(0).toLongLong() != session.userId && !query.value(1).toBool())
            return fail("PermissionDenied", QStringLiteral("没有此资源的访问权限。"));
        const auto media = QJsonDocument::fromJson(query.value(2).toByteArray()).object();
        MediaRecord decoded; QString error;
        if (!media_json::fromJson(media, decoded, error)) return dbError();
        query.finish();
        query.prepare("INSERT OR IGNORE INTO work_sources(warehouse_id,item_id,work_id) VALUES(?,?,?)");
        query.addBindValue(warehouseId); query.addBindValue(itemId);
        query.addBindValue(QStringLiteral("srv:") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!query.exec()) return dbError();
        query.prepare("SELECT work_id FROM work_sources WHERE warehouse_id=? AND item_id=?");
        query.addBindValue(warehouseId); query.addBindValue(itemId);
        if (!query.exec() || !query.next()) return dbError();
        const care_contract::ResourceSummary work{{query.value(0).toString(), {warehouseId, itemId}}, decoded, {}};
        query.finish();
        const auto catalog = personCatalog(session.userId, {work});
        if (!catalog.status.ok() || !catalog.value) return dbError();
        return success({{"work", workJson(catalog.value->readableWorks.first())}});
    }
    if (type == "get_participation_settings") {
        query.prepare("SELECT history_enabled,personalization_enabled,contribute_public_trends FROM behavior_settings WHERE user_id=?");
        query.addBindValue(session.userId);
        if (!query.exec()) return dbError();
        if (!query.next()) return success({{"settings", QJsonObject{{"history_enabled", true}, {"personalization_enabled", true}, {"contribute_public_trends", false}}}});
        return success({{"settings", QJsonObject{{"history_enabled", query.value(0).toBool()},
                         {"personalization_enabled", query.value(1).toBool()}, {"contribute_public_trends", query.value(2).toBool()}}}});
    }
    if (type == "set_participation_settings") {
        if (!payload.value("settings").isObject()) return invalid();
        const auto settings = payload.value("settings").toObject();
        if (!settings.value("history_enabled").isBool() || !settings.value("personalization_enabled").isBool() ||
            !settings.value("contribute_public_trends").isBool()) return invalid();
        if (!db_.transaction()) return dbError();
        query.prepare("INSERT INTO behavior_settings VALUES(?,?,?,?) ON CONFLICT(user_id) DO UPDATE SET "
                      "history_enabled=excluded.history_enabled,personalization_enabled=excluded.personalization_enabled,"
                      "contribute_public_trends=excluded.contribute_public_trends");
        query.addBindValue(session.userId);
        query.addBindValue(settings.value("history_enabled").toBool());
        query.addBindValue(settings.value("personalization_enabled").toBool());
        query.addBindValue(settings.value("contribute_public_trends").toBool());
        if (!query.exec()) { db_.rollback(); return dbError(); }
        if (!settings.value("contribute_public_trends").toBool()) {
            query.prepare("UPDATE behavior_events SET public_eligible=0 WHERE user_id=?");
            query.addBindValue(session.userId);
            if (!query.exec()) { db_.rollback(); return dbError(); }
        }
        if (!db_.commit()) { db_.rollback(); return dbError(); }
        return success();
    }
    if (type == "clear_history") {
        query.prepare("DELETE FROM behavior_events WHERE user_id=?");
        query.addBindValue(session.userId);
        if (!query.exec()) return dbError();
        return success();
    }
    if (type == "record_behavior") {
        if (!payload.value("event").isObject()) return invalid();
        const auto event = payload.value("event").toObject();
        if (!event.value("event_id").isString() || !event.value("action").isString()) return invalid();
        care_contract::BehaviorEvent value;
        value.eventId = event.value("event_id").toString();
        const auto action = event.value("action").toString();
        QByteArray snapshot;
        bool sourcePublic = true;
        if (action == "search") {
            if (!event.value("query").isString() || (!event.value("resource").isUndefined() && !event.value("resource").isNull())) return invalid();
            value.action = care_contract::BehaviorAction::Search;
            value.query = event.value("query").toString();
        } else if (action == "view") {
            if (!event.value("resource").isObject() || (!event.value("query").isUndefined() && !event.value("query").isString())) return invalid();
            const auto resource = event.value("resource").toObject();
            if (!resource.value("work_id").isString() || !resource.value("source").isObject()) return invalid();
            const auto source = resource.value("source").toObject();
            value.action = care_contract::BehaviorAction::View;
            value.query = event.value("query").isUndefined() ? QStringLiteral("") : event.value("query").toString();
            value.resource = care_contract::ResourceRef{resource.value("work_id").toString(),
                {readId(source.value("warehouse_id")), readId(source.value("item_id"))}};
        } else return invalid();
        const auto status = care_contract::validate(value);
        if (!status.ok()) return fail("InvalidArgument", status.message);
        // Check the authenticated user's existing event before looking up a resource:
        // retries must remain idempotent even if that resource was later deleted.
        query.prepare("SELECT action,query,work_id,warehouse_id,item_id FROM behavior_events WHERE user_id=? AND event_id=?");
        query.addBindValue(session.userId);
        query.addBindValue(value.eventId);
        if (!query.exec()) return dbError();
        if (query.next()) {
            if (query.value(0).toString() != action || query.value(1).toString() != value.query ||
                (value.resource && (query.value(2).toString() != value.resource->workId ||
                  query.value(3).toLongLong() != value.resource->source.warehouseId || query.value(4).toLongLong() != value.resource->source.itemId)))
                return fail("InvalidArgument", QStringLiteral("行为编号已用于其他内容。"));
            return success();
        }
        if (query.lastError().isValid()) return dbError();
        query.finish();
        if (value.resource) {
            query.prepare("SELECT w.owner_id,w.is_public,i.media,s.work_id FROM warehouses w "
                          "JOIN warehouse_items i ON i.warehouse_id=w.id "
                          "LEFT JOIN work_sources s ON s.warehouse_id=w.id AND s.item_id=i.id WHERE w.id=? AND i.id=?");
            query.addBindValue(value.resource->source.warehouseId); query.addBindValue(value.resource->source.itemId);
            if (!query.exec()) return dbError();
            if (!query.next()) return fail("NotFound", QStringLiteral("资源不存在。"));
            if (query.value(0).toLongLong() != session.userId && !query.value(1).toBool())
                return fail("PermissionDenied", QStringLiteral("没有此资源的访问权限。"));
            if (query.value(3).toString() != value.resource->workId) return invalid();
            sourcePublic = query.value(1).toBool(); snapshot = query.value(2).toByteArray();
            query.finish();
        }
        query.prepare("SELECT history_enabled,contribute_public_trends FROM behavior_settings WHERE user_id=?");
        query.addBindValue(session.userId);
        if (!query.exec()) return dbError();
        const bool hasSettings = query.next();
        const bool historyEnabled = !hasSettings || query.value(0).toBool();
        const bool publicEligible = hasSettings && query.value(1).toBool() && sourcePublic;
        query.finish();
        if (!historyEnabled) return success();
        if (value.resource) {
            auto object = QJsonDocument::fromJson(snapshot).object();
            MediaRecord media; QString error;
            if (!media_json::fromJson(object, media, error)) return dbError();
            const auto catalog = personCatalog(session.userId, {{*value.resource, media, {}}});
            if (!catalog.status.ok() || !catalog.value) return dbError();
            object.insert(person_snapshot_json::Field,
                person_snapshot_json::encode(catalog.value->readableWorks.first().people));
            snapshot = QJsonDocument(object).toJson(QJsonDocument::Compact);
        }
        query.prepare("INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,public_eligible,work_id,warehouse_id,item_id,media_snapshot) VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(user_id,event_id) DO NOTHING");
        query.addBindValue(session.userId);
        query.addBindValue(value.eventId);
        query.addBindValue(action);
        query.addBindValue(value.query);
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
        query.addBindValue(publicEligible);
        query.addBindValue(value.resource ? QVariant(value.resource->workId) : QVariant());
        query.addBindValue(value.resource ? QVariant(value.resource->source.warehouseId) : QVariant());
        query.addBindValue(value.resource ? QVariant(value.resource->source.itemId) : QVariant());
        query.addBindValue(value.resource ? QVariant(snapshot) : QVariant());
        if (!query.exec()) return dbError();
        if (query.numRowsAffected() == 0) {
            query.prepare("SELECT action,query,work_id,warehouse_id,item_id FROM behavior_events WHERE user_id=? AND event_id=?");
            query.addBindValue(session.userId); query.addBindValue(value.eventId);
            if (!query.exec() || !query.next()) return dbError();
            if (query.value(0).toString() != action || query.value(1).toString() != value.query ||
                (value.resource && (query.value(2).toString() != value.resource->workId ||
                 query.value(3).toLongLong() != value.resource->source.warehouseId || query.value(4).toLongLong() != value.resource->source.itemId)))
                return fail("InvalidArgument", QStringLiteral("行为编号已用于其他内容。"));
        }
        return success();
    }
    if (type == "list_my_warehouses" || type == "list_public_warehouses") {
        if (type == "list_my_warehouses") {
            query.prepare("SELECT id,name,owner_id,is_public FROM warehouses WHERE owner_id=? ORDER BY id");
            query.addBindValue(session.userId);
        } else {
            query.prepare("SELECT id,name,owner_id,is_public FROM warehouses WHERE is_public=1 ORDER BY id");
        }
        if (!query.exec())
            return dbError();
        QJsonArray list;
        while (query.next())
            list.append(summaryJson(readSummary(query)));
        return success({{"warehouses", list}});
    }
    if (type == "create_warehouse") {
        const QString name = payload.value("name").toString().trimmed();
        const QString visibility = payload.value("visibility").toString();
        if (name.isEmpty() || name.size() > 100 || (visibility != "public" && visibility != "private"))
            return invalid();
        const bool publicAccess = visibility == "public";
        query.prepare("INSERT INTO warehouses(name,owner_id,is_public) VALUES (?,?,?)");
        query.addBindValue(name);
        query.addBindValue(session.userId);
        query.addBindValue(publicAccess);
        if (!query.exec())
            return dbError();
        return success({{"warehouse", summaryJson({query.lastInsertId().toLongLong(), name, session.userId,
                                            publicAccess ? Visibility::Public : Visibility::Private})}});
    }

    if (type != "get_warehouse" && type != "add_item" && type != "remove_item")
        return fail("InvalidArgument", QStringLiteral("未知请求类型。"));
    const qint64 warehouseId = readId(payload.value("warehouse_id"));
    if (!warehouseId)
        return invalid();
    query.prepare("SELECT id,name,owner_id,is_public FROM warehouses WHERE id=?");
    query.addBindValue(warehouseId);
    if (!query.exec())
        return dbError();
    if (!query.next())
        return fail("NotFound", QStringLiteral("仓库不存在。"));
    const auto warehouse = readSummary(query);
    query.finish();
    if (warehouse.ownerId != session.userId && (type != "get_warehouse" || warehouse.visibility != Visibility::Public))
        return fail("PermissionDenied", QStringLiteral("没有此仓库的操作权限。"));

    if (type == "get_warehouse") {
        const auto items = listItems(db_, warehouseId);
        if (!items)
            return dbError();
        QJsonArray list;
        for (const auto& item : *items)
            list.append(itemJson(item));
        return success({{"warehouse", summaryJson(warehouse)}, {"items", list}});
    }
    if (type == "add_item") {
        MediaRecord media;
        QString error;
        if (!media_json::fromJson(payload.value("media").toObject(), media, error))
            return fail("InvalidArgument", error);
        const auto snapshot = QJsonDocument(media_json::toJson(media)).toJson(QJsonDocument::Compact);
        if (snapshot.size() > 64 * 1024)
            return invalid();
        query.prepare("INSERT INTO warehouse_items(warehouse_id,media) VALUES (?,?)");
        query.addBindValue(warehouseId);
        query.addBindValue(snapshot);
        if (!query.exec())
            return dbError();
        return success({{"item", itemJson({query.lastInsertId().toLongLong(), media})}});
    }
    const qint64 itemId = readId(payload.value("item_id"));
    if (!itemId)
        return invalid();
    query.prepare("DELETE FROM warehouse_items WHERE id=? AND warehouse_id=?");
    query.addBindValue(itemId);
    query.addBindValue(warehouseId);
    if (!query.exec())
        return dbError();
    if (query.numRowsAffected() != 1)
        return fail("NotFound", QStringLiteral("条目不存在。"));
    return success();
}
