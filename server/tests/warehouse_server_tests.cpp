#include "warehouse_server.h"
#include "warehouse_protocol.h"
#include "media_json.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
QJsonObject exchange(QTcpSocket& socket, const QJsonObject& request) {
    QString error;
    const auto frame = warehouse_protocol::encodeFrame(request, error);
    if (frame.isEmpty()) return {};
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    warehouse_protocol::FrameDecoder decoder;
    QList<QJsonObject> responses;
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] {
        if (!decoder.feed(socket.readAll(), responses, error) || !responses.isEmpty()) loop.quit();
    });
    socket.write(frame);
    timer.start(5000);
    loop.exec();
    return responses.isEmpty() ? QJsonObject{} : responses.first();
}
QJsonObject request(const QString& type, const QString& id, const QString& token, const QJsonObject& payload) {
    QJsonObject result{{"type",type},{"req_id",id},{"payload",payload}};
    if (!token.isEmpty()) result.insert("token",token);
    return result;
}
}

class WarehouseServerTests : public QObject {
    Q_OBJECT
private slots:
    void permissionsAndRestart();
};
void WarehouseServerTests::permissionsAndRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WarehouseServer server;
    QString error;
    const QString path = dir.filePath("server.sqlite");
    QVERIFY2(server.open(path, error), qPrintable(error));
    QVERIFY2(server.addInitialUser("alice", "one", error), qPrintable(error));
    QVERIFY(!server.addInitialUser("extra", "pw", error));
    QVERIFY2(server.addUser("bob", "two", error), qPrintable(error));
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0, error), qPrintable(error));
    QTcpSocket alice, bob;
    alice.connectToHost(QHostAddress::LocalHost, server.port());
    bob.connectToHost(QHostAddress::LocalHost, server.port());
    QTRY_COMPARE(alice.state(), QAbstractSocket::ConnectedState);
    QTRY_COMPARE(bob.state(), QAbstractSocket::ConnectedState);
    auto r = exchange(alice, request("login","1",{},{{"username","alice"},{"password","wrong"}}));
    QCOMPARE(r.value("code").toString(), QStringLiteral("AuthenticationFailed"));
    r = exchange(alice, request("login","2",{},{{"username","alice"},{"password","one"}}));
    const QString a = r.value("payload").toObject().value("token").toString();
    QVERIFY(!a.isEmpty());
    r = exchange(bob, request("login","1",{},{{"username","bob"},{"password","two"}}));
    const QString b = r.value("payload").toObject().value("token").toString();
    QVERIFY(!b.isEmpty());
    r = exchange(alice, request("create_warehouse","3",a,{{"name","private"},{"visibility","private"}}));
    const QString privateId = r.value("payload").toObject().value("warehouse").toObject().value("warehouse_id").toString();
    QVERIFY(!privateId.isEmpty());
    r = exchange(bob, request("get_warehouse","2",b,{{"warehouse_id",privateId}}));
    QCOMPARE(r.value("code").toString(), QStringLiteral("PermissionDenied"));
    r = exchange(alice, request("create_warehouse","4",a,{{"name","public"},{"visibility","public"}}));
    const QString publicId = r.value("payload").toObject().value("warehouse").toObject().value("warehouse_id").toString();
    QVERIFY(!publicId.isEmpty());
    MediaRecord media; media.id = "tvmaze:1"; media.fileName = "title";
    r = exchange(alice, request("add_item","5",a,{{"warehouse_id",publicId},{"media",media_json::toJson(media)}}));
    const QString itemId = r.value("payload").toObject().value("item").toObject().value("item_id").toString();
    QVERIFY(!itemId.isEmpty());
    r = exchange(bob, request("get_warehouse","3",b,{{"warehouse_id",publicId}}));
    QCOMPARE(r.value("payload").toObject().value("items").toArray().size(), 1);
    r = exchange(bob, request("remove_item","4",b,{{"warehouse_id",publicId},{"item_id",itemId}}));
    QCOMPARE(r.value("code").toString(), QStringLiteral("PermissionDenied"));
    server.close();
    QVERIFY2(server.open(path, error), qPrintable(error));
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0, error), qPrintable(error));
    QTcpSocket restarted;
    restarted.connectToHost(QHostAddress::LocalHost, server.port());
    QTRY_COMPARE(restarted.state(), QAbstractSocket::ConnectedState);
    r = exchange(restarted, request("login","1",{},{{"username","alice"},{"password","one"}}));
    const QString newToken = r.value("payload").toObject().value("token").toString();
    QVERIFY(!newToken.isEmpty());
    r = exchange(restarted, request("get_warehouse","2",newToken,{{"warehouse_id",publicId}}));
    QCOMPARE(r.value("payload").toObject().value("items").toArray().size(), 1);
}
QTEST_GUILESS_MAIN(WarehouseServerTests)
#include "warehouse_server_tests.moc"
