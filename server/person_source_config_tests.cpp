#include "person_schema.h"
#include "person_source_config.h"
#include "person_source_config_tests.h"
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {
QByteArray validConfig(const QByteArray& firstFeed = "https://feeds.example.org/one")
{
    QByteArray json = R"({
  "schema": "care-person-source-config-v1",
  "provenance": "Operator-reviewed local feed configuration for this test.",
  "sources": [
    {"person_id": "person:one", "feed_url": "__FIRST_FEED__"},
    {"person_id": "person:one", "feed_url": "__FIRST_FEED__"},
    {"person_id": "person:one", "feed_url": "https://feeds.example.org/one-season"},
    {"person_id": "person:two", "feed_url": "https://feeds.example.org/two"}
  ]
})";
    return json.replace("__FIRST_FEED__", firstFeed);
}
QByteArray unknownPersonConfig()
{
    return R"({
  "schema": "care-person-source-config-v1",
  "provenance": "Operator-reviewed local feed configuration for this test.",
  "sources": [
    {"person_id": "person:one", "feed_url": "https://feeds.example.org/valid"},
    {"person_id": "person:missing", "feed_url": "https://feeds.example.org/missing"}
  ]
})";
}
QByteArray insecureConfig()
{
    return R"({
  "schema": "care-person-source-config-v1",
  "provenance": "reviewed",
  "sources": [{"person_id": "person:one", "feed_url": "http://feeds.example.org/one"}]
})";
}
QByteArray credentialConfig()
{
    return R"({
  "schema": "care-person-source-config-v1",
  "provenance": "reviewed",
  "sources": [{"person_id": "person:one", "feed_url": "https://user:pass@feeds.example.org/one"}]
})";
}
QByteArray wrongSchemaConfig()
{
    return R"({
  "schema": "wrong",
  "provenance": "reviewed",
  "sources": []
})";
}
QByteArray additiveConfig()
{
    return R"({
  "schema": "care-person-source-config-v1",
  "provenance": "reviewed",
  "sources": [
    {"person_id": "person:one", "feed_url": "https://feeds.example.org/existing"},
    {"person_id": "person:two", "feed_url": "https://feeds.example.org/new"}
  ]
})";
}
}

bool PersonSourceConfigTests::fixture(QSqlDatabase& db) {
        QSqlQuery q(db);
        if (!q.exec("PRAGMA foreign_keys=ON") ||
            !q.exec("CREATE TABLE network_users(id INTEGER PRIMARY KEY)") ||
            !q.exec("CREATE TABLE warehouses(id INTEGER PRIMARY KEY,owner_id INTEGER NOT NULL,is_public INTEGER NOT NULL)") ||
            !q.exec("CREATE TABLE warehouse_items(id INTEGER PRIMARY KEY,warehouse_id INTEGER NOT NULL)") ||
            !q.exec("CREATE TABLE work_sources(warehouse_id INTEGER,item_id INTEGER,work_id TEXT UNIQUE)") ||
            !q.exec("CREATE TABLE server_schema_migrations(version INTEGER PRIMARY KEY)") ||
            !q.exec("INSERT INTO server_schema_migrations VALUES(1)") ||
            !q.exec("INSERT INTO network_users VALUES(1)") ||
            !q.exec("CREATE TABLE behavior_events(user_id INTEGER,event_id TEXT)") ||
            !db.transaction()) return false;
        QString error;
        if (!person_schema::migrate(db,error) || !db.commit()) return false;
        return q.exec("INSERT INTO person_catalog VALUES"
                      "('person:one','One','https://example.org/one'),"
                      "('person:two','Two','https://example.org/two')");
}

void PersonSourceConfigTests::feedFragmentsAreRejectedWithoutPartialRegistration()
{
    const QString name = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        QVERIFY(fixture(db));
        const auto clean = person_source_config::parse(validConfig("https://feeds.example.org/a%23b"));
        QVERIFY(clean.status.ok() && clean.value);
        QVERIFY(person_source_config::registerBatch(db, *clean.value).status.ok());

        for (const auto& invalid : {QString("https://feeds.example.org/a#"),
                                    QString("https://feeds.example.org/a#section")}) {
            QVERIFY(!person_source_config::parse(validConfig(invalid.toUtf8())).status.ok());
            const person_source_config::Batch batch{"Reviewed test fixture", {
                {"person:one", QUrl("https://feeds.example.org/valid-new")},
                {"person:two", QUrl(invalid)}
            }};
            QVERIFY(!person_source_config::registerBatch(db, batch).status.ok());
        }
        QSqlQuery rows(db);
        QVERIFY(rows.exec("SELECT source_url FROM person_sources ORDER BY source_url"));
        QStringList saved;
        while (rows.next()) saved.append(rows.value(0).toString());
        QCOMPARE(saved.size(), 3);
        QVERIFY(saved.contains("https://feeds.example.org/a%23b"));
        QVERIFY(!saved.contains("https://feeds.example.org/valid-new"));
    }
    QSqlDatabase::removeDatabase(name);
}

