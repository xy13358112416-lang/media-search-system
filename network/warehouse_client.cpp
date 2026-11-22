// >>> MAIN-DEFENSE | 主逻辑文件导读
// 在线TCP客户端：连接、请求编号、token、超时、拆包和响应回调。Qt通过接口调用它；不在客户端决定仓库读写权限。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "warehouse_client.h"
#include "media_json.h"
#include <QJsonArray>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <utility>

using namespace warehouse_contract;
namespace {
constexpr int TimeoutMs = 8000;
// >>> MAIN-DEFENSE | 函数入口｜fail
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status fail(ErrorCode code, const QString& text) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status fail(ErrorCode code, const QString& text) { return {code, text}; }
// >>> MAIN-DEFENSE | 函数入口｜invalidResponse
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status invalidResponse() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status invalidResponse() { return fail(ErrorCode::ProtocolError, QStringLiteral("服务器响应格式无效")); }
// >>> MAIN-DEFENSE | 函数入口｜unknownOutcome
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status unknownOutcome() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status unknownOutcome() { return fail(ErrorCode::OutcomeUnknown, QStringLiteral("写入结果未知，请重新查询后再操作")); }
// >>> MAIN-DEFENSE | 函数入口｜idFrom
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool idFrom(const QJsonValue& value, qint64& id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool idFrom(const QJsonValue& value, qint64& id) {
    if (!value.isString()) return false;
    static const QRegularExpression re(QStringLiteral("^[1-9][0-9]*$"));
    const QString text = value.toString();
    // >>> MAIN-DEFENSE | idFrom｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!re.match(text).hasMatch()) return false;
    bool ok = false; id = text.toLongLong(&ok); return ok && id > 0;
}
// >>> MAIN-DEFENSE | 函数入口｜parseSummary
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool parseSummary(const QJsonValue& value, WarehouseSummary& out) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool parseSummary(const QJsonValue& value, WarehouseSummary& out) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    if (!idFrom(o.value("warehouse_id"), out.warehouseId) || !idFrom(o.value("owner_id"), out.ownerId) ||
        !o.value("name").isString() || o.value("name").toString().trimmed().isEmpty() ||
        !o.value("visibility").isString()) return false;
    const QString v = o.value("visibility").toString();
    // >>> MAIN-DEFENSE | parseSummary｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (v != "private" && v != "public") return false;
    out.name = o.value("name").toString();
    out.visibility = v == "public" ? Visibility::Public : Visibility::Private;
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜parseItem
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool parseItem(const QJsonValue& value, WarehouseItem& out) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool parseItem(const QJsonValue& value, WarehouseItem& out) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    QString error;
    // >>> MAIN-DEFENSE | parseItem｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return idFrom(o.value("item_id"), out.itemId) && o.value("media").isObject() &&
           media_json::fromJson(o.value("media").toObject(), out.media, error);
}
// >>> MAIN-DEFENSE | 函数入口｜errorCode
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：ErrorCode errorCode(const QString& code) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
ErrorCode errorCode(const QString& code) {
    if (code == "InvalidArgument") return ErrorCode::InvalidArgument;
    if (code == "NotConnected") return ErrorCode::NotConnected;
    if (code == "NotAuthenticated") return ErrorCode::NotAuthenticated;
    // >>> MAIN-DEFENSE | errorCode｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (code == "AuthenticationFailed") return ErrorCode::AuthenticationFailed;
    if (code == "PermissionDenied") return ErrorCode::PermissionDenied;
    if (code == "NotFound") return ErrorCode::NotFound;
    if (code == "Timeout") return ErrorCode::Timeout;
    // >>> MAIN-DEFENSE | errorCode｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (code == "ProtocolError") return ErrorCode::ProtocolError;
    if (code == "ServerError") return ErrorCode::ServerError;
    if (code == "Cancelled") return ErrorCode::Cancelled;
    if (code == "OutcomeUnknown") return ErrorCode::OutcomeUnknown;
    // >>> MAIN-DEFENSE | errorCode｜处理阶段 03
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return ErrorCode::ProtocolError;
}
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::WarehouseClient
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：WarehouseClient::WarehouseClient(QObject* parent, int recommendationTimeoutMs)      : QObject(parent), handlePrefix_(QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char(':')),        recommendationTimeoutM
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
WarehouseClient::WarehouseClient(QObject* parent, int recommendationTimeoutMs)
    : QObject(parent), handlePrefix_(QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char(':')),
      recommendationTimeoutMs_(qMax(1, recommendationTimeoutMs)) {
    // The warehouse uses a direct TCP protocol, not an HTTP proxy transport.
    // Keep HTTP/model traffic's application proxy configuration untouched.
    socket_.setProxy(QNetworkProxy::NoProxy);
    // >>> MAIN-DEFENSE | WarehouseClient｜处理阶段 01
    // 注册异步信号回调：这里只登记后续要执行的处理，网络完成时才运行lambda，不是在此同步等结果。
    // <<< MAIN-DEFENSE
    connect(&socket_, &QTcpSocket::connected, this, [this] { finishConnect({}); });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] { receive(); });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        token_.clear(); decoder_.reset();
        const auto status = fail(ErrorCode::NotConnected, QStringLiteral("连接已断开"));
        finishConnect(status); failAll(status);
    });
    // >>> MAIN-DEFENSE | WarehouseClient｜处理阶段 02
    // 注册异步信号回调：这里只登记后续要执行的处理，网络完成时才运行lambda，不是在此同步等结果。
    // <<< MAIN-DEFENSE
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
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::~WarehouseClient
// 析构收尾：释放本对象持有的连接/请求等资源；注意Qt回调与数据库句柄的生命周期。
// 签名提示：WarehouseClient::~WarehouseClient() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
WarehouseClient::~WarehouseClient() { disconnectFromServer(); }
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::finishConnect
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：void WarehouseClient::finishConnect(Status status) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::finishConnect(Status status) {
    auto cb = std::move(connecting_); connecting_ = {};
    if (cb) cb(std::move(status));
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::failAll
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：void WarehouseClient::failAll(Status status) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::failAll(Status status) {
    auto pending = std::move(pending_); pending_.clear();
    for (auto& p : pending) if (p.callback) p.callback(pendingFailure(p, status), {});
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::pendingFailure
// 写入请求断线可能已到服务器，返回OutcomeUnknown提示先查询，不能保证失败就是没写入。
// 签名提示：Status WarehouseClient::pendingFailure(const Pending& p, Status status) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status WarehouseClient::pendingFailure(const Pending& p, Status status) {
    if (p.mutation && (status.code == ErrorCode::Cancelled || status.code == ErrorCode::Timeout ||
                       status.code == ErrorCode::NotConnected || status.code == ErrorCode::ProtocolError))
        return unknownOutcome();
    // >>> MAIN-DEFENSE | pendingFailure｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return status;
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::abortProtocol
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：void WarehouseClient::abortProtocol() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::abortProtocol() {
    auto pending = std::move(pending_); pending_.clear();
    token_.clear();
    socket_.abort();
    decoder_.reset();
    // >>> MAIN-DEFENSE | abortProtocol｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (auto& p : pending) if (p.callback) p.callback(pendingFailure(p, invalidResponse()), {});
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::sendCancellation
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：void WarehouseClient::sendCancellation(const QString& targetId) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::sendCancellation(const QString& targetId) {
    if (socket_.state() != QAbstractSocket::ConnectedState || token_.isEmpty()) return;
    const QString id = QString::number(++nextRequest_);
    const QJsonObject message{{"type","cancel_recommendation"},{"req_id",id},{"token",token_},
                              {"payload",QJsonObject{{"target_req_id",targetId}}}};
    QString error;
    const QByteArray frame = warehouse_protocol::encodeFrame(message, error);
    // >>> MAIN-DEFENSE | sendCancellation｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!frame.isEmpty()) socket_.write(frame); // 尽力通知服务端；迟到的取消响应会被忽略。
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::cancelSessionRequests
// 取消当前会话关联任务，不把旧请求带到后续登录。
// 签名提示：void WarehouseClient::cancelSessionRequests() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::cancelSessionRequests() {
    QList<Pending> cancelled;
    const auto ids = pending_.keys();
    for (const auto& id : ids) {
        const bool cancelOnServer = pending_.value(id).cancelOnServer;
        cancelled.append(pending_.take(id));
        // >>> MAIN-DEFENSE | cancelSessionRequests｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (cancelOnServer) sendCancellation(id);
    }
    for (auto& p : cancelled)
        p.callback(pendingFailure(p, fail(ErrorCode::Cancelled, QStringLiteral("会话已切换，请求已取消"))), {});
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::connectToServer
// 异步建立TCP连接并设超时；连接代次避免旧回调覆盖新连接。
// 签名提示：void WarehouseClient::connectToServer(const QString& host, quint16 port, StatusCallback done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::connectToServer(const QString& host, quint16 port, StatusCallback done) {
    const quint64 previousAttempt = connectAttempt_;
    disconnectFromServer();
    if (connectAttempt_ != previousAttempt + 1) {
        done(fail(ErrorCode::Cancelled, QStringLiteral("连接请求已被新的连接替代"))); return;
    }
    // >>> MAIN-DEFENSE | connectToServer｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (host.trimmed().isEmpty() || port == 0) {
        done(fail(ErrorCode::InvalidArgument, QStringLiteral("地址或端口无效"))); return;
    }
    connecting_ = std::move(done);
    const quint64 attempt = ++connectAttempt_;
    socket_.connectToHost(host, port);
    QTimer::singleShot(TimeoutMs, this, [this, attempt] {
        // >>> MAIN-DEFENSE | connectToServer｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (connecting_ && connectAttempt_ == attempt) {
            auto callback = std::move(connecting_); connecting_ = {};
            ++connectAttempt_;
            socket_.abort();
            callback(fail(ErrorCode::Timeout, QStringLiteral("连接超时")));
        }
    });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::disconnectFromServer
// 主动断开连接，清token并处理所有等待请求，避免新账号收到旧结果。
// 签名提示：void WarehouseClient::disconnectFromServer() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::disconnectFromServer() {
    ++connectAttempt_;
    const auto status = fail(ErrorCode::NotConnected, QStringLiteral("连接已关闭"));
    auto connecting = std::move(connecting_); connecting_ = {};
    auto pending = std::move(pending_); pending_.clear();
    token_.clear(); decoder_.reset();
    loginPending_ = false;
    logoutPending_ = false;
    socket_.abort();
    // >>> MAIN-DEFENSE | disconnectFromServer｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (connecting) connecting(status);
    for (auto& p : pending) if (p.callback) p.callback(pendingFailure(p, status), {});
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::request
// 构造type/req_id/token/payload，登记等待回调与超时，再将长度帧写入socket。
// 签名提示：QString WarehouseClient::request(const QString& type, const QJsonObject& payload, Response callback, const QString& logoutToken) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString WarehouseClient::request(const QString& type, const QJsonObject& payload, Response callback, const QString& logoutToken) {
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        callback(fail(ErrorCode::NotConnected, QStringLiteral("请先连接服务器")), {}); return {};
    }
    // >>> MAIN-DEFENSE | request｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (logoutPending_ && type != "logout") {
        callback(fail(ErrorCode::NotAuthenticated, QStringLiteral("正在退出登录")), {}); return {};
    }
    if (loginPending_ && type != "login" && type != "logout") {
        callback(fail(ErrorCode::NotAuthenticated, QStringLiteral("正在切换登录")), {}); return {};
    }
    const QString activeToken = type == "logout" ? logoutToken : token_;
    // >>> MAIN-DEFENSE | request｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (type != "login" && activeToken.isEmpty()) {
        callback(fail(ErrorCode::NotAuthenticated, QStringLiteral("请先登录")), {}); return {};
    }
    const QString id = QString::number(++nextRequest_);
    QJsonObject req{{"type",type},{"req_id",id},{"payload",payload}};
    // >>> MAIN-DEFENSE | request｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (type != "login") req.insert("token", activeToken);
    QString error;
    const QByteArray frame = warehouse_protocol::encodeFrame(req, error);
    if (frame.isEmpty()) { callback(fail(ErrorCode::InvalidArgument, error), {}); return {}; }
    const bool mutation = type == "create_warehouse" || type == "add_item" || type == "remove_item" ||
                          type == "record_behavior" || type == "set_participation_settings" || type == "clear_history" ||
                          type == "set_work_feedback" || type == "set_follow" ||
                          (type.startsWith("admin_") && type != "admin_list_users");
    const bool cancelOnServer = type == "recommend_resources" || type == "recommend_temporal" ||
        (type == "get_trending" && payload.value("summary_mode") == "model");
    pending_.insert(id, {type + QStringLiteral("_resp"), std::move(callback), mutation, cancelOnServer});
    // >>> MAIN-DEFENSE | request｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (socket_.write(frame) < 0) {
        auto p = pending_.take(id); p.callback(fail(ErrorCode::NotConnected, socket_.errorString()), {}); return {};
    }
    const int timeout = type == "search_person_web" ? 35000
        : cancelOnServer ? recommendationTimeoutMs_ : TimeoutMs;
    QTimer::singleShot(timeout, this, [this, id] {
        // >>> MAIN-DEFENSE | request｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!pending_.contains(id)) return;
        auto p = pending_.take(id);
        if (p.cancelOnServer) sendCancellation(id);
        p.callback(pendingFailure(p, fail(ErrorCode::Timeout, QStringLiteral("请求超时"))), {});
    });
    // >>> MAIN-DEFENSE | request｜处理阶段 06
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return id;
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::receive
// 从socket读字节交FrameDecoder，按req_id匹配等待请求；不合法响应应终止协议会话。
// 签名提示：void WarehouseClient::receive() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::receive() {
    QList<QJsonObject> messages; QString error;
    const bool valid = decoder_.feed(socket_.readAll(), messages, error);
    for (const auto& msg : messages) {
        // >>> MAIN-DEFENSE | receive｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!msg.value("req_id").isString() || msg.value("req_id").toString().isEmpty() ||
            !msg.value("type").isString()) {
            abortProtocol(); return;
        }
        const QString id = msg.value("req_id").toString();
        // >>> MAIN-DEFENSE | receive｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!pending_.contains(id)) continue; // 请求可能已经超时。
        auto p = pending_.take(id);
        const QString type = msg.value("type").toString();
        if (type == "error_resp") {
            // >>> MAIN-DEFENSE | receive｜处理阶段 03
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!msg.value("code").isString() || !msg.value("message").isString()) {
                abortProtocol();
                p.callback(pendingFailure(p, invalidResponse()), {});
                return;
            } else {
                const auto code = errorCode(msg.value("code").toString());
                // >>> MAIN-DEFENSE | receive｜处理阶段 04
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | receive｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!valid) abortProtocol();
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::login
// 按规范化用户名读密码哈希，验证明文输入；成功后建立当前会话，失败不能保留旧身份。
// 签名提示：void WarehouseClient::login(const QString& username, const QString& password, Callback<UserInfo> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::login(const QString& username, const QString& password, Callback<UserInfo> done) {
    loginWithRole(username, password, {}, std::move(done));
}
void WarehouseClient::loginAs(const QString& username, const QString& password, LoginRole role, Callback<UserInfo> done) {
    loginWithRole(username, password, role == LoginRole::Administrator ? "admin" : "user", std::move(done));
}
void WarehouseClient::loginWithRole(const QString& username, const QString& password,
                                    const QString& role, Callback<UserInfo> done) {
    if (loginPending_ || logoutPending_) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("已有登录请求正在进行")),{}}); return;
    }
    loginPending_ = true;
    cancelSessionRequests();
    const QString oldToken = std::exchange(token_, QString());
    // >>> MAIN-DEFENSE | login｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!oldToken.isEmpty()) {
        const quint64 attempt = connectAttempt_;
        request("logout", {}, [this, attempt, username, password, role, done=std::move(done)](Status status, QJsonObject) mutable {
            if (connectAttempt_ != attempt) {
                done({status.ok() ? fail(ErrorCode::Cancelled, QStringLiteral("登录请求已被新的连接操作替代")) : status, {}});
                return;
            }
            // >>> MAIN-DEFENSE | login｜处理阶段 02
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!status.ok()) {
                loginPending_ = false;
                disconnectFromServer();
                done({status,{}});
                return;
            }
            sendLogin(username, password, role, std::move(done));
        }, oldToken);
        return;
    }
    sendLogin(username, password, role, std::move(done));
}
void WarehouseClient::sendLogin(const QString& username, const QString& password, const QString& role, Callback<UserInfo> done) {
    QJsonObject input{{"username", username}, {"password", password}};
    if (!role.isEmpty()) input.insert("login_role", role);
    request("login", input,
        [this, role, done=std::move(done)](Status status, QJsonObject payload) {
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::sendLogin
// 发登录请求并验证返回字段；通过认证才保存token和用户身份。
// 签名提示：void WarehouseClient::sendLogin(const QString& username, const QString& password, Callback<UserInfo> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
            loginPending_ = false;
            // >>> MAIN-DEFENSE | sendLogin｜处理阶段 01
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!status.ok()) {
                // A timed-out login may already have created a server session whose token we never received.
                if (status.code == ErrorCode::Timeout) disconnectFromServer();
                done({status,{}}); return;
            }
            const auto u = payload.value("user").toObject();
            UserInfo result;
            // >>> MAIN-DEFENSE | sendLogin｜处理阶段 02
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!payload.value("token").isString() || payload.value("token").toString().isEmpty() ||
                !payload.value("user").isObject() || !idFrom(u.value("user_id"), result.userId) ||
                !u.value("username").isString() || u.value("username").toString().isEmpty()) {
                abortProtocol();
                done({invalidResponse(),{}}); return;
            }
            if (u.contains("is_admin") && !u.value("is_admin").isBool()) {
                abortProtocol(); done({invalidResponse(), {}}); return;
            }
            result.isAdmin = u.value("is_admin").toBool(false);
            result.username = u.value("username").toString();
            if (!role.isEmpty() && result.isAdmin != (role == "admin")) {
                disconnectFromServer();
                done({fail(ErrorCode::PermissionDenied, QStringLiteral("账号角色与登录入口不匹配。")), {}});
                return;
            }
            token_ = payload.value("token").toString();
            done({{}, result});
        });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::logout
