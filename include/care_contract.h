#pragma once

#include "behavior_contract.h"
#include "personalrecommendation_contract.h"
#include <QUrl>

namespace care_contract {
enum class PersonRole { Actor, Director, Musician };
enum class BrowseScene { Neutral, Relax, Focus, Explore }; // User-selected context, not inferred emotion.
enum class TrendingKind { Works, SearchTerms };
enum class UpdateKind { News, Release };
enum class RecommendationEngine { Temporal, DeepSeek };
enum class WorkFeedbackKind { Neutral, NotInterested };

struct PersonSummary {
    QString personId;
    QString name;
    QList<PersonRole> roles; // One stable person may have multiple roles; equal names do not imply equal IDs.
    bool followed = false;
};
struct ResourceSummary { ResourceRef resource; MediaRecord media; QList<PersonSummary> people; };
struct RecommendedResource { ResourceSummary work; QString reason; };
struct PersonSearchRequest { QString query; PageRequest page; };
struct WorkFeedbackRequest { ResourceRef resource; WorkFeedbackKind feedback = WorkFeedbackKind::NotInterested; };
struct WorkFeedback { ResourceSummary work; WorkFeedbackKind feedback = WorkFeedbackKind::NotInterested; };
struct PersonUpdatesRequest { QString personId; TimeRange range; PageRequest page; };
struct PersonUpdate {
    QString updateId;
    QString personId;
    QString title;
    UpdateKind kind = UpdateKind::News;
    QUrl sourceUrl;
    std::optional<qint64> publishedAtUtc;
    std::optional<qint64> eventAtUtc; // Unknown release dates stay absent and do not become calendar events.
    qint64 fetchedAtUtc = 0;
    bool stale = false;
};
struct TrendingRequest { TrendingKind kind = TrendingKind::Works; TimeRange range; int limit = 10; };
struct TrendingItem {
    std::optional<ResourceSummary> work; // Works: present and term empty. SearchTerms: absent and term set.
    QString term;
    qint64 distinctUsers = 0;
    qint64 effectiveEvents = 0; // Capped per user/object/local day, not raw clicks.
};
struct TrendingResult {
    TrendingKind kind = TrendingKind::Works;
    TimeRange range;
    QList<TrendingItem> items;
    qint64 generatedAtUtc = 0;
    QString summary;
    bool summaryFromModel = false;
    bool containsSyntheticData = false;
};
struct CountBucket { QString key; QString label; qint64 count = 0; };
struct PersonCount { QString personId; QString name; PersonRole role = PersonRole::Actor; qint64 count = 0; };
struct DailyActivity { QString localDate; qint64 views = 0; qint64 searches = 0; }; // YYYY-MM-DD, Asia/Shanghai.
struct PeriodFootprint {
    TimeRange range;
    std::optional<qint64> coverageStartUtc; // First actually available event; absent for no history.
    qint64 views = 0;
    qint64 searches = 0;
    qint64 uniqueWorks = 0;
    qint64 activeDays = 0;
    QList<CountBucket> categories;
    QList<PersonCount> people; // Credit each distinct (person,role) once per view; totals may exceed views.
    QList<DailyActivity> days;
};
struct FootprintRequest {
    TimeRange range;
    std::optional<TimeRange> comparison; // Explicit earlier non-overlapping window; never inferred by the algorithm.
};
struct FootprintReport {
    PeriodFootprint current;
    std::optional<PeriodFootprint> comparison;
    qint64 generatedAtUtc = 0;
    QString message;
    bool containsSyntheticData = false;
};
struct TemporalRecommendationRequest {
    recommendation_contract::PreferenceSelection category;
    recommendation_contract::PreferenceSelection fileType;
    BrowseScene scene = BrowseScene::Neutral;
    int limit = 5;
};
struct TemporalRecommendationResult {
    QList<RecommendedResource> items; // Ordered, canonical identities and current readable sources included.
    QString message;
    bool fallback = false; // Legitimate empty model results are not fallback.
    RecommendationEngine engine = RecommendationEngine::Temporal;
    qint64 generatedAtUtc = 0;
    bool containsSyntheticData = false;
};

Status validatePersonId(const QString& personId);
Status validate(const PersonUpdate& value);
Status validate(const PersonUpdatesRequest& value);
Status validate(const TrendingRequest& value);
Status validate(const FootprintRequest& value);
Status validate(const TemporalRecommendationRequest& value);
Status validate(const WorkFeedbackRequest& value);
Status validate(const PersonSearchRequest& value);

// Same authenticated online session as WarehouseService; no caller-supplied user ID.
// Callbacks run once on the owning thread, including errors. Immediate failure MAY be synchronous.
// Permission/DB failures are errors; an empty result means success with no data.
// Strings from external sources/models must be rendered as plain text.
class CareService {
public:
    virtual ~CareService() = default;
    virtual void recordBehavior(const BehaviorEvent& event, StatusCallback done) = 0;
    virtual void getParticipationSettings(Callback<ParticipationSettings> done) = 0;
    virtual void setParticipationSettings(const ParticipationSettings& settings, StatusCallback done) = 0;
    virtual void clearHistory(StatusCallback done) = 0;
    virtual void getResource(const WarehouseItemRef& source, Callback<ResourceSummary> done) = 0;
    virtual void searchPeople(const PersonSearchRequest& request, Callback<Page<PersonSummary>> done) = 0;
    virtual void listWorkFeedback(const PageRequest& page, Callback<Page<WorkFeedback>> done) = 0;
    virtual void setWorkFeedback(const WorkFeedbackRequest& request, StatusCallback done) = 0;
    virtual void listFollows(const PageRequest& page, Callback<Page<PersonSummary>> done) = 0;
    virtual void setFollow(const QString& personId, bool followed, StatusCallback done) = 0;
    virtual void getPersonWorks(const QString& personId, const PageRequest& page, Callback<Page<ResourceSummary>> done) = 0;
    virtual void getPersonUpdates(const PersonUpdatesRequest& request, Callback<Page<PersonUpdate>> done) = 0;
    virtual void getTrending(const TrendingRequest& request, Callback<TrendingResult> done) = 0;
    virtual void getFootprint(const FootprintRequest& request, Callback<FootprintReport> done) = 0;
    // Same handle ownership/cancellation rules as WarehouseService; distinct handles per pending request.
    virtual warehouse_contract::RecommendationHandle recommendTemporal(
        const TemporalRecommendationRequest& request, Callback<TemporalRecommendationResult> done) = 0;
    virtual bool cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) = 0;
};
} // namespace care_contract
