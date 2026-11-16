#include "care_temporal.h"
#include <QtTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include <limits>

using namespace care_contract;
using namespace care_algorithms;
using Mode = recommendation_contract::PreferenceMode;
namespace {
constexpr qint64 Day = 86400;
const qint64 Now = QDateTime(QDate(2026, 11, 15), QTime(12, 0), QTimeZone::UTC).toSecsSinceEpoch();
ResourceSummary work(QString id, QString category = {}, QString type = {}) {
    ResourceSummary value;
    value.resource = {id, {1, static_cast<qint64>(qHash(id, 0) % 1000000) + 10}};
    value.media.id = id;
    value.media.fileName = id;
    value.media.category = category;
    value.media.fileType = type;
    return value;
}
PersonSummary person(QString id, QList<PersonRole> roles = {PersonRole::Actor}) {
    return {id, id, roles, false};
}
ObservedBehavior view(QString id, const ResourceSummary& value, qint64 ageDays = 0) {
    ObservedBehavior event;
    event.userKey = "user";
    event.record.event = {id, BehaviorAction::View, value.resource, {}};
    event.record.occurredAtUtc = Now - ageDays * Day;
    event.workSnapshot = value;
    return event;
}
PersonalSnapshot snapshot(const QList<ResourceSummary>& works) {
    PersonalSnapshot value;
    value.userKey = "user";
    value.nowUtc = Now;
    value.readableWorks = works;
    return value;
}
QStringList ids(const TemporalRecommendationResult& value) {
    QStringList result;
    for (const auto& item : value.items) result.append(item.work.resource.workId);
    return result;
}
TemporalScoringConfig interestOnly() {
    TemporalScoringConfig config;
    config.heatWeight = 0;
    config.freshnessWeight = 0;
    return config;
}
}

class CareTemporalTests : public QObject {
    Q_OBJECT
private slots:
    void coldStartUsesHeatAndFreshness();
    void everyViewHasItsOwnDecayAndRecentShift();
    void preferenceModesAndUnknownTypes();
    void peopleUseStableIdentityOncePerView();
    void disabledPersonalizationAndHistorySwitch();
    void followsAndCrossSourceExclusion();
    void freshnessUsesShanghaiDateAndRejectsUnknownFuture();
    void eventsAreIsolatedDeduplicatedAndFutureFiltered();
    void failuresPropagateWithoutFallback();
    void sourcesAndTiesAreStable();
    void configurableWeightsAndHugeHeatStayFinite();
    void diversityDoesNotFillUnrelatedWorks();
    void syntheticTracksUsedHistoryOnly();
    void invalidRequestAndConfigFail();
    void existingMetadataCanEnrichMissingHistoricalSnapshots();
    void syntheticPublicHeatIsLabelled();
};

void CareTemporalTests::coldStartUsesHeatAndFreshness() {
    auto hot = work("hot");
    auto fresh = work("fresh");
    fresh.media.releaseDate = QDate(2026, 11, 15);
    auto state = snapshot({fresh, hot});
    state.publicHeat["hot"] = 100;
    const auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok());
    QVERIFY(result.value);
    QCOMPARE(ids(*result.value), QStringList({"hot", "fresh"}));
    QCOMPARE(result.value->generatedAtUtc, Now);
    QCOMPARE(result.value->engine, RecommendationEngine::Temporal);
    QVERIFY(!result.value->fallback);
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("公共热度")));
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("新鲜度")));
}

void CareTemporalTests::everyViewHasItsOwnDecayAndRecentShift() {
    auto old = work("long", "old");
    auto recent = work("recent", "new");
    auto state = snapshot({old, recent});
    for (int i = 0; i < 12; ++i) state.events.append(view(QString("old%1").arg(i), old, 30));
    state.events.append(view("new0", recent));
    // Existing interest wins initially. Three recent views over the next
    // fourteen days must overtake it under the mandated .35/.40 mix.
    auto result = recommendTemporal({}, state, interestOnly());
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"long", "recent"}));
    state.nowUtc += 14 * Day;
    for (int i = 1; i < 4; ++i) {
        auto event = view(QString("new%1").arg(i), recent);
        event.record.occurredAtUtc = state.nowUtc - i * Day;
        state.events.append(event);
    }
    result = recommendTemporal({}, state, interestOnly());
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("recent"));
    TemporalScoringConfig longOnly = interestOnly();
    longOnly.recentWeight = 0;
    result = recommendTemporal({}, snapshot({}), longOnly);
    QVERIFY(result.status.ok() && result.value && result.value->items.isEmpty());
    auto initial = snapshot({old, recent});
    initial.events = {view("one", old, 90), view("two", old, 0), view("three", recent, 0)};
    // 0.5 + 1 vs 1: the .6 category profile contribution is .6, not 2/3.
    result = recommendTemporal({}, initial, longOnly);
    QVERIFY(result.status.ok() && result.value);
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("长期兴趣=0.600000")));
}