// 清当前登录状态，使后续受保护操作不能沿用退出前身份。
// 签名提示：void WarehouseClient::logout(StatusCallback done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::logout(StatusCallback done) {
    if (logoutPending_) {
        done(fail(ErrorCode::InvalidArgument, QStringLiteral("退出请求正在进行"))); return;
    }
    // >>> MAIN-DEFENSE | logout｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (loginPending_) {
        // 未拿到 token 无法发送登出；关闭连接使服务器侧登录结果失效。
        disconnectFromServer();
        done({});
        return;
    }
    logoutPending_ = true;
    const quint64 attempt = connectAttempt_;
    cancelSessionRequests();
    // >>> MAIN-DEFENSE | logout｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (connectAttempt_ != attempt) {
        // Cancellation callbacks may have replaced the connection; leave that new session alone.
        done(fail(ErrorCode::Cancelled, QStringLiteral("退出请求已被新的连接操作替代"))); return;
    }
    const QString oldToken = std::exchange(token_, QString());
    request("logout", {}, [this, attempt, done=std::move(done)](Status status, QJsonObject) {
        // >>> MAIN-DEFENSE | logout｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (connectAttempt_ == attempt) {
            logoutPending_ = false;
            if (!status.ok()) disconnectFromServer();
        }
        done(status);
    }, oldToken);
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::listMyWarehouses
// 只查询当前会话拥有的仓库，身份从token解析。
// 签名提示：void WarehouseClient::listMyWarehouses(Callback<QList<WarehouseSummary>> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::listMyWarehouses(Callback<QList<WarehouseSummary>> done) {
    request("list_my_warehouses", {}, [this, done=std::move(done)](Status status, QJsonObject p) {
        if (!status.ok()) { done({status,{}}); return; }
        if (!p.value("warehouses").isArray()) { abortProtocol(); done({invalidResponse(),{}}); return; }
        QList<WarehouseSummary> list;
        // >>> MAIN-DEFENSE | listMyWarehouses｜处理阶段 01
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& value : p.value("warehouses").toArray()) {
            WarehouseSummary item; if (!parseSummary(value, item)) { abortProtocol(); done({invalidResponse(),{}}); return; }
            list.append(item);
        }
        done({{},list});
    });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::listPublicWarehouses
