#include "care_trending.h"
#include <QDateTime>
#include <QTimeZone>
#include <QtTest>
#include <algorithm>

using namespace care_algorithms;
using namespace care_contract;

namespace {
qint64 utc(const char* text)
{
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate).toSecsSinceEpoch();
}
ResourceSummary work(const QString& id, qint64 warehouse = 1, qint64 item = 1)
{
    ResourceSummary value;
    value.resource = {id, {warehouse, item}};
    value.media.fileName = id;
    return value;
}
ObservedBehavior view(const QString& user, const QString& eventId, qint64 at,
                      const ResourceSummary& resource, bool synthetic = false)
{
    ObservedBehavior value;
    value.userKey = user;
    value.record.event.eventId = eventId;
    value.record.event.action = BehaviorAction::View;
    value.record.event.resource = resource.resource;
    value.record.occurredAtUtc = at;
    value.record.synthetic = synthetic;
    value.publicEligible = true;
    return value;
}
ObservedBehavior search(const QString& user, const QString& eventId, qint64 at,
                        const QString& query, bool synthetic = false)
{
    auto value = view(user, eventId, at, work(QStringLiteral("unused")), synthetic);
    value.record.event.action = BehaviorAction::Search;
    value.record.event.resource.reset();
    value.record.event.query = query;
    return value;
}
PublicSnapshot snapshot()
{
    PublicSnapshot value;
    value.nowUtc = utc("2026-11-15T12:00:00Z");
    return value;
}
TrendingRequest request(TrendingKind kind = TrendingKind::Works)
{
    return {kind, {utc("2026-11-13T00:00:00Z"), utc("2026-11-16T00:00:00Z")}, 10};
}
void usersView(PublicSnapshot& data, const ResourceSummary& resource, qint64 at,
               const QString& prefix = QStringLiteral("e"))
{
    for (int i = 1; i <= 3; ++i)
        data.events.append(view(QString::number(i), prefix + QString::number(i), at, resource));
}
void usersSearch(PublicSnapshot& data, const QString& query, qint64 at,
                 const QString& prefix = QStringLiteral("s"))
{
    for (int i = 1; i <= 3; ++i)
        data.events.append(search(QString::number(i), prefix + QString::number(i), at, query));
}
}

class CareTrendingTests : public QObject {
    Q_OBJECT
private slots:
    void requiresThreeDistinctUsers();
    void halfOpenWindowAndFutureCutoff();
    void capsPerBeijingDayAndDeduplicatesRetries();
    void combinesCurrentPublicSourcesAndSelectsStableSource();
    void excludesPrivateAndOptedOutContributions();
    void normalizesSearchTermsWithoutRemovingPunctuation();
    void rejectsEmptySearchData();
    void ordersByUsersThenEventsThenKeyAndAppliesLimit();
    void syntheticFlagOnlyUsesReturnedEffectiveContributions();
    void summaryDoesNotExposeSuppressedCounts();
    void rejectsInvalidRequestsAndUnavailableData();
    void reportsCorruptData();
    void historicalShanghaiDaysRespectTimeZoneTransitions();
};

void CareTrendingTests::historicalShanghaiDaysRespectTimeZoneTransitions() {
    auto data = snapshot(); data.nowUtc = utc("1988-07-18T00:00:00Z");
    const auto resource = work("w1"); data.publicWorks = {resource};
    const auto before = utc("1988-07-16T14:30:00Z");
    const auto after = utc("1988-07-16T15:30:00Z");
    const QTimeZone zone("Asia/Shanghai");
    if (QDateTime::fromSecsSinceEpoch(before, zone).date() == QDateTime::fromSecsSinceEpoch(after, zone).date())
        QSKIP("Installed Windows timezone data does not expose Shanghai historical daylight-saving transitions.");
    data.events = {view("u1", "e1", before, resource), view("u1", "e2", after, resource),
                   view("u2", "e3", before, resource), view("u3", "e4", before, resource)};
    const auto result = buildTrending({TrendingKind::Works, {utc("1988-07-16T00:00:00Z"), data.nowUtc}, 10}, data);
    QVERIFY(result.status.ok() && result.value); QCOMPARE(result.value->items.size(), 1);
    QCOMPARE(result.value->items.first().effectiveEvents, qint64(4));
}

