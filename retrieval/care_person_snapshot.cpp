// >>> DEFENSE-NOTE | 文件导读｜可信人物关联的快照适配
// 阅读路线：清除未确认人物 → 精确三元组匹配 → 验证人物 → 附加关注标记
// 联调关系：主逻辑提供PersonContext，推荐/足迹消费附加后的快照；历史快照保留当时事实，不被今天的数据补写。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "care_person_snapshot.h"
#include "care_algorithm_common.h"
#include <QMap>
#include <algorithm>

namespace care_algorithms {
namespace {
using namespace care_contract;
using ResourceKey = QPair<QString, QPair<qint64, qint64>>;
// >>> DEFENSE-NOTE | key = 作品与来源的三元组键
// 使用(workId,(warehouseId,itemId))匹配关联；仅workId相同不足以确认某个来源的映射。
// <<< DEFENSE-NOTE | 本段讲解结束
ResourceKey key(const ResourceRef& resource) {
    return {resource.workId, {resource.source.warehouseId, resource.source.itemId}};
}
// >>> DEFENSE-NOTE | validPersonId = 稳定人物ID校验
// 复用公开契约，不按同名去匹配不同的人。
// <<< DEFENSE-NOTE | 本段讲解结束
bool validPersonId(const QString& id) {
    // Reuse the public contract's identifier rules rather than invent a weaker
    // rule for trusted-store records or follow entries.
    return validate(PersonUpdatesRequest{id, {0, 1}, {}}).ok();
}
// >>> DEFENSE-NOTE | validName = 可展示姓名校验
// 拒绝空、过长和控制字符；姓名仍是展示属性，不能替代人物ID。
// <<< DEFENSE-NOTE | 本段讲解结束
bool validName(const QString& name) {
    if (name.simplified().isEmpty() || name.size() > MaxQueryUnits) return false;
    for (const auto ch : name)
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
// >>> DEFENSE-NOTE | confirmedPeople = 整理已确认人物
// 按personId合并角色，冲突姓名报错，角色排序去重并限制单作品人物数；followed标记后续从当前用户上下文重建。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<QList<PersonSummary>> confirmedPeople(const QList<PersonSummary>& input) {
    QMap<QString, PersonSummary> unique;
    for (auto person : input) {
        if (!validPersonId(person.personId) || !validName(person.name) || person.roles.isEmpty())
            return {detail::serverError(QStringLiteral("已确认人物记录包含无效身份、姓名或角色。")), {}};
        person.name = person.name.simplified();
        person.followed = false; // Only the context's explicit follow set is authoritative.
        for (const auto role : person.roles)
            if (role != PersonRole::Actor && role != PersonRole::Director && role != PersonRole::Musician)
                return {detail::serverError(QStringLiteral("已确认人物记录包含无效角色。")), {}};
        auto found = unique.find(person.personId);
        if (found == unique.end()) unique.insert(person.personId, person);
        else {
            if (found->name != person.name)
                return {detail::serverError(QStringLiteral("同一人物标识包含冲突姓名。")), {}};
            found->roles.append(person.roles);
        }
        if (unique.size() > MaxConfirmedPeoplePerWork)
            return {detail::serverError(QStringLiteral("单个作品已确认人物数量超出上限。")), {}};
    }
    auto result = unique.values();
    for (auto& person : result) {
        std::sort(person.roles.begin(), person.roles.end(), [](auto a, auto b) {
            return static_cast<int>(a) < static_cast<int>(b);
        });
        person.roles.erase(std::unique(person.roles.begin(), person.roles.end()), person.roles.end());
    }
    return {{}, result};
}
// >>> DEFENSE-NOTE | samePeople = 比较两份人物关联
// 比较稳定身份、姓名及角色，用于发现同一来源被提供了矛盾关联。
// <<< DEFENSE-NOTE | 本段讲解结束
bool samePeople(const QList<PersonSummary>& a, const QList<PersonSummary>& b) {
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].personId != b[i].personId || a[i].name != b[i].name || a[i].roles != b[i].roles) return false;
    return true;
}
} // namespace

