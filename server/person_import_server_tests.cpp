#include "online_care_client.h"
#include "warehouse_server.h"
#include "person_credit_import.h"
#include "person_store_sqlite.h"
#include "care_person.h"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace care_contract;
using warehouse_contract::WarehouseSummary;
using warehouse_contract::WarehouseItem;
using warehouse_contract::Visibility;
using warehouse_contract::ErrorCode;

class PersonImportServerTests : public QObject {
    Q_OBJECT
private slots:
    void importedPeopleReachResourceHistoryFootprintAndFollowRecommendation() {
        QTemporaryDir dir;
        const auto path = dir.filePath("server.sqlite");
        ResourceRef ref;
        qint64 aliceId = 0;
        QString error;
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY2(server.openDatabase(path,error), qPrintable(error));
            QVERIFY(server.addUser("alice","password-123",error));
            QVERIFY(server.addUser("bob","password-123",error));
            QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice,bob;
            bool done = false;
            Status status;
            for (auto* client : {&alice,&bob}) {
                client->connectToServer("127.0.0.1",server.serverPort(),[&](Status s){status=s;done=true;});
                QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
                client->login(client==&alice?"alice":"bob","password-123",[&](auto r){
                    status=r.status; if(client==&alice && r.value) aliceId=r.value->userId;done=true;
                });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            }
            Result<WarehouseSummary> warehouse;
            alice.createWarehouse("Private demo",Visibility::Private,[&](auto r){warehouse=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(warehouse.value);done=false;
            MediaRecord media;media.id="local-row";media.fileName="Fictional performance.mp4";
            media.fileType="MP4";media.category="music";media.actors={"Unconfirmed CSV name"};
            media.releaseDate=QDate(2024,1,1);
            Result<WarehouseItem> item;
            alice.addItem(warehouse.value->warehouseId,media,[&](auto r){item=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(item.value);done=false;
            Result<ResourceSummary> work;
            alice.getResource({warehouse.value->warehouseId,item.value->itemId},[&](auto r){work=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(work.value);QVERIFY(work.value->people.isEmpty());done=false;
            ref=work.value->resource;
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE","person-import-live-test");
                db.setDatabaseName(path);QVERIFY(db.open());
                QSqlQuery q(db);QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
                person_credit_import::Batch batch;
                batch.synthetic=true;batch.provenance="Hand-authored fictional integration fixture.";
                batch.people={{"person:demo","Fictional Person",QUrl("https://example.invalid/person/demo"),{"Demo alias"}}};
                batch.credits={{ref,"person:demo",PersonRole::Actor,QUrl("https://example.invalid/credit/demo")},
                               {ref,"person:demo",PersonRole::Musician,QUrl("https://example.invalid/credit/demo")}};
                auto imported=person_credit_import::importBatch(db,batch,{true});
                QVERIFY2(imported.status.ok(),qPrintable(imported.status.message));QVERIFY(imported.value);
                QCOMPARE(imported.value->peopleInserted,1);QCOMPARE(imported.value->creditsInserted,2);
                const auto again=person_credit_import::importBatch(db,batch,{true});
                QVERIFY(again.status.ok() && again.value);QCOMPARE(again.value->creditsInserted,0);
                db.close();
            }
            QSqlDatabase::removeDatabase("person-import-live-test");

            Result<Page<PersonSummary>> found;
            bob.searchPeople({"Demo alias", {}}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.isEmpty()); done=false;
            alice.searchPeople({QStringLiteral("Ｄｅｍｏ alias"), {}}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.status.ok() && found.value); done=false;
            QCOMPARE(found.value->items.size(), 1);
            QCOMPARE(found.value->items.first().personId, QStringLiteral("person:demo"));
            QCOMPARE(found.value->items.first().roles.size(),2);
            QVERIFY(!found.value->items.first().followed);

            Result<Page<ResourceSummary>> linked;
            bob.getPersonWorks("person:demo", {}, [&](auto r){linked=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(linked.status.code, ErrorCode::NotFound); done=false;
            alice.getPersonWorks("person:demo", {}, [&](auto r){linked=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(linked.status.ok() && linked.value); done=false;
            QCOMPARE(linked.value->items.size(), 1);
            QCOMPARE(linked.value->items.first().resource.source.itemId, ref.source.itemId);

            bob.setFollow("person:demo", true, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QCOMPARE(status.code,ErrorCode::PermissionDenied); done=false;
            alice.setFollow("person:demo", true, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            bob.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.isEmpty()); done=false;
            alice.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.size()==1); done=false;

            const auto now = QDateTime::currentSecsSinceEpoch();
            const PersonUpdatesRequest updates{"person:demo", {now-3600,now+86400}, {}};
            Result<Page<PersonUpdate>> news;
            alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(news.status.code,ErrorCode::ServerError); done=false;
            QVERIFY(news.status.message.contains(QStringLiteral("尚未配置")));
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE","person-import-updates-test");
                db.setDatabaseName(path); QVERIFY(db.open());
                QSqlQuery q(db); QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
                const QUrl feed("https://example.invalid/feed");
                QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) VALUES('person:demo','https://example.invalid/feed')"));
                person_store::SqliteStore store(db);
                const auto gen=store.beginUpdatesRefresh("person:demo",feed);
                QVERIFY(gen.status.ok() && gen.value);
                const PersonUpdate newsItem{"news:demo","person:demo","Fictional story",UpdateKind::News,
                                            QUrl("https://example.invalid/story"),std::nullopt,std::nullopt,now,false};
                QVERIFY(store.mergeUpdates("person:demo",feed,*gen.value,now,{newsItem}).ok());
                db.close();
            }
            QSqlDatabase::removeDatabase("person-import-updates-test");
            bob.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(news.status.code,ErrorCode::PermissionDenied); done=false;
            alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(news.status.ok() && news.value); done=false;
            QCOMPARE(news.value->items.size(),1);
            QCOMPARE(news.value->items.first().title,QStringLiteral("Fictional story"));
            QVERIFY(!news.value->items.first().publishedAtUtc);

            bob.getResource(ref.source,[&](auto r){work=r;done=true;});
            QTRY_VERIFY(done);QCOMPARE(work.status.code,ErrorCode::PermissionDenied);done=false;
            alice.getResource(ref.source,[&](auto r){work=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(work.value);QCOMPARE(work.value->people.size(),1);
            QCOMPARE(work.value->people.first().roles.size(),2);QVERIFY(work.value->people.first().followed);done=false;
            care_algorithms::CatalogSnapshot catalog;
            catalog.readableWorks={*work.value};catalog.followedPeople={"person:demo"};
            const auto people=care_algorithms::searchPeople({"Fictional",{}},catalog);
            QVERIFY(people.status.ok() && people.value);QCOMPARE(people.value->items.size(),1);
            const auto works=care_algorithms::worksForPerson("person:demo",{},catalog);
            QVERIFY(works.status.ok() && works.value);QCOMPARE(works.value->items.size(),1);
            Result<TemporalRecommendationResult> recommendation;
            alice.recommendTemporal({},[&](auto r){recommendation=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(recommendation.status.ok() && recommendation.value);done=false;
            QCOMPARE(recommendation.value->items.size(),1);
            QVERIFY(recommendation.value->items.first().reason.contains(QString::fromUtf8("主动关注=0.050000")));
            alice.recordBehavior({"view:imported",BehaviorAction::View,ref,{}},[&](Status s){status=s;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok());done=false;
            const auto history=server.queryBehavior(aliceId,{0,MaxUtcExclusive});
            QVERIFY(history);QCOMPARE(history->size(),1);QCOMPARE(history->first().peopleSnapshot.size(),1);
            alice.removeItem(ref.source.warehouseId,ref.source.itemId,[&](Status s){status=s;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok()); done=false;
            alice.getPersonWorks("person:demo", {}, [&](auto r){linked=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(linked.status.code,ErrorCode::NotFound); done=false;
            alice.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.size()==1); done=false;
            alice.setFollow("person:demo", false, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            alice.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.isEmpty());
        }
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY(server.openDatabase(path,error));QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice;bool done=false;Status status;
            alice.connectToServer("127.0.0.1",server.serverPort(),[&](Status s){status=s;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok());done=false;
            alice.login("alice","password-123",[&](auto r){status=r.status;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok());done=false;
            Result<FootprintReport> report;const auto now=QDateTime::currentSecsSinceEpoch();
            alice.getFootprint({{now-86400,now+2},{}},[&](auto r){report=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(report.status.ok() && report.value);
            QCOMPARE(report.value->current.views,1);QCOMPARE(report.value->current.people.size(),2);
            for (const auto& person:report.value->current.people) {QCOMPARE(person.personId,QString("person:demo"));QCOMPARE(person.count,1);}
        }
    }
    void cliRejectsMissingDatabaseAndDefaultsToRejectingSyntheticInput() {
        QTemporaryDir dir;QString error;
        const auto path=dir.filePath("operator.sqlite");
        {WarehouseServer server(llm_recommendation::Config{});QVERIFY(server.openDatabase(path,error));}
        const auto exe=QCoreApplication::applicationDirPath()+"/media_person_import"
#ifdef Q_OS_WIN
            +".exe"
#endif
            ;
        QVERIFY(QFile::exists(exe));
        const auto fixture=QString(PERSON_IMPORT_SOURCE_DIR)+"/sample-data/person/import-demo.synthetic.json";
        QProcess process;
        process.start(exe,{dir.filePath("missing.sqlite"),fixture});
        QVERIFY(process.waitForFinished());QVERIFY(process.exitCode()!=0);QVERIFY(!QFile::exists(dir.filePath("missing.sqlite")));
        process.start(exe,{path,fixture});QVERIFY(process.waitForFinished());QVERIFY(process.exitCode()!=0);
        QVERIFY(!process.readAllStandardError().isEmpty());
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE","person-import-cli-seed");db.setDatabaseName(path);QVERIFY(db.open());
            QSqlQuery q(db);
            // Synthetic registered source fixture; the importer never creates these rows.
            QVERIFY(q.exec("INSERT INTO network_users(id,username,username_key,password_hash) VALUES(1,'fixture-user','fixture-user','unused')"));
            QVERIFY(q.exec("INSERT INTO warehouses(id,owner_id,name,is_public) VALUES(1,1,'Fixture',0)"));
            QVERIFY(q.exec("INSERT INTO warehouse_items(id,warehouse_id,media) VALUES(1,1,'{}')"));
            QVERIFY(q.exec("INSERT INTO work_sources(warehouse_id,item_id,work_id) VALUES(1,1,'srv:demo-registered-work')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("person-import-cli-seed");
        process.start(exe,{"--allow-synthetic",path,fixture});QVERIFY(process.waitForFinished());
        QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        const auto report=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QCOMPARE(report.value("people_inserted").toInt(),1);QCOMPARE(report.value("credits_inserted").toInt(),2);
        process.start(exe,{"--allow-synthetic",path,fixture});QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
        const auto repeated=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QCOMPARE(repeated.value("people_inserted").toInt(),0);QCOMPARE(repeated.value("credits_inserted").toInt(),0);
        process.start(exe,{"--help"});QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
    }
};
QTEST_GUILESS_MAIN(PersonImportServerTests)
#include "person_import_server_tests.moc"