void CareTrendingTests::requiresThreeDistinctUsers()
{
    auto data = snapshot();
    const auto item = work(QStringLiteral("a"));
    data.publicWorks = {item};
    for (int i = 0; i < 100; ++i)
        data.events.append(view(QStringLiteral("1"), QString::number(i), data.nowUtc, item));
    auto result = buildTrending(request(), data);
    QVERIFY(result.status.ok());
    QVERIFY(result.value);
    QVERIFY(result.value->items.isEmpty());
    data.events.append(view(QStringLiteral("2"), QStringLiteral("two"), data.nowUtc, item));
    QVERIFY(buildTrending(request(), data).value->items.isEmpty());
    data.events.append(view(QStringLiteral("3"), QStringLiteral("three"), data.nowUtc, item));
    result = buildTrending(request(), data);
    QCOMPARE(result.value->items.size(), 1);
    QCOMPARE(result.value->items[0].distinctUsers, 3);
    QCOMPARE(result.value->items[0].effectiveEvents, 3);
    QCOMPARE(result.value->generatedAtUtc, data.nowUtc);
    QVERIFY(!result.value->summaryFromModel);
}

void CareTrendingTests::halfOpenWindowAndFutureCutoff()
{
    auto data = snapshot();
    auto req = request();
    const auto item = work(QStringLiteral("a"));
    data.publicWorks = {item};
    usersView(data, item, req.range.beginUtc - 1, QStringLiteral("before"));
    usersView(data, item, req.range.beginUtc, QStringLiteral("begin"));
    usersView(data, item, req.range.endUtc, QStringLiteral("end"));
    usersView(data, item, data.nowUtc + 1, QStringLiteral("future"));
    usersView(data, item, data.nowUtc, QStringLiteral("now"));
    auto result = buildTrending(req, data);
    QVERIFY(result.status.ok());
    QCOMPARE(result.value->items.size(), 1);
    QCOMPARE(result.value->items[0].effectiveEvents, 6);
    req.range.endUtc = data.nowUtc;
    result = buildTrending(req, data);
    QCOMPARE(result.value->items[0].effectiveEvents, 3);
    req.range.beginUtc = data.nowUtc + 1;
    req.range.endUtc = data.nowUtc + 100;
    QVERIFY(buildTrending(req, data).value->items.isEmpty());
}

void CareTrendingTests::capsPerBeijingDayAndDeduplicatesRetries()
{
    auto data = snapshot();
    const auto item = work(QStringLiteral("a"));
    data.publicWorks = {item};
    usersView(data, item, utc("2026-11-13T15:59:59Z"), QStringLiteral("day1"));
    usersView(data, item, utc("2026-11-13T16:00:00Z"), QStringLiteral("day2"));
    usersView(data, item, utc("2026-11-14T00:00:00Z"), QStringLiteral("sameDay"));
    data.events.append(data.events[0]);
    auto result = buildTrending(request(), data);
    QVERIFY(result.status.ok());
    QCOMPARE(result.value->items[0].effectiveEvents, 6);
    QCOMPARE(result.value->items[0].distinctUsers, 3);
}

void CareTrendingTests::combinesCurrentPublicSourcesAndSelectsStableSource()
{
    auto data = snapshot();
    const auto high = work(QStringLiteral("a"), 9, 1);
    const auto low = work(QStringLiteral("a"), 2, 3);
    const auto sameWarehouse = work(QStringLiteral("a"), 2, 4);
    data.publicWorks = {high, sameWarehouse, low};
    data.events = {view(QStringLiteral("1"), QStringLiteral("e1"), data.nowUtc, high),
                   view(QStringLiteral("2"), QStringLiteral("e2"), data.nowUtc, low),
                   view(QStringLiteral("3"), QStringLiteral("e3"), data.nowUtc, sameWarehouse)};
    auto result = buildTrending(request(), data);
    QVERIFY(result.status.ok());
    QCOMPARE(result.value->items.size(), 1);
    QCOMPARE(result.value->items[0].work->resource.source.warehouseId, 2);
    QCOMPARE(result.value->items[0].work->resource.source.itemId, 3);
    QCOMPARE(result.value->items[0].effectiveEvents, 3);
    data.events.append(view(QStringLiteral("1"), QStringLiteral("again"), data.nowUtc, low));
    QCOMPARE(buildTrending(request(), data).value->items[0].effectiveEvents, 3);
}

