#include "warehouse_server.h"
#include "media_json.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace {

class Connection {
public:
    QTcpSocket socket;
    QList<QJsonObject> received;
    warehouse_protocol::FrameDecoder decoder;
    QString token;

    explicit Connection(quint16 port)
    {
        QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [this] {
            QString error;
            if (!decoder.feed(socket.readAll(), received, error))
                socket.abort();
        });
        socket.connectToHost(QHostAddress::LocalHost, port);
    }

    QByteArray request(const QString& type, const QJsonObject& payload = {}, const QString& explicitId = {})
    {
        const QString generated = QString::number(++sequence);
        QJsonObject request{{"type", type}, {"req_id", explicitId.isEmpty() ? generated : explicitId}, {"payload", payload}};
        if (type != "login")
            request.insert("token", token);
        QString error;
        return warehouse_protocol::encodeFrame(request, error);
    }

private:
    int sequence = 0;
};

QJsonObject payload(const QJsonObject& response)
{
    return response.value("payload").toObject();
}

class FakeModel : public QTcpServer {
public:
    struct Request { QTcpSocket* socket; QJsonObject body; };
    QList<Request> requests;
    QHash<QTcpSocket*, QByteArray> buffers;
    FakeModel()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    auto& data = buffers[socket];
                    data += socket->readAll();
                    const int split = data.indexOf("\r\n\r\n");
                    if (split < 0) return;
                    const auto match = QRegularExpression("content-length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption)
                                           .match(QString::fromLatin1(data.left(split)));
                    if (!match.hasMatch()) return;
                    const int length = match.captured(1).toInt();
                    if (data.size() < split + 4 + length) return;
                    requests.append({socket, QJsonDocument::fromJson(data.mid(split + 4, length)).object()});
                    disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
                });
            }
        });
    }
    QJsonArray candidates(int index) const
    {
        const auto messages = requests.at(index).body.value("messages").toArray();
        return QJsonDocument::fromJson(messages.at(1).toObject().value("content").toString().toUtf8())
            .object().value("candidates").toArray();
    }
    void respond(int index, const QJsonArray& selections)
    {
        const QByteArray content = QJsonDocument(QJsonObject{{"recommendations", selections}}).toJson(QJsonDocument::Compact);
        const QByteArray body = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
            {"finish_reason", "stop"}, {"message", QJsonObject{{"content", QString::fromUtf8(content)}}}}}}})
            .toJson(QJsonDocument::Compact);
        auto* socket = requests.at(index).socket;
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
};

} // namespace

