#include "warehouse_client.h"
#include "warehouse_protocol.h"
#include "media_json.h"
#include <QJsonArray>
#include <QPointer>
#include <QTcpServer>
#include <QtTest>
#include <memory>

using namespace warehouse_contract;

// 可控制响应顺序的协议桩，验证客户端只转发及按连接、请求关联结果。
class FakeServer : public QObject {
public:
    QTcpServer server;
    QList<QPair<QPointer<QTcpSocket>, QJsonObject>> recommendations;
    QList<QPair<QPointer<QTcpSocket>, QJsonObject>> otherRequests;
    QList<QJsonObject> cancellations;
    QList<QPair<QPointer<QTcpSocket>, QJsonObject>> logins;
    bool holdLoginResponses = false;
    bool rejectLogout = false;
    int appliedMutations = 0;
    FakeServer() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                auto decoder = std::make_shared<warehouse_protocol::FrameDecoder>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, decoder] {
                    QList<QJsonObject> messages; QString error;
                    if (!decoder->feed(socket->readAll(), messages, error)) { socket->abort(); return; }
                    for (const auto& m : messages) {
                        const QString type = m.value("type").toString();
                        const QString id = m.value("req_id").toString();
                        if (type == "login") {
                            logins.append({socket, m});
                            if (holdLoginResponses) continue;
                            const QString name = m.value("payload").toObject().value("username").toString();
                            send(socket, {{"type","login_resp"},{"req_id",id},{"payload",QJsonObject{
                                {"token",name + "-token"},{"user",QJsonObject{{"user_id",name == "alice" ? "1" : "2"},{"username",name}}}}}});
                        } else if (type == "recommend_resources") {
                            recommendations.append({socket,m});
                        } else if (type == "cancel_recommendation") {
                            cancellations.append(m);
                            send(socket, {{"type","cancel_recommendation_resp"},{"req_id",id},{"payload",QJsonObject{}}});
                        } else if (type == "logout") {
                            if (rejectLogout) send(socket, {{"type","error_resp"},{"req_id",id},{"code","ServerError"},{"message","failed"}});
                            else send(socket, {{"type","logout_resp"},{"req_id",id},{"payload",QJsonObject{}}});
                        } else if (type == "get_warehouse") {
                            otherRequests.append({socket,m});
                        } else if (type == "create_warehouse") {
                            ++appliedMutations; // 模拟服务器已写入，响应仍在途中。
                            otherRequests.append({socket,m});
                        }
                    }
                });
            }
        });
    }
    static void send(QTcpSocket* socket, const QJsonObject& msg) {
        QString error; socket->write(warehouse_protocol::encodeFrame(msg, error));
    }
    void reply(int index, const QString& mediaId, bool fallback) {
        const auto request = recommendations.at(index);
        MediaRecord media; media.id = mediaId; media.fileName = "title";
        const QJsonObject item{{"warehouse_id","1"},{"item_id","2"},{"media",media_json::toJson(media)},{"reason","reason-" + mediaId}};
        send(request.first, {{"type","recommend_resources_resp"},{"req_id",request.second.value("req_id")},
                             {"payload",QJsonObject{{"items",QJsonArray{item}},{"fallback",fallback},{"message","overall"}}}});
    }
};

class RecommendationClientTests : public QObject {
    Q_OBJECT
private slots:
    void twoClientsReceiveTheirOwnResponses();
    void logoutCancelsAndIgnoresLateReply();
    void timeoutSendsCancellation();
    void disconnectCancelsPending();
    void oneClientMatchesOutOfOrderReplies();
    void logoutDuringLoginClosesConnection();
    void reentrantRequestCannotUseOldSession();
    void oldWarehouseReplyDoesNotCrossSessions();
    void failedLogoutClosesConnection();
    void sentMutationHasUnknownOutcomeAfterAccountSwitch();
    void sentMutationHasUnknownOutcomeOnTransportFailure();
    void explicitCancelAndEmptyResult();
    void reconnectFromCancelledConnectCallback();
    void immediateFailureReturnsEmptyHandle();
    void loginTimeoutClosesIndeterminateSession();
    void reconnectFromLogoutCancellationCallback();
    void reconnectWhileLogoutResponseIsPending_data();
    void reconnectWhileLogoutResponseIsPending();
};

