#include "warehouse_server.h"
#include "media_json.h"
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
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

    QByteArray request(const QString& type, const QJsonObject& payload = {})
    {
        QJsonObject request{{"type", type}, {"req_id", QString::number(++sequence)}, {"payload", payload}};
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

} // namespace

class ServerTests : public QObject {
    Q_OBJECT
private slots:
    void existingDatabaseMigratesWithoutLosingData()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("old.sqlite");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "old-fixture");
            db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE network_users(id INTEGER PRIMARY KEY,username TEXT NOT NULL,username_key TEXT NOT NULL UNIQUE,password_hash TEXT NOT NULL)"));
            QVERIFY(q.exec("CREATE TABLE warehouses(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL,owner_id INTEGER NOT NULL REFERENCES network_users(id),is_public INTEGER NOT NULL)"));
            QVERIFY(q.exec("CREATE TABLE warehouse_items(id INTEGER PRIMARY KEY AUTOINCREMENT,warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),media BLOB NOT NULL)"));
            QVERIFY(q.exec("INSERT INTO network_users VALUES(1,'old','old','hash')"));
            QVERIFY(q.exec("INSERT INTO warehouses VALUES(1,'preserved',1,0)"));
        }
        QSqlDatabase::removeDatabase("old-fixture");
        QString error;
        {
            WarehouseServer server;
            QVERIFY2(server.openDatabase(path, error), qPrintable(error));
            auto history = server.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
            QVERIFY(history.has_value()); QVERIFY(history->isEmpty());
            auto warehouses = server.readableCandidates(1);
            QVERIFY(warehouses.has_value()); QCOMPARE(warehouses->size(), 1);
            QCOMPARE(warehouses->first().warehouse.name, QStringLiteral("preserved"));
        }
        WarehouseServer restarted;
        QVERIFY2(restarted.openDatabase(path, error), qPrintable(error));
        auto warehouses = restarted.readableCandidates(1);
        QVERIFY(warehouses.has_value()); QCOMPARE(warehouses->size(), 1);
    }
    void behaviorViewIdentityAndPermission()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("view.sqlite");
        QString error, warehouseId, itemId, workId;
        const MediaRecord media{"original", "Music.mp3", "MP3", 1, "Director", {"Actor"}, 1,
                                QDate(2025, 1, 1), {}, "music"};
        {
            WarehouseServer server;
            QVERIFY(server.openDatabase(path, error));
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
            alice.socket.write(alice.request("create_warehouse", {{"name", "private"}, {"visibility", "private"}}));
            QTRY_COMPARE(alice.received.size(), 1);
            warehouseId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
            alice.socket.write(alice.request("add_item", {{"warehouse_id", warehouseId}, {"media", media_json::toJson(media)}}));
            QTRY_COMPARE(alice.received.size(), 1);
            itemId = payload(alice.received.takeFirst()).value("item").toObject().value("item_id").toString();
            const QJsonObject source{{"warehouse_id", warehouseId}, {"item_id", itemId}};
            bob.socket.write(bob.request("get_resource", {{"source", source}}));
            QTRY_COMPARE(bob.received.size(), 1);
            QCOMPARE(bob.received.takeFirst().value("code").toString(), "PermissionDenied");
            alice.socket.write(alice.request("get_resource", {{"source", source}}));
            QTRY_COMPARE(alice.received.size(), 1);
            workId = payload(alice.received.takeFirst()).value("work").toObject().value("resource").toObject().value("work_id").toString();
            QVERIFY(!workId.isEmpty());
            QJsonObject event{{"event_id", "view-1"}, {"action", "view"}, {"resource", QJsonObject{{"work_id", workId}, {"source", source}}},
                              {"user_id", "2"}, {"occurred_at_utc", "42"}, {"synthetic", true}};
            bob.socket.write(bob.request("record_behavior", {{"event", event}}));
            QTRY_COMPARE(bob.received.size(), 1);
            QCOMPARE(bob.received.takeFirst().value("code").toString(), "PermissionDenied");
            alice.socket.write(alice.request("record_behavior", {{"event", event}}) + alice.request("record_behavior", {{"event", event}}));
            QTRY_COMPARE(alice.received.size(), 2);
            QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
            QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
            auto resource = event.value("resource").toObject(); resource["work_id"] = "fake:id"; event["resource"] = resource;
            alice.socket.write(alice.request("record_behavior", {{"event", event}}));
            QTRY_COMPARE(alice.received.size(), 1);
            QCOMPARE(alice.received.takeFirst().value("code").toString(), "InvalidArgument");
            auto history = server.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
            QVERIFY(history.has_value()); QCOMPARE(history->size(), 1);
            QCOMPARE(history->first().record.event.resource->workId, workId);
            QVERIFY(!history->first().record.synthetic);
            QVERIFY(history->first().record.occurredAtUtc > 42);
            QVERIFY(history->first().mediaSnapshot.has_value());
            QCOMPARE(history->first().mediaSnapshot->category, media.category);
            auto other = server.queryBehavior(2, {0, care_contract::MaxUtcExclusive});
            QVERIFY(other.has_value()); QVERIFY(other->isEmpty());
            const auto timestamp = history->first().record.occurredAtUtc;
            auto before = server.queryBehavior(1, {0, timestamp});
            QVERIFY(before.has_value()); QVERIFY(before->isEmpty());
            auto included = server.queryBehavior(1, {timestamp, timestamp + 1});
            QVERIFY(included.has_value()); QCOMPARE(included->size(), 1);
            alice.socket.write(alice.request("remove_item", {{"warehouse_id", warehouseId}, {"item_id", itemId}}));
            QTRY_COMPARE(alice.received.size(), 1);
            QCOMPARE(alice.received.takeFirst().value("type").toString(), "remove_item_resp");
            history = server.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
            QVERIFY(history.has_value()); QCOMPARE(history->size(), 1);
            QCOMPARE(history->first().mediaSnapshot->category, media.category);
        }
        WarehouseServer restarted;
        QVERIFY(restarted.openDatabase(path, error));
        auto history = restarted.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
        QVERIFY(history.has_value()); QCOMPARE(history->size(), 1);
        QCOMPARE(history->first().record.event.resource->workId, workId);
    }
    void behaviorSearchIsAuthenticatedAndIdempotent()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("behavior.sqlite");
        QString error;
        WarehouseServer server;
        QVERIFY(server.openDatabase(path, error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.addUser("bob", "bob-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Connection alice(server.serverPort()), bob(server.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        QTRY_COMPARE(bob.socket.state(), QAbstractSocket::ConnectedState);
        const QJsonObject event{{"event_id", "evt-1"}, {"action", "search"}, {"query", "music"}};
        alice.socket.write(alice.request("record_behavior", {{"event", event}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("code").toString(), "NotAuthenticated");
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        bob.socket.write(bob.request("login", {{"username", "bob"}, {"password", "bob-demo"}}));
        QTRY_COMPARE(bob.received.size(), 1);
        bob.token = payload(bob.received.takeFirst()).value("token").toString();
        bob.socket.write(bob.request("record_behavior", {{"event", event}}));
        QTRY_COMPARE(bob.received.size(), 1);
        QCOMPARE(bob.received.takeFirst().value("type").toString(), "record_behavior_resp");
        alice.socket.write(alice.request("record_behavior", {{"event", event}}) +
                           alice.request("record_behavior", {{"event", event}}));
        QTRY_COMPARE(alice.received.size(), 2);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
        auto altered = event;
        altered["query"] = "different";
        alice.socket.write(alice.request("record_behavior", {{"event", altered}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("code").toString(), "InvalidArgument");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "behavior-test");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM behavior_events WHERE user_id=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        }
        QSqlDatabase::removeDatabase("behavior-test");
        alice.socket.write(alice.request("get_participation_settings"));
        QTRY_COMPARE(alice.received.size(), 1);
        auto settings = payload(alice.received.takeFirst()).value("settings").toObject();
        QCOMPARE(settings.value("history_enabled").toBool(), true);
        QCOMPARE(settings.value("contribute_public_trends").toBool(), false);
        settings["contribute_public_trends"] = true;
        alice.socket.write(alice.request("set_participation_settings", {{"settings", settings}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "set_participation_settings_resp");
        auto contributed = event; contributed["event_id"] = "evt-2";
        alice.socket.write(alice.request("record_behavior", {{"event", contributed}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
        auto beforeRevoke = server.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
        QVERIFY(beforeRevoke.has_value()); QCOMPARE(beforeRevoke->size(), 2);
        QCOMPARE(beforeRevoke->at(0).publicEligible, false);
        QCOMPARE(beforeRevoke->at(1).publicEligible, true);
        settings["contribute_public_trends"] = false;
        alice.socket.write(alice.request("set_participation_settings", {{"settings", settings}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "set_participation_settings_resp");
        auto afterRevoke = server.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
        QVERIFY(afterRevoke.has_value()); QCOMPARE(afterRevoke->size(), 2);
        QCOMPARE(afterRevoke->at(1).publicEligible, false);
        settings["history_enabled"] = false;
        alice.socket.write(alice.request("set_participation_settings", {{"settings", settings}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "set_participation_settings_resp");
        auto paused = event;
        paused["event_id"] = "evt-paused";
        alice.socket.write(alice.request("record_behavior", {{"event", paused}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "behavior-paused-test");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM behavior_events WHERE user_id=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
        }
        QSqlDatabase::removeDatabase("behavior-paused-test");
        alice.socket.write(alice.request("clear_history"));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "clear_history_resp");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "behavior-clear-test");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM behavior_events WHERE user_id=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
        }
        QSqlDatabase::removeDatabase("behavior-clear-test");
        auto bobHistory = server.queryBehavior(2, {0, care_contract::MaxUtcExclusive});
        QVERIFY(bobHistory.has_value()); QCOMPARE(bobHistory->size(), 1);
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
