#include "online_care_client.h"
#include "warehouse_server.h"
#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest>

using namespace warehouse_contract;
using care_contract::BehaviorAction;
using care_contract::BehaviorEvent;
using care_contract::MaxUtcExclusive;
using care_contract::ParticipationSettings;
using care_contract::ResourceSummary;

class CareClientServerTests : public QObject {
    Q_OBJECT
private slots:
    void logoutDuringSentWriteReportsUnknownOnce()
    {
        QTemporaryDir directory; QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(directory.filePath("server.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient client;
        bool done = false; Status status;
        client.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "alice-demo", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok());
        int calls = 0;
        client.recordBehavior({"event-logout", BehaviorAction::Search, std::nullopt, "music"},
                              [&](Status s) { status = s; ++calls; });
        client.logout([](Status) {});
        QCOMPARE(calls, 1); QCOMPARE(status.code, ErrorCode::OutcomeUnknown);
        QTest::qWait(100); QCOMPARE(calls, 1);
    }
    void unsupportedAndDisconnectedFailOnce()
    {
        OnlineCareClient client;
        care_contract::CareService* care = &client;
        int calls = 0;
        Status status;
        care->searchPeople({}, [&](auto result) { ++calls; status = result.status; QVERIFY(!result.value); });
        QCOMPARE(calls, 1); QCOMPARE(status.code, ErrorCode::ServerError);
        calls = 0;
        care->recordBehavior({"search-1", BehaviorAction::Search, std::nullopt, "music"},
                             [&](Status result) { ++calls; status = result; });
        QCOMPARE(calls, 1); QCOMPARE(status.code, ErrorCode::NotConnected);
        calls = 0;
        const auto handle = care->recommendTemporal({}, [&](auto result) { ++calls; status = result.status; });
        QVERIFY(handle.isEmpty()); QCOMPARE(calls, 1); QCOMPARE(status.code, ErrorCode::ServerError);
    }
    void realServerBehaviorFlow()
    {
        QTemporaryDir directory;
        QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(directory.filePath("server.sqlite"), error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.addUser("bob", "bob-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient client;
        bool done = false;
        Status status;
        client.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "alice-demo", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        Result<WarehouseSummary> warehouse;
        client.createWarehouse("private", Visibility::Private, [&](auto r) { warehouse = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(warehouse.status.ok()); done = false;
        MediaRecord media; media.id = "fixture:1"; media.fileName = "Music"; media.category = "music";
        Result<WarehouseItem> item;
        client.addItem(warehouse.value->warehouseId, media, [&](auto r) { item = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(item.status.ok()); done = false;
        Result<ResourceSummary> resource;
        client.getResource({warehouse.value->warehouseId, item.value->itemId}, [&](auto r) { resource = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(resource.status.ok()); QVERIFY(resource.value); done = false;
        QCOMPARE(resource.value->media.id, media.id);
        BehaviorEvent event{"same-event", BehaviorAction::View, resource.value->resource, {}};
        client.recordBehavior(event, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.recordBehavior(event, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        auto history = server.queryBehavior(1, {0, MaxUtcExclusive});
        QVERIFY(history.has_value()); QCOMPARE(history->size(), 1);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        Result<care_contract::FootprintReport> footprint;
        client.getFootprint({{now - 60, now + 60}, std::nullopt}, [&](auto r) { footprint = r; done = true; });
        QTRY_VERIFY(done); QVERIFY2(footprint.status.ok(), qPrintable(footprint.status.message));
        QVERIFY(footprint.value); QCOMPARE(footprint.value->current.views, 1);
        QCOMPARE(footprint.value->current.uniqueWorks, 1); done = false;
        QCOMPARE(footprint.value->current.categories.size(), 1);
        QCOMPARE(footprint.value->current.categories.first().label, QStringLiteral("music"));
        QVERIFY(footprint.value->message.contains(QStringLiteral("人物")));
        client.removeItem(warehouse.value->warehouseId, item.value->itemId, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.getFootprint({{now - 60, now + 60}, care_contract::TimeRange{now - 180, now - 60}},
                            [&](auto r) { footprint = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(footprint.status.ok()); QVERIFY(footprint.value->comparison);
        QCOMPARE(footprint.value->current.views, 1);
        QCOMPARE(footprint.value->current.categories.first().label, QStringLiteral("music"));
        QCOMPARE(footprint.value->comparison->views, 0); done = false;
        OnlineCareClient bob;
        bob.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        bob.login("bob", "bob-demo", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        bob.getFootprint({{now - 60, now + 60}, std::nullopt}, [&](auto r) { footprint = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(footprint.status.ok()); QCOMPARE(footprint.value->current.views, 0); done = false;
        client.getFootprint({{now, now}, std::nullopt}, [&](auto r) { footprint = r; done = true; });
        QVERIFY(done); QCOMPARE(footprint.status.code, ErrorCode::InvalidArgument); done = false;
        Result<ParticipationSettings> settings;
        client.getParticipationSettings([&](auto r) { settings = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(settings.status.ok()); QVERIFY(settings.value); QVERIFY(settings.value->historyEnabled); done = false;
        auto changed = *settings.value; changed.historyEnabled = false;
        client.setParticipationSettings(changed, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.clearHistory([&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok());
        history = server.queryBehavior(1, {0, MaxUtcExclusive});
        QVERIFY(history.has_value()); QVERIFY(history->isEmpty());
        done = false;
        client.getFootprint({{now - 60, now + 60}, std::nullopt}, [&](auto r) { footprint = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(footprint.status.ok()); QCOMPARE(footprint.value->current.views, 0);
    }
};
QTEST_GUILESS_MAIN(CareClientServerTests)
#include "care_client_server_tests.moc"
