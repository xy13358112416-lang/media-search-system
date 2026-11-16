#include "care_person.h"
#include <QtTest>
using namespace care_contract;
using namespace care_algorithms;
namespace {
ResourceSummary work(QString id, QString personId = "p1", QString name = "Alex", PersonRole role = PersonRole::Actor) {
    ResourceSummary w; w.resource = {id, {1, id == "w1" ? 1 : 2}};
    w.media.fileName = id; w.people = {{personId, name, {role}, false}}; return w;
}
}
class PersonTests : public QObject {
    Q_OBJECT
private slots:
    void newUsersCanFindStablePersonIdentities() {
        CatalogSnapshot snapshot; snapshot.readableWorks = {work("w1"), work("w2", "p2", "Alex", PersonRole::Musician)};
        auto result = searchPeople({"ＡＬＥＸ", {}}, snapshot);
        QVERIFY(result.status.ok() && result.value); QCOMPARE(result.value->items.size(), 2);
        QCOMPARE(result.value->items[0].personId, QString("p1")); QCOMPARE(result.value->items[1].personId, QString("p2"));
        snapshot.followedPeople.insert("p1"); result = searchPeople({"alex", {0, 1}}, snapshot);
        QVERIFY(result.value->items.first().followed); QCOMPARE(*result.value->nextOffset, qint64(1));
    }
    void samePersonRolesMergeButNamesDoNotMergeIdentities() {
        CatalogSnapshot snapshot; snapshot.readableWorks = {work("w2", "p1", "Alex", PersonRole::Director), work("w1")};
        auto result = searchPeople({"Alex", {}}, snapshot);
        QVERIFY(result.status.ok() && result.value); QCOMPARE(result.value->items.size(), 1);
        QCOMPARE(result.value->items.first().roles.size(), 2);
        auto works = worksForPerson("p1", {}, snapshot);
        QVERIFY(works.status.ok() && works.value); QCOMPARE(works.value->items.size(), 2);
        QCOMPARE(works.value->items.first().resource.workId, QString("w1"));
    }
    void unknownPeopleAndBadQueriesHaveExplicitStates() {
        CatalogSnapshot snapshot; snapshot.readableWorks = {work("w1")};
        auto unknown = worksForPerson("private-person", {}, snapshot);
        QCOMPARE(unknown.status.code, warehouse_contract::ErrorCode::NotFound);
        QVERIFY(!searchPeople({" ", {}}, snapshot).status.ok());
        QVERIFY(!worksForPerson("p1", {-1, 20}, snapshot).status.ok());
        snapshot.dataAvailable = false;
        QCOMPARE(searchPeople({"Alex", {}}, snapshot).status.code, warehouse_contract::ErrorCode::ServerError);
    }
    void suggestionsUseOwnHistoryAndRespectCurrentScope() {
        auto a = work("w1"); auto b = work("w2", "p2", "Other");
        PersonalSnapshot input; input.userKey = "u"; input.nowUtc = 100; input.readableWorks = {a};
        ObservedBehavior event; event.userKey = "u"; event.record.event = {"e1", BehaviorAction::View, a.resource, {}};
        event.record.occurredAtUtc = 10; event.workSnapshot = a;
        input.events = {event, event};
        auto hidden = event; hidden.record.event.eventId = "hidden"; hidden.record.event.resource = b.resource; hidden.workSnapshot = b;
        input.events.append(hidden);
        auto result = suggestPeople(5, input);
        QVERIFY(result.status.ok() && result.value); QCOMPARE(result.value->size(), 1);
        QCOMPARE(result.value->first().personId, QString("p1"));
        input.followedPeople.insert("p1"); QVERIFY(suggestPeople(5, input).value->isEmpty());
        input.followedPeople.clear(); input.settings.personalizationEnabled = false;
        QVERIFY(suggestPeople(5, input).value->isEmpty());
        input.historyAvailable = false; QVERIFY(!suggestPeople(5, input).status.ok());
    }
    void sourceCacheHasTrustedDatesAndStablePagination() {
        CatalogSnapshot snapshot; snapshot.readableWorks = {work("w1")};
        PersonUpdate a; a.updateId = "a"; a.personId = "p1"; a.title = "News";
        a.sourceUrl = QUrl("https://example.org/a"); a.publishedAtUtc = 20; a.fetchedAtUtc = 25;
        auto b = a; b.updateId = "b"; b.kind = UpdateKind::Release; b.eventAtUtc = 80; b.stale = true;
        const PersonUpdatesRequest request{"p1", {10, 90}, {0, 1}};
        auto result = personUpdates(request, snapshot, {a, b}, 50);
        QVERIFY(result.status.ok() && result.value); QCOMPARE(result.value->items.first().updateId, QString("b"));
        QVERIFY(result.value->items.first().stale); QCOMPARE(*result.value->nextOffset, qint64(1));
        b.eventAtUtc.reset(); b.publishedAtUtc.reset();
        result = personUpdates({"p1", {10, 90}, {}}, snapshot, {b, a}, 50);
        QCOMPARE(result.value->items.first().updateId, QString("a"));
        b.sourceUrl = QUrl("http://example.org/insecure");
        QVERIFY(!personUpdates(request, snapshot, {b}, 50).status.ok());
        QVERIFY(!personUpdates(request, snapshot, {a}, 50, false).status.ok());
    }
    void latestCachedVersionWinsBeforeDateFiltering() {
        CatalogSnapshot snapshot; snapshot.readableWorks = {work("w1")};
        PersonUpdate a; a.updateId = "a"; a.personId = "p1"; a.title = "Release"; a.kind = UpdateKind::Release;
        a.sourceUrl = QUrl("https://example.org/a"); a.eventAtUtc = 20; a.fetchedAtUtc = 25;
        auto updated = a; updated.eventAtUtc = 100; updated.fetchedAtUtc = 30;
        const auto result = personUpdates({"p1", {10, 90}, {}}, snapshot, {a, updated}, 50);
        QVERIFY(result.status.ok() && result.value); QVERIFY(result.value->items.isEmpty());
    }
};
QTEST_APPLESS_MAIN(PersonTests)
#include "care_person_tests.moc"