// 查询公开仓库；公开可读不代表所有人可增删。
// 签名提示：void WarehouseClient::listPublicWarehouses(Callback<QList<WarehouseSummary>> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::listPublicWarehouses(Callback<QList<WarehouseSummary>> done) {
    request("list_public_warehouses", {}, [this, done=std::move(done)](Status status, QJsonObject p) {
        if (!status.ok()) { done({status,{}}); return; }
        if (!p.value("warehouses").isArray()) { abortProtocol(); done({invalidResponse(),{}}); return; }
        QList<WarehouseSummary> list;
        // >>> MAIN-DEFENSE | listPublicWarehouses｜处理阶段 01
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& value : p.value("warehouses").toArray()) {
            WarehouseSummary item; if (!parseSummary(value, item)) { abortProtocol(); done({invalidResponse(),{}}); return; }
            list.append(item);
        }
        done({{},list});
    });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::getWarehouse
// 获取一个有读权限仓库的详情与条目。
// 签名提示：void WarehouseClient::getWarehouse(qint64 warehouseId, Callback<WarehouseDetail> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::getWarehouse(qint64 warehouseId, Callback<WarehouseDetail> done) {
    request("get_warehouse", {{"warehouse_id",QString::number(warehouseId)}},
        [this, done=std::move(done)](Status status, QJsonObject p) {
            if (!status.ok()) { done({status,{}}); return; }
            WarehouseDetail detail;
            // >>> MAIN-DEFENSE | getWarehouse｜处理阶段 01
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
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
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::createWarehouse
// 创建归当前用户所有的公开或私有仓库。
// 签名提示：void WarehouseClient::createWarehouse(const QString& name, Visibility visibility, Callback<WarehouseSummary> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::createWarehouse(const QString& name, Visibility visibility, Callback<WarehouseSummary> done) {
    if (visibility != Visibility::Private && visibility != Visibility::Public) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("可见性无效")),{}}); return;
    }
    request("create_warehouse", {{"name",name},{"visibility",visibility == Visibility::Public ? "public" : "private"}},
        [this, done=std::move(done)](Status status, QJsonObject p) {
            // >>> MAIN-DEFENSE | createWarehouse｜处理阶段 01
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!status.ok()) { done({status,{}}); return; }
            WarehouseSummary item;
            if (!parseSummary(p.value("warehouse"), item)) { abortProtocol(); done({unknownOutcome(),{}}); return; }
            done({{},item});
        });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::addItem
