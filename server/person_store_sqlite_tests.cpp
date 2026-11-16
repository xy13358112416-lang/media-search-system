#include "person_schema.h"
#include "person_store_sqlite.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace cc = care_contract;
using warehouse_contract::ErrorCode;

class PersonStoreSqliteTests : public QObject {
    Q_OBJECT
private:
    static bool fixture(QSqlDatabase& db) {
        QSqlQuery q(db);
        if (!q.exec("PRAGMA foreign_keys=ON") ||
            !q.exec("CREATE TABLE network_users(id INTEGER PRIMARY KEY)") ||
            !q.exec("CREATE TABLE warehouses(id INTEGER PRIMARY KEY,owner_id INTEGER NOT NULL,is_public INTEGER NOT NULL)") ||
            !q.exec("CREATE TABLE warehouse_items(id INTEGER PRIMARY KEY,warehouse_id INTEGER NOT NULL)") ||
            !q.exec("CREATE TABLE work_sources(warehouse_id INTEGER,item_id INTEGER,work_id TEXT UNIQUE)") ||
            !q.exec("CREATE TABLE server_schema_migrations(version INTEGER PRIMARY KEY)") ||
            !q.exec("INSERT INTO server_schema_migrations VALUES(1)") ||
            !q.exec("INSERT INTO network_users VALUES(1),(2)") ||
            !q.exec("INSERT INTO warehouses VALUES(11,1,0),(12,1,1)") ||
            !q.exec("INSERT INTO warehouse_items VALUES(101,11),(102,12)") ||
            !q.exec("INSERT INTO work_sources VALUES(11,101,'private-work'),(12,102,'public-work')")) return false;
        QString error; if (!db.transaction() || !person_schema::migrate(db,error) || !db.commit()) return false;
        return q.exec("INSERT INTO person_catalog VALUES('p1','Alex','source1'),('p2','Alex','source2')") &&
            q.exec("INSERT INTO person_credits VALUES('p1','private-work','actor','source'),('p1','private-work','director','source'),('p2','public-work','musician','source')");
    }
    static cc::PersonUpdate updateRow(const QString& personId, const QString& updateId,
                                      const QString& title, qint64 fetchedAtUtc)
    {
        return {updateId, personId, title, cc::UpdateKind::News,
                QUrl("https://example.invalid/article/" + updateId), std::nullopt,
                std::nullopt, fetchedAtUtc, false};
    }
private slots:
    void contextRespectsExactSourceAndVisibility() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            person_store::SqliteStore store(db);
            const cc::ResourceRef priv{"private-work",{11,101}}, pub{"public-work",{12,102}};
            const auto alice = store.contextFor(1,{priv,pub}); QVERIFY(alice.status.ok()); QVERIFY(alice.value);
            QCOMPARE(alice.value->credits.size(),2); QCOMPARE(alice.value->credits.first().people.size(),1);
            QCOMPARE(alice.value->credits.first().people.first().personId,QStringLiteral("p1"));
            QCOMPARE(alice.value->credits.first().people.first().roles.size(),2);
            const auto bob = store.contextFor(2,{pub}); QVERIFY(bob.status.ok()); QCOMPARE(bob.value->credits.first().people.first().personId,QStringLiteral("p2"));
            QCOMPARE(store.contextFor(2,{priv}).status.code,ErrorCode::PermissionDenied);
            QCOMPARE(store.contextFor(1,{{"private-work",{12,102}}}).status.code,ErrorCode::PermissionDenied);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void followIsPrivateIdempotentAndRemovableAfterAccessLoss() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            person_store::SqliteStore store(db);
            QCOMPARE(store.setFollow(2,"p1",true).code,ErrorCode::PermissionDenied);
            QVERIFY(store.setFollow(1,"p1",true).ok()); QVERIFY(store.setFollow(1,"p1",true).ok());
            auto a=store.followsFor(1,{0,20}); QVERIFY(a.status.ok()); QCOMPARE(a.value->items.size(),1);
            auto b=store.followsFor(2,{0,20}); QVERIFY(b.status.ok()); QVERIFY(b.value->items.isEmpty());
            QSqlQuery q(db); QVERIFY(q.exec("DELETE FROM warehouse_items WHERE id=101"));
            a=store.followsFor(1,{0,20}); QVERIFY(a.status.ok()); QCOMPARE(a.value->items.size(),1);
            QCOMPARE(a.value->items.first().name,QStringLiteral("Alex"));
            QVERIFY(store.setFollow(1,"p1",false).ok()); QVERIFY(store.setFollow(1,"p1",false).ok());
            a=store.followsFor(1,{0,20}); QVERIFY(a.status.ok()); QVERIFY(a.value->items.isEmpty());
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void followsPageByOpaqueIdAndKeepEmptyResultSuccessful() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            person_store::SqliteStore store(db);
            QVERIFY(store.setFollow(1,"p2",true).ok());
            QVERIFY(store.setFollow(1,"p1",true).ok());