// >>> DEFENSE-NOTE | attachConfirmedPeople = 向当前目录附加可信人物
// 先清空作品原有people，不把未经确认的客户端字段当证据。
// 仅采用在可读三元组集合里的credits；按已确认映射附加人物并设置本人followed状态。
// <<< DEFENSE-NOTE | 本段讲解结束
care_contract::Result<CatalogSnapshot> attachConfirmedPeople(
    const CatalogSnapshot& snapshot, const PersonContext& context) {
    if (!snapshot.dataAvailable || !context.dataAvailable)
        return {detail::serverError(QStringLiteral("当前资源或人物目录不可用。")), {}};
    for (const auto& id : context.followedPeople)
        if (!validPersonId(id)) return {detail::serverError(QStringLiteral("关注记录包含无效人物标识。")), {}};

    CatalogSnapshot result = snapshot;
    result.followedPeople.clear();
    QSet<ResourceKey> readable;
    for (auto& work : result.readableWorks) {
        // A pre-existing people array is not confirmation. Remove it before
        // validating current resource metadata and attaching authoritative data.
        work.people.clear();
        readable.insert(key(work.resource));
    }
    // Validate source collisions without adopting canonicalWorks' deduplicated
    // output: every actual authorized source must survive exact matching below.
    // The warehouse accepts whitespace titles; attaching people must not add
    // the algorithm candidate layer's stricter title requirement.
    const auto current = detail::canonicalWorks(result.readableWorks, false);
    if (!current.status.ok()) return {current.status, {}};

    QMap<ResourceKey, QList<PersonSummary>> mappings;
    QMap<QString, QString> visibleNames;
    for (const auto& credit : context.credits) {
        if (!validate(credit.resource).ok())
            return {detail::serverError(QStringLiteral("人物关联包含无效作品或来源标识。")), {}};
        const auto resourceKey = key(credit.resource);
        if (!readable.contains(resourceKey)) continue;
        const auto people = confirmedPeople(credit.people);
        if (!people.status.ok()) return {people.status, {}};
        const auto existing = mappings.constFind(resourceKey);
        if (existing != mappings.cend() && !samePeople(existing.value(), *people.value))
            return {detail::serverError(QStringLiteral("同一作品来源包含冲突人物关联。")), {}};
        for (const auto& person : *people.value) {
            const auto name = visibleNames.constFind(person.personId);
            if (name != visibleNames.cend() && name.value() != person.name)
                return {detail::serverError(QStringLiteral("可读人物目录中的同一标识包含冲突姓名。")), {}};
            visibleNames.insert(person.personId, person.name);
        }
        mappings.insert(resourceKey, *people.value);
    }
    for (auto& work : result.readableWorks) {
        work.people = mappings.value(key(work.resource));
        for (auto& person : work.people) {
            person.followed = context.followedPeople.contains(person.personId);
            if (person.followed) result.followedPeople.insert(person.personId);
        }
    }
    return {{}, result};
}
// >>> DEFENSE-NOTE | attachConfirmedPeople = 向个人快照附加当前人物
// 复用目录重载，只更新readableWorks/followedPeople；不遍历重写events，历史快照里的空人物也是当时事实。
// <<< DEFENSE-NOTE | 本段讲解结束
care_contract::Result<PersonalSnapshot> attachConfirmedPeople(
    const PersonalSnapshot& snapshot, const PersonContext& context) {
    if (!snapshot.historyAvailable)
        return {detail::serverError(QStringLiteral("个人历史数据不可用。")), {}};
    const auto attached = attachConfirmedPeople(CatalogSnapshot{true, snapshot.readableWorks, {}}, context);
    if (!attached.status.ok()) return {attached.status, {}};
    PersonalSnapshot result = snapshot;
    result.readableWorks = attached.value->readableWorks;
    result.followedPeople = attached.value->followedPeople;
    // No traversal of events: a recorded historical snapshot (even empty) is
    // evidence from that time, and must never acquire today's people or flags.
    return {{}, result};
}
} // namespace care_algorithms
