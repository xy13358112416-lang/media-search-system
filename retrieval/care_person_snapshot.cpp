#include "care_person_snapshot.h"
#include "care_algorithm_common.h"
#include <QMap>
#include <algorithm>

namespace care_algorithms {
namespace {
using namespace care_contract;
using ResourceKey = QPair<QString, QPair<qint64, qint64>>;
ResourceKey key(const ResourceRef& resource) {
    return {resource.workId, {resource.source.warehouseId, resource.source.itemId}};
}
bool validPersonId(const QString& id) {
    // Reuse the public contract's identifier rules rather than invent a weaker
    // rule for trusted-store records or follow entries.
    return validate(PersonUpdatesRequest{id, {0, 1}, {}}).ok();
}
bool validName(const QString& name) {
    if (name.simplified().isEmpty() || name.size() > MaxQueryUnits) return false;
    for (const auto ch : name)
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
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
bool samePeople(const QList<PersonSummary>& a, const QList<PersonSummary>& b) {
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].personId != b[i].personId || a[i].name != b[i].name || a[i].roles != b[i].roles) return false;
    return true;
}
} // namespace

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