            const auto first = store.followsFor(1,{0,1});
            QVERIFY(first.status.ok() && first.value);
            QCOMPARE(first.value->items.size(),1);
            QCOMPARE(first.value->items.first().personId,QStringLiteral("p1"));
            QVERIFY(first.value->nextOffset);
            QCOMPARE(*first.value->nextOffset,qint64(1));

            const auto second = store.followsFor(1,{1,1});
            QVERIFY(second.status.ok() && second.value);
            QCOMPARE(second.value->items.size(),1);
            QCOMPARE(second.value->items.first().personId,QStringLiteral("p2"));
            QVERIFY(!second.value->nextOffset);

            const auto empty = store.followsFor(1,{2,1});
            QVERIFY(empty.status.ok() && empty.value);
            QVERIFY(empty.value->items.isEmpty());
            QVERIFY(!empty.value->nextOffset);

            const auto noContext = store.contextFor(1,{});
            QVERIFY(noContext.status.ok() && noContext.value);
            QVERIFY(noContext.value->credits.isEmpty());
            QVERIFY(noContext.value->followedPeople.isEmpty());
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void corruptFollowDataAndClosedDatabaseReturnErrors() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            person_store::SqliteStore store(db);
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_follows VALUES(1,'p1','Alex','not-json')"));
            QCOMPARE(store.followsFor(1,{0,20}).status.code,ErrorCode::ServerError);

            QVERIFY(q.exec("UPDATE person_follows SET known_roles_json='[\"actor\"]',known_name='' WHERE user_id=1"));
            QCOMPARE(store.followsFor(1,{0,20}).status.code,ErrorCode::ServerError);

