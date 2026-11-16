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
    void feedbackIsPerUserPersistentAndExcludesRecommendations()
    {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("feedback.sqlite");
        care_contract::ResourceRef publicRef, privateRef;
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY(server.openDatabase(path, error));
            QVERIFY(server.addUser("alice", "password-123", error)); QVERIFY(server.addUser("bob", "password-123", error));
            QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice, bob; bool done = false; Status status;
            for (auto* client : {&alice, &bob}) {
                client->connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
                client->login(client == &alice ? "alice" : "bob", "password-123", [&](auto r) { status = r.status; done = true; });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            }
            Result<WarehouseSummary> pub, priv;
            alice.createWarehouse("public", Visibility::Public, [&](auto r) { pub = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(pub.status.ok() && pub.value); done = false;
            alice.createWarehouse("private", Visibility::Private, [&](auto r) { priv = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(priv.status.ok() && priv.value); done = false;
            MediaRecord media; media.id = "item"; media.fileName = "Media"; media.category = "music";
            Result<WarehouseItem> publicItem, privateItem;
            alice.addItem(pub.value->warehouseId, media, [&](auto r) { publicItem = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(publicItem.status.ok() && publicItem.value); done = false;
            alice.addItem(priv.value->warehouseId, media, [&](auto r) { privateItem = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(privateItem.status.ok() && privateItem.value); done = false;
            Result<ResourceSummary> resource;
            alice.getResource({pub.value->warehouseId, publicItem.value->itemId}, [&](auto r) { resource = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(resource.status.ok() && resource.value); publicRef = resource.value->resource; done = false;
            alice.getResource({priv.value->warehouseId, privateItem.value->itemId}, [&](auto r) { resource = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(resource.status.ok() && resource.value); privateRef = resource.value->resource; done = false;
            bob.setWorkFeedback({privateRef, care_contract::WorkFeedbackKind::NotInterested}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::PermissionDenied); done = false;
            for (int i = 0; i < 2; ++i) {
                alice.setWorkFeedback({publicRef, care_contract::WorkFeedbackKind::NotInterested}, [&](Status s) { status = s; done = true; });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            }
            alice.setWorkFeedback({privateRef, care_contract::WorkFeedbackKind::NotInterested}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            Result<care_contract::Page<care_contract::WorkFeedback>> page;
            bob.listWorkFeedback({0, 1}, [&](auto r) { page = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(page.status.ok() && page.value); QVERIFY(page.value->items.isEmpty()); done = false;
            alice.listWorkFeedback({0, 1}, [&](auto r) { page = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(page.status.ok() && page.value); QCOMPARE(page.value->items.size(), 1); QVERIFY(page.value->nextOffset); done = false;
            alice.listWorkFeedback({*page.value->nextOffset, 1}, [&](auto r) { page = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(page.status.ok() && page.value); QCOMPARE(page.value->items.size(), 1); QVERIFY(!page.value->nextOffset); done = false;
            alice.clearHistory([&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.setParticipationSettings({true, false, false}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", "feedback-stale");
                db.setDatabaseName(path); QVERIFY(db.open()); QSqlQuery q(db);
                QVERIFY(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10001) "
                    "INSERT INTO work_feedback(user_id,work_id,warehouse_id,item_id) SELECT 1,'stale-'||x,999999,999999 FROM n"));
                db.close();
            }
            QSqlDatabase::removeDatabase("feedback-stale");
            Result<care_contract::TemporalRecommendationResult> rec;
            alice.recommendTemporal({}, [&](auto r) { rec = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(rec.status.ok() && rec.value); QVERIFY(rec.value->items.isEmpty()); done = false;
            bob.recommendTemporal({}, [&](auto r) { rec = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(rec.status.ok() && rec.value); QCOMPARE(rec.value->items.size(), 1);
        }
        {
            WarehouseServer server(llm_recommendation::Config{}); QVERIFY(server.openDatabase(path, error)); QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice; bool done = false; Status status;
            alice.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.login("alice", "password-123", [&](auto r) { status = r.status; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            Result<care_contract::Page<care_contract::WorkFeedback>> page;
            alice.listWorkFeedback({0, 10}, [&](auto r) { page = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(page.status.ok() && page.value); QCOMPARE(page.value->items.size(), 2); done = false;
            alice.removeItem(publicRef.source.warehouseId, publicRef.source.itemId, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.listWorkFeedback({0, 10}, [&](auto r) { page = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(page.status.ok() && page.value); QCOMPARE(page.value->items.size(), 1); done = false;
            alice.setWorkFeedback({publicRef, care_contract::WorkFeedbackKind::Neutral}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            auto alternateSource = privateRef; alternateSource.source = {999999, 999999};
            alice.setWorkFeedback({alternateSource, care_contract::WorkFeedbackKind::Neutral}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.listWorkFeedback({0, 10}, [&](auto r) { page = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(page.status.ok() && page.value); QVERIFY(page.value->items.isEmpty());
        }
    }
    void temporalUsesCompletePublicHeatBeyondTopFifty()
    {
        QTemporaryDir dir; QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("heat.sqlite"), error));
        for (const char* user : {"alice", "bob", "carol", "dave"}) QVERIFY(server.addUser(user, "password-123", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient alice, dave; bool done = false; Status status;
        for (auto* client : {&alice, &dave}) {
            client->connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            client->login(client == &alice ? "alice" : "dave", "password-123", [&](auto r) { status = r.status; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        }
        Result<WarehouseSummary> warehouse;
        alice.createWarehouse("public", Visibility::Public, [&](auto r) { warehouse = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(warehouse.status.ok() && warehouse.value); done = false;
        MediaRecord music; music.id = "music"; music.fileName = "Music"; music.category = "music";
        MediaRecord film = music; film.id = "film"; film.fileName = "Film"; film.category = "film";
        Result<WarehouseItem> musicItem, filmItem;
        alice.addItem(warehouse.value->warehouseId, music, [&](auto r) { musicItem = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(musicItem.status.ok() && musicItem.value); done = false;
        alice.addItem(warehouse.value->warehouseId, film, [&](auto r) { filmItem = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(filmItem.status.ok() && filmItem.value); done = false;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "heat-fixture");
            db.setDatabaseName(dir.filePath("heat.sqlite")); QVERIFY(db.open()); QSqlQuery q(db);
            q.prepare("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<49) "
                      "INSERT INTO warehouse_items(warehouse_id,media) SELECT warehouse_id,media FROM warehouse_items,n WHERE id=?");
            q.addBindValue(musicItem.value->itemId); QVERIFY(q.exec());
            q.prepare("INSERT INTO work_sources(warehouse_id,item_id,work_id) SELECT warehouse_id,id,printf('work:%03d',row_number() OVER (ORDER BY id)) "
                      "FROM warehouse_items WHERE id<>?");
            q.addBindValue(filmItem.value->itemId); QVERIFY(q.exec());
            q.prepare("INSERT INTO work_sources(warehouse_id,item_id,work_id) VALUES(?,?,'work:051')");
            q.addBindValue(warehouse.value->warehouseId); q.addBindValue(filmItem.value->itemId); QVERIFY(q.exec());
            QVERIFY(q.exec("INSERT INTO behavior_settings SELECT id,1,1,1 FROM network_users WHERE id<=3"));
            QVERIFY(q.exec("INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,synthetic,public_eligible,work_id,warehouse_id,item_id) "
                "SELECT u.id,'event-'||s.work_id,'view','',strftime('%s','now'),s.work_id='work:051',1,s.work_id,s.warehouse_id,s.item_id "
                "FROM work_sources s CROSS JOIN network_users u WHERE u.id<=3"));
            db.close();
        }
        QSqlDatabase::removeDatabase("heat-fixture");
        care_contract::TemporalRecommendationRequest request;
        request.category = {recommendation_contract::PreferenceMode::Explicit, "film"};
        Result<care_contract::TemporalRecommendationResult> result;
        dave.recommendTemporal(request, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY2(result.status.ok(), qPrintable(result.status.message)); QVERIFY(result.value);
        QVERIFY(!result.value->items.isEmpty());
        QCOMPARE(result.value->items.first().work.resource.workId, QStringLiteral("work:051"));
        QVERIFY(result.value->items.first().reason.contains(QStringLiteral("公共热度=1.000000")));
        QVERIFY(result.value->containsSyntheticData);
    }
    void temporalMapsFiveHundredNewCandidates()
    {
        QTemporaryDir dir; QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("bulk.sqlite"), error));
        QVERIFY(server.addUser("alice", "password-123", error)); QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient alice; bool done = false; Status status;
        alice.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        alice.login("alice", "password-123", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        Result<WarehouseSummary> warehouse;
        alice.createWarehouse("bulk", Visibility::Public, [&](auto r) { warehouse = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(warehouse.status.ok() && warehouse.value); done = false;
        MediaRecord media; media.id = "sample"; media.fileName = "Sample"; media.category = "music";
        Result<WarehouseItem> seed;
        alice.addItem(warehouse.value->warehouseId, media, [&](auto r) { seed = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(seed.status.ok() && seed.value); done = false;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "temporal-bulk");
            db.setDatabaseName(dir.filePath("bulk.sqlite")); QVERIFY(db.open());
            QSqlQuery q(db);
            q.prepare("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<499) "
                      "INSERT INTO warehouse_items(warehouse_id,media) SELECT warehouse_id,media FROM warehouse_items,n WHERE id=?");
            q.addBindValue(seed.value->itemId); QVERIFY(q.exec()); db.close();
        }
        QSqlDatabase::removeDatabase("temporal-bulk");
        Result<care_contract::TemporalRecommendationResult> result;
        alice.recommendTemporal({}, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY2(result.status.ok(), qPrintable(result.status.message)); QVERIFY(result.value); done = false;
        QCOMPARE(result.value->items.size(), 5); const auto first = result.value->items.first().work.resource.workId;
        alice.recommendTemporal({}, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); QCOMPARE(result.value->items.first().work.resource.workId, first);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "temporal-bulk-check");
            db.setDatabaseName(dir.filePath("bulk.sqlite")); QVERIFY(db.open());
            QSqlQuery q(db); QVERIFY(q.exec("SELECT COUNT(*) FROM work_sources")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 500);
            db.close();
        }
        QSqlDatabase::removeDatabase("temporal-bulk-check");
    }
    void temporalRecommendationsRespectSessionHistoryAndPermissions()
    {
        QTemporaryDir dir; QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("temporal.sqlite"), error));
        QVERIFY(server.addUser("alice", "password-123", error)); QVERIFY(server.addUser("bob", "password-123", error));
        QVERIFY(server.addUser("carol", "password-123", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient alice, bob, carol;
        bool done = false; Status status;
        for (auto* client : {&alice, &bob, &carol}) {
            client->connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            client->login(client == &alice ? "alice" : client == &bob ? "bob" : "carol", "password-123", [&](auto r) { status = r.status; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        }
        Result<WarehouseSummary> publicWarehouse, privateWarehouse;
        alice.createWarehouse("public", Visibility::Public, [&](auto r) { publicWarehouse = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(publicWarehouse.status.ok() && publicWarehouse.value); done = false;
        alice.createWarehouse("private", Visibility::Private, [&](auto r) { privateWarehouse = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(privateWarehouse.status.ok() && privateWarehouse.value); done = false;
        MediaRecord music; music.id = "music"; music.fileName = "Music"; music.category = "music";
        MediaRecord film = music; film.id = "film"; film.fileName = "Film"; film.category = "film";
        Result<WarehouseItem> pubItem, privateItem;
        alice.addItem(publicWarehouse.value->warehouseId, music, [&](auto r) { pubItem = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(pubItem.status.ok() && pubItem.value); done = false;
        alice.addItem(privateWarehouse.value->warehouseId, film, [&](auto r) { privateItem = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(privateItem.status.ok() && privateItem.value); done = false;
        Result<ResourceSummary> viewed;
        alice.getResource({privateWarehouse.value->warehouseId, privateItem.value->itemId}, [&](auto r) { viewed = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(viewed.status.ok() && viewed.value); done = false;
        alice.recordBehavior({"view-film", BehaviorAction::View, viewed.value->resource, {}}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        care_contract::TemporalRecommendationRequest request;
        Result<care_contract::TemporalRecommendationResult> result;
        alice.recommendTemporal(request, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY2(result.status.ok(), qPrintable(result.status.message)); QVERIFY(result.value); done = false;
        QVERIFY(!result.value->items.isEmpty()); QCOMPARE(result.value->items.first().work.media.category, QStringLiteral("film"));
        request.category = {recommendation_contract::PreferenceMode::Explicit, "music"};
        alice.recommendTemporal(request, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); QVERIFY(!result.value->items.isEmpty()); done = false;
        QCOMPARE(result.value->items.first().work.media.category, QStringLiteral("music"));
        request.category = {};
        Result<ResourceSummary> publicResource;
        alice.getResource({publicWarehouse.value->warehouseId, pubItem.value->itemId}, [&](auto r) { publicResource = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(publicResource.status.ok() && publicResource.value); done = false;
        for (auto* client : {&alice, &bob, &carol}) {
            client->setParticipationSettings({true, true, true}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            client->recordBehavior({"public-heat", BehaviorAction::View, publicResource.value->resource, {}}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        }
        bob.recommendTemporal(request, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); done = false;
        QVERIFY(!result.value->items.isEmpty());
        for (const auto& row : result.value->items) QCOMPARE(row.work.resource.source.warehouseId, publicWarehouse.value->warehouseId);
        QVERIFY(result.value->items.first().reason.contains(QStringLiteral("公共热度=1.000000")));
        carol.setParticipationSettings({true, true, false}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        bob.recommendTemporal(request, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value && !result.value->items.isEmpty()); done = false;
        QVERIFY(result.value->items.first().reason.contains(QStringLiteral("公共热度=0.000000")));
        bob.setParticipationSettings({true, false, true}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        bob.recommendTemporal(request, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value && !result.value->items.isEmpty()); done = false;
        QVERIFY(result.value->items.first().reason.contains(QStringLiteral("长期兴趣=0.000000")));
        QVERIFY(result.value->items.first().reason.contains(QStringLiteral("近期兴趣=0.000000")));
        int calls = 0;
        const auto handle = bob.recommendTemporal(request, [&](auto r) { result = r; ++calls; });
        QVERIFY(!handle.isEmpty()); QVERIFY(bob.cancelRecommendation(handle));
        QCOMPARE(calls, 1); QCOMPARE(result.status.code, ErrorCode::Cancelled);
        QTest::qWait(100); QCOMPARE(calls, 1);
    }
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
        done = false;
        client.login("alice", "alice-demo", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok());
        calls = 0;
        client.setWorkFeedback({{"work:test", {1, 1}}, care_contract::WorkFeedbackKind::NotInterested},
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
        QVERIFY(handle.isEmpty()); QCOMPARE(calls, 1); QCOMPARE(status.code, ErrorCode::NotConnected);
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
