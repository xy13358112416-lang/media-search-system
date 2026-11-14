#include "warehouse_server.h"
#include "warehouse_protocol.h"
#include "media_json.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <sodium.h>
#include <utility>

namespace {
QString keyFor(const QString& name) {
    return name.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded()
        .normalized(QString::NormalizationForm_C);
}
bool positiveId(const QJsonValue& value, qint64& id) {
    if (!value.isString()) return false;
    const QString text = value.toString();
    static const QRegularExpression re(QStringLiteral("^[1-9][0-9]*$"));
    if (!re.match(text).hasMatch()) return false;
    bool ok = false;
    id = text.toLongLong(&ok);
    return ok && id > 0;
}
QJsonObject summary(const QSqlQuery& q) {
    return {{"warehouse_id", q.value(0).toString()}, {"name", q.value(1).toString()},
            {"owner_id", q.value(2).toString()}, {"visibility", q.value(3).toString()}};
}
}

struct WarehouseServer::Connection {
    QTcpSocket* socket = nullptr;
    warehouse_protocol::FrameDecoder decoder;
    QString token;
    qint64 userId = 0;
};

WarehouseServer::WarehouseServer(QObject* parent) : QObject(parent) {
    connect(&listener_, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
}
WarehouseServer::~WarehouseServer() { close(); }

bool WarehouseServer::open(const QString& path, QString& error) {
    close();
    error.clear();
    if (sodium_init() < 0) { error = QStringLiteral("密码库初始化失败"); return false; }
    dbName_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), dbName_);
    db_.setDatabaseName(path);
    if (!db_.open()) { error = db_.lastError().text(); close(); return false; }
    const QStringList statements{
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS server_users (user_id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL, username_key TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS warehouses (warehouse_id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, owner_id INTEGER NOT NULL REFERENCES server_users(user_id), visibility TEXT NOT NULL CHECK(visibility IN ('private','public')))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS warehouse_items (item_id INTEGER PRIMARY KEY AUTOINCREMENT, warehouse_id INTEGER NOT NULL REFERENCES warehouses(warehouse_id), media_json TEXT NOT NULL)")
    };
    {
        QSqlQuery q(db_);
        for (const auto& sql : statements) {
            if (!q.exec(sql)) { error = q.lastError().text(); break; }
        }
    }
    if (!error.isEmpty()) { close(); return false; }
    return true;
}
bool WarehouseServer::hasUsers(bool& exists, QString& error) const {
    if (!db_.isOpen()) { error = QStringLiteral("数据库未打开"); return false; }
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("SELECT EXISTS(SELECT 1 FROM server_users)")) || !q.next()) {
        error = q.lastError().text(); return false;
    }
    exists = q.value(0).toBool(); error.clear(); return true;
}
bool WarehouseServer::readableWarehouses(qint64 userId, QList<warehouse_contract::WarehouseDetail>& out,
                                         QString& error) const {
    if (!db_.isOpen() || userId <= 0) { error = QStringLiteral("数据库或用户无效"); return false; }
    QSqlQuery user(db_);
    user.prepare(QStringLiteral("SELECT 1 FROM server_users WHERE user_id=?"));
    user.addBindValue(userId);
    if (!user.exec()) { error = QStringLiteral("查询用户失败"); return false; }
    if (!user.next()) { error = QStringLiteral("用户不存在"); return false; }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT w.warehouse_id,w.name,w.owner_id,w.visibility,i.item_id,"
        "CASE WHEN length(CAST(i.media_json AS BLOB))<=4194304 THEN i.media_json ELSE NULL END,"
        "length(CAST(i.media_json AS BLOB)) "
        "FROM warehouses w LEFT JOIN warehouse_items i ON i.warehouse_id=w.warehouse_id "
        "WHERE w.owner_id=? OR w.visibility='public' ORDER BY w.warehouse_id,i.item_id"));
    q.addBindValue(userId);
    if (!q.exec()) { error = QStringLiteral("读取可访问仓库失败"); return false; }
    QList<warehouse_contract::WarehouseDetail> result;
    constexpr qint64 maxSnapshotBytes = 4 * 1024 * 1024;
    constexpr int maxItems = 1000;
    constexpr int maxWarehouses = 2000;
    qint64 snapshotBytes = 0;
    int itemCount = 0;
    while (q.next()) {
        const qint64 warehouseId = q.value(0).toLongLong();
        if (result.isEmpty() || result.last().warehouse.warehouseId != warehouseId) {
            if (result.size() >= maxWarehouses) {
                error = QStringLiteral("可访问仓库数量超过候选上限"); return false;
            }
            warehouse_contract::WarehouseDetail detail;
            detail.warehouse.warehouseId = warehouseId;
            detail.warehouse.name = q.value(1).toString();
            detail.warehouse.ownerId = q.value(2).toLongLong();
            const QString visibility = q.value(3).toString();
            if (visibility != "public" && visibility != "private") {
                error = QStringLiteral("仓库可见性无效"); return false;
            }
            detail.warehouse.visibility = visibility == "public"
                ? warehouse_contract::Visibility::Public : warehouse_contract::Visibility::Private;
            result.append(detail);
        }
        if (!q.value(4).isNull()) {
            const qint64 length = q.value(6).toLongLong();
            if (++itemCount > maxItems || length < 0 || length > maxSnapshotBytes - snapshotBytes) {
                error = QStringLiteral("可访问媒体快照超过候选上限"); return false;
            }
            snapshotBytes += length;
            warehouse_contract::WarehouseItem item;
            item.itemId = q.value(4).toLongLong();
            const auto doc = QJsonDocument::fromJson(q.value(5).toByteArray());
            QString parseError;
            if (item.itemId <= 0 || !doc.isObject() || !media_json::fromJson(doc.object(), item.media, parseError)) {
                error = QStringLiteral("仓库媒体快照无效"); return false;
            }
            result.last().items.append(item);
        }
    }
    out = std::move(result);
    error.clear();
    return true;
}
bool WarehouseServer::addUser(const QString& name, const QString& password, QString& error) {
    if (!db_.isOpen() || keyFor(name).isEmpty() || password.isEmpty()) {
        error = QStringLiteral("数据库未打开或用户名、密码为空"); return false;
    }
    const QByteArray secret = password.toUtf8();
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash, secret.constData(), static_cast<unsigned long long>(secret.size()),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        error = QStringLiteral("密码哈希失败"); return false;
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO server_users(username,username_key,password_hash) VALUES(?,?,?)"));
    q.addBindValue(name.trimmed()); q.addBindValue(keyFor(name)); q.addBindValue(QString::fromLatin1(hash));
    if (!q.exec()) { error = q.lastError().text(); return false; }
    error.clear(); return true;
}
bool WarehouseServer::addInitialUser(const QString& name, const QString& password, QString& error) {
    if (!db_.isOpen()) { error = QStringLiteral("数据库未打开"); return false; }
    QSqlQuery tx(db_);
    if (!tx.exec(QStringLiteral("BEGIN IMMEDIATE"))) { error = tx.lastError().text(); return false; }
    bool exists = false;
    if (!hasUsers(exists, error) || exists) {
        if (exists) error = QStringLiteral("账户库不为空，不能再次初始化");
        tx.exec(QStringLiteral("ROLLBACK")); return false;
    }
    if (!addUser(name, password, error)) { tx.exec(QStringLiteral("ROLLBACK")); return false; }
    if (!tx.exec(QStringLiteral("COMMIT"))) {
        error = tx.lastError().text(); tx.exec(QStringLiteral("ROLLBACK")); return false;
    }
    return true;
}
bool WarehouseServer::listen(const QHostAddress& address, quint16 p, QString& error) {
    if (!db_.isOpen()) { error = QStringLiteral("数据库未打开"); return false; }
    if (!listener_.listen(address, p)) { error = listener_.errorString(); return false; }
    error.clear(); return true;
}
quint16 WarehouseServer::port() const { return listener_.serverPort(); }
void WarehouseServer::close() {
    listener_.close();
    const auto sockets = sockets_;
    for (auto* socket : sockets) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        delete socket;
    }
    sockets_.clear();
    if (db_.isValid()) {
        db_.close(); db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(dbName_); dbName_.clear();
    }
}
void WarehouseServer::acceptConnections() {
    while (auto* socket = listener_.nextPendingConnection()) {
        sockets_.insert(socket);
        auto c = std::make_shared<Connection>();
        c->socket = socket;
        connect(socket, &QTcpSocket::readyRead, this, [this, c] { receive(c); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            sockets_.remove(socket); socket->deleteLater();
        });
    }
}
void WarehouseServer::receive(const std::shared_ptr<Connection>& c) {
    QList<QJsonObject> requests;
    QString error;
    const bool valid = c->decoder.feed(c->socket->readAll(), requests, error);
    for (const auto& request : requests) {
        if (c->socket->state() != QAbstractSocket::ConnectedState) break;
        process(c, request);
    }
    if (!valid) c->socket->disconnectFromHost();
}
void WarehouseServer::process(const std::shared_ptr<Connection>& c, const QJsonObject& req) {
    const auto idValue = req.value(QStringLiteral("req_id"));
    if (!idValue.isString() || idValue.toString().isEmpty() || idValue.toString().size() > 128) {
        c->socket->disconnectFromHost(); return;
    }
    const QString id = idValue.toString();
    const QString type = req.value(QStringLiteral("type")).toString();
    auto send = [&](const QJsonObject& result) {
        QString error;
        const QByteArray frame = warehouse_protocol::encodeFrame(result, error);
        if (frame.isEmpty()) c->socket->disconnectFromHost();
        else c->socket->write(frame);
    };
    auto fail = [&](const char* code, const QString& message) {
        send({{"type","error_resp"},{"req_id",id},{"code",QString::fromLatin1(code)},{"message",message}});
    };
    auto success = [&](const QJsonObject& payload) {
        send({{"type",type + QStringLiteral("_resp")},{"req_id",id},{"payload",payload}});
    };
    if (type.isEmpty() || !req.value("payload").isObject()) {
        fail("InvalidArgument", QStringLiteral("请求格式无效")); return;
    }
    const QJsonObject payload = req.value("payload").toObject();
    if (type == "login") {
        // 开始新的登录尝试时清除旧会话，失败后不能继续沿用上一账号。
        c->userId = 0;
        c->token.clear();
        if (!payload.value("username").isString() || !payload.value("password").isString()) {
            fail("InvalidArgument", QStringLiteral("用户名或密码格式无效")); return;
        }
        QSqlQuery q(db_);
        q.prepare(QStringLiteral("SELECT user_id,username,password_hash FROM server_users WHERE username_key=?"));
        q.addBindValue(keyFor(payload.value("username").toString()));
        if (!q.exec()) { fail("ServerError", QStringLiteral("账户查询失败")); return; }
        const QByteArray password = payload.value("password").toString().toUtf8();
        if (!q.next() || crypto_pwhash_str_verify(q.value(2).toByteArray().constData(), password.constData(),
                                                 static_cast<unsigned long long>(password.size())) != 0) {
            fail("AuthenticationFailed", QStringLiteral("用户名或密码错误")); return;
        }
        c->userId = q.value(0).toLongLong();
        c->token = QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
        success({{"token",c->token},{"user",QJsonObject{{"user_id",QString::number(c->userId)},{"username",q.value(1).toString()}}}});
        return;
    }
    if (c->userId <= 0 || c->token.isEmpty() || !req.value("token").isString() ||
        req.value("token").toString() != c->token) {
        fail("NotAuthenticated", QStringLiteral("请先登录")); return;
    }
    if (type == "logout") {
        c->userId = 0; c->token.clear(); success({}); return;
    }
    if (type == "cancel_recommendation") {
        const auto target = payload.value("target_req_id");
        if (!target.isString() || target.toString().isEmpty() || target.toString().size() > 128) {
            fail("InvalidArgument", QStringLiteral("目标请求编号无效")); return;
        }
        // 目前没有服务端模型任务；后续接线必须取消此连接自己的对应请求。
        success({}); return;
    }
    if (type == "recommend_resources") {
        qint64 target = 0;
        const auto query = payload.value("query");
        const auto limit = payload.value("limit");
        if (!query.isString() || query.toString().trimmed().isEmpty() || query.toString().size() > 1000 ||
            !limit.isDouble() || limit.toDouble() != limit.toInt() || limit.toInt() < 1 || limit.toInt() > 50 ||
            (payload.contains("warehouse_id") && !positiveId(payload.value("warehouse_id"), target))) {
            fail("InvalidArgument", QStringLiteral("推荐条件无效")); return;
        }
        fail("ServerError", QStringLiteral("服务端智能推荐尚未接入")); return;
    }
    if (type == "create_warehouse") {
        if (!payload.value("name").isString() || payload.value("name").toString().trimmed().isEmpty() ||
            payload.value("name").toString().size() > 256 ||
            !payload.value("visibility").isString() ||
            (payload.value("visibility").toString() != "public" && payload.value("visibility").toString() != "private")) {
            fail("InvalidArgument", QStringLiteral("仓库名称或可见性无效")); return;
        }
        const QString name = payload.value("name").toString().trimmed();
        const QString visibility = payload.value("visibility").toString();
        QSqlQuery q(db_);
        // 单条 SQL 原子检查列表数量，保证最坏情况下列表响应仍能放入 4 MiB。
        q.prepare(QStringLiteral("INSERT INTO warehouses(name,owner_id,visibility) SELECT ?,?,? "
                                 "WHERE (SELECT COUNT(*) FROM warehouses WHERE owner_id=?) < 1000 "
                                 "AND (? <> 'public' OR (SELECT COUNT(*) FROM warehouses WHERE visibility='public') < 1000)"));
        q.addBindValue(name); q.addBindValue(c->userId); q.addBindValue(visibility);
        q.addBindValue(c->userId); q.addBindValue(visibility);
        if (!q.exec()) { fail("ServerError", QStringLiteral("创建仓库失败")); return; }
        if (q.numRowsAffected() == 0) {
            fail("InvalidArgument", QStringLiteral("仓库数量已达到上限")); return;
        }
        success({{"warehouse",QJsonObject{{"warehouse_id",q.lastInsertId().toString()},{"name",name},
                                           {"owner_id",QString::number(c->userId)},{"visibility",visibility}}}});
        return;
    }
    if (type == "list_my_warehouses" || type == "list_public_warehouses") {
        QSqlQuery q(db_);
        q.prepare(type == "list_my_warehouses"
            ? QStringLiteral("SELECT warehouse_id,name,owner_id,visibility FROM warehouses WHERE owner_id=? ORDER BY warehouse_id")
            : QStringLiteral("SELECT warehouse_id,name,owner_id,visibility FROM warehouses WHERE visibility='public' ORDER BY warehouse_id"));
        if (type == "list_my_warehouses") q.addBindValue(c->userId);
        if (!q.exec()) { fail("ServerError", QStringLiteral("读取仓库列表失败")); return; }
        QJsonArray list; while (q.next()) list.append(summary(q));
        success({{"warehouses",list}}); return;
    }
    if (type != "get_warehouse" && type != "add_item" && type != "remove_item") {
        fail("InvalidArgument", QStringLiteral("未知操作")); return;
    }
    qint64 warehouseId = 0;
    if (!positiveId(payload.value("warehouse_id"), warehouseId)) {
        fail("InvalidArgument", QStringLiteral("仓库编号无效")); return;
    }
    QSqlQuery w(db_);
    w.prepare(QStringLiteral("SELECT warehouse_id,name,owner_id,visibility FROM warehouses WHERE warehouse_id=?"));
    w.addBindValue(warehouseId);
    if (!w.exec()) { fail("ServerError", QStringLiteral("读取仓库失败")); return; }
    if (!w.next()) { fail("NotFound", QStringLiteral("仓库不存在")); return; }
    const bool owner = w.value(2).toLongLong() == c->userId;
    if ((!owner && type != "get_warehouse") || (!owner && w.value(3).toString() != "public")) {
        fail("PermissionDenied", QStringLiteral("无权访问该仓库")); return;
    }
    if (type == "get_warehouse") {
        const QJsonObject warehouse = summary(w);
        QSqlQuery q(db_); q.prepare(QStringLiteral("SELECT item_id,media_json FROM warehouse_items WHERE warehouse_id=? ORDER BY item_id"));
        q.addBindValue(warehouseId);
        if (!q.exec()) { fail("ServerError", QStringLiteral("读取仓库内容失败")); return; }
        QJsonArray items;
        while (q.next()) {
            const auto doc = QJsonDocument::fromJson(q.value(1).toByteArray());
            MediaRecord record; QString error;
            if (!doc.isObject() || !media_json::fromJson(doc.object(), record, error)) {
                fail("ServerError", QStringLiteral("仓库数据损坏")); return;
            }
            items.append(QJsonObject{{"item_id",q.value(0).toString()},{"media",doc.object()}});
        }
        success({{"warehouse",warehouse},{"items",items}}); return;
    }
    if (type == "add_item") {
        MediaRecord record; QString error;
        if (!payload.value("media").isObject() || !media_json::fromJson(payload.value("media").toObject(), record, error)) {
            fail("InvalidArgument", QStringLiteral("媒体数据无效")); return;
        }
        const QJsonObject media = media_json::toJson(record);
        // 提交前检查新增响应和完整仓库详情都能装入协议帧，避免写入成功但客户端收到断线。
        // 控制字符在 JSON 中会被转义为六个字节，覆盖 req_id 的最长编码。
        const QString maxRequestId(128, QChar(1));
        const QString maxItemId = QStringLiteral("9223372036854775807");
        const QJsonObject newItem{{"item_id",maxItemId},{"media",media}};
        QString sizeError;
        const QJsonObject addResponse{{"type","add_item_resp"},{"req_id",maxRequestId},
                                      {"payload",QJsonObject{{"item",newItem}}}};
        if (warehouse_protocol::encodeFrame(addResponse, sizeError).isEmpty()) {
            fail("InvalidArgument", QStringLiteral("媒体数据超过协议帧大小")); return;
        }
        QJsonArray existing;
        QSqlQuery current(db_);
        current.prepare(QStringLiteral("SELECT item_id,media_json FROM warehouse_items WHERE warehouse_id=? ORDER BY item_id"));
        current.addBindValue(warehouseId);
        if (!current.exec()) { fail("ServerError", QStringLiteral("读取仓库内容失败")); return; }
        while (current.next()) {
            const auto doc = QJsonDocument::fromJson(current.value(1).toByteArray());
            MediaRecord oldRecord; QString oldError;
            if (!doc.isObject() || !media_json::fromJson(doc.object(), oldRecord, oldError)) {
                fail("ServerError", QStringLiteral("仓库数据损坏")); return;
            }
            existing.append(QJsonObject{{"item_id",current.value(0).toString()},{"media",doc.object()}});
        }
        existing.append(newItem);
        const QJsonObject details{{"type","get_warehouse_resp"},{"req_id",maxRequestId},
                                  {"payload",QJsonObject{{"warehouse",summary(w)},{"items",existing}}}};
        if (warehouse_protocol::encodeFrame(details, sizeError).isEmpty()) {
            fail("InvalidArgument", QStringLiteral("仓库内容超过协议帧大小")); return;
        }
        QSqlQuery q(db_);
        q.prepare(QStringLiteral("INSERT INTO warehouse_items(warehouse_id,media_json) VALUES(?,?)"));
        q.addBindValue(warehouseId); q.addBindValue(QString::fromUtf8(QJsonDocument(media).toJson(QJsonDocument::Compact)));
        if (!q.exec()) { fail("ServerError", QStringLiteral("保存媒体失败")); return; }
        success({{"item",QJsonObject{{"item_id",q.lastInsertId().toString()},{"media",media}}}}); return;
    }
    qint64 itemId = 0;
    if (!positiveId(payload.value("item_id"), itemId)) { fail("InvalidArgument", QStringLiteral("条目编号无效")); return; }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM warehouse_items WHERE warehouse_id=? AND item_id=?"));
    q.addBindValue(warehouseId); q.addBindValue(itemId);
    if (!q.exec()) { fail("ServerError", QStringLiteral("删除条目失败")); return; }
    if (q.numRowsAffected() == 0) { fail("NotFound", QStringLiteral("条目不存在")); return; }
    success({});
}
