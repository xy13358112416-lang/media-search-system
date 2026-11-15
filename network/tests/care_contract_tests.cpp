#include "care_contract.h"
#include "warehouse_protocol.h"
#include <QtTest>
#include <type_traits>
#include <limits>

using namespace care_contract;
static_assert(std::is_abstract_v<CareService>);
static_assert(!std::is_same_v<FootprintRequest, TemporalRecommendationRequest>);
static_assert(!std::is_same_v<FootprintReport, TemporalRecommendationResult>);
static_assert(std::is_same_v<decltype(TemporalRecommendationResult{}.items), QList<RecommendedResource>>);
static_assert(std::is_member_function_pointer_v<decltype(&CareService::getResource)>);
static_assert(std::is_member_function_pointer_v<decltype(&CareService::searchPeople)>);

class CareContractTests : public QObject {
    Q_OBJECT
private slots:
    void safeDefaultsAndSeparateResults() {
        ParticipationSettings settings;
        QVERIFY(settings.personalizationEnabled);
        QVERIFY(!settings.contributePublicTrends);
        QCOMPARE(PageRequest{}.limit, 20);
        QCOMPARE(TemporalRecommendationRequest{}.limit, 5);
        QCOMPARE(TemporalRecommendationRequest{}.scene, BrowseScene::Neutral);
        QVERIFY(TemporalRecommendationResult{}.items.isEmpty());
        QVERIFY(!TemporalRecommendationResult{}.fallback);
        QVERIFY(!FootprintReport{}.comparison.has_value());
        QVERIFY(!FootprintReport{}.current.coverageStartUtc.has_value());
    }
    void validatesTimeRangeWithoutMillisecondConfusion() {
        QVERIFY(validate(TimeRange{0, 1}).ok());
        QVERIFY(validate(TimeRange{1700000000, 1700003600}).ok());
        QVERIFY(!validate(TimeRange{-1, 10}).ok());
        QVERIFY(!validate(TimeRange{10, 10}).ok());
        QVERIFY(!validate(TimeRange{20, 10}).ok());
        QVERIFY(!validate(TimeRange{1700000000000LL, 1700003600000LL}).ok());
        QVERIFY(validate(TimeRange{MaxUtcExclusive - 1, MaxUtcExclusive}).ok());
        QVERIFY(!validate(TimeRange{0, MaxUtcExclusive + 1}).ok());
    }
    void pagingBoundaries() {
        QVERIFY(validate(PageRequest{0, 1}).ok());
        QVERIFY(validate(PageRequest{0, MaxPageSize}).ok());
        QVERIFY(!validate(PageRequest{-1, 20}).ok());
        QVERIFY(!validate(PageRequest{0, 0}).ok());
        QVERIFY(!validate(PageRequest{0, MaxPageSize + 1}).ok());
        QVERIFY(!validate(PageRequest{std::numeric_limits<qint64>::max(), 1}).ok());
    }
    void resourceNeedsWorkIdentityAndWarehouseLocation() {
        QVERIFY(validate(ResourceRef{"uci:AA13", {1, 2}}).ok());
        QVERIFY(!validate(ResourceRef{"", {1, 2}}).ok());
        QVERIFY(!validate(ResourceRef{" uci:AA13", {1, 2}}).ok());
        QVERIFY(!validate(ResourceRef{"uci:AA13", {0, 2}}).ok());
        QVERIFY(!validate(ResourceRef{"uci:AA13", {1, -2}}).ok());
    }
    void viewAndSearchHaveDifferentRequiredFields() {
        BehaviorEvent view;
        view.eventId = "event-1"; view.resource = ResourceRef{"uci:AA13", {1, 2}};
        QVERIFY(validate(view).ok());
        view.query = "hidden-query";
        QVERIFY(!validate(view).ok());
        view.query.clear(); view.resource.reset();
        QVERIFY(!validate(view).ok());
        BehaviorEvent search;
        search.eventId = "event-2"; search.action = BehaviorAction::Search; search.query = "Pygmalion";
        QVERIFY(validate(search).ok());
        search.resource = ResourceRef{"uci:AA13", {1, 2}};
        QVERIFY(!validate(search).ok());
        search.resource.reset(); search.query = "  ";
        QVERIFY(!validate(search).ok());
        search.query = QString(MaxQueryUnits, QLatin1Char('a'));
        QVERIFY(validate(search).ok());
        search.query.append('a'); QVERIFY(!validate(search).ok());
        search.query = "x"; search.action = static_cast<BehaviorAction>(999);
        QCOMPARE(validate(search).code, warehouse_contract::ErrorCode::InvalidArgument);
    }
    void eventIdentifiersAreBounded() {
        BehaviorEvent event;
        event.resource = ResourceRef{"uci:AA13", {1, 2}};
        QVERIFY(!validate(event).ok());
        event.eventId = QString(MaxIdentifierUnits, QLatin1Char('a'));
        QVERIFY(validate(event).ok());
        event.eventId.append('a'); QVERIFY(!validate(event).ok());
        event.eventId = "line\nbreak"; QVERIFY(!validate(event).ok());
    }
    void reportComparisonCannotOverlapCurrentPeriod() {
        FootprintRequest request;
        request.range = {100, 200}; request.comparison = TimeRange{0, 100};
        QVERIFY(validate(request).ok());
        request.comparison = TimeRange{0, 101}; QVERIFY(!validate(request).ok());
        request.comparison = TimeRange{0, 0}; QVERIFY(!validate(request).ok());
        request.comparison.reset(); QVERIFY(validate(request).ok());
    }
    void rankingPreferencesAreExplicitAndBounded() {
        TemporalRecommendationRequest request;
        QVERIFY(validate(request).ok());
        request.limit = MaxRecommendationLimit; QVERIFY(validate(request).ok());
        request.limit = 0; QVERIFY(!validate(request).ok());
        request.limit = MaxRecommendationLimit + 1; QVERIFY(!validate(request).ok());
        request.limit = 5;
        request.category = {recommendation_contract::PreferenceMode::Explicit, " "};
        QVERIFY(!validate(request).ok());
        request.category.value = QStringLiteral("音乐"); QVERIFY(validate(request).ok());
        request.scene = static_cast<BrowseScene>(999); QVERIFY(!validate(request).ok());
        request.scene = BrowseScene::Relax;
        request.category.mode = static_cast<recommendation_contract::PreferenceMode>(999);
        QVERIFY(!validate(request).ok());
    }
    void validatesTrendingAndPersonUpdateQueries() {
        TrendingRequest trend;
        trend.range = {100, 200}; QVERIFY(validate(trend).ok());
        trend.kind = static_cast<TrendingKind>(999); QVERIFY(!validate(trend).ok());
        PersonUpdatesRequest updates;
        updates.personId = "person-1"; updates.range = {100, 200};
        QVERIFY(validate(updates).ok());
        updates.personId.clear(); QVERIFY(!validate(updates).ok());
        PersonSearchRequest people;
        people.query = "director"; QVERIFY(validate(people).ok());
        people.query = " "; QVERIFY(!validate(people).ok());
        people.query = "actor"; people.page.limit = 0; QVERIFY(!validate(people).ok());
    }
    void recommendedResourcesCanBecomeBehaviorWithoutGuessingIdentity() {
        RecommendedResource item;
        item.work.resource = {"work:canonical-1", {1, 2}};
        item.work.media.id = "same-local-id";
        item.work.people = {{"person:1", "Alex", {PersonRole::Actor}, false}};
        BehaviorEvent event;
        event.eventId = "event-3"; event.resource = item.work.resource;
        QVERIFY(validate(event).ok());
        QCOMPARE(event.resource->workId, QStringLiteral("work:canonical-1"));
        QCOMPARE(item.work.people.first().personId, QStringLiteral("person:1"));
    }
    void feedbackCanBeSetAndReversed() {
        WorkFeedbackRequest request{{"uci:AA13", {1, 2}}, WorkFeedbackKind::NotInterested};
        QVERIFY(validate(request).ok());
        request.feedback = WorkFeedbackKind::Neutral; QVERIFY(validate(request).ok());
        request.feedback = static_cast<WorkFeedbackKind>(999); QVERIFY(!validate(request).ok());
        request.feedback = WorkFeedbackKind::Neutral;
        request.resource.workId.clear(); QVERIFY(!validate(request).ok());
    }
    void operationNamesAreUnique() {
        using M = warehouse_protocol::MessageType;
        const QList<QPair<M, QString>> operations = {
            {M::RecordBehavior, "record_behavior"}, {M::GetParticipationSettings, "get_participation_settings"},
            {M::SetParticipationSettings, "set_participation_settings"}, {M::ClearHistory, "clear_history"},
            {M::ListFollows, "list_follows"}, {M::SetFollow, "set_follow"},
            {M::GetPersonWorks, "get_person_works"}, {M::GetPersonUpdates, "get_person_updates"},
            {M::GetTrending, "get_trending"}, {M::GetFootprint, "get_footprint"},
            {M::RecommendTemporal, "recommend_temporal"},
            {M::ListWorkFeedback, "list_work_feedback"}, {M::SetWorkFeedback, "set_work_feedback"},
            {M::GetResource, "get_resource"}, {M::SearchPeople, "search_people"}
        };
        QSet<QString> seen;
        for (const auto& operation : operations) {
            QCOMPARE(warehouse_protocol::messageName(operation.first), operation.second);
            QVERIFY(!seen.contains(operation.second)); seen.insert(operation.second);
        }
        QCOMPARE(warehouse_protocol::messageName(M::CancelRecommendation), QStringLiteral("cancel_recommendation"));
    }
};
QTEST_GUILESS_MAIN(CareContractTests)
#include "care_contract_tests.moc"