void CareTrendingTests::excludesPrivateAndOptedOutContributions()
{
    auto data = snapshot();
    const auto publicSource = work(QStringLiteral("a"));
    const auto privateSource = work(QStringLiteral("a"), 2, 1);
    data.publicWorks = {publicSource};
    usersView(data, privateSource, data.nowUtc, QStringLiteral("private"));
    QVERIFY(buildTrending(request(), data).value->items.isEmpty());
    usersView(data, publicSource, data.nowUtc, QStringLiteral("public"));
    data.events.last().publicEligible = false;
    QVERIFY(buildTrending(request(), data).value->items.isEmpty());
    data.events.last().publicEligible = true;
    QCOMPARE(buildTrending(request(), data).value->items.size(), 1);
    data.publicWorks.clear();
    QVERIFY(buildTrending(request(), data).value->items.isEmpty());
    usersSearch(data, QStringLiteral("public search"), data.nowUtc);
    auto req = request(TrendingKind::SearchTerms);
    QCOMPARE(buildTrending(req, data).value->items.size(), 1);
    data.events.last().publicEligible = false;
    QVERIFY(buildTrending(req, data).value->items.isEmpty());
}

void CareTrendingTests::normalizesSearchTermsWithoutRemovingPunctuation()
{
    auto data = snapshot();
    data.events = {search(QStringLiteral("1"), QStringLiteral("e1"), data.nowUtc, QString::fromUtf8("  ＡＢＣ　Straße\t ＦＯＯ！ ")),
                   search(QStringLiteral("2"), QStringLiteral("e2"), data.nowUtc, QStringLiteral("abc STRASSE foo!")),
                   search(QStringLiteral("3"), QStringLiteral("e3"), data.nowUtc, QStringLiteral("ABC strasse  FOO!"))};
    usersSearch(data, QStringLiteral("abc strasse foo"), data.nowUtc, QStringLiteral("plain"));
    auto result = buildTrending(request(TrendingKind::SearchTerms), data);
    QVERIFY(result.status.ok());
    QCOMPARE(result.value->items.size(), 2);
    QCOMPARE(result.value->items[0].term, QStringLiteral("abc strasse foo"));
    QCOMPARE(result.value->items[1].term, QStringLiteral("abc strasse foo!"));
    QVERIFY(!result.value->items[0].work);
    QCOMPARE(result.value->items[1].distinctUsers, 3);
    data.events.append(search(QStringLiteral("1"), QStringLiteral("variant"), data.nowUtc,
                              QStringLiteral("ABC strasse FOO!")));
    QCOMPARE(buildTrending(request(TrendingKind::SearchTerms), data).value->items[1].effectiveEvents, 3);
}

void CareTrendingTests::rejectsEmptySearchData()
{
    auto data = snapshot();
    usersSearch(data, QString::fromUtf8("　 \t"), data.nowUtc);
    auto result = buildTrending(request(TrendingKind::SearchTerms), data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!result.value);
}

