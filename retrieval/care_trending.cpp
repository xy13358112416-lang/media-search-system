// >>> DEFENSE-NOTE | 文件导读｜公开热榜聚合
// 阅读路线：合法公共快照 → 时间/类型过滤 → 用户每日封顶 → 三人门槛 → 稳定排序 → 规则摘要
// 联调关系：服务端先核实公开资源与用户参与设置；本文件只统计，DeepSeek摘要是后续可选文字层。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "care_trending.h"
#include "care_algorithm_common.h"
#include <QMap>
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>

namespace care_algorithms {
namespace {
// >>> DEFENSE-NOTE | Contributions = 某个热榜对象的贡献记录
// QMap<用户ID,QSet<北京时间日编号>>；不同用户数取Map大小，有效事件数按用户/对象/日封顶。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Contributions {
    QMap<QString, QSet<qint64>> daysByUser;
    qint64 effectiveEvents = 0;
    bool synthetic = false;

    // >>> DEFENSE-NOTE | add = 累加一次合格行为
    // 同用户当天第一次贡献才增加 effectiveEvents；重复点击不能刷有效次数。
    // 模拟数据来源采用OR保留，不能因为同天又出现真实点击就抹去模拟标记。
    // <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | buildTrending = 建立作品榜或搜索词榜
// 先校验请求、时区和可信输入；统计时不访问用户数据库，不负责自己推断公共资格。
// <<< DEFENSE-NOTE | 本段讲解结束
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

    // >>> DEFENSE-NOTE | 步骤 01｜按榜单对象分桶
    // 作品榜只收浏览，key=workId且作品当前公开可用；搜索词榜只收搜索，key为规范化词。
    // 窗口使用 [begin,end)，未来行为排除。
    // <<< DEFENSE-NOTE | 本段讲解结束
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

    // >>> DEFENSE-NOTE | 步骤 02｜至少三名不同用户才公开
    // 不是三次点击。单用户点一百次也不满足三人门槛。
    // 输出分别保留 distinctUsers 与 effectiveEvents 两个指标，不能混说原始点击总数。
    // <<< DEFENSE-NOTE | 本段讲解结束
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
    // >>> DEFENSE-NOTE | 步骤 03｜按用户数、有效次数、稳定键排序
    // 先人数降序，再有效次数降序，最后稳定字符串键；复杂度包含排序，通常 O(k log k)。
    // <<< DEFENSE-NOTE | 本段讲解结束
    std::sort(ranked.begin(), ranked.end(), [](const RankedItem& a, const RankedItem& b) {
        if (a.item.distinctUsers != b.item.distinctUsers)
            return a.item.distinctUsers > b.item.distinctUsers;
        if (a.item.effectiveEvents != b.item.effectiveEvents)
            return a.item.effectiveEvents > b.item.effectiveEvents;
        return a.stableKey < b.stableKey;
    });

    // >>> DEFENSE-NOTE | 步骤 04｜取前 limit 并产生规则摘要
    // 这里的摘要由精确聚合结果拼接；总有效数对应返回榜单条目，不是所有未展示对象的总量。
    // <<< DEFENSE-NOTE | 本段讲解结束
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
