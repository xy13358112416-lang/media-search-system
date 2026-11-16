#include "warehouse_client.h"
#include "media_json.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <utility>

using namespace warehouse_contract;
namespace {
constexpr int TimeoutMs = 8000;
Status fail(ErrorCode code, const QString& text) { return {code, text}; }
Status invalidResponse() { return fail(ErrorCode::ProtocolError, QStringLiteral("服务器响应格式无效")); }
Status unknownOutcome() { return fail(ErrorCode::OutcomeUnknown, QStringLiteral("写入结果未知，请重新查询后再操作")); }
bool idFrom(const QJsonValue& value, qint64& id) {
    if (!value.isString()) return false;
    static const QRegularExpression re(QStringLiteral("^[1-9][0-9]*$"));
    const QString text = value.toString();
    if (!re.match(text).hasMatch()) return false;
    bool ok = false; id = text.toLongLong(&ok); return ok && id > 0;
}
bool parseSummary(const QJsonValue& value, WarehouseSummary& out) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    if (!idFrom(o.value("warehouse_id"), out.warehouseId) || !idFrom(o.value("owner_id"), out.ownerId) ||
        !o.value("name").isString() || o.value("name").toString().trimmed().isEmpty() ||
        !o.value("visibility").isString()) return false;
    const QString v = o.value("visibility").toString();
    if (v != "private" && v != "public") return false;
    out.name = o.value("name").toString();
    out.visibility = v == "public" ? Visibility::Public : Visibility::Private;
    return true;
}
bool parseItem(const QJsonValue& value, WarehouseItem& out) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    QString error;
    return idFrom(o.value("item_id"), out.itemId) && o.value("media").isObject() &&
           media_json::fromJson(o.value("media").toObject(), out.media, error);
}
ErrorCode errorCode(const QString& code) {
    if (code == "InvalidArgument") return ErrorCode::InvalidArgument;
    if (code == "NotConnected") return ErrorCode::NotConnected;
    if (code == "NotAuthenticated") return ErrorCode::NotAuthenticated;
    if (code == "AuthenticationFailed") return ErrorCode::AuthenticationFailed;
    if (code == "PermissionDenied") return ErrorCode::PermissionDenied;
    if (code == "NotFound") return ErrorCode::NotFound;
    if (code == "Timeout") return ErrorCode::Timeout;
    if (code == "ProtocolError") return ErrorCode::ProtocolError;
    if (code == "ServerError") return ErrorCode::ServerError;
    if (code == "Cancelled") return ErrorCode::Cancelled;
    if (code == "OutcomeUnknown") return ErrorCode::OutcomeUnknown;
    return ErrorCode::ProtocolError;
}
}

