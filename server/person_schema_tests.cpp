#include "person_schema.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class PersonSchemaTests : public QObject {
    Q_OBJECT
private:
    static bool initial(QSqlDatabase& db) {
        QSqlQuery q(db);
        return q.exec("PRAGMA foreign_keys=ON") &&
            q.exec("CREATE TABLE network_users(id INTEGER PRIMARY KEY, username TEXT)") &&
            q.exec("CREATE TABLE behavior_events(user_id INTEGER NOT NULL,event_id TEXT NOT NULL,PRIMARY KEY(user_id,event_id))") &&
            q.exec("CREATE TABLE server_schema_migrations(version INTEGER PRIMARY KEY)") &&
            q.exec("INSERT INTO server_schema_migrations VALUES(1)") &&
            q.exec("INSERT INTO network_users VALUES(1,'existing-user')") &&
            q.exec("INSERT INTO behavior_events VALUES(1,'old-event')");
    }
private slots:
    void preservesExistingDataAndCanRunTwice() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("old.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QString error; QVERIFY(db.transaction()); QVERIFY2(person_schema::migrate(db, error), qPrintable(error)); QVERIFY(db.commit());
            QVERIFY(db.transaction()); QVERIFY2(person_schema::migrate(db, error), qPrintable(error)); QVERIFY(db.commit());
            QSqlQuery q(db); QVERIFY(q.exec("SELECT username FROM network_users WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QStringLiteral("existing-user"));
            QVERIFY(q.exec("SELECT COUNT(*) FROM behavior_events WHERE event_id='old-event'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT COUNT(*) FROM server_schema_migrations WHERE version=2")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT COUNT(*) FROM server_schema_migrations WHERE version=3")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("PRAGMA table_info(person_sources)"));
            bool hasRefreshGeneration = false;
            while (q.next()) if (q.value(1).toString() == "refresh_generation") hasRefreshGeneration = true;
            QVERIFY(hasRefreshGeneration);
            QVERIFY(q.exec("INSERT INTO person_catalog VALUES('person:1','Name','https://example.org/person')"));
            QVERIFY(q.exec("INSERT INTO person_sources(person_id,source_url) VALUES('person:1','https://example.org/feed')"));
            QVERIFY(q.exec("INSERT INTO person_updates(person_id,update_id,feed_url,source_url,title,kind,published_at_utc,event_at_utc,fetched_at_utc) "
                           "VALUES('person:1','update:1','https://example.org/feed','https://example.org/article','Title','release',NULL,200,100)"));
            QVERIFY(!q.exec("INSERT INTO person_updates(person_id,update_id,feed_url,source_url,title,kind,fetched_at_utc) "
                            "VALUES('person:1','update:2','https://unconfigured.example/feed','https://example.org/article','Title','news',100)"));
            // 人物快照由既有 behavior_events.media_snapshot 唯一保存，不另建第二套。
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE name='person_history_snapshots'"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void upgradesVersionTwoWithoutLosingSourceState() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("v2.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE person_catalog(person_id TEXT PRIMARY KEY NOT NULL,display_name TEXT NOT NULL,identity_source TEXT NOT NULL)"));
            QVERIFY(q.exec("CREATE TABLE person_aliases(person_id TEXT NOT NULL REFERENCES person_catalog(person_id),alias TEXT NOT NULL,PRIMARY KEY(person_id,alias))"));
            QVERIFY(q.exec("CREATE TABLE person_credits(person_id TEXT NOT NULL REFERENCES person_catalog(person_id),work_id TEXT NOT NULL,role TEXT NOT NULL CHECK(role IN ('actor','director','musician')),credit_source TEXT NOT NULL,PRIMARY KEY(person_id,work_id,role))"));
            QVERIFY(q.exec("CREATE INDEX person_credits_work ON person_credits(work_id,person_id)"));
            QVERIFY(q.exec("CREATE TABLE person_follows(user_id INTEGER NOT NULL REFERENCES network_users(id),person_id TEXT NOT NULL REFERENCES person_catalog(person_id),known_name TEXT NOT NULL,known_roles_json BLOB NOT NULL,PRIMARY KEY(user_id,person_id))"));
            QVERIFY(q.exec("CREATE TABLE person_sources(person_id TEXT NOT NULL REFERENCES person_catalog(person_id),source_url TEXT NOT NULL,last_success_utc INTEGER,stale INTEGER NOT NULL DEFAULT 0 CHECK(stale IN (0,1)),PRIMARY KEY(person_id,source_url))"));
            QVERIFY(q.exec("CREATE TABLE person_updates(person_id TEXT NOT NULL,update_id TEXT NOT NULL,feed_url TEXT NOT NULL,source_url TEXT NOT NULL,title TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('news','release')),published_at_utc INTEGER,event_at_utc INTEGER,fetched_at_utc INTEGER NOT NULL,stale INTEGER NOT NULL DEFAULT 0 CHECK(stale IN (0,1)),PRIMARY KEY(person_id,update_id,feed_url),FOREIGN KEY(person_id,feed_url) REFERENCES person_sources(person_id,source_url))"));
            QVERIFY(q.exec("INSERT INTO person_catalog VALUES('person:1','Name','https://example.org/person')"));
            QVERIFY(q.exec("INSERT INTO person_sources VALUES('person:1','https://example.org/feed',200,0)"));
            QVERIFY(q.exec("INSERT INTO server_schema_migrations VALUES(2)"));
            QString error; QVERIFY(db.transaction());
            QVERIFY2(person_schema::migrate(db,error),qPrintable(error));
            QVERIFY(db.commit());
            QVERIFY(q.exec("SELECT last_success_utc,stale,refresh_generation FROM person_sources WHERE person_id='person:1'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(),qint64(200));
            QCOMPARE(q.value(1).toInt(),0);
            QCOMPARE(q.value(2).toInt(),0);
            QVERIFY(q.exec("SELECT COUNT(*) FROM server_schema_migrations WHERE version=3"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1);
            q.finish();
            QVERIFY(db.transaction());
            QVERIFY2(person_schema::migrate(db,error),qPrintable(error));
            QVERIFY(db.commit());
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void collisionRollsBackEntireMigration() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("collision.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QSqlQuery q(db); QVERIFY(q.exec("CREATE VIEW person_catalog AS SELECT 'keep' AS original"));
            QString error; QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(!error.isEmpty()); QVERIFY(db.rollback());
            QVERIFY(q.exec("SELECT original FROM person_catalog")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QStringLiteral("keep"));
            QVERIFY(q.exec("SELECT COUNT(*) FROM server_schema_migrations WHERE version>=2")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='person_follows'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void rejectsUnknownNewerSchema() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("newer.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QSqlQuery q(db); QVERIFY(q.exec("INSERT INTO server_schema_migrations VALUES(99)"));
            QString error; QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback());
            QVERIFY(q.exec("SELECT COUNT(*) FROM server_schema_migrations WHERE version=99")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void rejectsOverflowAndDisabledForeignKeys() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("overflow.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QSqlQuery q(db); QVERIFY(q.exec("INSERT INTO server_schema_migrations VALUES(4294967297)"));
            QString error; QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback());
            QVERIFY(q.exec("DELETE FROM server_schema_migrations WHERE version=4294967297"));
            QVERIFY(q.exec("PRAGMA foreign_keys=OFF")); QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback());
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void rejectsVersionTwoWithMissingConstraints() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("broken.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QString error; QVERIFY(db.transaction()); QVERIFY(person_schema::migrate(db, error)); QVERIFY(db.commit());
            QSqlQuery q(db); QVERIFY(q.exec("DROP TABLE person_follows"));
            QVERIFY(q.exec("CREATE TABLE person_follows(user_id INTEGER,person_id TEXT,known_name TEXT,known_roles_json BLOB)"));
            QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void rejectsAccidentallyNonNullableDate() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("date.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QString error; QVERIFY(db.transaction()); QVERIFY(person_schema::migrate(db, error)); QVERIFY(db.commit());
            QSqlQuery q(db); QVERIFY(q.exec("DROP TABLE person_updates"));
            QVERIFY(q.exec("CREATE TABLE person_updates(person_id TEXT NOT NULL,update_id TEXT NOT NULL,feed_url TEXT NOT NULL,source_url TEXT NOT NULL,title TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('news','release')),published_at_utc INTEGER NOT NULL,event_at_utc INTEGER,fetched_at_utc INTEGER NOT NULL,stale INTEGER NOT NULL DEFAULT 0 CHECK(stale IN (0,1)),PRIMARY KEY(person_id,update_id,feed_url),FOREIGN KEY(person_id,feed_url) REFERENCES person_sources(person_id,source_url))"));
            QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void rejectsUnexpectedRequiredColumn() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("extra.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QString error; QVERIFY(db.transaction()); QVERIFY(person_schema::migrate(db, error)); QVERIFY(db.commit());
            QSqlQuery q(db); QVERIFY(q.exec("ALTER TABLE person_catalog ADD COLUMN must_fill TEXT NOT NULL"));
            QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void acceptsSqliteEquivalentRenamedSchema() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("renamed.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QString error; QVERIFY(db.transaction()); QVERIFY(person_schema::migrate(db, error)); QVERIFY(db.commit());
            QSqlQuery q(db); QVERIFY(q.exec("ALTER TABLE person_catalog RENAME TO person_catalog_temp"));
            QVERIFY(q.exec("ALTER TABLE person_catalog_temp RENAME TO person_catalog"));
            QVERIFY(db.transaction()); QVERIFY2(person_schema::migrate(db, error), qPrintable(error)); QVERIFY(db.commit()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void failureAfterCreatingTablesRollsBack() {
        QTemporaryDir dir; const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(dir.filePath("index-conflict.sqlite")); QVERIFY(db.open()); QVERIFY(initial(db));
            QSqlQuery q(db); QVERIFY(q.exec("CREATE INDEX person_credits_work ON behavior_events(event_id)"));
            QString error; QVERIFY(db.transaction()); QVERIFY(!person_schema::migrate(db, error)); QVERIFY(db.rollback());
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name LIKE 'person_%'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='person_credits_work'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT COUNT(*) FROM behavior_events WHERE event_id='old-event'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
};
QTEST_GUILESS_MAIN(PersonSchemaTests)
#include "person_schema_tests.moc"