void RecommendationClientTests::twoClientsReceiveTheirOwnResponses() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient a, b;
    bool ca = false, cb = false;
    a.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { ca = s.ok(); });
    b.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { cb = s.ok(); });
    QTRY_VERIFY(ca && cb);
    bool la = false, lb = false;
    a.login("alice","pw",[&](auto r) { la = r.status.ok(); });
    b.login("bob","pw",[&](auto r) { lb = r.status.ok(); });
    QTRY_VERIFY(la && lb);
    WarehouseRecommendationRequest request;
    request.query = QStringLiteral("科幻"); request.limit = 3; request.targetWarehouseId = 42;
    Result<WarehouseRecommendationResult> ra, rb;
    bool da = false, db = false;
    const auto aliceHandle = a.recommendResources(request,[&](auto r) { ra = r; da = true; });
    QTRY_COMPARE(fake.recommendations.size(), 1);
    request.query = QStringLiteral("音乐"); request.targetWarehouseId.reset();
    b.recommendResources(request,[&](auto r) { rb = r; db = true; });
    QTRY_COMPARE(fake.recommendations.size(), 2);
    QCOMPARE(fake.recommendations[0].second.value("req_id"), fake.recommendations[1].second.value("req_id"));
    QVERIFY(!b.cancelRecommendation(aliceHandle));
    QCOMPARE(fake.recommendations[0].second.value("payload").toObject().value("warehouse_id").toString(), QStringLiteral("42"));
    QVERIFY(!fake.recommendations[1].second.value("payload").toObject().contains("warehouse_id"));
    fake.reply(1, "bob-media", true);
    fake.reply(0, "alice-media", false);
    QTRY_VERIFY(da && db);
    QVERIFY(ra.status.ok()); QVERIFY(rb.status.ok());
    QCOMPARE(ra.value->items.first().media.id, QStringLiteral("alice-media"));
    QCOMPARE(rb.value->items.first().media.id, QStringLiteral("bob-media"));
    QVERIFY(!ra.value->fallback); QVERIFY(rb.value->fallback);
    QCOMPARE(ra.value->items.first().reason, QStringLiteral("reason-alice-media"));
    QCOMPARE(ra.value->message, QStringLiteral("overall"));
}

void RecommendationClientTests::logoutCancelsAndIgnoresLateReply() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int callbacks = 0; ErrorCode error = ErrorCode::None;
    client.recommendResources({QStringLiteral("电影"),{},5}, [&](auto r) { ++callbacks; error = r.status.code; });
    QTRY_COMPARE(fake.recommendations.size(), 1);
    bool loggedOut = false;
    client.logout([&](Status s) { loggedOut = s.ok(); });
    QTRY_VERIFY(loggedOut);
    QCOMPARE(callbacks, 1);
    QCOMPARE(error, ErrorCode::Cancelled);
    QTRY_COMPARE(fake.cancellations.size(), 1);
    QCOMPARE(fake.cancellations.first().value("payload").toObject().value("target_req_id"),
             fake.recommendations.first().second.value("req_id"));
    fake.reply(0, "late", false);
    QTest::qWait(100);
    QCOMPARE(callbacks, 1);
}

void RecommendationClientTests::timeoutSendsCancellation() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client(nullptr, 250);
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int callbacks = 0; ErrorCode code = ErrorCode::None;
    client.recommendResources({QStringLiteral("电影"),{},5}, [&](auto r) { ++callbacks; code = r.status.code; });
    QTRY_COMPARE(fake.recommendations.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 12000);
    QCOMPARE(code, ErrorCode::Timeout);
    QTRY_COMPARE(fake.cancellations.size(), 1);
    fake.reply(0, "late", false);
    QTest::qWait(100);
    QCOMPARE(callbacks, 1);
}

void RecommendationClientTests::disconnectCancelsPending() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int callbacks = 0; ErrorCode code = ErrorCode::None;
    client.recommendResources({QStringLiteral("电影"),{},5}, [&](auto r) { ++callbacks; code = r.status.code; });
    QTRY_COMPARE(fake.recommendations.size(), 1);
    client.disconnectFromServer();
    QTRY_COMPARE(callbacks, 1);
    QCOMPARE(code, ErrorCode::NotConnected);
    QCoreApplication::processEvents();
    QCOMPARE(callbacks, 1);
}

void RecommendationClientTests::oneClientMatchesOutOfOrderReplies() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    Result<WarehouseRecommendationResult> first, second;
    bool firstDone = false, secondDone = false;
    client.recommendResources({QStringLiteral("电影"),{},5}, [&](auto r) { first = r; firstDone = true; });
    client.recommendResources({QStringLiteral("音乐"),{},5}, [&](auto r) { second = r; secondDone = true; });
    QTRY_COMPARE(fake.recommendations.size(), 2);
    QVERIFY(fake.recommendations[0].second.value("req_id") != fake.recommendations[1].second.value("req_id"));
    fake.reply(1,"second",true);
    fake.reply(0,"first",false);
    QTRY_VERIFY(firstDone && secondDone);
    QVERIFY(first.status.ok()); QVERIFY(second.status.ok());
    QCOMPARE(first.value->items.first().media.id, QStringLiteral("first"));
    QCOMPARE(second.value->items.first().media.id, QStringLiteral("second"));
}

