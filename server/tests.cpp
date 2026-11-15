#include "warehouse_server.h"
#include "media_json.h"
#include <QJsonArray>
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
