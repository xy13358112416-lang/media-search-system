#include "person_credit_import.h"
#include "person_schema.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <limits>

using namespace person_credit_import;
using care_contract::PersonRole;

namespace {
Person person(const QString& id = QStringLiteral("person:one")) {
    return {id, QStringLiteral("Same Name"), QUrl("https://example.org/identity/" + id), {"Alias", "Alias"}};
}
Credit credit(const QString& id = QStringLiteral("person:one"), PersonRole role = PersonRole::Actor) {
    return {{"work:one", {10, 20}}, id, role, QUrl("https://example.org/credits")};
}
Batch batch() { return {false, "Curated fixture", {person()}, {credit()}}; }
QJsonObject jsonBatch() {
    return {{"schema", "care-person-import-v1"}, {"synthetic", false}, {"provenance", "Curated fixture"},
        {"people", QJsonArray{QJsonObject{{"person_id", "person:one"}, {"name", "Same Name"},
            {"identity_source", "https://example.org/identity/person:one"}, {"aliases", QJsonArray{"Alias"}}}}},
        {"credits", QJsonArray{QJsonObject{{"person_id", "person:one"}, {"role", "actor"},
            {"credit_source", "https://example.org/credits"},
            {"resource", QJsonObject{{"work_id", "work:one"}, {"source", QJsonObject{
                {"warehouse_id", "10"}, {"item_id", "20"}}}}}}}}};
}
}

class PersonCreditImportTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString connection;
    QSqlDatabase db;

    qint64 count(const QString& table) {
        QSqlQuery q(db);
        if (!q.exec("SELECT COUNT(*) FROM " + table) || !q.next()) return -1;
        return q.value(0).toLongLong();
    }