void PersonSourceConfigTests::registrationIsIdempotentAndDoesNotFakeRefreshSuccess() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:");
            QVERIFY(db.open()); QVERIFY(fixture(db));
            const auto batch = person_source_config::parse(validConfig());
            QVERIFY(batch.status.ok() && batch.value);
            QCOMPARE(batch.value->entries.size(),4);

            const auto first = person_source_config::registerBatch(db,*batch.value);
            QVERIFY(first.status.ok() && first.value);
            QCOMPARE(first.value->inserted,3);
            QCOMPARE(first.value->unchanged,1);
            const auto second = person_source_config::registerBatch(db,*batch.value);
            QVERIFY(second.status.ok() && second.value);
            QCOMPARE(second.value->inserted,0);
            QCOMPARE(second.value->unchanged,4);

            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT source_url,last_success_utc,stale,refresh_generation "
                           "FROM person_sources ORDER BY person_id,source_url"));
            int rows = 0;
            while (q.next()) {
                ++rows;
                QVERIFY(q.value(1).isNull());
                QCOMPARE(q.value(2).toInt(),0);
                QCOMPARE(q.value(3).toInt(),0);
            }
            QCOMPARE(rows,3);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
}

void PersonSourceConfigTests::invalidEntryRollsBackWholeRegistration() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:");
            QVERIFY(db.open()); QVERIFY(fixture(db));
            const auto batch = person_source_config::parse(unknownPersonConfig());
            QVERIFY(batch.status.ok() && batch.value);
            QCOMPARE(person_source_config::registerBatch(db,*batch.value).status.code,
                     warehouse_contract::ErrorCode::NotFound);
            QSqlQuery q(db); QVERIFY(q.exec("SELECT COUNT(*) FROM person_sources")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(),0);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
}

void PersonSourceConfigTests::rejectsInsecureOrMalformedConfiguration() {
        const auto insecure = person_source_config::parse(insecureConfig());
        QCOMPARE(insecure.status.code,warehouse_contract::ErrorCode::InvalidArgument);
        const auto credentials = person_source_config::parse(credentialConfig());
        QCOMPARE(credentials.status.code,warehouse_contract::ErrorCode::InvalidArgument);
        const auto wrongSchema = person_source_config::parse(wrongSchemaConfig());
        QCOMPARE(wrongSchema.status.code,warehouse_contract::ErrorCode::InvalidArgument);
}

void PersonSourceConfigTests::readsPagedSourcesWithoutChangingRefreshState() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:");
            QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources VALUES"
                           "('person:one','https://feeds.example.org/a',NULL,0,0),"
                           "('person:one','https://feeds.example.org/b',100,1,7),"
                           "('person:two','https://feeds.example.org/c',200,0,3)"));
            const auto first = person_source_config::listSources(db,{0,1});
            QVERIFY(first.status.ok() && first.value);
            QCOMPARE(first.value->items.size(),1);
            QCOMPARE(first.value->items.first().personId,QStringLiteral("person:one"));
            QCOMPARE(first.value->items.first().feedUrl,QUrl("https://feeds.example.org/a"));
            QVERIFY(!first.value->items.first().lastSuccessUtc);
            QVERIFY(first.value->nextOffset);

            const auto second = person_source_config::listSources(db,{*first.value->nextOffset,2});
            QVERIFY(second.status.ok() && second.value);
            QCOMPARE(second.value->items.size(),2);
            QCOMPARE(second.value->items.first().lastSuccessUtc.value_or(0),qint64(100));
            QVERIFY(second.value->items.first().stale);
            QCOMPARE(second.value->items.first().refreshGeneration,qint64(7));
            QVERIFY(!second.value->nextOffset);

            const auto person = person_source_config::listSourcesForPerson(db,"person:one",{0,20});
            QVERIFY(person.status.ok() && person.value);
            QCOMPARE(person.value->items.size(),2);
            QCOMPARE(person_source_config::listSourcesForPerson(db,"", {0,20}).status.code,
                     warehouse_contract::ErrorCode::InvalidArgument);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
}

void PersonSourceConfigTests::additiveRegistrationPreservesCachedSourceState() {
        const QString name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(":memory:");
            QVERIFY(db.open()); QVERIFY(fixture(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO person_sources VALUES"
                           "('person:one','https://feeds.example.org/existing',123,1,9)"));
            const auto batch = person_source_config::parse(additiveConfig());
            QVERIFY(batch.status.ok() && batch.value);
            const auto result = person_source_config::registerBatch(db,*batch.value);
            QVERIFY(result.status.ok() && result.value);
            QCOMPARE(result.value->inserted,1);
            QCOMPARE(result.value->unchanged,1);
            QVERIFY(q.exec("SELECT source_url,last_success_utc,stale,refresh_generation "
                           "FROM person_sources ORDER BY source_url"));
            QVERIFY(q.next());
            QCOMPARE(q.value(1).toLongLong(),qint64(123));
            QCOMPARE(q.value(2).toInt(),1);
            QCOMPARE(q.value(3).toInt(),9);
            QVERIFY(q.next());
            QVERIFY(q.value(1).isNull());
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
}
QTEST_GUILESS_MAIN(PersonSourceConfigTests)
