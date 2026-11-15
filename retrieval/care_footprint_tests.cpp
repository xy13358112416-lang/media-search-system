#include "care_footprint.h"
#include <QDateTime>
#include <QTimeZone>
#include <QtTest>

using namespace care_algorithms;
using namespace care_contract;

namespace {
qint64 utc(const char* value) {
    return QDateTime::fromString(QString::fromLatin1(value), Qt::ISODate).toSecsSinceEpoch();
}
qint64 midnight(const QDate& date) {
    return QDateTime(date, QTime(0, 0), QTimeZone("Asia/Shanghai")).toSecsSinceEpoch();
}
TimeRange range(const char* first, const char* end) { return {utc(first), utc(end)}; }
ResourceSummary work(const QString& id, const QString& category = QStringLiteral("电影")) {
    ResourceSummary value;
    value.resource = {id, {1, 1}};
    value.media.id = id;
    value.media.fileName = id;
    value.media.category = category;
    return value;
}
ObservedBehavior view(const QString& id, qint64 at, const ResourceSummary& resource) {
    ObservedBehavior value;
    value.userKey = QStringLiteral("owner");
    value.record = {{id, BehaviorAction::View, resource.resource, {}}, at, false};
    value.workSnapshot = resource;
    return value;
}
ObservedBehavior search(const QString& id, qint64 at) {
    ObservedBehavior value;
    value.userKey = QStringLiteral("owner");
    value.record = {{id, BehaviorAction::Search, std::nullopt, QStringLiteral("test")}, at, false};
    return value;
}
PersonalSnapshot snapshot() {
    PersonalSnapshot value;
    value.userKey = QStringLiteral("owner");
    value.nowUtc = utc("2024-04-01T00:00:00Z");
    return value;
}
FootprintRequest february() {
    return {range("2024-01-31T16:00:00Z", "2024-02-29T16:00:00Z"), std::nullopt};
}
qint64 categoryCount(const PeriodFootprint& period, const QString& category) {
    for (const auto& bucket : period.categories) if (bucket.label == category) return bucket.count;
    return 0;
}
qint64 personCount(const PeriodFootprint& period, const QString& id, PersonRole role) {
    for (const auto& person : period.people)
        if (person.personId == id && person.role == role) return person.count;
    return 0;
}
} // namespace

class CareFootprintTests : public QObject {
    Q_OBJECT
private slots:
    void countsRawDeduplicatedViewsSearchesAndCredits();
    void halfOpenShanghaiDaysIncludeLeapDay();
    void historicalSnapshotWinsAndDeletedWorkStillCounts();
    void missingSnapshotFallsBackByWorkId();
    void comparisonAndSyntheticFlagUseIndependentWindows();
    void syntheticOutsideBothWindowsDoesNotMarkReport();
    void coverageUsesEarliestAvailableHistory();
    void emptyHistoryIsExplicitSuccessWithZeroDays();
    void settingsDoNotEraseExistingFootprint();
    void foreignAndFutureEventsDoNotCount();
    void invalidInputsAndUnavailableHistoryFail();
    void conflictingDuplicateIsDataError();
    void maximumNaturalDaySpanIsEnforced();
    void partialDaysCountTowardSpanLimit();
    void nearEpochAndMaximumDateWork();
};

