#include "care_algorithm_common.h"
#include <QtTest>
using namespace care_algorithms;
using namespace care_contract;
namespace {
ResourceSummary work(QString id = "work:1", qint64 warehouse = 1) {
    ResourceSummary result; result.resource = {id, {warehouse, 1}}; result.media.fileName = "Title"; return result;
}
ObservedBehavior view(QString user = "u1", QString id = "e1", qint64 time = 10) {
    ObservedBehavior result; result.userKey = user; result.record.event.eventId = id;
    result.record.event.resource = work().resource; result.record.occurredAtUtc = time;
    result.workSnapshot = work(); result.publicEligible = true; return result;
}
}
class CommonTests : public QObject {
    Q_OBJECT
private slots:
    void normalizesWithoutRemovingMeaningfulPunctuation() {
        QCOMPARE(detail::normalizeText(QStringLiteral("  ＡＢＣ\t Hello   World! ")), QStringLiteral("abc hello world!"));
        QCOMPARE(detail::normalizeText(QStringLiteral("Straße")), detail::normalizeText("STRASSE"));
        QCOMPARE(detail::normalizeText(QString::fromUtf8("\xC4\xB0")), QString::fromUtf8("i\xCC\x87"));
    }
    void canonicalSourcesAreStable() {
        const auto a = detail::canonicalWorks({work("w1", 2), work("w1", 1)});
        QVERIFY(a.status.ok()); QCOMPARE(a.value->size(), 1); QCOMPARE(a.value->first().resource.source.warehouseId, qint64(1));
        auto invalid = work(); invalid.resource.source.itemId = 0;
        QVERIFY(!detail::canonicalWorks({invalid}).status.ok());
    }
    void deduplicatesCurrentUserAndExcludesForeignOrFuture() {
        PersonalSnapshot input; input.userKey = "u1"; input.nowUtc = 20;
        input.events = {view(), view(), view("foreign"), view("u1", "future", 21)};
        auto result = detail::personalEvents(input);
        QVERIFY(result.status.ok()); QCOMPARE(result.value->size(), 1);
        input.historyAvailable = false; QVERIFY(!detail::personalEvents(input).status.ok());
    }
    void conflictingRetriesAndSnapshotsFail() {
        PersonalSnapshot input; input.userKey = "u1"; input.nowUtc = 20;
        auto conflict = view(); conflict.record.occurredAtUtc = 11;
        input.events = {view(), conflict}; QVERIFY(!detail::personalEvents(input).status.ok());
        input.events = {view()}; input.events[0].workSnapshot->resource.workId = "wrong";
        QVERIFY(!detail::personalEvents(input).status.ok());
    }
    void publicSourcesAreCheckedBeforeCounting() {
        PublicSnapshot input; input.nowUtc = 20; input.publicWorks = {work()};
        auto privateEvent = view("u2", "e2"); privateEvent.publicEligible = false;
        auto hidden = view("u3", "e3"); hidden.record.event.resource->source.warehouseId = 2;
        hidden.workSnapshot.reset();
        input.events = {view(), view(), privateEvent, hidden};
        const auto result = detail::publicEvents(input);
        QVERIFY(result.status.ok()); QCOMPARE(result.value->size(), 1);
        input.dataAvailable = false; QVERIFY(!detail::publicEvents(input).status.ok());
    }
};
QTEST_GUILESS_MAIN(CommonTests)
#include "care_algorithm_common_tests.moc"
