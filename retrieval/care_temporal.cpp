#include "care_temporal.h"
#include "care_algorithm_common.h"
#include <QDateTime>
#include <QMap>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace care_algorithms {
namespace {
using namespace care_contract;
using Mode = recommendation_contract::PreferenceMode;
struct Profile {
    QMap<QString, double> categories;
    QMap<QString, double> people;
    double categoryTotal = 0;
    double peopleTotal = 0;
    bool syntheticCategories = false;
    bool syntheticPeople = false;
};
struct Interest {
    double score = 0;
    bool synthetic = false;
};
struct ScoredWork {
    ResourceSummary work;
    QString category;
    double longScore = 0;
    double recentScore = 0;
    double heat = 0;
    double freshness = 0;
    double categoryBonus = 0;
    double typeBonus = 0;
    double followBonus = 0;
    double total = 0;
    bool synthetic = false;
};
bool finiteNonnegative(double value) { return std::isfinite(value) && value >= 0; }
Status validateConfig(const TemporalScoringConfig& config) {
    for (const auto value : {config.longHalfLifeDays, config.recentHalfLifeDays, config.freshnessHalfLifeDays})
        if (!std::isfinite(value) || value <= 0)
            return {warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("评分半衰期必须为有限正数。")};
    for (const auto value : {config.categoryWeight, config.peopleWeight, config.longWeight, config.recentWeight,
                            config.heatWeight, config.freshnessWeight, config.explicitCategoryBonus,
                            config.explicitTypeBonus, config.followBonus})
        if (!finiteNonnegative(value) || value > 1)
            return {warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("评分权重必须为0到1的有限数值。")};
    if (config.categoryWeight + config.peopleWeight > 1)
        return {warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("兴趣匹配权重之和不能超过1。")};
    return {};
}
QSet<QString> personIds(const ResourceSummary& work) {
    QSet<QString> result;
    for (const auto& person : work.people) result.insert(person.personId);
    return result;
}
void addView(Profile& profile, const ObservedBehavior& event, double weight, bool useCategory) {
    if (weight <= 0) return;
    const auto& work = *event.workSnapshot;
    const auto category = detail::normalizeText(work.media.category);
    if (useCategory && !category.isEmpty()) {
        profile.categories[category] += weight;
        profile.categoryTotal += weight;
        profile.syntheticCategories |= event.record.synthetic;
    }
    // QSet eliminates duplicate credits; sorting also keeps floating-point sums
    // independent of the hash seed/process when a view credits several people.
    auto people = personIds(work).values();
    std::sort(people.begin(), people.end());
    for (const auto& id : people) {
        profile.people[id] += weight;
        profile.peopleTotal += weight;
        profile.syntheticPeople |= event.record.synthetic;
    }
}
Interest match(const Profile& profile, const ResourceSummary& work, const TemporalScoringConfig& config) {
    Interest result;
    const auto category = detail::normalizeText(work.media.category);
    const auto people = personIds(work);
    const bool hasCategory = profile.categoryTotal > 0;
    const bool hasPeople = profile.peopleTotal > 0 && !people.isEmpty();
    double categoryWeight = config.categoryWeight;
    if (!hasPeople) categoryWeight += config.peopleWeight;
    const double categoryMatch = hasCategory ? profile.categories.value(category) / profile.categoryTotal : 0;
    double peopleMatch = 0;
    if (hasPeople) {
        auto ordered = people.values(); std::sort(ordered.begin(), ordered.end());
        for (const auto& id : ordered) peopleMatch += profile.people.value(id) / profile.peopleTotal;
    }
    result.score = categoryWeight * categoryMatch + config.peopleWeight * peopleMatch;
    result.synthetic = (categoryWeight > 0 && categoryMatch > 0 && profile.syntheticCategories) ||
                       (config.peopleWeight > 0 && peopleMatch > 0 && profile.syntheticPeople);
    return result;
}
bool hasInterest(const Profile& profile, const TemporalScoringConfig& config) {
    return ((config.categoryWeight + config.peopleWeight > 0) && profile.categoryTotal > 0) ||
           (config.peopleWeight > 0 && profile.peopleTotal > 0);
}
QString reason(const ScoredWork& work, double longWeight, double recentWeight,
               double heatWeight, double freshWeight) {
    return QStringLiteral("长期兴趣=%1（权重%2）；近期兴趣=%3（权重%4）；公共热度=%5（权重%6）；"
                          "新鲜度=%7（权重%8）；显式类别=%9；显式类型=%10；主动关注=%11；总分=%12。")
        .arg(work.longScore, 0, 'f', 6).arg(longWeight, 0, 'f', 6)
        .arg(work.recentScore, 0, 'f', 6).arg(recentWeight, 0, 'f', 6)
        .arg(work.heat, 0, 'f', 6).arg(heatWeight, 0, 'f', 6)
        .arg(work.freshness, 0, 'f', 6).arg(freshWeight, 0, 'f', 6)
        .arg(work.categoryBonus, 0, 'f', 6).arg(work.typeBonus, 0, 'f', 6)
        .arg(work.followBonus, 0, 'f', 6).arg(work.total, 0, 'f', 6);
}
} // namespace