void CareFootprintTests::countsRawDeduplicatedViewsSearchesAndCredits() {
    auto data = snapshot();
    auto a = work(QStringLiteral("uci:a"));
    a.people = {{QStringLiteral("p1"), QStringLiteral("同名"), {PersonRole::Actor, PersonRole::Actor, PersonRole::Director}},
                {QStringLiteral("p1"), QStringLiteral("同名"), {PersonRole::Actor}},
                {QStringLiteral("p2"), QStringLiteral("同名"), {PersonRole::Actor}}};
    auto first = view(QStringLiteral("v1"), utc("2024-02-01T12:00:00Z"), a);
    data.events = {first, first,
                   view(QStringLiteral("v2"), utc("2024-02-01T12:00:01Z"), a),
                   search(QStringLiteral("s1"), utc("2024-02-03T12:00:00Z")),
                   search(QStringLiteral("s2"), utc("2024-02-03T12:01:00Z"))};
    const auto result = buildFootprint(february(), data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    const auto& report = *result.value;
    QCOMPARE(report.current.views, 2); QCOMPARE(report.current.searches, 2);
    QCOMPARE(report.current.uniqueWorks, 1); QCOMPARE(report.current.activeDays, 2);
    QCOMPARE(categoryCount(report.current, QStringLiteral("电影")), 2);
    QCOMPARE(report.current.people.size(), 3);
    QCOMPARE(personCount(report.current, QStringLiteral("p1"), PersonRole::Actor), 2);
    QCOMPARE(personCount(report.current, QStringLiteral("p1"), PersonRole::Director), 2);
    QCOMPARE(personCount(report.current, QStringLiteral("p2"), PersonRole::Actor), 2);
    QCOMPARE(report.generatedAtUtc, data.nowUtc);
    QVERIFY(!report.message.isEmpty()); QVERIFY(!report.comparison);
}

void CareFootprintTests::halfOpenShanghaiDaysIncludeLeapDay() {
    auto data = snapshot();
    const auto request = february();
    const auto a = work(QStringLiteral("uci:a"));
    data.events = {view(QStringLiteral("before"), request.range.beginUtc - 1, a),
                   view(QStringLiteral("start"), request.range.beginUtc, a),
                   search(QStringLiteral("utc-day-end"), utc("2024-02-01T15:59:59Z")),
                   search(QStringLiteral("local-day-start"), utc("2024-02-01T16:00:00Z")),
                   view(QStringLiteral("last"), request.range.endUtc - 1, a),
                   view(QStringLiteral("end"), request.range.endUtc, a)};
    const auto result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    const auto& period = result.value->current;
    QCOMPARE(period.views, 2); QCOMPARE(period.searches, 2); QCOMPARE(period.activeDays, 3);
    QCOMPARE(period.days.size(), 29);
    QCOMPARE(period.days.first().localDate, QStringLiteral("2024-02-01"));
    QCOMPARE(period.days.last().localDate, QStringLiteral("2024-02-29"));
    QCOMPARE(period.days[0].views, 1); QCOMPARE(period.days[0].searches, 1);
    QCOMPARE(period.days[1].searches, 1); QCOMPARE(period.days[2].views, 0);
    QCOMPARE(period.days[2].searches, 0); QCOMPARE(period.days[28].views, 1);
}

void CareFootprintTests::historicalSnapshotWinsAndDeletedWorkStillCounts() {
    auto data = snapshot();
    auto historical = work(QStringLiteral("uci:a"), QStringLiteral("旧类别"));
    historical.people = {{QStringLiteral("old"), QStringLiteral("过去人物"), {PersonRole::Director}}};
    auto current = work(QStringLiteral("uci:a"), QStringLiteral("新类别"));
    current.people = {{QStringLiteral("new"), QStringLiteral("当前人物"), {PersonRole::Actor}}};
    auto unknown = view(QStringLiteral("unknown"), utc("2024-02-02T00:00:00Z"), work(QStringLiteral("uci:deleted")));
    unknown.workSnapshot.reset();
    auto missingCategory = work(QStringLiteral("uci:a"), QStringLiteral("  "));
    data.events = {view(QStringLiteral("historical"), utc("2024-02-01T00:00:00Z"), historical), unknown,
                   view(QStringLiteral("empty-category"), utc("2024-02-03T00:00:00Z"), missingCategory)};
    data.readableWorks = {current};
    const auto result = buildFootprint(february(), data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    const auto& period = result.value->current;
    QCOMPARE(period.views, 3); QCOMPARE(period.uniqueWorks, 2);
    QCOMPARE(categoryCount(period, QStringLiteral("旧类别")), 1);
    QCOMPARE(categoryCount(period, QStringLiteral("未知")), 2);
    QCOMPARE(categoryCount(period, QStringLiteral("新类别")), 0);
    QCOMPARE(personCount(period, QStringLiteral("old"), PersonRole::Director), 1);
    QCOMPARE(personCount(period, QStringLiteral("new"), PersonRole::Actor), 0);
}

void CareFootprintTests::missingSnapshotFallsBackByWorkId() {
    auto data = snapshot();
    auto current = work(QStringLiteral("uci:a"), QStringLiteral("当前类别"));
    current.resource.source = {7, 8};
    current.people = {{QStringLiteral("person"), QStringLiteral("人物"), {PersonRole::Musician}}};
    auto event = view(QStringLiteral("view"), utc("2024-02-01T00:00:00Z"), work(QStringLiteral("uci:a")));
    event.workSnapshot.reset();
    data.events = {event}; data.readableWorks = {current};
    const auto result = buildFootprint(february(), data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QCOMPARE(categoryCount(result.value->current, QStringLiteral("当前类别")), 1);
    QCOMPARE(personCount(result.value->current, QStringLiteral("person"), PersonRole::Musician), 1);
}

void CareFootprintTests::comparisonAndSyntheticFlagUseIndependentWindows() {
    auto data = snapshot();
    auto request = february();
    request.comparison = range("2023-12-31T16:00:00Z", "2024-01-31T16:00:00Z");
    const auto a = work(QStringLiteral("uci:a"));
    auto synthetic = view(QStringLiteral("comparison"), request.comparison->beginUtc, a);
    synthetic.record.synthetic = true;
    data.events = {synthetic, search(QStringLiteral("current"), request.range.beginUtc)};
    const auto result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value); QVERIFY(result.value->comparison);
    QCOMPARE(result.value->current.views, 0); QCOMPARE(result.value->current.searches, 1);
    QCOMPARE(result.value->comparison->views, 1); QCOMPARE(result.value->comparison->searches, 0);
    QCOMPARE(result.value->comparison->days.size(), 31);
    QCOMPARE(result.value->current.days.size(), 29);
    QVERIFY(result.value->containsSyntheticData);
    QCOMPARE(result.value->comparison->range.beginUtc, request.comparison->beginUtc);
}

void CareFootprintTests::syntheticOutsideBothWindowsDoesNotMarkReport() {
    auto data = snapshot();
    auto event = search(QStringLiteral("outside"), utc("2023-12-01T00:00:00Z"));
    event.record.synthetic = true;
    data.events = {event};
    const auto result = buildFootprint(february(), data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QVERIFY(!result.value->containsSyntheticData);
    QCOMPARE(result.value->current.coverageStartUtc, std::optional<qint64>(event.record.occurredAtUtc));
}

void CareFootprintTests::coverageUsesEarliestAvailableHistory() {
    auto data = snapshot();
    auto request = february();
    request.comparison = range("2024-01-01T00:00:00Z", "2024-01-02T00:00:00Z");
    const auto earliest = utc("2024-01-15T00:00:00Z");
    data.events = {search(QStringLiteral("later"), utc("2024-02-12T00:00:00Z")),
                   search(QStringLiteral("earliest"), earliest)};
    const auto result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value); QVERIFY(result.value->comparison);
    QCOMPARE(result.value->current.coverageStartUtc, std::optional<qint64>(earliest));
    QCOMPARE(result.value->comparison->coverageStartUtc, std::optional<qint64>(earliest));
}

void CareFootprintTests::emptyHistoryIsExplicitSuccessWithZeroDays() {
    const auto result = buildFootprint(february(), snapshot());
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    const auto& period = result.value->current;
    QCOMPARE(period.views, 0); QCOMPARE(period.searches, 0); QCOMPARE(period.activeDays, 0);
    QCOMPARE(period.uniqueWorks, 0); QVERIFY(!period.coverageStartUtc);
    QVERIFY(period.categories.isEmpty()); QVERIFY(period.people.isEmpty());
    QCOMPARE(period.days.size(), 29);
    for (const auto& day : period.days) { QCOMPARE(day.views, 0); QCOMPARE(day.searches, 0); }
}

void CareFootprintTests::settingsDoNotEraseExistingFootprint() {
    auto data = snapshot();
    data.settings = {false, false, false};
    data.events = {search(QStringLiteral("old"), utc("2024-02-01T00:00:00Z"))};
    const auto result = buildFootprint(february(), data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QCOMPARE(result.value->current.searches, 1);
}

void CareFootprintTests::foreignAndFutureEventsDoNotCount() {
    auto data = snapshot();
    auto foreign = search(QStringLiteral("foreign"), utc("2024-02-01T00:00:00Z"));
    foreign.userKey = QStringLiteral("other"); foreign.record.synthetic = true;
    auto future = search(QStringLiteral("future"), data.nowUtc + 1);
    future.record.synthetic = true;
    data.events = {foreign, future, search(QStringLiteral("own"), data.nowUtc)};
    const FootprintRequest request{range("2024-01-01T00:00:00Z", "2024-05-01T00:00:00Z"), std::nullopt};
    const auto result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QCOMPARE(result.value->current.searches, 1);
    QCOMPARE(result.value->current.coverageStartUtc, std::optional<qint64>(data.nowUtc));
    QVERIFY(!result.value->containsSyntheticData);
}

void CareFootprintTests::invalidInputsAndUnavailableHistoryFail() {
    auto data = snapshot();
    data.historyAvailable = false;
    auto result = buildFootprint(february(), data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    data = snapshot(); data.nowUtc = -1;
    result = buildFootprint(february(), data);
    QVERIFY(!result.status.ok()); QVERIFY(!result.value);
    data = snapshot(); data.events = {search(QStringLiteral("bad"), -1)};
    result = buildFootprint(february(), data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    auto request = february(); request.range.endUtc = request.range.beginUtc;
    result = buildFootprint(request, snapshot());
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    request = february(); request.comparison = request.range;
    result = buildFootprint(request, snapshot());
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
}

void CareFootprintTests::conflictingDuplicateIsDataError() {
    auto data = snapshot();
    data.events = {search(QStringLiteral("duplicate"), utc("2024-02-01T00:00:00Z")),
                   search(QStringLiteral("duplicate"), utc("2024-02-02T00:00:00Z"))};
    const auto result = buildFootprint(february(), data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
}

void CareFootprintTests::maximumNaturalDaySpanIsEnforced() {
    auto data = snapshot();
    const QDate begin(2010, 1, 1);
    FootprintRequest request{{midnight(begin), midnight(begin.addDays(MaxReportDays))}, std::nullopt};
    auto result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QCOMPARE(result.value->current.days.size(), MaxReportDays);
    request.range.endUtc = midnight(begin.addDays(MaxReportDays + 1));
    result = buildFootprint(request, data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    request = february();
    request.comparison = TimeRange{midnight(begin), midnight(begin.addDays(MaxReportDays + 1))};
    result = buildFootprint(request, data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
}

void CareFootprintTests::partialDaysCountTowardSpanLimit() {
    const QDate begin(2010, 1, 1);
    const FootprintRequest request{{midnight(begin) + 86399, midnight(begin.addDays(MaxReportDays)) + 1}, std::nullopt};
    const auto result = buildFootprint(request, snapshot());
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
}

void CareFootprintTests::nearEpochAndMaximumDateWork() {
    auto data = snapshot();
    data.nowUtc = MaxUtcExclusive - 1;
    data.events = {search(QStringLiteral("epoch"), 0), search(QStringLiteral("last"), data.nowUtc)};
    FootprintRequest request{{0, 1}, std::nullopt};
    auto result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QCOMPARE(result.value->current.days.size(), 1);
    QCOMPARE(result.value->current.days.first().localDate, QStringLiteral("1970-01-01"));
    QCOMPARE(result.value->current.searches, 1);
    request.range = {MaxUtcExclusive - 1, MaxUtcExclusive};
    result = buildFootprint(request, data);
    QVERIFY(result.status.ok()); QVERIFY(result.value);
    QCOMPARE(result.value->current.days.size(), 1);
    QCOMPARE(result.value->current.days.first().localDate, QStringLiteral("10000-01-01"));
    QCOMPARE(result.value->current.searches, 1);
}

QTEST_GUILESS_MAIN(CareFootprintTests)
#include "care_footprint_tests.moc"
