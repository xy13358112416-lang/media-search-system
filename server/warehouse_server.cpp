#include "warehouse_server.h"
#include "media_json.h"
#include "behavior_contract.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QTcpSocket>
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

qint64 readId(const QJsonValue& value)
{
    const QString text = value.toString();
    static const QRegularExpression format(QStringLiteral("\\A[1-9][0-9]*\\z"));
    return format.match(text).hasMatch() ? text.toLongLong() : 0;
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

std::optional<QList<WarehouseItem>> listItems(const QSqlDatabase& db, qint64 warehouseId)
{
    QSqlQuery query(db);
    query.prepare("SELECT id,media FROM warehouse_items WHERE warehouse_id=? ORDER BY id");
    query.addBindValue(warehouseId);
    if (!query.exec())
        return std::nullopt;
    QList<WarehouseItem> items;
    while (query.next()) {
        MediaRecord media;
        QString error;
        if (!media_json::fromJson(QJsonDocument::fromJson(query.value(1).toByteArray()).object(), media, error))
            return std::nullopt;
        items.append({query.value(0).toLongLong(), media});
    }
    return items;
}

} // namespace

WarehouseServer::WarehouseServer() : dbName_(QUuid::createUuid().toString())
{
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = nextPendingConnection()) {
            auto session = std::make_shared<Session>();
            socket->setReadBufferSize(64 * 1024);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, session] {
                readRequests(socket, *session);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

WarehouseServer::~WarehouseServer()
{
    close();
    db_ = {};
    QSqlDatabase::removeDatabase(dbName_);
}

bool WarehouseServer::openDatabase(const QString& path, QString& error)
{
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
        !query.exec("CREATE TABLE IF NOT EXISTS behavior_settings "
                    "(user_id INTEGER PRIMARY KEY REFERENCES network_users(id), history_enabled INTEGER NOT NULL, "
                    "personalization_enabled INTEGER NOT NULL, contribute_public_trends INTEGER NOT NULL)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS server_schema_migrations(version INTEGER PRIMARY KEY)") ||
        !query.exec("INSERT OR IGNORE INTO server_schema_migrations(version) VALUES(1)") ||
        !db_.commit()) {
        db_.rollback();
        error = QStringLiteral("无法创建仓库表。");
        return false;
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
    while (query.next()) {
        const auto warehouse = readSummary(query);
        auto items = listItems(db_, warehouse.warehouseId);
        if (!items)
            return std::nullopt;
        rows.append({warehouse, std::move(*items)});
    }
    return rows;
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
            if (!media_json::fromJson(QJsonDocument::fromJson(bytes).object(), media, error)) return std::nullopt;
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
    return result;
}

void WarehouseServer::readRequests(QTcpSocket* socket, Session& session)
{
    QList<QJsonObject> requests;
    QString error;
    const bool valid = session.decoder.feed(socket->readAll(), requests, error);
    for (const auto& request : requests) {
        if (!request.value("req_id").isString() || request.value("req_id").toString().trimmed().isEmpty() ||
            request.value("req_id").toString().size() > 128) {
            socket->disconnectFromHost();
            return;
        }
        auto response = dispatch(session, request);
        auto frame = warehouse_protocol::encodeFrame(response, error);
        if (frame.isEmpty()) {
            response = {{"type", "error_resp"}, {"req_id", request.value("req_id")},
                        {"code", "ServerError"}, {"message", QStringLiteral("响应超过帧大小限制。")}};
            frame = warehouse_protocol::encodeFrame(response, error);
        }
        if (socket->bytesToWrite() > warehouse_protocol::MaxFrameSize || socket->write(frame) != frame.size()) {
            socket->abort();
            return;
        }
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
        const QJsonObject cleanSource{{"warehouse_id", QString::number(warehouseId)}, {"item_id", QString::number(itemId)}};
        const QJsonObject ref{{"work_id", query.value(0).toString()}, {"source", cleanSource}};
        return success({{"work", QJsonObject{{"resource", ref}, {"media", media}, {"people", QJsonArray{}}}}});
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
        query.finish();
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
