#include "warehouse_server.h"
#include "media_json.h"

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
    : recommender_(std::move(config)), dbName_(QUuid::createUuid().toString())
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
        if (session->pending.contains(id)) {
            socket->abort();
            return;
        }
        const auto type = request.value("type").toString();
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
