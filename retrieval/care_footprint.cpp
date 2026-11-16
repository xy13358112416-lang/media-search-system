#include "care_footprint.h"
#include "care_algorithm_common.h"
#include <QDateTime>
#include <QMap>
#include <QTimeZone>

namespace care_algorithms {
namespace {
using namespace care_contract;

struct CalendarWindow {
    QDate first;
    int days = 0;
};

QDate localDate(qint64 utc, const QTimeZone& zone) {
    return QDateTime::fromSecsSinceEpoch(utc, zone).date();
}

Result<CalendarWindow> calendarWindow(const TimeRange& range, const QTimeZone& zone) {
    const auto first = localDate(range.beginUtc, zone);
    const auto last = localDate(range.endUtc - 1, zone);
    if (!first.isValid() || !last.isValid())
        return {detail::serverError(QStringLiteral("无法生成北京时间自然日。")), {}};
    const auto count = first.daysTo(last) + 1;
    if (count <= 0 || count > MaxReportDays)
        return {{warehouse_contract::ErrorCode::InvalidArgument,
                 QStringLiteral("每个足迹区间最多覆盖%1个北京时间自然日。").arg(MaxReportDays)}, {}};
    return {{}, CalendarWindow{first, static_cast<int>(count)}};
}

PeriodFootprint summarize(const TimeRange& range, const CalendarWindow& calendar,
                          const QList<ObservedBehavior>& events,
                          const QHash<QString, const ResourceSummary*>& currentWorks,
                          const QTimeZone& zone, std::optional<qint64> coverageStart,
                          bool& containsSyntheticData) {
    PeriodFootprint result;
    result.range = range;
    result.coverageStartUtc = coverageStart;
    result.days.reserve(calendar.days);
    for (int i = 0; i < calendar.days; ++i)
        // The last legal UTC seconds fall on Shanghai's year 10000 day;
        // Qt::ISODate would return an empty string beyond year 9999.
        result.days.append({calendar.first.addDays(i).toString(QStringLiteral("yyyy-MM-dd")), 0, 0});

    QSet<QString> uniqueWorks;
    QMap<QString, qint64> categories;
    QMap<QPair<QString, int>, PersonCount> people;
    for (const auto& observed : events) {
        const auto& record = observed.record;
        if (record.occurredAtUtc < range.beginUtc || record.occurredAtUtc >= range.endUtc) continue;
        containsSyntheticData = containsSyntheticData || record.synthetic;
        auto& day = result.days[calendar.first.daysTo(localDate(record.occurredAtUtc, zone))];
        if (record.event.action == BehaviorAction::Search) {
            ++result.searches;
            ++day.searches;
            continue;
        }

        ++result.views;
        ++day.views;
        const auto& workId = record.event.resource->workId;
        uniqueWorks.insert(workId);
        // A historical snapshot is atomic: missing old fields are not replaced
        // by a different credit/category observed in today's catalog.
        const auto* metadata = observed.workSnapshot ? &*observed.workSnapshot : currentWorks.value(workId, nullptr);
        const auto category = metadata ? metadata->media.category.trimmed() : QString{};
        ++categories[category.isEmpty() ? QStringLiteral("未知") : category];
        if (!metadata) continue;

        QSet<QPair<QString, int>> credited;
        for (const auto& person : metadata->people) {
            for (const auto role : person.roles) {
                const auto key = qMakePair(person.personId, static_cast<int>(role));
                if (credited.contains(key)) continue;
                credited.insert(key);
                auto entry = people.find(key);
                if (entry == people.end())
                    entry = people.insert(key, {person.personId, person.name, role, 0});
                ++entry->count;
            }
        }
    }
    result.uniqueWorks = uniqueWorks.size();
    for (const auto& day : result.days)
        if (day.views || day.searches) ++result.activeDays;
    // Stable keys keep serialization and repeated reports independent of hash order.
    for (auto bucket = categories.cbegin(); bucket != categories.cend(); ++bucket)
        result.categories.append({bucket.key(), bucket.key(), bucket.value()});
    result.people = people.values();
    return result;
}
} // namespace

care_contract::Result<care_contract::FootprintReport> buildFootprint(
    const care_contract::FootprintRequest& request, const PersonalSnapshot& snapshot) {
    const auto status = care_contract::validate(request);
    if (!status.ok()) return {status, {}};
    const QTimeZone zone("Asia/Shanghai");
    if (!zone.isValid()) return {detail::serverError(QStringLiteral("北京时间时区数据不可用。")), {}};
    const auto currentCalendar = calendarWindow(request.range, zone);
    if (!currentCalendar.status.ok()) return {currentCalendar.status, {}};
    std::optional<CalendarWindow> comparisonCalendar;
    if (request.comparison) {
        const auto calendar = calendarWindow(*request.comparison, zone);
        if (!calendar.status.ok()) return {calendar.status, {}};
        comparisonCalendar = *calendar.value;
    }
    const auto events = detail::personalEvents(snapshot);
    if (!events.status.ok()) return {events.status, {}};
    const auto works = detail::canonicalWorks(snapshot.readableWorks);
    if (!works.status.ok()) return {works.status, {}};
    QHash<QString, const care_contract::ResourceSummary*> currentWorks;
    for (const auto& work : *works.value) currentWorks.insert(work.resource.workId, &work);
    std::optional<qint64> coverageStart;
    for (const auto& event : *events.value)
        if (!coverageStart || event.record.occurredAtUtc < *coverageStart) coverageStart = event.record.occurredAtUtc;

    care_contract::FootprintReport report;
    report.generatedAtUtc = snapshot.nowUtc;
    report.current = summarize(request.range, *currentCalendar.value, *events.value, currentWorks,
                               zone, coverageStart, report.containsSyntheticData);
    if (request.comparison)
        report.comparison = summarize(*request.comparison, *comparisonCalendar, *events.value, currentWorks,
                                      zone, coverageStart, report.containsSyntheticData);
    report.message = coverageStart
        ? QStringLiteral("按可用的浏览与搜索事件统计；历史覆盖起点以实际记录为准。")
        : QStringLiteral("暂无可用的浏览与搜索历史；所选区间已按北京时间自然日补零。");
    return {{}, report};
}
} // namespace care_algorithms
