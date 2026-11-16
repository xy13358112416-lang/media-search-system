#include "care_trending.h"
#include "care_algorithm_common.h"
#include <QMap>
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>

namespace care_algorithms {
namespace {
struct Contributions {
    QMap<QString, QSet<qint64>> daysByUser;
    qint64 effectiveEvents = 0;
    bool synthetic = false;

    void add(const ObservedBehavior& event)
    {
        const auto day = QDateTime::fromSecsSinceEpoch(event.record.occurredAtUtc,
            QTimeZone("Asia/Shanghai")).date().toJulianDay();
        auto& days = daysByUser[event.userKey];
        if (!days.contains(day)) {
            days.insert(day);
            ++effectiveEvents;
        }
        // A capped day may contain both imported and real events. Report the
        // provenance of that effective contribution regardless of input order.
        synthetic = synthetic || event.record.synthetic;
    }
};
struct RankedItem {
    care_contract::TrendingItem item;
    QString stableKey;
    bool synthetic = false;
};
}

care_contract::Result<care_contract::TrendingResult> buildTrending(
    const care_contract::TrendingRequest& request, const PublicSnapshot& snapshot)
{
    const auto requestStatus = care_contract::validate(request);
    if (!requestStatus.ok()) return {requestStatus, std::nullopt};
    if (!QTimeZone("Asia/Shanghai").isValid())
        return {detail::serverError(QStringLiteral("北京时间时区数据不可用。")), std::nullopt};
    const auto events = detail::publicEvents(snapshot);
    if (!events.status.ok()) return {events.status, std::nullopt};
    const auto works = detail::canonicalWorks(snapshot.publicWorks);
    if (!works.status.ok()) return {works.status, std::nullopt};

    QMap<QString, care_contract::ResourceSummary> currentWorks;
    for (const auto& work : *works.value)
        currentWorks.insert(work.resource.workId, work);

    QMap<QString, Contributions> contributions;
    for (const auto& event : *events.value) {
        const auto at = event.record.occurredAtUtc;
        if (at < request.range.beginUtc || at >= request.range.endUtc || at > snapshot.nowUtc)
            continue;
        QString key;
        if (request.kind == care_contract::TrendingKind::Works) {
            if (event.record.event.action != care_contract::BehaviorAction::View) continue;
            key = event.record.event.resource->workId;
            if (!currentWorks.contains(key)) continue;
        } else {
            if (event.record.event.action != care_contract::BehaviorAction::Search) continue;
            key = detail::normalizeText(event.record.event.query);
            if (key.isEmpty()) continue;
        }
        contributions[key].add(event);
    }

    QList<RankedItem> ranked;
    for (auto it = contributions.cbegin(); it != contributions.cend(); ++it) {
        if (it->daysByUser.size() < 3) continue;
        RankedItem value;
        value.stableKey = it.key();
        value.synthetic = it->synthetic;
        value.item.distinctUsers = it->daysByUser.size();
        value.item.effectiveEvents = it->effectiveEvents;
        if (request.kind == care_contract::TrendingKind::Works)
            value.item.work = currentWorks.value(it.key());
        else
            value.item.term = it.key();
        ranked.append(value);
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedItem& a, const RankedItem& b) {
        if (a.item.distinctUsers != b.item.distinctUsers)
            return a.item.distinctUsers > b.item.distinctUsers;
        if (a.item.effectiveEvents != b.item.effectiveEvents)
            return a.item.effectiveEvents > b.item.effectiveEvents;
        return a.stableKey < b.stableKey;
    });

    care_contract::TrendingResult output;
    output.kind = request.kind;
    output.range = request.range;
    output.generatedAtUtc = snapshot.nowUtc;
    qint64 totalEffectiveEvents = 0;
    for (const auto& value : ranked) {
        if (output.items.size() >= request.limit) break;
        output.items.append(value.item);
        totalEffectiveEvents += value.item.effectiveEvents;
        output.containsSyntheticData = output.containsSyntheticData || value.synthetic;
    }
    if (output.items.isEmpty()) {
        output.summary = QStringLiteral("当前窗口暂无达到公开门槛的热榜条目。");
    } else {
        output.summary = QStringLiteral("当前窗口公开热榜共%1项，合计%2次有效行为；首项有%3位贡献用户、%4次有效行为。")
            .arg(output.items.size()).arg(totalEffectiveEvents)
            .arg(output.items.first().distinctUsers).arg(output.items.first().effectiveEvents);
    }
    return {{}, output};
}
} // namespace care_algorithms
