#include "person_schema.h"
#include "person_store_sqlite.h"
#include <QDateTime>
#include <QSqlQuery>
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
};
QTEST_GUILESS_MAIN(PersonStoreSqliteTests)
#include "person_store_sqlite_tests.moc"