void CareTemporalTests::preferenceModesAndUnknownTypes() {
    auto a = work("a", "A", "MP4");
    auto b = work("b", "B", "FLAC");
    auto state = snapshot({b, a});
    state.events = {view("e", b)};
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"b"}));
    TemporalRecommendationRequest request;
    request.category = {Mode::Any, "ignored"};
    request.fileType = {Mode::Any, "ignored"};
    result = recommendTemporal(request, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b"}));
    request.category = {Mode::Explicit, "A"};
    request.fileType = {Mode::Explicit, "FLAC"};
    result = recommendTemporal(request, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b"}));
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("显式类别=0.200000")));
    request.category = {Mode::Any, {}};
    request.fileType = {Mode::Explicit, "MP4"};
    auto unknown = work("0-unknown", {}, {});
    unknown.media.fileName = "fake.MP4";
    state.readableWorks = {unknown, a, b};
    result = recommendTemporal(request, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a"}));
}

void CareTemporalTests::peopleUseStableIdentityOncePerView() {
    auto history = work("history", "C");
    history.people = {person("p", {PersonRole::Actor, PersonRole::Director}), person("p"), person("q")};
    auto a = work("a", "C"); a.people = {person("p", {PersonRole::Musician})};
    auto b = work("b", "C"); b.people = {person("q")};
    auto state = snapshot({b, a});
    state.events = {view("e", history)};
    const auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b"}));
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("长期兴趣=0.800000")));
    // Names are not identities.
    b.people.first().personId = "other";
    b.people.first().name = "p";
    state.readableWorks = {b, a};
    const auto other = recommendTemporal({}, state);
    QVERIFY(other.status.ok() && other.value);
    QCOMPARE(other.value->items.first().work.resource.workId, QString("a"));
}

void CareTemporalTests::disabledPersonalizationAndHistorySwitch() {
    auto a = work("a", "A"); auto b = work("b", "B");
    b.people = {person("follow")};
    auto state = snapshot({b, a});
    state.events = {view("e", b)};
    state.followedPeople.insert("follow");
    state.settings.personalizationEnabled = false;
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b"}));
    TemporalRecommendationRequest request; request.category = {Mode::Explicit, "B"};
    result = recommendTemporal(request, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"b"}));
    state.settings.personalizationEnabled = true;
    state.settings.historyEnabled = false; // Pauses ingestion; existing history remains usable.
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("b"));
}

void CareTemporalTests::followsAndCrossSourceExclusion() {
    auto a = work("a"); auto b = work("b"); b.people = {person("p")};
    auto source = b; source.resource.source = {2, 3};
    auto state = snapshot({a, source, b}); state.followedPeople.insert("p");
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"b"}));
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("主动关注=0.050000")));
    state.notInterestedWorkIds.insert("b");
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a"}));
    state.notInterestedWorkIds.insert("a");
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value && result.value->items.isEmpty());
    QVERIFY(!result.value->message.isEmpty()); QVERIFY(!result.value->fallback);
}

void CareTemporalTests::freshnessUsesShanghaiDateAndRejectsUnknownFuture() {
    auto today = work("today"); today.media.releaseDate = QDate(2026, 11, 16);
    auto month = work("month"); month.media.releaseDate = today.media.releaseDate.addDays(-30);
    auto future = work("future"); future.media.releaseDate = today.media.releaseDate.addDays(1);
    auto unknown = work("unknown");
    auto state = snapshot({future, month, unknown, today});
    state.nowUtc = QDateTime(QDate(2026, 11, 15), QTime(16, 0), QTimeZone::UTC).toSecsSinceEpoch();
    const auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"today", "month"}));
    QVERIFY(result.value->items.last().reason.contains(QStringLiteral("新鲜度=0.500000")));
}

void CareTemporalTests::eventsAreIsolatedDeduplicatedAndFutureFiltered() {
    auto a = work("a", "A"); auto b = work("b", "B");
    auto state = snapshot({a, b});
    auto valid = view("valid", a); state.events = {valid, valid, view("valid-b", b)};
    auto foreign = view("foreign", b); foreign.userKey = "other"; foreign.record.synthetic = true;
    auto future = view("future", b, -1); future.record.synthetic = true;
    state.events << foreign << future;
    const auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b"}));
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("长期兴趣=0.500000")));
    QVERIFY(!result.value->containsSyntheticData);
}

void CareTemporalTests::failuresPropagateWithoutFallback() {
    auto a = work("a", "A"); auto state = snapshot({a});
    state.historyAvailable = false;
    auto result = recommendTemporal({}, state);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    state.settings.personalizationEnabled = false;
    result = recommendTemporal({}, state);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    state.historyAvailable = true;
    auto first = view("conflict", a); auto second = first;
    second.record.occurredAtUtc--;
    state.events = {first, second};
    result = recommendTemporal({}, state);
    QVERIFY(!result.status.ok()); QVERIFY(!result.value);
    state.events = {}; state.publicHeat["a"] = -1;
    result = recommendTemporal({}, state);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
}

