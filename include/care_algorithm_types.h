// >>> DEFENSE-NOTE | 头文件导读｜服务端传给纯算法的可信快照
// ObservedBehavior是一条带身份/时间/来源的行为；PersonalSnapshot仅本人历史及当前可读作品；PublicSnapshot仅公开统计输入。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once
#include "care_contract.h"
#include <QHash>
#include <QSet>

// Server-side inputs only, never accepted as client-authored JSON. The network
// service must authenticate and construct the permitted snapshot before calling.
namespace care_algorithms {
// >>> DEFENSE-NOTE | 类型说明｜ObservedBehavior
// 一次实际/模拟行为与当时作品快照。publicEligible由服务端判定，算法不会相信客户端自报资格。
// <<< DEFENSE-NOTE | 本段讲解结束
struct ObservedBehavior {
    QString userKey; // Stable server identity, not a caller-supplied client identity.
    care_contract::BehaviorRecord record;
    std::optional<care_contract::ResourceSummary> workSnapshot; // Minimal historical credit/category snapshot.
    bool publicEligible = false; // Server-established opt-in/public-search eligibility at ingestion and now.
};
// >>> DEFENSE-NOTE | 类型说明｜PersonalSnapshot
// 本人快照：历史、当前可读作品、关注、不感兴趣、可信公共热度。只在服务端组装，不接受客户端伪造整份数据。
// <<< DEFENSE-NOTE | 本段讲解结束
struct PersonalSnapshot {
    QString userKey;
    qint64 nowUtc = 0;
    bool historyAvailable = true;
    care_contract::ParticipationSettings settings;
    QList<ObservedBehavior> events; // Must contain this user's history; helpers also exclude foreign rows.
    QList<care_contract::ResourceSummary> readableWorks; // Current authorized candidates only.
    QSet<QString> followedPeople;
    QSet<QString> notInterestedWorkIds;
    QHash<QString, qint64> publicHeat; // Canonical work -> trusted public effective event count.
    bool publicHeatContainsSyntheticData = false; // Set from the public statistic's provenance, not client JSON.
};
// >>> DEFENSE-NOTE | 类型说明｜PublicSnapshot
// 公共快照：当前公开作品与合格公共事件，范围比“用户可读”更严格。
// <<< DEFENSE-NOTE | 本段讲解结束
struct PublicSnapshot {
    qint64 nowUtc = 0;
    bool dataAvailable = true;
    QList<ObservedBehavior> events;
    QList<care_contract::ResourceSummary> publicWorks; // Currently PUBLIC sources only, not all user-readable works.
};
// >>> DEFENSE-NOTE | 类型说明｜CatalogSnapshot
// 人物目录输入：可读作品、本人关注和可信别名；不可见资源不因关注扩大权限。
// <<< DEFENSE-NOTE | 本段讲解结束
struct CatalogSnapshot {
    bool dataAvailable = true;
    QList<care_contract::ResourceSummary> readableWorks;
    QSet<QString> followedPeople;
    QHash<QString, QStringList> aliasesByPerson;
};
constexpr int MaxReportDays = 3660; // Resource cap for zero-filled reports, including custom ranges.
} // namespace care_algorithms
