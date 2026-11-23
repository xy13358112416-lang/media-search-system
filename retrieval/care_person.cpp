// >>> DEFENSE-NOTE | 文件导读｜人物检索、作品关联和关注建议
// 阅读路线：可信目录 → 名称/别名搜索 → 人物作品 → 历史建议 → 资讯分页
// 联调关系：服务端仓储提供已授权人物关联与缓存；算法不凭演员名字生成真实人物ID，不直接写关注表。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "care_person.h"
#include "care_algorithm_common.h"
#include <QMap>
#include <algorithm>

namespace care_algorithms {
namespace {
using namespace care_contract;
// >>> DEFENSE-NOTE | validPersonId = 复用契约验证身份
// 使用同一公共规则检查人物ID，避免导入和查询采用两套不兼容规则。
// <<< DEFENSE-NOTE | 本段讲解结束
bool validPersonId(const QString& id) {
    return validate(PersonUpdatesRequest{id, {0, 1}, {}}).ok();
}
// >>> DEFENSE-NOTE | pageOf = 内存分页
// offset 是起始位置，limit是上限；超过末尾返回空页，有剩余时填写nextOffset。
// <<< DEFENSE-NOTE | 本段讲解结束
template<class T> Page<T> pageOf(const QList<T>& items, const PageRequest& page) {
    Page<T> result;
    if (page.offset >= items.size()) return result;
    result.items = items.mid(page.offset, page.limit);
    if (page.offset + result.items.size() < items.size()) result.nextOffset = page.offset + result.items.size();
    return result;
}
// >>> DEFENSE-NOTE | Directory = 当前可见人物目录
// 作品列表、personId→人物、personId→规范化别名集合；QMap保持稳定遍历顺序。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Directory {
    QList<ResourceSummary> works;
    QMap<QString, PersonSummary> people;
    QMap<QString, QSet<QString>> aliases;
};
// >>> DEFENSE-NOTE | directory = 整理可信目录
// 先验证作品，再按personId合并角色与别名；关注状态来自当前用户follow集合，不采信旧展示标记。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<Directory> directory(const CatalogSnapshot& snapshot) {
    if (!snapshot.dataAvailable) return {detail::serverError(QStringLiteral("人物目录不可用。")), {}};
    const auto canonical = detail::canonicalWorks(snapshot.readableWorks);
    if (!canonical.status.ok()) return {canonical.status, {}};
    Directory result; result.works = *canonical.value;
    for (auto& work : result.works) {
        for (auto& person : work.people) {
            person.followed = snapshot.followedPeople.contains(person.personId);
            result.aliases[person.personId].insert(detail::normalizeText(person.name));
            auto found = result.people.find(person.personId);
            if (found == result.people.end()) result.people.insert(person.personId, person);
            else {
                if (person.name < found->name) found->name = person.name;
                for (const auto role : person.roles) if (!found->roles.contains(role)) found->roles.append(role);
            }
        }
    }
    for (auto& person : result.people) {
        const auto aliases = snapshot.aliasesByPerson.constFind(person.personId);
        if (aliases != snapshot.aliasesByPerson.cend()) {
            for (const auto& name : aliases.value()) {
                const auto alias = detail::normalizeText(name);
                if (!alias.isEmpty())
                    result.aliases[person.personId].insert(alias);
            }
        }
        std::sort(person.roles.begin(), person.roles.end(), [](const auto a, const auto b) { return static_cast<int>(a) < static_cast<int>(b); });
        person.roles.erase(std::unique(person.roles.begin(), person.roles.end()), person.roles.end());
    }
    return {{}, result};
}
}
// >>> DEFENSE-NOTE | searchPeople = 按名称或别名检索人物
// 规范化query后在可见别名中做包含匹配；返回人物分页，不会因为同名就合并ID。
// <<< DEFENSE-NOTE | 本段讲解结束
care_contract::Result<care_contract::Page<care_contract::PersonSummary>> searchPeople(
    const care_contract::PersonSearchRequest& request, const CatalogSnapshot& snapshot) {
    const auto status = validate(request); if (!status.ok()) return {status, {}};
    const auto data = directory(snapshot); if (!data.status.ok()) return {data.status, {}};
    const auto query = detail::normalizeText(request.query);
    QList<PersonSummary> matches;
    for (auto it = data.value->people.cbegin(); it != data.value->people.cend(); ++it)
        for (const auto& alias : data.value->aliases.value(it.key()))
            if (alias.contains(query)) { matches.append(it.value()); break; }
    return {{}, pageOf(matches, request.page)};
}
// >>> DEFENSE-NOTE | worksForPerson = 查询人物参与作品
// 校验人物ID与页参数，仅遍历已授权目录中包含该personId的作品，不扩大仓库权限。
// <<< DEFENSE-NOTE | 本段讲解结束
care_contract::Result<care_contract::Page<care_contract::ResourceSummary>> worksForPerson(
    const QString& personId, const care_contract::PageRequest& page, const CatalogSnapshot& snapshot) {
    if (!validPersonId(personId)) return {{warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("人物标识无效。")}, {}};
    const auto status = validate(page); if (!status.ok()) return {status, {}};
    const auto data = directory(snapshot); if (!data.status.ok()) return {data.status, {}};
    if (!data.value->people.contains(personId)) return {{warehouse_contract::ErrorCode::NotFound, QStringLiteral("没有可访问的人物记录。")}, {}};
    QList<ResourceSummary> matches;
    for (const auto& work : data.value->works)
        for (const auto& person : work.people)
            if (person.personId == personId) { matches.append(work); break; }
    return {{}, pageOf(matches, page)};
}
// >>> DEFENSE-NOTE | suggestPeople = 从历史建议关注人物
// 只看浏览，单次浏览按personId去重；排除已关注和当前不可见人物。
// 先按涉及浏览次数降序，再按最近浏览时间和稳定ID；关闭个性化时返回空建议。
// <<< DEFENSE-NOTE | 本段讲解结束
care_contract::Result<QList<care_contract::PersonSummary>> suggestPeople(int limit, const PersonalSnapshot& snapshot) {
    if (limit <= 0 || limit > MaxRecommendationLimit)
        return {{warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("人物建议数量无效。")}, {}};
    const auto events = detail::personalEvents(snapshot); if (!events.status.ok()) return {events.status, {}};
    const auto data = directory({true, snapshot.readableWorks, snapshot.followedPeople});
    if (!data.status.ok()) return {data.status, {}};
    if (!snapshot.settings.personalizationEnabled) return {{}, QList<PersonSummary>{}};
    QHash<QString, ResourceSummary> current;
    for (const auto& work : data.value->works) current.insert(work.resource.workId, work);
    struct Interest { QString id; qint64 count = 0; qint64 last = 0; };
    QMap<QString, Interest> scores;
    for (const auto& event : *events.value) {
        if (event.record.event.action != BehaviorAction::View) continue;
        const auto& id = event.record.event.resource->workId;
        const auto fallback = current.constFind(id);
        const auto* work = event.workSnapshot ? &*event.workSnapshot : fallback == current.cend() ? nullptr : &fallback.value();
        if (!work) continue;
        QSet<QString> credited;
        for (const auto& person : work->people) {
            if (credited.contains(person.personId) || snapshot.followedPeople.contains(person.personId) || !data.value->people.contains(person.personId)) continue;
            credited.insert(person.personId);
            auto& interest = scores[person.personId]; interest.id = person.personId;
            ++interest.count; interest.last = std::max(interest.last, event.record.occurredAtUtc);
        }
    }
    auto ranked = scores.values();
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.last != b.last) return a.last > b.last;
        return a.id < b.id;
    });
    QList<PersonSummary> result;
    for (const auto& row : ranked) { result.append(data.value->people.value(row.id)); if (result.size() >= limit) break; }
    return {{}, result};
}
// >>> DEFENSE-NOTE | personUpdates = 校验并分页缓存资讯
// 只处理目标人物的缓存；验证HTTPS来源和日期，以updateId去重，同时间冲突报错。
// 按活动/发表/抓取时间过滤，再按已知日期优先、日期降序、ID排序；这里不调用Search/Fetch。
// <<< DEFENSE-NOTE | 本段讲解结束
care_contract::Result<care_contract::Page<care_contract::PersonUpdate>> personUpdates(
    const care_contract::PersonUpdatesRequest& request, const CatalogSnapshot& snapshot,
    const QList<care_contract::PersonUpdate>& cache, qint64 nowUtc, bool cacheAvailable) {
    auto status = validate(request); if (!status.ok()) return {status, {}};
    status = detail::validateNow(nowUtc); if (!status.ok()) return {status, {}};
    const auto data = directory(snapshot); if (!data.status.ok()) return {data.status, {}};
    if (!data.value->people.contains(request.personId)) return {{warehouse_contract::ErrorCode::NotFound, QStringLiteral("没有可访问的人物记录。")}, {}};
    if (!cacheAvailable) return {detail::serverError(QStringLiteral("人物资讯缓存不可用。")), {}};
    QMap<QString, PersonUpdate> unique;
    for (const auto& row : cache) {
        if (row.personId != request.personId) continue;
        const auto validTime = [](qint64 time) { return time >= 0 && time < MaxUtcExclusive; };
        if (!validPersonId(row.updateId) || row.title.trimmed().isEmpty() ||
            (row.kind != UpdateKind::News && row.kind != UpdateKind::Release) ||
            !row.sourceUrl.isValid() || row.sourceUrl.scheme() != "https" || row.sourceUrl.host().isEmpty() || !row.sourceUrl.userInfo().isEmpty() ||
            !validTime(row.fetchedAtUtc) || row.fetchedAtUtc > nowUtc ||
            (row.publishedAtUtc && (!validTime(*row.publishedAtUtc) || *row.publishedAtUtc > nowUtc)) ||
            (row.eventAtUtc && !validTime(*row.eventAtUtc)))
            return {detail::serverError(QStringLiteral("人物资讯包含无效来源或日期。")), {}};
        auto found = unique.find(row.updateId);
        if (found == unique.end() || found->fetchedAtUtc < row.fetchedAtUtc) unique.insert(row.updateId, row);
        else if (found->fetchedAtUtc == row.fetchedAtUtc && (found->title != row.title || found->sourceUrl != row.sourceUrl ||
                 found->kind != row.kind || found->publishedAtUtc != row.publishedAtUtc || found->eventAtUtc != row.eventAtUtc))
            return {detail::serverError(QStringLiteral("相同资讯编号包含冲突内容。")), {}};
        else if (found->fetchedAtUtc == row.fetchedAtUtc) found->stale = found->stale || row.stale;
    }
    auto rows = unique.values();
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& row) {
        const auto effective = row.eventAtUtc.value_or(row.publishedAtUtc.value_or(row.fetchedAtUtc));
        return effective < request.range.beginUtc || effective >= request.range.endUtc;
    }), rows.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        const auto x = a.eventAtUtc ? a.eventAtUtc : a.publishedAtUtc;
        const auto y = b.eventAtUtc ? b.eventAtUtc : b.publishedAtUtc;
        if (x.has_value() != y.has_value()) return x.has_value();
        if (x && *x != *y) return *x > *y;
        return a.updateId < b.updateId;
    });
    return {{}, pageOf(rows, request.page)};
}
} // namespace care_algorithms