void CareTemporalTests::sourcesAndTiesAreStable() {
    auto a = work("a"); a.resource.source = {3, 1};
    auto a2 = a; a2.resource.source = {1, 5};
    auto a3 = a; a3.resource.source = {1, 2};
    auto b = work("b"); auto state = snapshot({b, a, a2, a3});
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b"}));
    QCOMPARE(result.value->items.first().work.resource.source.warehouseId, qint64(1));
    QCOMPARE(result.value->items.first().work.resource.source.itemId, qint64(2));
    std::reverse(state.readableWorks.begin(), state.readableWorks.end());
    auto reverse = recommendTemporal({}, state);
    QVERIFY(reverse.status.ok() && reverse.value);
    QCOMPARE(ids(*result.value), ids(*reverse.value));
    TemporalRecommendationRequest scene; scene.scene = BrowseScene::Explore;
    reverse = recommendTemporal(scene, state);
    QVERIFY(reverse.status.ok() && reverse.value);
    QCOMPARE(ids(*result.value), ids(*reverse.value));
}

void CareTemporalTests::configurableWeightsAndHugeHeatStayFinite() {
    auto a = work("a", "A"); auto b = work("b", "B");
    auto state = snapshot({a, b}); state.events = {view("e", a)};
    state.publicHeat["b"] = std::numeric_limits<qint64>::max();
    state.publicHeat["a"] = 1;
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("a"));
    TemporalScoringConfig config; config.longWeight = 0; config.recentWeight = 0;
    config.heatWeight = 1; config.freshnessWeight = 0;
    result = recommendTemporal({}, state, config);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("b"));
    for (const auto& item : result.value->items) {
        QVERIFY(!item.reason.contains("nan", Qt::CaseInsensitive));
        QVERIFY(!item.reason.contains("inf", Qt::CaseInsensitive));
    }
}

void CareTemporalTests::diversityDoesNotFillUnrelatedWorks() {
    auto a = work("a", "A"); auto b = work("b", "A");
    auto c = work("c", "A"); auto d = work("d", "B");
    auto state = snapshot({a, b, c, d});
    state.publicHeat = {{"a", 10}, {"b", 9}, {"c", 8}, {"d", 1}};
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b", "d", "c"}));
    state.publicHeat.remove("d");
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b", "c"}));
    state.publicHeat.clear();
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(ids(*result.value), QStringList({"a", "b", "c", "d"}));
}

void CareTemporalTests::syntheticTracksUsedHistoryOnly() {
    auto a = work("a", "A"); auto state = snapshot({a});
    auto event = view("e", a); event.record.synthetic = true; state.events = {event};
    auto result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value && result.value->containsSyntheticData);
    state.settings.personalizationEnabled = false;
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value && !result.value->containsSyntheticData);
    state.settings.personalizationEnabled = true;
    TemporalRecommendationRequest request; request.category.mode = Mode::Any;
    result = recommendTemporal(request, state);
    QVERIFY(result.status.ok() && result.value && !result.value->containsSyntheticData);
    event.record.event = {"search", BehaviorAction::Search, std::nullopt, "A"};
    event.workSnapshot.reset(); state.events = {event};
    result = recommendTemporal({}, state);
    QVERIFY(result.status.ok() && result.value && !result.value->containsSyntheticData);
}

void CareTemporalTests::invalidRequestAndConfigFail() {
    const auto state = snapshot({work("a")});
    TemporalRecommendationRequest request; request.limit = 0;
    auto result = recommendTemporal(request, state);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    request = {}; request.scene = static_cast<BrowseScene>(999);
    result = recommendTemporal(request, state);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    request = {}; request.category = {Mode::Explicit, " "};
    result = recommendTemporal(request, state);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    TemporalScoringConfig config; config.recentHalfLifeDays = 0;
    result = recommendTemporal({}, state, config);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    config = {}; config.heatWeight = std::numeric_limits<double>::infinity();
    result = recommendTemporal({}, state, config);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    config = {}; config.longWeight = -1;
    result = recommendTemporal({}, state, config);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
    config = {}; config.categoryWeight = 0.8; config.peopleWeight = 0.8;
    result = recommendTemporal({}, state, config);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::InvalidArgument); QVERIFY(!result.value);
}

void CareTemporalTests::existingMetadataCanEnrichMissingHistoricalSnapshots() {
    auto unrelated = work("a", "Other"); auto target = work("z", "Interest");
    auto input = snapshot({unrelated, target});
    auto event = view("e", target); event.workSnapshot.reset(); input.events = {event};
    const auto result = recommendTemporal({}, input);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("z"));
}
void CareTemporalTests::syntheticPublicHeatIsLabelled() {
    auto input = snapshot({work("hot")}); input.publicHeat["hot"] = 5;
    input.publicHeatContainsSyntheticData = true;
    const auto result = recommendTemporal({}, input);
    QVERIFY(result.status.ok() && result.value);
    QVERIFY(result.value->containsSyntheticData);
}

QTEST_APPLESS_MAIN(CareTemporalTests)
#include "care_temporal_tests.moc"