private slots:
    void init() {
        QVERIFY(directory.isValid());
        connection = "person-import-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(directory.filePath(connection + ".sqlite"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
        QVERIFY(q.exec("CREATE TABLE server_schema_migrations(version INTEGER PRIMARY KEY NOT NULL)"));
        QVERIFY(q.exec("INSERT INTO server_schema_migrations VALUES(1)"));
        QVERIFY(q.exec("CREATE TABLE network_users(id INTEGER PRIMARY KEY)"));
        QVERIFY(q.exec("CREATE TABLE warehouse_items(id INTEGER PRIMARY KEY,warehouse_id INTEGER NOT NULL)"));
        QVERIFY(q.exec("CREATE TABLE work_sources(warehouse_id INTEGER,item_id INTEGER,work_id TEXT,PRIMARY KEY(warehouse_id,item_id))"));
        QVERIFY(q.exec("INSERT INTO warehouse_items VALUES(20,10),(21,11)"));
        QVERIFY(q.exec("INSERT INTO work_sources VALUES(10,20,'work:one'),(11,21,'work:two')"));
        QString error;
        QVERIFY2(person_schema::migrate(db,error),qPrintable(error));
    }
    void cleanup() {
        db.close(); db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
    }
    void importsSameNameDistinctPeopleAndMultipleRolesIdempotently() {
        auto input = batch(); input.people.append(person("person:two"));
        input.credits.append(credit("person:one", PersonRole::Director));
        input.credits.append(credit("person:two", PersonRole::Musician));
        input.people.append(person()); input.credits.append(credit());
        auto result = importBatch(db,input);
        QVERIFY2(result.status.ok(),qPrintable(result.status.message)); QVERIFY(result.value);
        QCOMPARE(result.value->peopleInserted,2); QCOMPARE(result.value->aliasesInserted,2);
        QCOMPARE(result.value->creditsInserted,3);
        result = importBatch(db,input);
        QVERIFY(result.status.ok()); QVERIFY(result.value);
        QCOMPARE(result.value->peopleInserted,0); QCOMPARE(result.value->aliasesInserted,0);
        QCOMPARE(result.value->creditsInserted,0);
        QCOMPARE(count("person_catalog"),qint64(2)); QCOMPARE(count("person_credits"),qint64(3));
        QCOMPARE(count("person_sources"),qint64(0)); QCOMPARE(count("network_users"),qint64(0));
        QCOMPARE(count("work_sources"),qint64(2));
    }
    void creditsMayReferenceExistingPerson() {
        auto input = batch(); input.credits.clear(); QVERIFY(importBatch(db,input).status.ok());
        input = batch(); input.people.clear(); QVERIFY(importBatch(db,input).status.ok());
        QCOMPARE(count("person_credits"),qint64(1));
    }
    void namesAndAliasesNormalizeWhitespaceWithoutMergingIds() {
        auto input = batch(); input.people[0].name = "  Same   Name  ";
        input.people[0].aliases = {"  Alias  ", "Alias"};
        auto result = importBatch(db,input); QVERIFY(result.status.ok());
        QCOMPARE(result.value->aliasesInserted,1);
        QVERIFY(importBatch(db,batch()).status.ok());
        QSqlQuery q(db); QVERIFY(q.exec("SELECT display_name FROM person_catalog")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QStringLiteral("Same Name"));
    }
    void exactResourceTripleAndPhysicalItemAreRequired() {
        auto input = batch(); input.credits[0].resource.source.warehouseId = 11;
        QVERIFY(!importBatch(db,input).status.ok()); QCOMPARE(count("person_catalog"),qint64(0));
        input = batch(); input.credits[0].resource.workId = "work:two";
        QVERIFY(!importBatch(db,input).status.ok()); QCOMPARE(count("person_catalog"),qint64(0));
        QSqlQuery q(db); QVERIFY(q.exec("DELETE FROM warehouse_items WHERE id=20"));
        QVERIFY(!importBatch(db,batch()).status.ok()); QCOMPARE(count("person_catalog"),qint64(0));
    }
    void conflictingIdentityNameAndEvidenceNeverOverwrite() {
        QVERIFY(importBatch(db,batch()).status.ok());
        auto input = batch(); input.people[0].name = "Changed";
        QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].identitySource = QUrl("https://example.org/different");
        QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.credits[0].creditSource = QUrl("https://example.org/different");
        QVERIFY(!importBatch(db,input).status.ok());
        QVERIFY(importBatch(db,batch()).status.ok()); QCOMPARE(count("person_catalog"),qint64(1));
    }
    void duplicateConflictsRollbackEarlierPeopleAliasesAndCredits() {
        auto input = batch(); input.people.append(person("person:two"));
        input.credits.append(credit("person:two")); input.credits.append(credit());
        input.credits.last().creditSource = QUrl("https://example.org/contradictory");
        QVERIFY(!importBatch(db,input).status.ok());
        QCOMPARE(count("person_catalog"),qint64(0)); QCOMPARE(count("person_aliases"),qint64(0));
        QCOMPARE(count("person_credits"),qint64(0));
        input = batch(); input.people.append(person()); input.people.last().name = "Conflicting";
        QVERIFY(!importBatch(db,input).status.ok()); QCOMPARE(count("person_catalog"),qint64(0));
    }
    void outerTransactionRemainsOwnedByCallerOnSuccessAndFailure() {
        QVERIFY(db.transaction());
        QSqlQuery q(db); QVERIFY(q.exec("INSERT INTO network_users VALUES(7)"));
        QVERIFY(importBatch(db,batch()).status.ok());
        auto input = batch(); input.people = {person("person:two")};
        input.credits = {credit("missing:person")};
        QVERIFY(!importBatch(db,input).status.ok());
        QCOMPARE(count("network_users"),qint64(1)); QCOMPARE(count("person_catalog"),qint64(1));
        QVERIFY(db.rollback()); QCOMPARE(count("network_users"),qint64(0));
        QCOMPARE(count("person_catalog"),qint64(0));
    }
    void databaseWriteFailureRollsBackBatch() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TRIGGER reject_credit BEFORE INSERT ON person_credits BEGIN SELECT RAISE(ABORT,'fixture rejection'); END"));
        QVERIFY(!importBatch(db,batch()).status.ok());
        QCOMPARE(count("person_catalog"),qint64(0)); QCOMPARE(count("person_aliases"),qint64(0));
    }
    void invalidTypedInputsAreRejected() {
        auto input = batch(); input.people[0].personId = "bad id"; QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].name = "  "; QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].name = "Name\nInjected"; QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].name = QString(1001,'n'); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].aliases = {"bad\talias"}; QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].identitySource = QUrl("http://example.org/person"); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].identitySource = QUrl("https://user:secret@example.org/person"); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.credits[0].creditSource = QUrl("https://example.org/" + QString(2048,'x')); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].identitySource = QUrl("http://example.org/person"); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.people[0].identitySource = QUrl("https://user:pass@example.org/person"); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.credits[0].creditSource = QUrl("https://example.org/" + QString(2048,'x')); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.credits[0].role = static_cast<PersonRole>(99); QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.credits[0].resource.source.itemId = 0; QVERIFY(!importBatch(db,input).status.ok());
        input = batch(); input.provenance = " "; QVERIFY(!importBatch(db,input).status.ok());
        QCOMPARE(count("person_catalog"),qint64(0));
    }
    void syntheticIsExplicitOptInAndInvalidEvidenceRequiresSynthetic() {
        auto input = batch(); input.synthetic = true;
        input.people[0].identitySource = QUrl("https://demo.invalid/person");
        input.credits[0].creditSource = QUrl("https://demo.invalid/credit");
        QVERIFY(!importBatch(db,input).status.ok());
        QVERIFY(importBatch(db,input,Options{true}).status.ok());
        input.synthetic = false; QVERIFY(!importBatch(db,input,Options{true}).status.ok());
    }
    void rejectsClosedDatabaseDisabledForeignKeysAndOtherSchemaVersion() {
        db.close(); QVERIFY(!importBatch(db,batch()).status.ok()); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("PRAGMA foreign_keys=OFF"));
        QVERIFY(!importBatch(db,batch()).status.ok()); QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
        QVERIFY(q.exec("INSERT INTO server_schema_migrations VALUES(4)"));
        QVERIFY(!importBatch(db,batch()).status.ok()); QCOMPARE(count("person_catalog"),qint64(0));
    }
    void enforcesDistinctPeopleBoundRatherThanRoleCount() {
        auto input = batch(); input.people.clear(); input.credits.clear();
        for (int i=0;i<256;++i) {
            const auto id = "person:" + QString::number(i); input.people.append(person(id));
            input.credits.append(credit(id)); input.credits.append(credit(id,PersonRole::Director));
        }
        QVERIFY(importBatch(db,input).status.ok()); QCOMPARE(count("person_credits"),qint64(512));
        input = batch(); input.people = {person("person:overflow")}; input.credits = {credit("person:overflow")};
        QVERIFY(!importBatch(db,input).status.ok()); QCOMPARE(count("person_catalog"),qint64(256));
    }
    void parserPreservesSigned64BitDecimalStrings() {
        auto json = jsonBatch(); auto credits = json["credits"].toArray(); auto c = credits[0].toObject();
        auto resource = c["resource"].toObject(); auto source = resource["source"].toObject();
        source["warehouse_id"] = "9007199254740993"; source["item_id"] = "9223372036854775807";
        resource["source"] = source; c["resource"] = resource; credits[0] = c; json["credits"] = credits;
        auto result = parse(QJsonDocument(json).toJson());
        QVERIFY2(result.status.ok(),qPrintable(result.status.message)); QVERIFY(result.value);
        QCOMPARE(result.value->credits[0].resource.source.warehouseId,Q_INT64_C(9007199254740993));
        QCOMPARE(result.value->credits[0].resource.source.itemId,std::numeric_limits<qint64>::max());
        QVERIFY(importBatch(db,*parse(QJsonDocument(jsonBatch()).toJson()).value).status.ok());
    }
    void parserRejectsNonCanonicalOrNumericIds_data() {
        QTest::addColumn<QJsonValue>("id");
        QTest::newRow("number") << QJsonValue(20);
        for (const auto value : {"0", "-1", "+20", " 20", "20 ", "020", "2e1", "20.0", "9223372036854775808", ""})
            QTest::newRow(value) << QJsonValue(value);
    }
    void parserRejectsNonCanonicalOrNumericIds() {
        QFETCH(QJsonValue,id); auto json = jsonBatch(); auto credits = json["credits"].toArray();
        auto c = credits[0].toObject(); auto resource = c["resource"].toObject();
        auto source = resource["source"].toObject(); source["item_id"] = id;
        resource["source"] = source; c["resource"] = resource; credits[0] = c; json["credits"] = credits;
        QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
    }
    void parserValidatesSchemaTypesSizesAndEvidence() {
        QVERIFY(!parse("not-json").status.ok()); QVERIFY(!parse("[]").status.ok());
        QVERIFY(!parse(QByteArray(8*1024*1024+1,' ')).status.ok());
        auto json = jsonBatch(); json["schema"] = "other"; QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
        json = jsonBatch(); json["synthetic"] = "false"; QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
        json = jsonBatch(); json.remove("people"); QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
        json = jsonBatch(); auto people = json["people"].toArray(); auto p = people[0].toObject();
        p["aliases"] = QJsonArray{42}; people[0]=p; json["people"]=people;
        QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
        json = jsonBatch(); people = json["people"].toArray(); p = people[0].toObject();
        p["identity_source"] = "https://example.org/%zz"; people[0]=p; json["people"]=people;
        QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
        json = jsonBatch(); people = json["people"].toArray(); p = people[0].toObject();
        p["identity_source"] = "https://demo.invalid/person"; people[0]=p; json["people"]=people;
        QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok()); json["synthetic"]=true;
        QVERIFY(parse(QJsonDocument(json).toJson()).status.ok());
        json = jsonBatch(); QJsonArray excessive;
        for (int i=0;i<10001;++i) excessive.append(json["people"].toArray()[0]);
        json["people"]=excessive; QVERIFY(!parse(QJsonDocument(json).toJson()).status.ok());
        auto input = batch(); input.people.clear(); for (int i=0;i<10001;++i) input.people.append(person());
        QVERIFY(!importBatch(db,input).status.ok());
    }
};
QTEST_GUILESS_MAIN(PersonCreditImportTests)
#include "person_credit_import_tests.moc"
