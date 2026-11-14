#include "warehouse_server.h"
#include "warehouse_protocol.h"
#include "warehouse_contract.h"
#include "media_json.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
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
    void sessionsAndInvalidFrames();
    void candidateLimitsDoNotReturnPartialData();
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
    r = exchange(bob, request("create_warehouse","5",b,{{"name","bob-private"},{"visibility","private"}}));
    const QString bobPrivate = r.value("payload").toObject().value("warehouse").toObject().value("warehouse_id").toString();
    QVERIFY(!bobPrivate.isEmpty());
    MediaRecord secret; secret.id = "private-only"; secret.fileName = "secret";
    r = exchange(bob, request("add_item","6",b,{{"warehouse_id",bobPrivate},{"media",media_json::toJson(secret)}}));
    QVERIFY(!r.value("payload").toObject().value("item").toObject().isEmpty());
    QList<warehouse_contract::WarehouseDetail> readable;
    QVERIFY2(server.readableWarehouses(1, readable, error), qPrintable(error));
    QCOMPARE(readable.size(), 2);
    QSet<qint64> ids;
    for (const auto& detail : readable) ids.insert(detail.warehouse.warehouseId);
    QVERIFY(ids.contains(privateId.toLongLong()));
    QVERIFY(ids.contains(publicId.toLongLong()));
    QVERIFY(!ids.contains(bobPrivate.toLongLong()));
    QCOMPARE(readable.last().items.size(), 1);
    QCOMPARE(readable.last().items.first().media.id, QStringLiteral("tvmaze:1"));
    QVERIFY2(server.readableWarehouses(2, readable, error), qPrintable(error));
    QCOMPARE(readable.size(), 2);
    ids.clear();
    for (const auto& detail : readable) ids.insert(detail.warehouse.warehouseId);
    QVERIFY(ids.contains(publicId.toLongLong()));
    QVERIFY(ids.contains(bobPrivate.toLongLong()));
    QVERIFY(!ids.contains(privateId.toLongLong()));
    QVERIFY(!server.readableWarehouses(999, readable, error));
    QCOMPARE(readable.size(), 2); // 失败时不替换调用方原结果。
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
void WarehouseServerTests::sessionsAndInvalidFrames() {
    QTemporaryDir dir;
    WarehouseServer server;
    QString error;
    QVERIFY(server.open(dir.filePath("sessions.sqlite"), error));
    QVERIFY(server.addInitialUser("alice", "one", error));
    QVERIFY(server.listen(QHostAddress::LocalHost, 0, error));
    QTcpSocket first, second;
    first.connectToHost(QHostAddress::LocalHost, server.port());
    second.connectToHost(QHostAddress::LocalHost, server.port());
    QTRY_COMPARE(first.state(), QAbstractSocket::ConnectedState);
    QTRY_COMPARE(second.state(), QAbstractSocket::ConnectedState);
    auto r = exchange(first, request("login","1",{},{{"username","alice"},{"password","one"}}));
    const QString token = r.value("payload").toObject().value("token").toString();
    QVERIFY(!token.isEmpty());
    r = exchange(second, request("list_my_warehouses","1",token,{}));
    QCOMPARE(r.value("code").toString(), QStringLiteral("NotAuthenticated"));
    r = exchange(first, request("logout","2",token,{}));
    QCOMPARE(r.value("type").toString(), QStringLiteral("logout_resp"));
    r = exchange(first, request("list_my_warehouses","3",token,{}));
    QCOMPARE(r.value("code").toString(), QStringLiteral("NotAuthenticated"));
    r = exchange(first, request("login","4",{},{{"username","alice"},{"password","one"}}));
    const QString oldToken = r.value("payload").toObject().value("token").toString();
    QVERIFY(!oldToken.isEmpty());
    first.disconnectFromHost();
    QTRY_COMPARE(first.state(), QAbstractSocket::UnconnectedState);
    QTcpSocket replacement;
    replacement.connectToHost(QHostAddress::LocalHost, server.port());
    QTRY_COMPARE(replacement.state(), QAbstractSocket::ConnectedState);
    r = exchange(replacement, request("list_my_warehouses","1",oldToken,{}));
    QCOMPARE(r.value("code").toString(), QStringLiteral("NotAuthenticated"));
    second.write(QByteArray::fromHex("00000000"));
    QTRY_COMPARE(second.state(), QAbstractSocket::UnconnectedState);
}
void WarehouseServerTests::candidateLimitsDoNotReturnPartialData() {
    for (int mode = 0; mode < 2; ++mode) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("limits.sqlite");
        WarehouseServer server;
        QString error;
        QVERIFY(server.open(path, error));
        QVERIFY(server.addInitialUser("alice", "one", error));
        const QString connection = QStringLiteral("candidate-limit-%1").arg(mode);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            {
                QSqlQuery q(db);
                QVERIFY(q.exec("INSERT INTO warehouses(name,owner_id,visibility) VALUES('test',1,'private')"));
                MediaRecord media; media.id = "sample";
                media.fileName = mode == 0 ? QStringLiteral("small") : QString(5 * 1024 * 1024, QLatin1Char('x'));
                const QString json = QString::fromUtf8(QJsonDocument(media_json::toJson(media)).toJson(QJsonDocument::Compact));
                q.prepare(mode == 0
                    ? "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) INSERT INTO warehouse_items(warehouse_id,media_json) SELECT 1,? FROM n"
                    : "INSERT INTO warehouse_items(warehouse_id,media_json) VALUES(1,?)");
                q.addBindValue(json);
                QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QList<warehouse_contract::WarehouseDetail> result(1);
        result[0].warehouse.name = "sentinel";
        QVERIFY(!server.readableWarehouses(1, result, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(result.size(), 1);
        QCOMPARE(result.first().warehouse.name, QStringLiteral("sentinel"));
    }
}
QTEST_GUILESS_MAIN(WarehouseServerTests)
#include "warehouse_server_tests.moc"