void RecommendationClientTests::logoutDuringLoginClosesConnection() {
    QTcpServer silent; QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", silent.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    QTRY_VERIFY(silent.hasPendingConnections());
    QTcpSocket* peer = silent.nextPendingConnection();
    int loginCallbacks = 0;
    client.login("alice","pw",[&](auto) { ++loginCallbacks; });
    bool logoutDone = false; Status logoutStatus;
    client.logout([&](Status s) { logoutStatus = s; logoutDone = true; });
    QTRY_VERIFY(logoutDone);
    QVERIFY(logoutStatus.ok());
    QCOMPARE(loginCallbacks, 1);
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
}

void RecommendationClientTests::reentrantRequestCannotUseOldSession() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int oldCount = 0, reentrantCount = 0;
    ErrorCode reentrantCode = ErrorCode::None;
    client.recommendResources({QStringLiteral("旧请求"),{},5}, [&](auto) {
        ++oldCount;
        client.recommendResources({QStringLiteral("重入"),{},5}, [&](auto r) {
            ++reentrantCount; reentrantCode = r.status.code;
        });
    });
    QTRY_COMPARE(fake.recommendations.size(), 1);
    bool bob = false;
    client.login("bob","pw",[&](auto r) { bob = r.status.ok(); });
    QTRY_VERIFY(bob);
    QCOMPARE(oldCount, 1);
    QCOMPARE(reentrantCount, 1);
    QCOMPARE(reentrantCode, ErrorCode::NotAuthenticated);
    fake.reply(0, "old", false);
    QTest::qWait(100);
    QCOMPARE(fake.recommendations.size(), 1);
    QCOMPARE(oldCount, 1);
}

void RecommendationClientTests::oldWarehouseReplyDoesNotCrossSessions() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool alice = false;
    client.login("alice","pw",[&](auto r) { alice = r.status.ok(); });
    QTRY_VERIFY(alice);
    int callbacks = 0; ErrorCode code = ErrorCode::None;
    client.getWarehouse(1,[&](auto r) { ++callbacks; code = r.status.code; });
    QTRY_COMPARE(fake.otherRequests.size(), 1);
    bool bob = false;
    client.login("bob","pw",[&](auto r) { bob = r.status.ok(); });
    QTRY_VERIFY(bob);
    QCOMPARE(callbacks, 1);
    QCOMPARE(code, ErrorCode::Cancelled);
    const auto old = fake.otherRequests.first();
    FakeServer::send(old.first, {{"type","get_warehouse_resp"},{"req_id",old.second.value("req_id")},{"payload",QJsonObject{}}});
    QTest::qWait(100);
    QCOMPARE(callbacks, 1);
}

