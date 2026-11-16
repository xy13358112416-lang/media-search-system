#include "online_care_client.h"
#include "warehouse_server.h"
#include <QDateTime>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
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
    void publicTrendingUsesOptedInUsersAndDeduplicates()
    {
        QTemporaryDir dir; QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("trending.sqlite"), error));
        for (const char* name : {"alice", "bob", "carol"}) QVERIFY(server.addUser(name, "password-123", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient alice, bob, carol;
        QList<OnlineCareClient*> clients{&alice, &bob, &carol};
        const QStringList names{"alice", "bob", "carol"};
        bool done = false; Status status;
        for (int i = 0; i < clients.size(); ++i) {
            clients[i]->connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            clients[i]->login(names[i], "password-123", [&](auto r) { status = r.status; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            clients[i]->setParticipationSettings({true, true, true}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        }
        Result<WarehouseSummary> shared;
        alice.createWarehouse("shared", Visibility::Public, [&](auto r) { shared = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(shared.status.ok() && shared.value); done = false;
        MediaRecord media; media.id = "row"; media.fileName = "Music"; media.category = "music";
        Result<WarehouseItem> item;
        alice.addItem(shared.value->warehouseId, media, [&](auto r) { item = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(item.status.ok() && item.value); done = false;
        Result<ResourceSummary> resource;
        alice.getResource({shared.value->warehouseId, item.value->itemId}, [&](auto r) { resource = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(resource.status.ok() && resource.value); done = false;
        for (int i = 0; i < clients.size(); ++i) {
            clients[i]->recordBehavior({"view", BehaviorAction::View, resource.value->resource, {}}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            clients[i]->recordBehavior({"search", BehaviorAction::Search, std::nullopt, "music"}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        }
        alice.recordBehavior({"extra-view", BehaviorAction::View, resource.value->resource, {}}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        // Ineligible private history must not consume the public snapshot limit.
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "trending-regression");
            db.setDatabaseName(dir.filePath("trending.sqlite")); QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("EXPLAIN QUERY PLAN SELECT 1 FROM behavior_events e WHERE e.warehouse_id=1 AND e.item_id=1 "
                           "AND e.work_id='test' AND e.public_eligible=1 AND e.action='view' AND e.occurred_at_utc>=1 AND e.occurred_at_utc<2"));
            QVERIFY(q.next()); QVERIFY(q.value(3).toString().contains("behavior_events_public_source"));
            QVERIFY(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10001) "
                "INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,synthetic,public_eligible) "
                "SELECT 1,'private-'||x,'search','private',strftime('%s','now'),0,0 FROM n"));
            QVERIFY(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<100001) "
                "INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,synthetic,public_eligible,work_id,warehouse_id,item_id) "
                "SELECT 1,'deleted-'||x,'view','',strftime('%s','now'),0,1,'deleted-work',999999,999999 FROM n"));
            // An unrelated broken catalog row must not break search-term statistics.
            q.prepare("INSERT INTO warehouse_items(warehouse_id,media) VALUES (?,?)");
            q.addBindValue(shared.value->warehouseId); q.addBindValue(QByteArray("{}")); QVERIFY(q.exec());
            const auto brokenId = q.lastInsertId().toLongLong();
            q.prepare("INSERT INTO work_sources(warehouse_id,item_id,work_id) VALUES (?,?,?)");
            q.addBindValue(shared.value->warehouseId); q.addBindValue(brokenId); q.addBindValue("unrelated-broken"); QVERIFY(q.exec());
            q.prepare("INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,synthetic,public_eligible,work_id,warehouse_id,item_id) "
                      "VALUES(1,'future','view','',strftime('%s','now')+30,0,1,'unrelated-broken',?,?)");
            q.addBindValue(shared.value->warehouseId); q.addBindValue(brokenId); QVERIFY(q.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase("trending-regression");
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        Result<care_contract::TrendingResult> result;
        care_contract::TrendingRequest req{care_contract::TrendingKind::Works, {now - 60, now + 60}, 10};
        OnlineCareClient outsider;
        outsider.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        outsider.getTrending(req, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QCOMPARE(result.status.code, ErrorCode::NotAuthenticated); done = false;
        bob.getTrending(req, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY2(result.status.ok(), qPrintable(result.status.message)); QVERIFY(result.value); done = false;
        QCOMPARE(result.value->items.size(), 1); QCOMPARE(result.value->items.first().distinctUsers, 3);
        QCOMPARE(result.value->items.first().effectiveEvents, 3); QVERIFY(result.value->items.first().work);
        QCOMPARE(result.value->items.first().work->resource.workId, resource.value->resource.workId);
        req.kind = care_contract::TrendingKind::SearchTerms;
        bob.getTrending(req, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); done = false;
        QCOMPARE(result.value->items.size(), 1); QCOMPARE(result.value->items.first().term, QStringLiteral("music"));
        alice.removeItem(shared.value->warehouseId, item.value->itemId, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        req.kind = care_contract::TrendingKind::Works;
        bob.getTrending(req, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); QVERIFY(result.value->items.isEmpty()); done = false;
        req.kind = care_contract::TrendingKind::SearchTerms;
        bob.setParticipationSettings({true, true, false}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        carol.getTrending(req, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); QVERIFY(result.value->items.isEmpty());
        done = false; req.limit = 0;
        carol.getTrending(req, [&](auto r) { result = r; done = true; });
        QVERIFY(done); QCOMPARE(result.status.code, ErrorCode::InvalidArgument);
    }
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