void CareTrendingTests::ordersByUsersThenEventsThenKeyAndAppliesLimit()
{
    auto data = snapshot();
    for (const auto& id : {QStringLiteral("z"), QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")}) {
        const auto item = work(id, 1, data.publicWorks.size() + 1);
        data.publicWorks.append(item);
        usersView(data, item, data.nowUtc, id);
    }
    data.events.append(view(QStringLiteral("4"), QStringLiteral("z4"), data.nowUtc, data.publicWorks[0]));
    usersView(data, data.publicWorks[1], utc("2026-11-13T00:00:00Z"), QStringLiteral("cOld"));
    auto req = request();
    auto result = buildTrending(req, data);
    QVERIFY(result.status.ok());
    QCOMPARE(result.value->items.size(), 4);
    QCOMPARE(result.value->items[0].work->resource.workId, QStringLiteral("z"));
    QCOMPARE(result.value->items[1].work->resource.workId, QStringLiteral("c"));
    QCOMPARE(result.value->items[2].work->resource.workId, QStringLiteral("a"));
    QCOMPARE(result.value->items[3].work->resource.workId, QStringLiteral("b"));
    req.limit = 2;
    result = buildTrending(req, data);
    QCOMPARE(result.value->items.size(), 2);
    QCOMPARE(result.value->summary, QStringLiteral("当前窗口公开热榜共2项，合计10次有效行为；首项有4位贡献用户、4次有效行为。"));
    std::reverse(data.events.begin(), data.events.end());
    auto reversed = buildTrending(req, data);
    QCOMPARE(reversed.value->summary, result.value->summary);
    QCOMPARE(reversed.value->items[0].work->resource.workId, QStringLiteral("z"));
}

void CareTrendingTests::syntheticFlagOnlyUsesReturnedEffectiveContributions()
{
    auto data = snapshot();
    const auto a = work(QStringLiteral("a"));
    const auto b = work(QStringLiteral("b"), 2, 1);
    data.publicWorks = {a, b};
    usersView(data, a, data.nowUtc);
    data.events.append(view(QStringLiteral("1"), QStringLiteral("suppressed"), data.nowUtc, b, true));
    auto req = request();
    QVERIFY(!buildTrending(req, data).value->containsSyntheticData);
    usersView(data, b, data.nowUtc, QStringLiteral("b"));
    req.limit = 1;
    QVERIFY(!buildTrending(req, data).value->containsSyntheticData);
    req.limit = 2;
    QVERIFY(buildTrending(req, data).value->containsSyntheticData);
    std::reverse(data.events.begin(), data.events.end());
    QVERIFY(buildTrending(req, data).value->containsSyntheticData);
    std::reverse(data.events.begin(), data.events.end());
    data.events[0].record.synthetic = true;
    req.limit = 1;
    QVERIFY(buildTrending(req, data).value->containsSyntheticData);
    data.events.clear();
    data.events.append(view(QStringLiteral("1"), QStringLiteral("private"), data.nowUtc, a, true));
    QVERIFY(!buildTrending(req, data).value->containsSyntheticData);
    usersView(data, a, data.nowUtc);
    data.events[0].record.occurredAtUtc = data.nowUtc + 1;
    QVERIFY(!buildTrending(req, data).value->containsSyntheticData);
    data.events[0].record.occurredAtUtc = request().range.beginUtc - 1;
    QVERIFY(!buildTrending(req, data).value->containsSyntheticData);
}

void CareTrendingTests::summaryDoesNotExposeSuppressedCounts()
{
    auto data = snapshot();
    auto empty = buildTrending(request(TrendingKind::SearchTerms), data);
    QVERIFY(empty.status.ok());
    QVERIFY(!empty.value->summary.isEmpty());
    data.events.append(search(QStringLiteral("1"), QStringLiteral("secret"), data.nowUtc, QStringLiteral("secret"), true));
    auto suppressed = buildTrending(request(TrendingKind::SearchTerms), data);
    QCOMPARE(suppressed.value->summary, empty.value->summary);
    QVERIFY(!suppressed.value->containsSyntheticData);
    usersSearch(data, QStringLiteral("public"), data.nowUtc);
    auto visible = buildTrending(request(TrendingKind::SearchTerms), data);
    QVERIFY(!visible.value->summary.contains(QStringLiteral("secret")));
    QVERIFY(!visible.value->summaryFromModel);
    QCOMPARE(visible.value->summary, QStringLiteral("当前窗口公开热榜共1项，合计3次有效行为；首项有3位贡献用户、3次有效行为。"));
    QCOMPARE(visible.value->items[0].effectiveEvents, 3);
}

void CareTrendingTests::rejectsInvalidRequestsAndUnavailableData()
{
    auto data = snapshot();
    auto req = request();
    req.limit = 0;
    auto result = buildTrending(req, data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument);
    QVERIFY(!result.value);
    req = request();
    req.kind = static_cast<TrendingKind>(99);
    QCOMPARE(buildTrending(req, data).status.code, warehouse_contract::ErrorCode::InvalidArgument);
    req = request();
    req.range.endUtc = req.range.beginUtc;
    QCOMPARE(buildTrending(req, data).status.code, warehouse_contract::ErrorCode::InvalidArgument);
    req = request();
    data.dataAvailable = false;
    result = buildTrending(req, data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!result.value);
    data.dataAvailable = true;
    data.nowUtc = -1;
    QCOMPARE(buildTrending(req, data).status.code, warehouse_contract::ErrorCode::ServerError);
}

void CareTrendingTests::reportsCorruptData()
{
    auto data = snapshot();
    auto item = work(QStringLiteral("a"));
    data.publicWorks = {item};
    usersView(data, item, data.nowUtc);
    data.events[0].record.occurredAtUtc = -1;
    auto result = buildTrending(request(), data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!result.value);
    data.events.clear();
    item.resource.source.itemId = 0;
    data.publicWorks = {item};
    QCOMPARE(buildTrending(request(), data).status.code, warehouse_contract::ErrorCode::ServerError);
    item = work(QStringLiteral("a"));
    data.publicWorks = {item};
    usersView(data, item, data.nowUtc);
    auto conflict = data.events[0];
    --conflict.record.occurredAtUtc;
    data.events.append(conflict);
    result = buildTrending(request(), data);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!result.value);
}

QTEST_APPLESS_MAIN(CareTrendingTests)
#include "care_trending_tests.moc"