void RecommendationClientTests::failedLogoutClosesConnection() {
    FakeServer fake; fake.rejectLogout = true;
    QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    bool done = false; Status result;
    client.logout([&](Status s) { result = s; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(result.code, ErrorCode::ServerError);
    bool called = false; ErrorCode code = ErrorCode::None;
    client.recommendResources({QStringLiteral("电影"),{},5},[&](auto r) { called = true; code = r.status.code; });
    QTRY_VERIFY(called);
    QCOMPARE(code, ErrorCode::NotConnected);
}

void RecommendationClientTests::sentMutationHasUnknownOutcomeAfterAccountSwitch() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool alice = false;
    client.login("alice","pw",[&](auto r) { alice = r.status.ok(); });
    QTRY_VERIFY(alice);
    int callbacks = 0; ErrorCode code = ErrorCode::None;
    client.createWarehouse("test", Visibility::Private, [&](auto r) { ++callbacks; code = r.status.code; });
    QTRY_COMPARE(fake.appliedMutations, 1);
    bool bob = false;
    client.login("bob","pw",[&](auto r) { bob = r.status.ok(); });
    QTRY_VERIFY(bob);
    QCOMPARE(callbacks, 1);
    QCOMPARE(code, ErrorCode::OutcomeUnknown);
    const auto old = fake.otherRequests.first();
    FakeServer::send(old.first, {{"type","create_warehouse_resp"},{"req_id",old.second.value("req_id")},
                                {"payload",QJsonObject{{"warehouse",QJsonObject{{"warehouse_id","1"},
                                {"name","test"},{"owner_id","1"},{"visibility","private"}}}}}});
    QTest::qWait(100);
    QCOMPARE(callbacks, 1);
    QCOMPARE(fake.appliedMutations, 1);
}

void RecommendationClientTests::sentMutationHasUnknownOutcomeOnTransportFailure() {
    for (int mode = 0; mode < 3; ++mode) {
        FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
        WarehouseClient client;
        bool connected = false;
        client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
        QTRY_VERIFY(connected);
        bool loggedIn = false;
        client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
        QTRY_VERIFY(loggedIn);
        int callbacks = 0; ErrorCode code = ErrorCode::None;
        client.createWarehouse("test", Visibility::Private, [&](auto r) { ++callbacks; code = r.status.code; });
        QTRY_COMPARE(fake.appliedMutations, 1);
        const auto request = fake.otherRequests.first();
        if (mode == 0) {
            client.disconnectFromServer();
        } else if (mode == 1) {
            FakeServer::send(request.first, {{"type","create_warehouse_resp"},
                             {"req_id",request.second.value("req_id")},{"payload",QJsonObject{}}});
        }
        // mode 2 模拟已提交但服务器始终不回复，等待请求超时。
        QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 12000);
        QCOMPARE(code, ErrorCode::OutcomeUnknown);
        client.disconnectFromServer();
        QCOMPARE(callbacks, 1);
    }
}

void RecommendationClientTests::explicitCancelAndEmptyResult() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice","pw",[&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int count = 0; ErrorCode code = ErrorCode::None;
    const auto handle = client.recommendResources({QStringLiteral("电影"),{},5},
        [&](auto r) { ++count; code = r.status.code; });
    QVERIFY(!handle.isEmpty());
    QTRY_COMPARE(fake.recommendations.size(), 1);
    QVERIFY(client.cancelRecommendation(handle));
    QCOMPARE(count, 1);
    QCOMPARE(code, ErrorCode::Cancelled);
    QVERIFY(!client.cancelRecommendation(handle));
    QTRY_COMPARE(fake.cancellations.size(), 1);
    fake.reply(0, "late", false);
    QTest::qWait(100);
    QCOMPARE(count, 1);
    Result<WarehouseRecommendationResult> result;
    bool done = false;
    const auto completed = client.recommendResources({QStringLiteral("音乐"),{},5}, [&](auto r) { result = r; done = true; });
    QVERIFY(!completed.isEmpty());
    QTRY_COMPARE(fake.recommendations.size(), 2);
    const auto req = fake.recommendations.at(1);
    FakeServer::send(req.first, {{"type","recommend_resources_resp"},{"req_id",req.second.value("req_id")},
                                {"payload",QJsonObject{{"items",QJsonArray{}},{"fallback",false},{"message",QStringLiteral("暂无匹配")}}}});
    QTRY_VERIFY(done);
    QVERIFY(result.status.ok());
    QVERIFY(result.value->items.isEmpty());
    QVERIFY(!result.value->fallback);
    QCOMPARE(result.value->message, QStringLiteral("暂无匹配"));
    QVERIFY(!client.cancelRecommendation(completed));
}

void RecommendationClientTests::reconnectFromCancelledConnectCallback() {
    QTcpServer first, second;
    QVERIFY(first.listen(QHostAddress::LocalHost, 0));
    QVERIFY(second.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    int firstCallbacks = 0, secondCallbacks = 0;
    bool secondConnected = false;
    client.connectToServer("127.0.0.1", first.serverPort(), [&](Status s) {
        ++firstCallbacks;
        if (!s.ok()) client.connectToServer("127.0.0.1", second.serverPort(), [&](Status result) {
            ++secondCallbacks;
            secondConnected = result.ok();
        });
    });
    client.disconnectFromServer();
    QTRY_COMPARE_WITH_TIMEOUT(secondCallbacks, 1, 10000);
    QCOMPARE(firstCallbacks, 1);
    QVERIFY(secondConnected);
}

void RecommendationClientTests::immediateFailureReturnsEmptyHandle() {
    WarehouseClient client;
    bool called = false;
    ErrorCode code = ErrorCode::None;
    const auto handle = client.recommendResources({QStringLiteral("电影"),{},5}, [&](auto r) {
        called = true;
        code = r.status.code;
    });
    QVERIFY(called);
    QCOMPARE(code, ErrorCode::NotConnected);
    QVERIFY(handle.isEmpty());
    QVERIFY(!client.cancelRecommendation(handle));
}

void RecommendationClientTests::loginTimeoutClosesIndeterminateSession() {
    FakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost, 0));
    fake.holdLoginResponses = true;
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    int loginCalls = 0; Status loginStatus;
    client.login("alice", "pw", [&](auto r) { ++loginCalls; loginStatus = r.status; });
    QTRY_COMPARE(fake.logins.size(), 1);
    const auto oldLogin = fake.logins.first();
    QTRY_COMPARE_WITH_TIMEOUT(loginCalls, 1, 12000);
    QCOMPARE(loginStatus.code, ErrorCode::Timeout);
    // The server may already have authenticated this connection, although its token is unknown locally.
    QTRY_COMPARE(oldLogin.first->state(), QAbstractSocket::UnconnectedState);
    fake.holdLoginResponses = false;
    connected = false;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice", "pw", [&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    QCOMPARE(fake.logins.size(), 2);
    QCOMPARE(loginCalls, 1);
}

