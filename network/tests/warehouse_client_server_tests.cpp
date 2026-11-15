#include "warehouse_client.h"
#include "warehouse_server.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace warehouse_contract;

class ClientServerTests : public QObject {
    Q_OBJECT
private slots:
    void twoUsersRespectServerPermissions();
    void switchAccountsOnOneConnection();
};

void ClientServerTests::twoUsersRespectServerPermissions() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WarehouseServer server;
    QString error;
    QVERIFY2(server.openDatabase(dir.filePath("server.sqlite"), error), qPrintable(error));
    QVERIFY2(server.addUser("alice", "alice-first", error), qPrintable(error));
    QVERIFY2(server.addUser("bob", "bob-second", error), qPrintable(error));
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient alice, bob;
    bool done = false;
    Status status;
    alice.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
    QTRY_VERIFY(done);
    QVERIFY2(status.ok(), qPrintable(status.message));
    done = false;
    bob.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
    QTRY_VERIFY(done);
    QVERIFY2(status.ok(), qPrintable(status.message));
    Result<UserInfo> user;
    done = false;
    alice.login("alice", "alice-first", [&](auto r) { user = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(user.status.ok());
    done = false;
    bob.login("bob", "bob-second", [&](auto r) { user = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(user.status.ok());
    Result<WarehouseSummary> created;
    done = false;
    alice.createWarehouse("private", Visibility::Private, [&](auto r) { created = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(created.status.ok());
    const auto privateId = created.value->warehouseId;
    Result<WarehouseDetail> detail;
    done = false;
    bob.getWarehouse(privateId, [&](auto r) { detail = r; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(detail.status.code, ErrorCode::PermissionDenied);
    done = false;
    alice.createWarehouse("public", Visibility::Public, [&](auto r) { created = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(created.status.ok());
    const auto publicId = created.value->warehouseId;
    MediaRecord media; media.id = "tvmaze:1"; media.fileName = "title";
    Result<WarehouseItem> item;
    done = false;
    alice.addItem(publicId, media, [&](auto r) { item = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY2(item.status.ok(), qPrintable(item.status.message));
    const auto itemId = item.value->itemId;
    done = false;
    bob.getWarehouse(publicId, [&](auto r) { detail = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(detail.status.ok());
    QCOMPARE(detail.value->items.size(), 1);
    QCOMPARE(detail.value->items.first().media.id, media.id);
    done = false;
    bob.removeItem(publicId, itemId, [&](Status s) { status = s; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(status.code, ErrorCode::PermissionDenied);
    done = false;
    bob.logout([&](Status s) { status = s; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(status.ok());
    done = false;
    bob.getWarehouse(publicId, [&](auto r) { detail = r; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(detail.status.code, ErrorCode::NotAuthenticated);
}

void ClientServerTests::switchAccountsOnOneConnection() {
    QTemporaryDir dir;
    WarehouseServer server;
    QString error;
    QVERIFY(server.openDatabase(dir.filePath("switch.sqlite"), error));
    QVERIFY(server.addUser("alice", "alice-first", error));
    QVERIFY(server.addUser("bob", "bob-second", error));
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    WarehouseClient client;
    bool done = false;
    Status status;
    client.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(status.ok());
    Result<UserInfo> user;
    done = false;
    client.login("alice", "alice-first", [&](auto r) { user = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(user.status.ok());
    done = false;
    client.login("bob", "bob-second", [&](auto r) { user = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY2(user.status.ok(), qPrintable(user.status.message));
    QCOMPARE(user.value->username, QStringLiteral("bob"));
}

QTEST_GUILESS_MAIN(ClientServerTests)
#include "warehouse_client_server_tests.moc"