class ServerTests : public QObject {
    Q_OBJECT
private slots:
    void recommendationConcurrencyPermissionAndCancellation()
    {
        FakeModel model;
        QVERIFY(model.listen(QHostAddress::LocalHost));
        llm_recommendation::Config config;
        config.apiKey = "test-only";
        config.endpoint = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(model.serverPort()));
        QTemporaryDir dir;
        QString error;
        WarehouseServer server(config);
        QVERIFY(server.openDatabase(dir.filePath("test.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.addUser("bob", "bob-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Connection alice(server.serverPort()), bob(server.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        QTRY_COMPARE(bob.socket.state(), QAbstractSocket::ConnectedState);
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        bob.socket.write(bob.request("login", {{"username", "bob"}, {"password", "bob-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QTRY_COMPARE(bob.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        bob.token = payload(bob.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("create_warehouse", {{"name", "Shared"}, {"visibility", "public"}}) +
                           alice.request("create_warehouse", {{"name", "Hidden"}, {"visibility", "private"}}));
        QTRY_COMPARE(alice.received.size(), 2);
        const QString publicId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
        const QString privateId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
        const MediaRecord record{"m1", "Music.mp3", "MP3", 100, "Director", {"Actor"}, 1,
                                 QDate(2025, 1, 1), {}, "音乐"};
        alice.socket.write(alice.request("add_item", {{"warehouse_id", publicId}, {"media", media_json::toJson(record)}}) +
                           alice.request("add_item", {{"warehouse_id", privateId}, {"media", media_json::toJson(record)}}));
        QTRY_COMPARE(alice.received.size(), 2);
        const QString itemId = payload(alice.received.takeFirst()).value("item").toObject().value("item_id").toString();
        alice.received.clear();
        bob.socket.write(bob.request("recommend_resources", {{"query", "Music"}, {"limit", 5}, {"warehouse_id", privateId}}));
        QTRY_COMPARE(bob.received.size(), 1);
        QCOMPARE(bob.received.takeFirst().value("code").toString(), "PermissionDenied");
        QCOMPARE(model.requests.size(), 0);
        bob.socket.write(bob.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}, "same-id"));
        QTRY_COMPARE(model.requests.size(), 1);
        QCOMPARE(model.candidates(0).size(), 1);
        const QString key = model.candidates(0).first().toObject().value("id").toString();
        QCOMPARE(key, publicId + ":" + itemId);
        alice.socket.write(alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}, "same-id"));
        QTRY_COMPARE(model.requests.size(), 2);
        QCOMPARE(model.candidates(1).size(), 2);
        bob.socket.write(bob.request("list_public_warehouses"));
        QTRY_COMPARE(bob.received.size(), 1);
        QCOMPARE(bob.received.takeFirst().value("type").toString(), "list_public_warehouses_resp");
        model.respond(1, {}); // A legitimate empty model result remains empty.
        QTRY_COMPARE(alice.received.size(), 1);
        const auto aliceResult = alice.received.takeFirst();
        QCOMPARE(aliceResult.value("req_id").toString(), "same-id");
        auto empty = payload(aliceResult);
        QVERIFY(empty.value("items").toArray().isEmpty());
        QCOMPARE(empty.value("fallback").toBool(), false);
        model.respond(0, QJsonArray{QJsonObject{{"id", key}, {"reason", "match"}}});
        QTRY_COMPARE(bob.received.size(), 1);
        const auto bobResult = bob.received.takeFirst();
        QCOMPARE(bobResult.value("req_id").toString(), "same-id");
        const auto validResult = payload(bobResult);
        QCOMPARE(validResult.value("fallback").toBool(), false);
        QCOMPARE(validResult.value("items").toArray().size(), 1);
        const auto validItem = validResult.value("items").toArray().first().toObject();
        QCOMPARE(validItem.value("warehouse_id").toString(), publicId);
        QCOMPARE(validItem.value("item_id").toString(), itemId);
        QCOMPARE(validItem.value("reason").toString(), QStringLiteral("match"));
        bob.socket.write(bob.request("recommend_resources", {{"query", "Music"}, {"limit", 5}, {"warehouse_id", publicId}}));
        QTRY_COMPARE(model.requests.size(), 3);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "permission-change-test");
            db.setDatabaseName(dir.filePath("test.sqlite"));
            QVERIFY(db.open());
            QSqlQuery update(db);
            QVERIFY(update.exec("UPDATE warehouses SET is_public=0 WHERE id=" + publicId));
        }
        QSqlDatabase::removeDatabase("permission-change-test");
        model.respond(2, QJsonArray{QJsonObject{{"id", key}, {"reason", "match"}}});
        QTRY_COMPARE(bob.received.size(), 1);
        QCOMPARE(bob.received.takeFirst().value("code").toString(), "PermissionDenied");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "permission-restore-test");
            db.setDatabaseName(dir.filePath("test.sqlite"));
            QVERIFY(db.open());
            QSqlQuery update(db);
            QVERIFY(update.exec("UPDATE warehouses SET is_public=1 WHERE id=" + publicId));
        }
        QSqlDatabase::removeDatabase("permission-restore-test");
        bob.socket.write(bob.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(model.requests.size(), 4);
        alice.socket.write(alice.request("remove_item", {{"warehouse_id", publicId}, {"item_id", itemId}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.received.clear();
        model.respond(3, QJsonArray{QJsonObject{{"id", key}, {"reason", "match"}}});
        QTRY_COMPARE(bob.received.size(), 1);
        QVERIFY(payload(bob.received.takeFirst()).value("items").toArray().isEmpty());

        // The pending recommendation and the cancellation use distinct request IDs.
        bob.socket.write(bob.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(bob.received.size(), 1); // No readable items: completes without HTTP.
        bob.received.clear();
        alice.socket.write(alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(model.requests.size(), 5);
        // Alice has sent eight requests; the latest pending recommendation has req_id 8.
        alice.socket.write(alice.request("cancel_recommendation", {{"target_req_id", "8"}}));
        QTRY_COMPARE(alice.received.size(), 2);
        QCOMPARE(alice.received.at(0).value("code").toString(), "Cancelled");
        QCOMPARE(alice.received.at(1).value("type").toString(), "cancel_recommendation_resp");
        alice.received.clear();
        model.respond(4, QJsonArray{QJsonObject{{"id", key}, {"reason", "late"}}});
        QTest::qWait(50);
        QVERIFY(alice.received.isEmpty());
        alice.socket.write(alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(model.requests.size(), 6);
        alice.socket.write(alice.request("logout"));
        QTRY_COMPARE(alice.received.size(), 2);
        QCOMPARE(alice.received.at(0).value("code").toString(), "Cancelled");
        QCOMPARE(alice.received.at(1).value("type").toString(), "logout_resp");
        alice.received.clear();
        model.respond(5, QJsonArray{QJsonObject{{"id", key}, {"reason", "late"}}});
        QTest::qWait(50);
        QVERIFY(alice.received.isEmpty());
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(model.requests.size(), 7);
        alice.socket.abort();
        QTest::qWait(50);
        model.respond(6, QJsonArray{QJsonObject{{"id", key}, {"reason", "late"}}});
        QTest::qWait(50);
        QVERIFY(alice.received.isEmpty());
    }
    void recommendationFallsBackWithoutKey()
    {
        QTemporaryDir dir;
        QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("test.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Connection alice(server.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("create_warehouse", {{"name", "Mine"}, {"visibility", "private"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const QString warehouseId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
        const MediaRecord record{"m1", "Music.mp3", "MP3", 100, "Director", {"Actor"}, 1,
                                 QDate(2025, 1, 1), {}, "音乐"};
        alice.socket.write(alice.request("add_item", {{"warehouse_id", warehouseId}, {"media", media_json::toJson(record)}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const QString itemId = payload(alice.received.takeFirst()).value("item").toObject().value("item_id").toString();
        alice.socket.write(alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const auto response = payload(alice.received.takeFirst());
        QCOMPARE(response.value("fallback").toBool(), true);
        QVERIFY(!response.value("message").toString().isEmpty());
        QCOMPARE(response.value("items").toArray().size(), 1);
        const auto result = response.value("items").toArray().first().toObject();
        QCOMPARE(result.value("warehouse_id").toString(), warehouseId);
        QCOMPARE(result.value("item_id").toString(), itemId);
        QCOMPARE(result.value("media").toObject(), media_json::toJson(record));
        QVERIFY(!result.value("reason").toString().isEmpty());
    }
    void oversizedCandidateSnapshotFailsWithoutStartingModel()
    {
        QTemporaryDir dir;
        QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("test.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "large-snapshot-test");
            db.setDatabaseName(dir.filePath("test.sqlite"));
            QVERIFY(db.open());
            QVERIFY(db.transaction());
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO warehouses(id,name,owner_id,is_public) VALUES(1,'test',1,0)"));
            const MediaRecord record{"m1", "Music.mp3", "MP3", 100, "Director", {"Actor"}, 1,
                                     QDate(2025, 1, 1), {}, "音乐"};
            q.prepare("INSERT INTO warehouse_items(warehouse_id,media) VALUES(1,?)");
            for (int i = 0; i < 1001; ++i) {
                q.bindValue(0, QJsonDocument(media_json::toJson(record)).toJson(QJsonDocument::Compact));
                QVERIFY(q.exec());
            }
            QVERIFY(db.commit());
        }
        QSqlDatabase::removeDatabase("large-snapshot-test");
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Connection alice(server.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("code").toString(), "ServerError");
        alice.socket.write(alice.request("list_my_warehouses"));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "list_my_warehouses_resp");
    }
    void recommendationRequiresSessionAndAllowsEmptyResult()
    {
        QTemporaryDir dir;
        QString error;
        WarehouseServer server;
        QVERIFY(server.openDatabase(dir.filePath("test.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Connection alice(server.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        alice.socket.write(alice.request("recommend_resources", {{"query", "音乐"}, {"limit", 5}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("code").toString(), "NotAuthenticated");
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("recommend_resources", {{"query", "音乐"}, {"limit", 5}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const auto response = alice.received.takeFirst();
        QCOMPARE(response.value("type").toString(), "recommend_resources_resp");
        QVERIFY(payload(response).value("items").toArray().isEmpty());
        QCOMPARE(payload(response).value("fallback").toBool(), false);
        QVERIFY(payload(response).value("message").isString());
    }
    void permissionsAndPersistence()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("warehouse.sqlite");
        QString error;
        QString publicId;
        QString itemId;
        const MediaRecord record{"media-id", "Example.mp4", "MP4", 123, "Director", {"Actor"},
                                 5, QDate(2025, 1, 1), {"Award"}, "电影"};
        {
            WarehouseServer server;
            QVERIFY2(server.openDatabase(path, error), qPrintable(error));
            QVERIFY(server.addUser("alice", "alice-demo", error));
            QVERIFY(server.addUser("bob", "bob-demo", error));
            QVERIFY(!server.addUser("ALICE", "duplicate", error));
            QVERIFY(server.listen(QHostAddress::LocalHost));
            Connection alice(server.serverPort());
            Connection bob(server.serverPort());
            QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
            QTRY_COMPARE(bob.socket.state(), QAbstractSocket::ConnectedState);
            bob.socket.write(bob.request("login", {{"username", "bob"}, {"password", "wrong"}}));
            QTRY_COMPARE(bob.received.size(), 1);
            QCOMPARE(bob.received.takeFirst().value("code").toString(), "AuthenticationFailed");
            const auto login = alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}});
            alice.socket.write(login.left(2));
            alice.socket.flush();
            QCoreApplication::processEvents();
            QVERIFY(alice.received.isEmpty());
            alice.socket.write(login.mid(2));
            bob.socket.write(bob.request("login", {{"username", "bob"}, {"password", "bob-demo"}}));
            QTRY_COMPARE(alice.received.size(), 1);
            QTRY_COMPARE(bob.received.size(), 1);
            alice.token = payload(alice.received.takeFirst()).value("token").toString();
            bob.token = payload(bob.received.takeFirst()).value("token").toString();
            QVERIFY(!alice.token.isEmpty());
            QVERIFY(!bob.token.isEmpty());
            const QByteArray createPrivate = alice.request("create_warehouse", {{"name", "Hidden"}, {"visibility", "private"}});
            const QByteArray createPublic = alice.request("create_warehouse", {{"name", "Shared"}, {"visibility", "public"}});
            alice.socket.write(createPrivate + createPublic);
            QTRY_COMPARE(alice.received.size(), 2);
            const QString privateId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
            publicId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
            QVERIFY(!privateId.isEmpty());
            QVERIFY(!publicId.isEmpty());
            const auto snapshot = media_json::toJson(record);
            alice.socket.write(alice.request("add_item", {{"warehouse_id", publicId}, {"media", snapshot}}));
            QTRY_COMPARE(alice.received.size(), 1);
            const auto item = payload(alice.received.takeFirst()).value("item").toObject();
            itemId = item.value("item_id").toString();
            QVERIFY(!itemId.isEmpty());
            QCOMPARE(item.value("media").toObject(), snapshot);

            QByteArray operations = bob.request("list_my_warehouses");
            operations += bob.request("list_public_warehouses");
            operations += bob.request("get_warehouse", {{"warehouse_id", publicId}});
            operations += bob.request("get_warehouse", {{"warehouse_id", privateId}});
            operations += bob.request("add_item", {{"warehouse_id", publicId}, {"media", snapshot}});
            operations += bob.request("remove_item", {{"warehouse_id", publicId}, {"item_id", itemId}});
            operations += bob.request("remove_item", {{"warehouse_id", "1oops"}, {"item_id", itemId}});
            bob.socket.write(operations);
            QTRY_COMPARE(bob.received.size(), 7);
            QCOMPARE(payload(bob.received.takeFirst()).value("warehouses").toArray().size(), 0);
            QCOMPARE(payload(bob.received.takeFirst()).value("warehouses").toArray().size(), 1);
            QCOMPARE(payload(bob.received.takeFirst()).value("items").toArray().size(), 1);
            for (int i = 0; i < 3; ++i)
                QCOMPARE(bob.received.takeFirst().value("code").toString(), "PermissionDenied");
            QCOMPARE(bob.received.takeFirst().value("code").toString(), "InvalidArgument");

            const auto aliceCandidates = server.readableCandidates(1);
            const auto bobCandidates = server.readableCandidates(2);
            QVERIFY(aliceCandidates && bobCandidates);
            QCOMPARE(aliceCandidates->size(), 2);
            QCOMPARE(bobCandidates->size(), 1);
            QCOMPARE(bobCandidates->first().items.first().media.fileName, record.fileName);

            const QString bobToken = bob.token;
            bob.token = alice.token;
            bob.socket.write(bob.request("list_my_warehouses"));
            QTRY_COMPARE(bob.received.size(), 1);
            QCOMPARE(bob.received.takeFirst().value("code").toString(), "NotAuthenticated");
            bob.token = bobToken;
            const QByteArray logout = bob.request("logout");
            const QByteArray afterLogout = bob.request("list_my_warehouses");
            bob.socket.write(logout + afterLogout);
            QTRY_COMPARE(bob.received.size(), 2);
            QCOMPARE(bob.received.takeFirst().value("type").toString(), "logout_resp");
            QCOMPARE(bob.received.takeFirst().value("code").toString(), "NotAuthenticated");

            const QString oldToken = alice.token;
            alice.socket.write(QByteArray(4, '\0'));
            QTRY_COMPARE(alice.socket.state(), QAbstractSocket::UnconnectedState);
            Connection reconnect(server.serverPort());
            QTRY_COMPARE(reconnect.socket.state(), QAbstractSocket::ConnectedState);
            reconnect.token = oldToken;
            reconnect.socket.write(reconnect.request("list_my_warehouses"));
            QTRY_COMPARE(reconnect.received.size(), 1);
            QCOMPARE(reconnect.received.takeFirst().value("code").toString(), "NotAuthenticated");
        }
        WarehouseServer restarted;
        QVERIFY2(restarted.openDatabase(path, error), qPrintable(error));
        QVERIFY(restarted.listen(QHostAddress::LocalHost));
        Connection alice(restarted.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("get_warehouse", {{"warehouse_id", publicId}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(payload(alice.received.takeFirst()).value("items").toArray().first().toObject().value("media").toObject(),
                 media_json::toJson(record));
        alice.socket.write(alice.request("remove_item", {{"warehouse_id", publicId}, {"item_id", itemId}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "remove_item_resp");
        alice.socket.write(alice.request("get_warehouse", {{"warehouse_id", publicId}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(payload(alice.received.takeFirst()).value("items").toArray().size(), 0);
    }
};

QTEST_GUILESS_MAIN(ServerTests)
#include "tests.moc"