// 向目标仓库新增条目；最终写权限必须由服务端检查owner。
// 签名提示：void WarehouseClient::addItem(qint64 warehouseId, const MediaRecord& media, Callback<WarehouseItem> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::addItem(qint64 warehouseId, const MediaRecord& media, Callback<WarehouseItem> done) {
    request("add_item", {{"warehouse_id",QString::number(warehouseId)},{"media",media_json::toJson(media)}},
        [this, done=std::move(done)](Status status, QJsonObject p) {
            if (!status.ok()) { done({status,{}}); return; }
            WarehouseItem item;
            // >>> MAIN-DEFENSE | addItem｜处理阶段 01
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!parseItem(p.value("item"), item)) { abortProtocol(); done({unknownOutcome(),{}}); return; }
            done({{},item});
        });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::removeItem
// 从目标仓库删除指定条目；不允许以客户端按钮状态代替owner校验。
// 签名提示：void WarehouseClient::removeItem(qint64 warehouseId, qint64 itemId, StatusCallback done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseClient::removeItem(qint64 warehouseId, qint64 itemId, StatusCallback done) {
    request("remove_item", {{"warehouse_id",QString::number(warehouseId)},{"item_id",QString::number(itemId)}},
        [done=std::move(done)](Status status, QJsonObject) { done(status); });
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::recommendResources
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：RecommendationHandle WarehouseClient::recommendResources(const WarehouseRecommendationRequest& r, Callback<WarehouseRecommendationResult> done) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RecommendationHandle WarehouseClient::recommendResources(const WarehouseRecommendationRequest& r, Callback<WarehouseRecommendationResult> done) {
    if (r.query.trimmed().isEmpty() || r.query.size() > 1000 || r.limit < 1 || r.limit > 50 ||
        (r.targetWarehouseId && *r.targetWarehouseId <= 0)) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("推荐条件无效")),{}}); return {};
    }
    QJsonObject payload{{"query",r.query},{"limit",r.limit}};
    // >>> MAIN-DEFENSE | recommendResources｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (r.targetWarehouseId) payload.insert("warehouse_id", QString::number(*r.targetWarehouseId));
    const QString id = request("recommend_resources", payload, [this, done=std::move(done)](Status status, QJsonObject p) {
        if (!status.ok()) { done({status,{}}); return; }
        if (!p.value("items").isArray() || !p.value("fallback").isBool() || !p.value("message").isString()) {
            abortProtocol(); done({invalidResponse(),{}}); return;
        }
        WarehouseRecommendationResult result;
        result.fallback = p.value("fallback").toBool();
        result.message = p.value("message").toString();
        // >>> MAIN-DEFENSE | recommendResources｜处理阶段 02
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& value : p.value("items").toArray()) {
            if (!value.isObject()) { abortProtocol(); done({invalidResponse(),{}}); return; }
            const auto o = value.toObject();
            WarehouseRecommendationItem item;
            QString error;
            // >>> MAIN-DEFENSE | recommendResources｜处理阶段 03
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | recommendResources｜处理阶段 04
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return id.isEmpty() ? RecommendationHandle{} : handlePrefix_ + id;
}
// >>> MAIN-DEFENSE | 函数入口｜WarehouseClient::cancelRecommendation
// 通过请求句柄取消模型类任务并抑制旧结果交付。
// 签名提示：bool WarehouseClient::cancelRecommendation(const RecommendationHandle& handle) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool WarehouseClient::cancelRecommendation(const RecommendationHandle& handle) {
    if (!handle.startsWith(handlePrefix_)) return false;
    const QString id = handle.mid(handlePrefix_.size());
    const auto it = pending_.constFind(id);
    // >>> MAIN-DEFENSE | cancelRecommendation｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (id.isEmpty() || it == pending_.cend() || (it->responseType != "recommend_resources_resp" && it->responseType != "recommend_temporal_resp")) return false;
    auto p = pending_.take(id);
    sendCancellation(id);
    p.callback(fail(ErrorCode::Cancelled, QStringLiteral("推荐请求已取消")), {});
    // >>> MAIN-DEFENSE | cancelRecommendation｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

void WarehouseClient::adminRequest(const QString& operation, const QJsonObject& payload,
                                   Callback<QJsonObject> done)
{
    if (!operation.startsWith("admin_")) {
        done({fail(ErrorCode::InvalidArgument, QStringLiteral("无效管理操作")), {}}); return;
    }
    request(operation, payload, [done=std::move(done)](Status status, QJsonObject result) {
        if (!status.ok()) { done({status, {}}); return; }
        done({{}, result});
    });
}