WarehouseClient::WarehouseClient(QObject* parent, int recommendationTimeoutMs)
    : QObject(parent), handlePrefix_(QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char(':')),
      recommendationTimeoutMs_(qMax(1, recommendationTimeoutMs)) {
    connect(&socket_, &QTcpSocket::connected, this, [this] { finishConnect({}); });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] { receive(); });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        token_.clear(); decoder_.reset();
        const auto status = fail(ErrorCode::NotConnected, QStringLiteral("连接已断开"));
        finishConnect(status); failAll(status);
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (connecting_) {
            const auto status = fail(ErrorCode::NotConnected, socket_.errorString());
            auto callback = std::move(connecting_); connecting_ = {};
            ++connectAttempt_;
            socket_.abort();
            callback(status);
        }
    });
}
WarehouseClient::~WarehouseClient() { disconnectFromServer(); }
void WarehouseClient::finishConnect(Status status) {
    auto cb = std::move(connecting_); connecting_ = {};
    if (cb) cb(std::move(status));
}
void WarehouseClient::failAll(Status status) {
    auto pending = std::move(pending_); pending_.clear();
    for (auto& p : pending) if (p.callback) p.callback(pendingFailure(p, status), {});
}
Status WarehouseClient::pendingFailure(const Pending& p, Status status) {
    if (p.mutation && (status.code == ErrorCode::Cancelled || status.code == ErrorCode::Timeout ||
                       status.code == ErrorCode::NotConnected || status.code == ErrorCode::ProtocolError))
        return unknownOutcome();
    return status;
}
void WarehouseClient::abortProtocol() {
    auto pending = std::move(pending_); pending_.clear();
    token_.clear();
    socket_.abort();
    decoder_.reset();
    for (auto& p : pending) if (p.callback) p.callback(pendingFailure(p, invalidResponse()), {});
}
void WarehouseClient::sendCancellation(const QString& targetId) {
    if (socket_.state() != QAbstractSocket::ConnectedState || token_.isEmpty()) return;
    const QString id = QString::number(++nextRequest_);
    const QJsonObject message{{"type","cancel_recommendation"},{"req_id",id},{"token",token_},
                              {"payload",QJsonObject{{"target_req_id",targetId}}}};
    QString error;
    const QByteArray frame = warehouse_protocol::encodeFrame(message, error);
    if (!frame.isEmpty()) socket_.write(frame); // 尽力通知服务端；迟到的取消响应会被忽略。
}
void WarehouseClient::cancelSessionRequests() {
    QList<Pending> cancelled;
    const auto ids = pending_.keys();
    for (const auto& id : ids) {
        const bool recommendation = pending_.value(id).responseType == "recommend_resources_resp";
        cancelled.append(pending_.take(id));
        if (recommendation) sendCancellation(id);
    }
    for (auto& p : cancelled)
        p.callback(pendingFailure(p, fail(ErrorCode::Cancelled, QStringLiteral("会话已切换，请求已取消"))), {});
}
void WarehouseClient::connectToServer(const QString& host, quint16 port, StatusCallback done) {
    const quint64 previousAttempt = connectAttempt_;
    disconnectFromServer();
    if (connectAttempt_ != previousAttempt + 1) {
        done(fail(ErrorCode::Cancelled, QStringLiteral("连接请求已被新的连接替代"))); return;
    }
    if (host.trimmed().isEmpty() || port == 0) {
        done(fail(ErrorCode::InvalidArgument, QStringLiteral("地址或端口无效"))); return;
    }
    connecting_ = std::move(done);
    const quint64 attempt = ++connectAttempt_;
    socket_.connectToHost(host, port);
    QTimer::singleShot(TimeoutMs, this, [this, attempt] {
        if (connecting_ && connectAttempt_ == attempt) {
            auto callback = std::move(connecting_); connecting_ = {};
            ++connectAttempt_;
            socket_.abort();
            callback(fail(ErrorCode::Timeout, QStringLiteral("连接超时")));
        }
    });
}
void WarehouseClient::disconnectFromServer() {
    ++connectAttempt_;
    const auto status = fail(ErrorCode::NotConnected, QStringLiteral("连接已关闭"));
    auto connecting = std::move(connecting_); connecting_ = {};
    auto pending = std::move(pending_); pending_.clear();
    token_.clear(); decoder_.reset();
    loginPending_ = false;
    logoutPending_ = false;
    socket_.abort();
    if (connecting) connecting(status);
    for (auto& p : pending) if (p.callback) p.callback(pendingFailure(p, status), {});
}
QString WarehouseClient::request(const QString& type, const QJsonObject& payload, Response callback, const QString& logoutToken) {
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        callback(fail(ErrorCode::NotConnected, QStringLiteral("请先连接服务器")), {}); return {};
    }
    if (logoutPending_ && type != "logout") {
        callback(fail(ErrorCode::NotAuthenticated, QStringLiteral("正在退出登录")), {}); return {};
    }
    if (loginPending_ && type != "login" && type != "logout") {
        callback(fail(ErrorCode::NotAuthenticated, QStringLiteral("正在切换登录")), {}); return {};
    }
    const QString activeToken = type == "logout" ? logoutToken : token_;
    if (type != "login" && activeToken.isEmpty()) {
        callback(fail(ErrorCode::NotAuthenticated, QStringLiteral("请先登录")), {}); return {};
    }
    const QString id = QString::number(++nextRequest_);
    QJsonObject req{{"type",type},{"req_id",id},{"payload",payload}};
    if (type != "login") req.insert("token", activeToken);
    QString error;
    const QByteArray frame = warehouse_protocol::encodeFrame(req, error);
    if (frame.isEmpty()) { callback(fail(ErrorCode::InvalidArgument, error), {}); return {}; }
    const bool mutation = type == "create_warehouse" || type == "add_item" || type == "remove_item" ||
                          type == "record_behavior" || type == "set_participation_settings" || type == "clear_history";
    pending_.insert(id, {type + QStringLiteral("_resp"), std::move(callback), mutation});
    if (socket_.write(frame) < 0) {
        auto p = pending_.take(id); p.callback(fail(ErrorCode::NotConnected, socket_.errorString()), {}); return {};
    }
    const int timeout = type == "recommend_resources" ? recommendationTimeoutMs_ : TimeoutMs;
    QTimer::singleShot(timeout, this, [this, id] {
        if (!pending_.contains(id)) return;
        auto p = pending_.take(id);
        if (p.responseType == "recommend_resources_resp") sendCancellation(id);
        p.callback(pendingFailure(p, fail(ErrorCode::Timeout, QStringLiteral("请求超时"))), {});
    });
    return id;
}
void WarehouseClient::receive() {
    QList<QJsonObject> messages; QString error;
    const bool valid = decoder_.feed(socket_.readAll(), messages, error);
    for (const auto& msg : messages) {
        if (!msg.value("req_id").isString() || msg.value("req_id").toString().isEmpty() ||
            !msg.value("type").isString()) {
            abortProtocol(); return;
        }
        const QString id = msg.value("req_id").toString();
        if (!pending_.contains(id)) continue; // 请求可能已经超时。
        auto p = pending_.take(id);
        const QString type = msg.value("type").toString();
        if (type == "error_resp") {
            if (!msg.value("code").isString() || !msg.value("message").isString()) {
                abortProtocol();
                p.callback(pendingFailure(p, invalidResponse()), {});
                return;
            } else {
                const auto code = errorCode(msg.value("code").toString());
                if (code == ErrorCode::ProtocolError) abortProtocol();
                p.callback(pendingFailure(p, fail(code, msg.value("message").toString())), {});
                if (code == ErrorCode::ProtocolError) return;
            }
        } else if (type != p.responseType || !msg.value("payload").isObject()) {
            abortProtocol();
            p.callback(pendingFailure(p, invalidResponse()), {});
            return;
        } else p.callback({}, msg.value("payload").toObject());
    }
    if (!valid) abortProtocol();
}
void WarehouseClient::login(const QString& username, const QString& password, Callback<UserInfo> done) {
    if (loginPending_ || logoutPending_) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("已有登录请求正在进行")),{}}); return;
    }
    loginPending_ = true;
    cancelSessionRequests();
    const QString oldToken = std::exchange(token_, QString());
    if (!oldToken.isEmpty()) {
        const quint64 attempt = connectAttempt_;
        request("logout", {}, [this, attempt, username, password, done=std::move(done)](Status status, QJsonObject) mutable {
            if (connectAttempt_ != attempt) {
                done({status.ok() ? fail(ErrorCode::Cancelled, QStringLiteral("登录请求已被新的连接操作替代")) : status, {}});
                return;
            }
            if (!status.ok()) {
                loginPending_ = false;
                disconnectFromServer();
                done({status,{}});
                return;
            }
            sendLogin(username, password, std::move(done));
        }, oldToken);
        return;
    }
    sendLogin(username, password, std::move(done));
}
void WarehouseClient::sendLogin(const QString& username, const QString& password, Callback<UserInfo> done) {
    request("login", {{"username",username},{"password",password}},
        [this, done=std::move(done)](Status status, QJsonObject payload) {
            loginPending_ = false;
            if (!status.ok()) {
                // A timed-out login may already have created a server session whose token we never received.
                if (status.code == ErrorCode::Timeout) disconnectFromServer();
                done({status,{}}); return;
            }
            const auto u = payload.value("user").toObject();
            UserInfo result;
            if (!payload.value("token").isString() || payload.value("token").toString().isEmpty() ||
                !payload.value("user").isObject() || !idFrom(u.value("user_id"), result.userId) ||
                !u.value("username").isString() || u.value("username").toString().isEmpty()) {
                abortProtocol();
                done({invalidResponse(),{}}); return;
            }
            result.username = u.value("username").toString();
            token_ = payload.value("token").toString();
            done({{}, result});
        });
}
void WarehouseClient::logout(StatusCallback done) {
    if (logoutPending_) {
        done(fail(ErrorCode::InvalidArgument, QStringLiteral("退出请求正在进行"))); return;
    }
    if (loginPending_) {
        // 未拿到 token 无法发送登出；关闭连接使服务器侧登录结果失效。
        disconnectFromServer();
        done({});
        return;
    }
    logoutPending_ = true;
    const quint64 attempt = connectAttempt_;
    cancelSessionRequests();
    if (connectAttempt_ != attempt) {
        // Cancellation callbacks may have replaced the connection; leave that new session alone.
        done(fail(ErrorCode::Cancelled, QStringLiteral("退出请求已被新的连接操作替代"))); return;
    }
    const QString oldToken = std::exchange(token_, QString());
    request("logout", {}, [this, attempt, done=std::move(done)](Status status, QJsonObject) {
        if (connectAttempt_ == attempt) {
            logoutPending_ = false;
            if (!status.ok()) disconnectFromServer();
        }
        done(status);
    }, oldToken);
}
void WarehouseClient::listMyWarehouses(Callback<QList<WarehouseSummary>> done) {
    request("list_my_warehouses", {}, [this, done=std::move(done)](Status status, QJsonObject p) {
        if (!status.ok()) { done({status,{}}); return; }
        if (!p.value("warehouses").isArray()) { abortProtocol(); done({invalidResponse(),{}}); return; }
        QList<WarehouseSummary> list;
        for (const auto& value : p.value("warehouses").toArray()) {
            WarehouseSummary item; if (!parseSummary(value, item)) { abortProtocol(); done({invalidResponse(),{}}); return; }
            list.append(item);
        }
        done({{},list});
    });
}
void WarehouseClient::listPublicWarehouses(Callback<QList<WarehouseSummary>> done) {
    request("list_public_warehouses", {}, [this, done=std::move(done)](Status status, QJsonObject p) {
        if (!status.ok()) { done({status,{}}); return; }
        if (!p.value("warehouses").isArray()) { abortProtocol(); done({invalidResponse(),{}}); return; }
        QList<WarehouseSummary> list;
        for (const auto& value : p.value("warehouses").toArray()) {
            WarehouseSummary item; if (!parseSummary(value, item)) { abortProtocol(); done({invalidResponse(),{}}); return; }
            list.append(item);
        }
        done({{},list});
    });
}
void WarehouseClient::getWarehouse(qint64 warehouseId, Callback<WarehouseDetail> done) {
    request("get_warehouse", {{"warehouse_id",QString::number(warehouseId)}},
        [this, done=std::move(done)](Status status, QJsonObject p) {
            if (!status.ok()) { done({status,{}}); return; }
            WarehouseDetail detail;
            if (!parseSummary(p.value("warehouse"), detail.warehouse) || !p.value("items").isArray()) {
                abortProtocol(); done({invalidResponse(),{}}); return;
            }
            for (const auto& value : p.value("items").toArray()) {
                WarehouseItem item; if (!parseItem(value, item)) { abortProtocol(); done({invalidResponse(),{}}); return; }
                detail.items.append(item);
            }
            done({{},detail});
        });
}
void WarehouseClient::createWarehouse(const QString& name, Visibility visibility, Callback<WarehouseSummary> done) {
    if (visibility != Visibility::Private && visibility != Visibility::Public) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("可见性无效")),{}}); return;
    }
    request("create_warehouse", {{"name",name},{"visibility",visibility == Visibility::Public ? "public" : "private"}},
        [this, done=std::move(done)](Status status, QJsonObject p) {
            if (!status.ok()) { done({status,{}}); return; }
            WarehouseSummary item;
            if (!parseSummary(p.value("warehouse"), item)) { abortProtocol(); done({unknownOutcome(),{}}); return; }
            done({{},item});
        });
}
void WarehouseClient::addItem(qint64 warehouseId, const MediaRecord& media, Callback<WarehouseItem> done) {
    request("add_item", {{"warehouse_id",QString::number(warehouseId)},{"media",media_json::toJson(media)}},
        [this, done=std::move(done)](Status status, QJsonObject p) {
            if (!status.ok()) { done({status,{}}); return; }
            WarehouseItem item;
            if (!parseItem(p.value("item"), item)) { abortProtocol(); done({unknownOutcome(),{}}); return; }
            done({{},item});
        });
}
void WarehouseClient::removeItem(qint64 warehouseId, qint64 itemId, StatusCallback done) {
    request("remove_item", {{"warehouse_id",QString::number(warehouseId)},{"item_id",QString::number(itemId)}},
        [done=std::move(done)](Status status, QJsonObject) { done(status); });
}
RecommendationHandle WarehouseClient::recommendResources(const WarehouseRecommendationRequest& r, Callback<WarehouseRecommendationResult> done) {
    if (r.query.trimmed().isEmpty() || r.query.size() > 1000 || r.limit < 1 || r.limit > 50 ||
        (r.targetWarehouseId && *r.targetWarehouseId <= 0)) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("推荐条件无效")),{}}); return {};
    }
    QJsonObject payload{{"query",r.query},{"limit",r.limit}};
    if (r.targetWarehouseId) payload.insert("warehouse_id", QString::number(*r.targetWarehouseId));
    const QString id = request("recommend_resources", payload, [this, done=std::move(done)](Status status, QJsonObject p) {
        if (!status.ok()) { done({status,{}}); return; }
        if (!p.value("items").isArray() || !p.value("fallback").isBool() || !p.value("message").isString()) {
            abortProtocol(); done({invalidResponse(),{}}); return;
        }
        WarehouseRecommendationResult result;
        result.fallback = p.value("fallback").toBool();
        result.message = p.value("message").toString();
        for (const auto& value : p.value("items").toArray()) {
            if (!value.isObject()) { abortProtocol(); done({invalidResponse(),{}}); return; }
            const auto o = value.toObject();
            WarehouseRecommendationItem item;
            QString error;
            if (!idFrom(o.value("warehouse_id"), item.warehouseId) || !idFrom(o.value("item_id"), item.itemId) ||
                !o.value("media").isObject() || !media_json::fromJson(o.value("media").toObject(), item.media, error) ||
                !o.value("reason").isString()) {
                abortProtocol(); done({invalidResponse(),{}}); return;
            }
            item.reason = o.value("reason").toString();
            result.items.append(item);
        }
        done({{},result});
    });
    return id.isEmpty() ? RecommendationHandle{} : handlePrefix_ + id;
}
bool WarehouseClient::cancelRecommendation(const RecommendationHandle& handle) {
    if (!handle.startsWith(handlePrefix_)) return false;
    const QString id = handle.mid(handlePrefix_.size());
    const auto it = pending_.constFind(id);
    if (id.isEmpty() || it == pending_.cend() || (it->responseType != "recommend_resources_resp" && it->responseType != "recommend_temporal_resp")) return false;
    auto p = pending_.take(id);
    if (p.responseType == "recommend_resources_resp") sendCancellation(id);
    p.callback(fail(ErrorCode::Cancelled, QStringLiteral("推荐请求已取消")), {});
    return true;
}