void RecommendationClientTests::reconnectFromLogoutCancellationCallback() {
    FakeServer first, second;
    QVERIFY(first.server.listen(QHostAddress::LocalHost, 0));
    QVERIFY(second.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", first.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice", "pw", [&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int recommendationCalls = 0, connectCalls = 0, logoutCalls = 0;
    Status recommendationStatus, connectStatus, logoutStatus;
    client.recommendResources({"Music", {}, 5}, [&](auto r) {
        ++recommendationCalls; recommendationStatus = r.status;
        client.connectToServer("127.0.0.1", second.server.serverPort(), [&](Status s) {
            ++connectCalls; connectStatus = s;
        });
    });
    QTRY_COMPARE(first.recommendations.size(), 1);
    client.logout([&](Status s) { ++logoutCalls; logoutStatus = s; });
    QTRY_COMPARE(connectCalls, 1);
    QVERIFY2(connectStatus.ok(), qPrintable(connectStatus.message));
    QCOMPARE(logoutCalls, 1);
    QCOMPARE(logoutStatus.code, ErrorCode::Cancelled);
    QCOMPARE(recommendationCalls, 1);
    QCOMPARE(recommendationStatus.code, ErrorCode::Cancelled);
    loggedIn = false;
    client.login("bob", "pw", [&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    QCOMPARE(second.logins.size(), 1);
    QCOMPARE(connectCalls, 1);
    QCOMPARE(logoutCalls, 1);
    QCOMPARE(recommendationCalls, 1);
}

void RecommendationClientTests::reconnectWhileLogoutResponseIsPending_data() {
    QTest::addColumn<bool>("switchAccount");
    QTest::newRow("explicit-logout") << false;
    QTest::newRow("account-switch-logout") << true;
}

void RecommendationClientTests::reconnectWhileLogoutResponseIsPending() {
    QFETCH(bool, switchAccount);
    FakeServer first, second;
    QVERIFY(first.server.listen(QHostAddress::LocalHost, 0));
    QVERIFY(second.server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool connected = false;
    client.connectToServer("127.0.0.1", first.server.serverPort(), [&](Status s) { connected = s.ok(); });
    QTRY_VERIFY(connected);
    bool loggedIn = false;
    client.login("alice", "pw", [&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    int logoutCalls = 0, connectCalls = 0;
    Status logoutStatus, connectStatus;
    if (switchAccount)
        client.login("bob", "pw", [&](auto r) { ++logoutCalls; logoutStatus = r.status; });
    else
        client.logout([&](Status s) { ++logoutCalls; logoutStatus = s; });
    // Replace the connection before the old logout response can be delivered.
    client.connectToServer("127.0.0.1", second.server.serverPort(), [&](Status s) {
        ++connectCalls; connectStatus = s;
    });
    QTRY_COMPARE(connectCalls, 1);
    QVERIFY2(connectStatus.ok(), qPrintable(connectStatus.message));
    QCOMPARE(logoutCalls, 1);
    QCOMPARE(logoutStatus.code, ErrorCode::NotConnected);
    loggedIn = false;
    client.login("bob", "pw", [&](auto r) { loggedIn = r.status.ok(); });
    QTRY_VERIFY(loggedIn);
    QCOMPARE(second.logins.size(), 1);
    QCOMPARE(logoutCalls, 1);
    QCOMPARE(connectCalls, 1);
}

QTEST_GUILESS_MAIN(RecommendationClientTests)
#include "recommendation_client_tests.moc"
