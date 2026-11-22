// >>> DEFENSE-NOTE | 文件导读｜个人媒体足迹统计
// 阅读路线：北京时间自然日 → 补齐日期桶 → 统计浏览/搜索/作品/人物 → 可选比较期
// 联调关系：服务端传本人历史与可读目录；返回 FootprintReport 给Qt，不做推荐排序，也不调用模型。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "care_footprint.h"
#include "care_algorithm_common.h"
#include <QDateTime>
#include <QMap>
#include <QTimeZone>

namespace care_algorithms {
namespace {
using namespace care_contract;

// >>> DEFENSE-NOTE | CalendarWindow = 报告日期窗口
// first 为第一个北京时间日期，days 为覆盖自然日数；日期桶包括零行为的日期。
// <<< DEFENSE-NOTE | 本段讲解结束
struct CalendarWindow {
    QDate first;
    int days = 0;
};

// >>> DEFENSE-NOTE | localDate = UTC秒转指定时区日期
// 同一UTC时间在不同时区可落在不同日期；所有足迹统一使用北京时间。
// <<< DEFENSE-NOTE | 本段讲解结束
QDate localDate(qint64 utc, const QTimeZone& zone) {
    return QDateTime::fromSecsSinceEpoch(utc, zone).date();
}

// >>> DEFENSE-NOTE | calendarWindow = 计算报告覆盖的自然日
// 时间区间左闭右开 [begin,end)，用 end-1 求最后一天，避免把终点零点多算一天。
// 限制最大自然日数，防止异常范围分配过多内存。
// <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | summarize = 聚合一段时间的足迹
// 先为每一天创建零计数，再遍历合法事件；QSet计不同作品，QMap计类别和人物角色。
// <<< DEFENSE-NOTE | 本段讲解结束
PeriodFootprint summarize(const TimeRange& range, const CalendarWindow& calendar,
                          const QList<ObservedBehavior>& events,
                          const QHash<QString, const ResourceSummary*>& currentWorks,
                          const QTimeZone& zone, std::optional<qint64> coverageStart,
                          bool& containsSyntheticData) {
    PeriodFootprint result;
    result.range = range;
    result.coverageStartUtc = coverageStart;
    result.days.reserve(calendar.days);
    for (int i = 0; i < calendar.days; ++i) {
        // Format numeric parts explicitly: Qt versions differ on the sign for year 10000.
        const QDate date = calendar.first.addDays(i);
        const QString label = QStringLiteral("%1-%2-%3").arg(date.year(), 4, 10, QLatin1Char('0'))
            .arg(date.month(), 2, 10, QLatin1Char('0')).arg(date.day(), 2, 10, QLatin1Char('0'));
        result.days.append({label, 0, 0});
    }

    // >>> DEFENSE-NOTE | 聚合步骤 01｜准备去重与计数容器
    // uniqueWorks 区分浏览次数和不同作品数；人物以(personId,role)为键。
    // 一个作品多人多角色，人物统计和可能大于总浏览次数。
    // <<< DEFENSE-NOTE | 本段讲解结束
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
        // >>> DEFENSE-NOTE | 聚合步骤 02｜优先使用当时的历史快照
        // 历史已保存的类别/人物不被今天的目录覆盖。只有整个历史快照不存在才回退当前目录；未知类别归入“未知”。
        // <<< DEFENSE-NOTE | 本段讲解结束
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
    // >>> DEFENSE-NOTE | 聚合步骤 03｜完成指标和稳定输出
    // 浏览/搜索任一非零即活跃日；作品数取集合大小；有序Map输出使重复统计顺序可复现。
    // <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | buildFootprint = 足迹总入口
// 验证主区间与比较区间、清洗当前用户事件及作品；汇总两期并返回实际历史覆盖起点。
// 无历史正常补零；historyAvailable=false 是错误，不能伪装成用户没浏览。
// <<< DEFENSE-NOTE | 本段讲解结束
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