care_contract::Result<care_contract::TemporalRecommendationResult> recommendTemporal(
    const care_contract::TemporalRecommendationRequest& request, const PersonalSnapshot& snapshot) {
    return recommendTemporal(request, snapshot, TemporalScoringConfig{});
}
care_contract::Result<care_contract::TemporalRecommendationResult> recommendTemporal(
    const care_contract::TemporalRecommendationRequest& request, const PersonalSnapshot& snapshot,
    const TemporalScoringConfig& config) {
    auto status = care_contract::validate(request);
    if (!status.ok()) return {status, {}};
    status = validateConfig(config);
    if (!status.ok()) return {status, {}};
    // Validate data even when personalization is switched off. Database failures
    // and corrupt history must never silently become a cold-start success.
    const auto events = detail::personalEvents(snapshot);
    if (!events.status.ok()) return {events.status, {}};
    const auto works = detail::canonicalWorks(snapshot.readableWorks);
    if (!works.status.ok()) return {works.status, {}};
    for (auto it = snapshot.publicHeat.cbegin(); it != snapshot.publicHeat.cend(); ++it)
        if (it.value() < 0) return {detail::serverError(QStringLiteral("公共热度计数无效。")), {}};

    Profile longProfile, recentProfile;
    QHash<QString, ResourceSummary> currentWorks;
    for (const auto& work : *works.value) currentWorks.insert(work.resource.workId, work);
    if (snapshot.settings.personalizationEnabled) {
        for (const auto& event : *events.value) {
            if (event.record.event.action != BehaviorAction::View) continue;
            auto enriched = event;
            if (!enriched.workSnapshot) {
                const auto found = currentWorks.constFind(event.record.event.resource->workId);
                if (found == currentWorks.cend()) continue;
                enriched.workSnapshot = found.value();
            }
            const double age = static_cast<double>(snapshot.nowUtc - event.record.occurredAtUtc) / 86400.0;
            addView(longProfile, enriched, std::exp2(-age / config.longHalfLifeDays), request.category.mode == Mode::Auto);
            addView(recentProfile, enriched, std::exp2(-age / config.recentHalfLifeDays), request.category.mode == Mode::Auto);
        }
    }

    const QTimeZone zone("Asia/Shanghai");
    if (!zone.isValid()) return {detail::serverError(QStringLiteral("北京时间时区数据不可用。")), {}};
    const auto today = QDateTime::fromSecsSinceEpoch(snapshot.nowUtc, zone).date();
    if (!today.isValid()) return {detail::serverError(QStringLiteral("北京时间日期不可用。")), {}};
    QList<ScoredWork> scored;
    qint64 maxHeat = 0;
    bool anyFreshness = false;
    for (const auto& work : *works.value) {
        if (snapshot.notInterestedWorkIds.contains(work.resource.workId)) continue;
        ScoredWork row; row.work = work; row.category = detail::normalizeText(work.media.category);
        maxHeat = std::max(maxHeat, snapshot.publicHeat.value(work.resource.workId));
        const auto date = work.media.releaseDate;
        if (date.isValid() && date <= today)
            row.freshness = std::exp2(-static_cast<double>(date.daysTo(today)) / config.freshnessHalfLifeDays);
        anyFreshness |= row.freshness > 0;
        scored.append(row);
    }
    const bool personal = (config.longWeight > 0 && hasInterest(longProfile, config)) ||
                          (config.recentWeight > 0 && hasInterest(recentProfile, config));
    double longWeight = personal ? config.longWeight : 0;
    double recentWeight = personal ? config.recentWeight : 0;
    double heatWeight = config.heatWeight;
    double freshWeight = config.freshnessWeight;
    if (!personal) {
        heatWeight = maxHeat > 0 ? heatWeight : 0;
        freshWeight = anyFreshness ? freshWeight : 0;
        const double available = heatWeight + freshWeight;
        if (available > 0) { heatWeight /= available; freshWeight /= available; }
    }
    const double heatDenominator = std::log1p(static_cast<double>(maxHeat));
    const auto categoryPreference = detail::normalizeText(request.category.value);
    const auto typePreference = detail::normalizeText(request.fileType.value);
    bool anyPositive = false;
    for (auto& row : scored) {
        const auto longInterest = match(longProfile, row.work, config);
        const auto recentInterest = match(recentProfile, row.work, config);
        row.longScore = longInterest.score;
        row.recentScore = recentInterest.score;
        if (heatDenominator > 0)
            row.heat = std::log1p(static_cast<double>(snapshot.publicHeat.value(row.work.resource.workId))) / heatDenominator;
        if (request.category.mode == Mode::Explicit && row.category == categoryPreference)
            row.categoryBonus = config.explicitCategoryBonus;
        const auto type = detail::normalizeText(row.work.media.fileType);
        if (request.fileType.mode == Mode::Explicit && !type.isEmpty() && type == typePreference)
            row.typeBonus = config.explicitTypeBonus;
        if (snapshot.settings.personalizationEnabled) {
            for (const auto& person : row.work.people)
                if (snapshot.followedPeople.contains(person.personId)) { row.followBonus = config.followBonus; break; }
        }
        row.total = longWeight * row.longScore + recentWeight * row.recentScore + heatWeight * row.heat +
                    freshWeight * row.freshness + row.categoryBonus + row.typeBonus + row.followBonus;
        row.synthetic = (longWeight > 0 && longInterest.synthetic) || (recentWeight > 0 && recentInterest.synthetic);
        row.synthetic |= heatWeight > 0 && row.heat > 0 && snapshot.publicHeatContainsSyntheticData;
        anyPositive |= row.total > 0;
    }
    if (anyPositive) scored.erase(std::remove_if(scored.begin(), scored.end(), [](const auto& row) {
        return row.total <= 0;
    }), scored.end());
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.total != b.total) return a.total > b.total;
        if (a.work.resource.workId != b.work.resource.workId) return a.work.resource.workId < b.work.resource.workId;
        const auto& x = a.work.resource.source; const auto& y = b.work.resource.source;
        if (x.warehouseId != y.warehouseId) return x.warehouseId < y.warehouseId;
        return x.itemId < y.itemId;
    });

    TemporalRecommendationResult result;
    result.generatedAtUtc = snapshot.nowUtc;
    QString previousCategory;
    int run = 0;
    while (!scored.isEmpty() && result.items.size() < request.limit) {
        qsizetype next = 0;
        if (anyPositive && run >= 2 && scored.first().category == previousCategory) {
            for (qsizetype i = 1; i < scored.size(); ++i) {
                if (scored[i].total > 0 && scored[i].category != previousCategory) { next = i; break; }
            }
        }
        const auto row = scored.takeAt(next);
        result.items.append({row.work, reason(row, longWeight, recentWeight, heatWeight, freshWeight)});
        result.containsSyntheticData |= row.synthetic;
        if (row.category == previousCategory) ++run;
        else { previousCategory = row.category; run = 1; }
    }
    if (result.items.isEmpty()) result.message = QStringLiteral("暂无符合条件的可读推荐作品。");
    return {{}, result};
}
} // namespace care_algorithms
