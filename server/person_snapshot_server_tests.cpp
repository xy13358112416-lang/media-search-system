#include "online_care_client.h"
#include "warehouse_server.h"
#include "person_store.h"
#include "person_snapshot_json.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace cc = care_contract;
namespace ca = care_algorithms;
using namespace warehouse_contract;

class PersonSnapshotServerTests : public QObject {
    Q_OBJECT
private slots:
    void sqliteStoreProvidesConfirmedPeopleToRealServer() {
        QTemporaryDir dir; const auto path=dir.filePath("store-integration.sqlite"); QString error;
        WarehouseServer server(llm_recommendation::Config{}); QVERIFY(server.openDatabase(path,error));
        QVERIFY(server.addUser("alice","password-123",error)); QVERIFY(server.addUser("bob","password-123",error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient alice,bob; bool done=false; Status status; qint64 aliceId=0;
        for (auto* client : {&alice,&bob}) {
            client->connectToServer("127.0.0.1",server.serverPort(),[&](Status s) { status=s; done=true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            client->login(client==&alice ? "alice" : "bob","password-123",[&](auto r) {
                status=r.status; if (client==&alice && r.value) aliceId=r.value->userId; done=true;
            });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
        }
        Result<WarehouseSummary> warehouse;
        alice.createWarehouse("private",Visibility::Private,[&](auto r) { warehouse=r; done=true; });
        QTRY_VERIFY(done); QVERIFY(warehouse.value); done=false;
        MediaRecord media; media.id="not-person-id"; media.fileName="Film";
        Result<WarehouseItem> item;
        alice.addItem(warehouse.value->warehouseId,media,[&](auto r) { item=r; done=true; });
        QTRY_VERIFY(done); QVERIFY(item.value); done=false;
        Result<cc::ResourceSummary> resource;
        alice.getResource({warehouse.value->warehouseId,item.value->itemId},[&](auto r) { resource=r; done=true; });
        QTRY_VERIFY(done); QVERIFY(resource.value); const auto ref=resource.value->resource; done=false;
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE","person-store-fixture"); db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery q(db); QVERIFY(q.exec("INSERT INTO person_catalog VALUES('person:test','Verified','fixture')"));
            q.prepare("INSERT INTO person_credits VALUES('person:test',?,'actor','fixture')"); q.addBindValue(ref.workId); QVERIFY(q.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase("person-store-fixture");
        alice.getResource(ref.source,[&](auto r) { resource=r; done=true; });
        QTRY_VERIFY(done); QVERIFY(resource.status.ok() && resource.value); QCOMPARE(resource.value->people.size(),1);
        QCOMPARE(resource.value->people.first().personId,QStringLiteral("person:test")); done=false;
        alice.recordBehavior({"store-view",cc::BehaviorAction::View,ref,{}},[&](Status s) { status=s; done=true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
        const auto history=server.queryBehavior(aliceId,{0,cc::MaxUtcExclusive}); QVERIFY(history); QCOMPARE(history->size(),1);
        QCOMPARE(history->first().peopleSnapshot.size(),1); QCOMPARE(history->first().peopleSnapshot.first().personId,QStringLiteral("person:test"));
        bob.getResource(ref.source,[&](auto r) { status=r.status; done=true; });
        QTRY_VERIFY(done); QCOMPARE(status.code,ErrorCode::PermissionDenied);
    }
    void historyExtensionRejectsCorruptionAndAllowsLegacy() {
        MediaRecord media; media.id = "m"; media.fileName = "Legacy";
        const cc::ResourceRef ref{"srv:legacy", {1, 2}};
        const auto legacy = person_snapshot_json::decode({}, ref, media);
        QVERIFY(legacy && legacy->isEmpty());
        QJsonObject snapshot{{person_snapshot_json::Field, QJsonArray{}}};
        const auto empty = person_snapshot_json::decode(snapshot, ref, media);
        QVERIFY(empty && empty->isEmpty());
        for (const auto value : {QJsonValue(QJsonValue::Null), QJsonValue("bad"),
             QJsonValue(QJsonArray{QJsonObject{{"person_id", "p"}, {"name", "Name"}, {"roles", QJsonArray{"invented"}}}})}) {
            snapshot[person_snapshot_json::Field] = value;
            QVERIFY(!person_snapshot_json::decode(snapshot, ref, media));
        }
    }
    void temporalRpcUsesServerFollowsAndHonorsSettingsAndFeedback() {
        QTemporaryDir dir; QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY(server.openDatabase(dir.filePath("temporal.sqlite"), error));
        QVERIFY(server.addUser("alice", "password-123", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; bool done = false; Status status;
        client.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "password-123", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        Result<WarehouseSummary> warehouse;
        client.createWarehouse("private", Visibility::Private, [&](auto r) { warehouse = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(warehouse.value); done = false;
        QList<cc::ResourceSummary> works;
        for (int i = 0; i < 2; ++i) {
            MediaRecord media; media.id = "same-untrusted-csv-id"; media.fileName = QString("work-%1").arg(i);
            Result<WarehouseItem> item;
            client.addItem(warehouse.value->warehouseId, media, [&](auto r) { item = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(item.value); done = false;
            Result<cc::ResourceSummary> resource;
            client.getResource({warehouse.value->warehouseId, item.value->itemId}, [&](auto r) { resource = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(resource.value); done = false;
            works.append(*resource.value);
        }
        server.setPersonContextLoader([&](qint64, const QList<cc::ResourceRef>& sources) -> cc::Result<ca::PersonContext> {
            ca::PersonContext context;
            context.followedPeople = {"person:followed"};
            for (const auto& source : sources)
                if (source.workId == works.last().resource.workId)
                    context.credits.append({source, {{"person:followed", "Confirmed", {cc::PersonRole::Actor}, false}}});
            return {{}, context};
        });
        Result<cc::TemporalRecommendationResult> result;
        client.recommendTemporal({}, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); done = false;
        QCOMPARE(result.value->items.size(), 1);
        QCOMPARE(result.value->items.first().work.resource.workId, works.last().resource.workId);
        QVERIFY(result.value->items.first().reason.contains(QString::fromUtf8("主动关注=0.050000")));
        client.setParticipationSettings({true, false, false}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.recommendTemporal({}, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value); done = false;
        QCOMPARE(result.value->items.size(), 2);
        for (const auto& item : result.value->items)
            QVERIFY(item.reason.contains(QString::fromUtf8("主动关注=0.000000")));
        client.setWorkFeedback({works.last().resource, cc::WorkFeedbackKind::NotInterested}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.setParticipationSettings({true, true, false}, [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.recommendTemporal({}, [&](auto r) { result = r; done = true; });
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value);
        for (const auto& item : result.value->items)
            QVERIFY(item.work.resource.workId != works.last().resource.workId);
    }
    void authorizedCreditsSurviveDeletionAndRestart() {
        QTemporaryDir dir; QString error;
        const auto path = dir.filePath("persons.sqlite");
        cc::ResourceRef ref;
        qint64 aliceId = 0;
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY(server.openDatabase(path, error));
            QVERIFY(server.addUser("alice", "password-123", error));
            QVERIFY(server.addUser("bob", "password-123", error));
            QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice, bob; bool done = false; Status status;
            for (auto* client : {&alice, &bob}) {
                client->connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
                client->login(client == &alice ? "alice" : "bob", "password-123", [&](auto r) {
                    status = r.status; if (client == &alice && r.value) aliceId = r.value->userId; done = true;
                });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            }
            Result<WarehouseSummary> warehouse;
            alice.createWarehouse("private", Visibility::Private, [&](auto r) { warehouse = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(warehouse.value); done = false;
            // Existing media_json permits a nonempty whitespace title. Person attachment
            // must not introduce stricter media validation into get_resource or capture.
            MediaRecord media; media.id = "untrusted-person:evil"; media.fileName = "   ";
            media.creator = "Wrong name"; media.actors = {"Wrong name"}; media.category = "film";
            Result<WarehouseItem> item;
            alice.addItem(warehouse.value->warehouseId, media, [&](auto r) { item = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(item.value); done = false;
            Result<cc::ResourceSummary> resource;
            alice.getResource({warehouse.value->warehouseId, item.value->itemId}, [&](auto r) { resource = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(resource.value); QVERIFY(resource.value->people.isEmpty()); done = false;
            ref = resource.value->resource;
            int loads = 0;
            server.setPersonContextLoader([&](qint64 user, const QList<cc::ResourceRef>& sources) -> cc::Result<ca::PersonContext> {
                ++loads;
                if (user != aliceId || sources.size() != 1 || sources.first().workId != ref.workId)
                    return {{ErrorCode::ServerError, "unexpected scope"}, {}};
                ca::PersonContext context;
                context.credits = {{ref, {{"person:test-001", "Confirmed name", {cc::PersonRole::Actor, cc::PersonRole::Director}, true}}}};
                context.followedPeople = {"person:test-001"};
                return {{}, context};
            });
            bob.getResource(ref.source, [&](auto r) { resource = r; done = true; });
            QTRY_VERIFY(done); QCOMPARE(resource.status.code, ErrorCode::PermissionDenied); QCOMPARE(loads, 0); done = false;
            alice.getResource(ref.source, [&](auto r) { resource = r; done = true; });
            QTRY_VERIFY(done); QVERIFY(resource.value); QCOMPARE(resource.value->people.size(), 1);
            QCOMPARE(resource.value->people.first().personId, QString("person:test-001"));
            QVERIFY(resource.value->people.first().followed); done = false;
            const cc::BehaviorEvent event{"view-person", cc::BehaviorAction::View, ref, {}};
            alice.recordBehavior(event, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            const auto history = server.queryBehavior(aliceId, {0, cc::MaxUtcExclusive});
            QVERIFY(history); QCOMPARE(history->size(), 1); QCOMPARE(history->first().peopleSnapshot.size(), 1);
            QCOMPARE(history->first().peopleSnapshot.first().roles.size(), 2);
            QVERIFY(!history->first().peopleSnapshot.first().followed);
            // Provider failure must not block a retry or rewrite an already captured event.
            server.setPersonContextLoader([](auto, const auto&) -> cc::Result<ca::PersonContext> {
                return {{ErrorCode::ServerError, "store unavailable"}, {}};
            });
            alice.recordBehavior(event, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.recordBehavior({"new-view", cc::BehaviorAction::View, ref, {}}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::ServerError); done = false;
            QCOMPARE(server.queryBehavior(aliceId, {0, cc::MaxUtcExclusive})->size(), 1);
            alice.setParticipationSettings({false, true, false}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.recordBehavior({"paused-view", cc::BehaviorAction::View, ref, {}}, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.removeItem(ref.source.warehouseId, ref.source.itemId, [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok());
        }
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY(server.openDatabase(path, error)); QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice; bool done = false; Status status;
            alice.connectToServer("127.0.0.1", server.serverPort(), [&](Status s) { status = s; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            alice.login("alice", "password-123", [&](auto r) { status = r.status; done = true; });
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
            Result<cc::FootprintReport> report;
            const auto now = QDateTime::currentSecsSinceEpoch();
            alice.getFootprint({{now - 86400, now + 2}, {}}, [&](auto r) { report = r; done = true; });
            QTRY_VERIFY(done); QVERIFY2(report.status.ok(), qPrintable(report.status.message)); QVERIFY(report.value);
            QCOMPARE(report.value->current.views, 1); QCOMPARE(report.value->current.people.size(), 2);
            for (const auto& person : report.value->current.people) {
                QCOMPARE(person.personId, QString("person:test-001")); QCOMPARE(person.count, 1);
            }
        }
    }
};
QTEST_GUILESS_MAIN(PersonSnapshotServerTests)
#include "person_snapshot_server_tests.moc"
