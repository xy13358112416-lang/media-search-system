#include "warehouse_server.h"
#include "media_json.h"
#include <QJsonArray>
#include <QFile>
#include <QSqlError>
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
    void historyAndRecommendationsShareOneSession()
    {
        QTemporaryDir dir;
        QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("combined.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Connection alice(server.serverPort());
        QTRY_COMPARE(alice.socket.state(), QAbstractSocket::ConnectedState);
        alice.socket.write(alice.request("login", {{"username", "alice"}, {"password", "alice-demo"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        alice.token = payload(alice.received.takeFirst()).value("token").toString();
        alice.socket.write(alice.request("create_warehouse", {{"name", "Mine"}, {"visibility", "private"}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const auto warehouseId = payload(alice.received.takeFirst()).value("warehouse").toObject().value("warehouse_id").toString();
        const MediaRecord media{"music:1", "Music.mp3", "MP3", 1, "Director", {"Actor"}, 1,
                                QDate(2025, 1, 1), {}, "音乐"};
        alice.socket.write(alice.request("add_item", {{"warehouse_id", warehouseId}, {"media", media_json::toJson(media)}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const auto itemId = payload(alice.received.takeFirst()).value("item").toObject().value("item_id").toString();
        const QJsonObject source{{"warehouse_id", warehouseId}, {"item_id", itemId}};
        alice.socket.write(alice.request("get_resource", {{"source", source}}));
        QTRY_COMPARE(alice.received.size(), 1);
        const auto resource = payload(alice.received.takeFirst()).value("work").toObject().value("resource").toObject();
        QVERIFY(!resource.value("work_id").toString().isEmpty());
        const QJsonObject event{{"event_id", "view-1"}, {"action", "view"}, {"resource", resource}};
        alice.socket.write(alice.request("record_behavior", {{"event", event}}) +
                           alice.request("recommend_resources", {{"query", "Music"}, {"limit", 5}}));
        QTRY_COMPARE(alice.received.size(), 2);
        QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
        const auto recommendation = alice.received.takeFirst();
        QCOMPARE(recommendation.value("type").toString(), "recommend_resources_resp");
        QCOMPARE(payload(recommendation).value("fallback").toBool(), true);
        QCOMPARE(payload(recommendation).value("items").toArray().size(), 1);
        const auto history = server.queryBehavior(1, {0, care_contract::MaxUtcExclusive});
        QVERIFY(history.has_value()); QCOMPARE(history->size(), 1);
        QCOMPARE(history->first().record.event.resource->workId, resource.value("work_id").toString());
    }
    void historyReadFailsOnMidStreamDatabaseError()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("corrupt-test.sqlite");
        QString error;
        {
            WarehouseServer server;
            QVERIFY(server.openDatabase(path, error));
            QVERIFY(server.addUser("alice", "alice-demo", error));
        }
        qint64 leaf = 0, pageSize = 0;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "history-corrupt-setup");
            db.setDatabaseName(path); QVERIFY(db.open()); QVERIFY(db.transaction());
            QSqlQuery q(db);
            QVERIFY(q.prepare("INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc) VALUES(1,?,'search',?,?)"));
            for (int i = 0; i < 1000; ++i) {
                q.bindValue(0, QStringLiteral("event-%1").arg(i, 4, 10, QLatin1Char('0')));
                q.bindValue(1, QString(180, QLatin1Char('x')));
                q.bindValue(2, 1700000000 + i);
                QVERIFY(q.exec());
            }
            QVERIFY(db.commit());
            QVERIFY(q.exec("PRAGMA page_size")); QVERIFY(q.next()); pageSize = q.value(0).toLongLong();
            if (!q.exec("SELECT pageno FROM dbstat WHERE name='behavior_events' AND pagetype='leaf' ORDER BY path DESC LIMIT 1"))
                QSKIP("SQLite dbstat is unavailable; cannot safely locate a test-only leaf page");
            QVERIFY(q.next()); leaf = q.value(0).toLongLong();
        }
        QSqlDatabase::removeDatabase("history-corrupt-setup");
        QVERIFY(leaf > 1 && pageSize > 0);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek((leaf - 1) * pageSize));
        QCOMPARE(file.write(QByteArray(1, '\0')), qint64(1));
        file.close();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "history-corrupt-check");
            db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT event_id FROM behavior_events WHERE user_id=1 AND occurred_at_utc>=0 AND occurred_at_utc<253402300800 ORDER BY occurred_at_utc,event_id"));
            int rows = 0; while (q.next()) ++rows;
            QVERIFY(rows > 0); QVERIFY(rows < 1000); QVERIFY(q.lastError().isValid());
        }
        QSqlDatabase::removeDatabase("history-corrupt-check");
        WarehouseServer server;
        QVERIFY2(server.openDatabase(path, error), qPrintable(error));
        QVERIFY(!server.queryBehavior(1, {0, care_contract::MaxUtcExclusive}).has_value());
    }
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
            resource["work_id"] = workId; event["resource"] = resource;
            alice.socket.write(alice.request("record_behavior", {{"event", event}}));
            QTRY_COMPARE(alice.received.size(), 1);
            QCOMPARE(alice.received.takeFirst().value("type").toString(), "record_behavior_resp");
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
        alice.socket.write(alice.request("record_behavior", {{"event", altered}}));
        QTRY_COMPARE(alice.received.size(), 1);
        QCOMPARE(alice.received.takeFirst().value("code").toString(), "InvalidArgument");
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