            QVERIFY(q.exec("UPDATE person_catalog SET display_name='' WHERE person_id='p1'"));
            QCOMPARE(store.contextFor(1,{{"private-work",{11,101}}}).status.code,ErrorCode::ServerError);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);

        const QString closedName = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", closedName);
            person_store::SqliteStore store(db);
            QCOMPARE(store.contextFor(1,{}).status.code,ErrorCode::ServerError);
            QCOMPARE(store.followsFor(1,{0,20}).status.code,ErrorCode::ServerError);
        }
        QSqlDatabase::removeDatabase(closedName);
    }
    void failedFollowInsertRollsBack() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TRIGGER reject_person_follow BEFORE INSERT ON person_follows "
                           "BEGIN SELECT RAISE(ABORT,'reject'); END"));
            person_store::SqliteStore store(db);
            QCOMPARE(store.setFollow(1,"p1",true).code,ErrorCode::ServerError);
            QVERIFY(q.exec("SELECT COUNT(*) FROM person_follows WHERE user_id=1 AND person_id='p1'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(),0);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void followsPersistAcrossDatabaseReopen() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("person-store.sqlite");
        const QString firstConnection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", firstConnection);
            db.setDatabaseName(path); QVERIFY(db.open()); QVERIFY(fixture(db));
            person_store::SqliteStore store(db);
            QVERIFY(store.setFollow(1,"p1",true).ok());
            db.close();
        }
        QSqlDatabase::removeDatabase(firstConnection);

        const QString secondConnection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", secondConnection);
            db.setDatabaseName(path); QVERIFY(db.open());
            person_store::SqliteStore store(db);
            const auto follows = store.followsFor(1,{0,20});
            QVERIFY(follows.status.ok() && follows.value);
            QCOMPARE(follows.value->items.size(),1);
            QCOMPARE(follows.value->items.first().personId,QStringLiteral("p1"));
            db.close();
        }
        QSqlDatabase::removeDatabase(secondConnection);
    }
    void followSnapshotContainsOnlyRolesVisibleToThatUser() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_catalog VALUES('p3','Morgan','source3')"));
            QVERIFY(q.exec("INSERT INTO person_credits VALUES"
                           "('p3','private-work','actor','source-private'),"
                           "('p3','public-work','musician','source-public')"));
            person_store::SqliteStore store(db);
            QVERIFY(store.setFollow(1,"p3",true).ok());
            QVERIFY(store.setFollow(2,"p3",true).ok());

            const auto owner = store.followsFor(1,{0,20});
            QVERIFY(owner.status.ok() && owner.value);
            QCOMPARE(owner.value->items.size(),1);
            QCOMPARE(owner.value->items.first().roles.size(),2);
            QVERIFY(owner.value->items.first().roles.contains(cc::PersonRole::Actor));
            QVERIFY(owner.value->items.first().roles.contains(cc::PersonRole::Musician));

            const auto publicViewer = store.followsFor(2,{0,20});
            QVERIFY(publicViewer.status.ok() && publicViewer.value);
            QCOMPARE(publicViewer.value->items.size(),1);
            QCOMPARE(publicViewer.value->items.first().roles.size(),1);
            QCOMPARE(publicViewer.value->items.first().roles.first(),cc::PersonRole::Musician);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void invalidPagePersonAndUserFailExplicitly() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            person_store::SqliteStore store(db);
            QCOMPARE(store.followsFor(1,{0,0}).status.code,ErrorCode::InvalidArgument);
            QCOMPARE(store.followsFor(1,{-1,1}).status.code,ErrorCode::InvalidArgument);
            QCOMPARE(store.setFollow(1,"bad person",true).code,ErrorCode::InvalidArgument);
            QCOMPARE(store.followsFor(3,{0,1}).status.code,ErrorCode::PermissionDenied);
            QCOMPARE(store.setFollow(3,"p1",true).code,ErrorCode::PermissionDenied);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void sameSecondRefreshUsesPersistentGenerationAndRejectsLateResult() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) "
                           "VALUES('p1','https://example.invalid/feed/alex')"));
            person_store::SqliteStore store(db);
            const QUrl feed("https://example.invalid/feed/alex");
            const auto first = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(first.status.ok() && first.value);
            const auto second = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(second.status.ok() && second.value);
            QCOMPARE(*second.value,*first.value + 1);

            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QCOMPARE(store.mergeUpdates("p1",feed,*first.value,now,
                                        {updateRow("p1","update:late","Late",now)}).code,
                     ErrorCode::InvalidArgument);
            QVERIFY(store.mergeUpdates("p1",feed,*second.value,now,
                                       {updateRow("p1","update:current","Current",now)}).ok());
            const auto cached = store.updatesFor(1,"p1");
            QVERIFY(cached.status.ok() && cached.value);
            QCOMPARE(cached.value->size(),1);
            QCOMPARE(cached.value->first().updateId,QStringLiteral("update:current"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void failedRefreshKeepsStaleCacheAndEmptyFailureIsError() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) VALUES"
                           "('p1','https://example.invalid/feed/alex'),"
                           "('p2','https://example.invalid/feed/morgan')"));
            person_store::SqliteStore store(db);
            const QUrl alexFeed("https://example.invalid/feed/alex");
            const QUrl morganFeed("https://example.invalid/feed/morgan");
            const qint64 now = QDateTime::currentSecsSinceEpoch();

            const auto seed = store.beginUpdatesRefresh("p1",alexFeed);
            QVERIFY(seed.status.ok() && seed.value);
            QVERIFY(store.mergeUpdates("p1",alexFeed,*seed.value,now,
                                       {updateRow("p1","update:old","Old",now)}).ok());
            const auto failed = store.beginUpdatesRefresh("p1",alexFeed);
            QVERIFY(failed.status.ok() && failed.value);
            QVERIFY(store.failUpdatesRefresh("p1",alexFeed,*failed.value).ok());
            const auto stale = store.updatesFor(1,"p1");
            QVERIFY(stale.status.ok() && stale.value);
            QCOMPARE(stale.value->size(),1);
            QVERIFY(stale.value->first().stale);

            const auto emptyFailure = store.beginUpdatesRefresh("p2",morganFeed);
            QVERIFY(emptyFailure.status.ok() && emptyFailure.value);
            QVERIFY(store.failUpdatesRefresh("p2",morganFeed,*emptyFailure.value).ok());
            QCOMPARE(store.updatesFor(2,"p2").status.code,ErrorCode::ServerError);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void invalidRefreshMarksOldCacheStaleWithoutOverwritingIt() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) "
                           "VALUES('p1','https://example.invalid/feed/alex')"));
            person_store::SqliteStore store(db);
            const QUrl feed("https://example.invalid/feed/alex");
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const auto seed = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(seed.status.ok() && seed.value);
            QVERIFY(store.mergeUpdates("p1",feed,*seed.value,now,
                                       {updateRow("p1","update:old","Old",now)}).ok());

            const auto bad = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(bad.status.ok() && bad.value);
            auto invalid = updateRow("p1","update:bad","Bad",now);
            invalid.sourceUrl = QUrl("http://example.invalid/insecure");
            QVERIFY(!store.mergeUpdates("p1",feed,*bad.value,now,{invalid}).ok());
            const auto cached = store.updatesFor(1,"p1");
            QVERIFY(cached.status.ok() && cached.value);
            QCOMPARE(cached.value->size(),1);
            QCOMPARE(cached.value->first().updateId,QStringLiteral("update:old"));
            QVERIFY(cached.value->first().stale);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void corruptCachedTimestampAndUnauthorizedPersonReturnErrors() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) "
                           "VALUES('p1','https://example.invalid/feed/alex')"));
            person_store::SqliteStore store(db);
            const QUrl feed("https://example.invalid/feed/alex");
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const auto refresh = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(refresh.status.ok() && refresh.value);
            QVERIFY(store.mergeUpdates("p1",feed,*refresh.value,now,
                                       {updateRow("p1","update:old","Old",now)}).ok());
            QCOMPARE(store.updatesFor(2,"p1").status.code,ErrorCode::PermissionDenied);
            QVERIFY(q.exec("UPDATE person_updates SET fetched_at_utc='broken'"));
            QCOMPARE(store.updatesFor(1,"p1").status.code,ErrorCode::ServerError);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void successfulEmptyRefreshIsValidAndGenerationPersistsAcrossReopen() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("person-cache.sqlite");
        const QUrl feed("https://example.invalid/feed/alex");
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        const QString firstConnection = QUuid::createUuid().toString();
        qint64 firstGeneration = 0;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", firstConnection);
            db.setDatabaseName(path); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) "
                           "VALUES('p1','https://example.invalid/feed/alex')"));
            person_store::SqliteStore store(db);
            const auto begun = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(begun.status.ok() && begun.value);
            firstGeneration = *begun.value;
            QVERIFY(store.mergeUpdates("p1",feed,firstGeneration,now,{}).ok());
            const auto empty = store.updatesFor(1,"p1");
            QVERIFY(empty.status.ok() && empty.value);
            QVERIFY(empty.value->isEmpty());
            db.close();
        }
        QSqlDatabase::removeDatabase(firstConnection);

        const QString secondConnection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", secondConnection);
            db.setDatabaseName(path); QVERIFY(db.open());
            person_store::SqliteStore store(db);
            const auto next = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(next.status.ok() && next.value);
            QCOMPARE(*next.value,firstGeneration + 1);
            QCOMPARE(store.mergeUpdates("p1",feed,firstGeneration,now + 1,
                                        {updateRow("p1","update:late","Late",now + 1)}).code,
                     ErrorCode::InvalidArgument);
            QVERIFY(store.mergeUpdates("p1",feed,*next.value,now + 1,
                                       {updateRow("p1","update:new","New",now + 1)}).ok());
            const auto current = store.updatesFor(1,"p1");
            QVERIFY(current.status.ok() && current.value);
            QCOMPARE(current.value->size(),1);
            QCOMPARE(current.value->first().updateId,QStringLiteral("update:new"));
            db.close();
        }
        QSqlDatabase::removeDatabase(secondConnection);
    }
    void cacheWriteFailureRollsBackAndMarksOldRowsStale() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:"); QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) "
                           "VALUES('p1','https://example.invalid/feed/alex')"));
            person_store::SqliteStore store(db);
            const QUrl feed("https://example.invalid/feed/alex");
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const auto seed = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(seed.status.ok() && seed.value);
            QVERIFY(store.mergeUpdates("p1",feed,*seed.value,now,
                                       {updateRow("p1","update:old","Old",now)}).ok());
            QVERIFY(q.exec("CREATE TRIGGER reject_person_update BEFORE INSERT ON person_updates "
                           "BEGIN SELECT RAISE(ABORT,'reject'); END"));

            const auto failedWrite = store.beginUpdatesRefresh("p1",feed);
            QVERIFY(failedWrite.status.ok() && failedWrite.value);
            QCOMPARE(store.mergeUpdates("p1",feed,*failedWrite.value,now + 1,
                                        {updateRow("p1","update:new","New",now + 1)}).code,
                     ErrorCode::ServerError);
            const auto cached = store.updatesFor(1,"p1");
            QVERIFY(cached.status.ok() && cached.value);
            QCOMPARE(cached.value->size(),1);
            QCOMPARE(cached.value->first().updateId,QStringLiteral("update:old"));
            QVERIFY(cached.value->first().stale);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
};
QTEST_GUILESS_MAIN(PersonStoreSqliteTests)
#include "person_store_sqlite_tests.moc"
