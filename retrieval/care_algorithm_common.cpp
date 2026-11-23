// >>> DEFENSE-NOTE | 文件导读｜算法共享的输入清洗与去重
// 阅读路线：文本规范 → 身份与来源一致性 → 同事件去重 → 时间排序
// 联调关系：足迹、热榜、时序、人物算法共用；调用者提供真实授权快照，清洗层不能取代服务器访问控制。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "care_algorithm_common.h"
#include "care_casefold_expansions.h"
#include <QMap>
#include <algorithm>

namespace care_algorithms::detail {
using namespace care_contract;
// >>> DEFENSE-NOTE | serverError = 统一数据异常状态
// 构造ServerError供上层返回；不要吞掉损坏历史再宣称冷启动成功。
// <<< DEFENSE-NOTE | 本段讲解结束
Status serverError(const QString& message) { return {warehouse_contract::ErrorCode::ServerError, message}; }
// >>> DEFENSE-NOTE | validateNow = 校验当前时间
// UTC秒必须在共享契约支持范围内，避免无效日期传入衰减或自然日分桶。
// <<< DEFENSE-NOTE | 本段讲解结束
Status validateNow(qint64 nowUtc) {
    return nowUtc >= 0 && nowUtc < MaxUtcExclusive ? Status{} : serverError(QStringLiteral("服务器时间无效。"));
}
// >>> DEFENSE-NOTE | normalizeText = 统一文本
// NFKC兼容规范化、Unicode大小写折叠、合并空白；扩展表处理一个字符映射多个字符的情况。
// 按已排序码点表二分查找扩展；它与精确检索的人名规则不是同一套模糊匹配承诺。
// <<< DEFENSE-NOTE | 本段讲解结束
QString normalizeText(const QString& text) {
    QString result;
    for (const auto code : text.normalized(QString::NormalizationForm_KC).toUcs4()) {
        const auto found = std::lower_bound(CaseFoldExpansions.begin(), CaseFoldExpansions.end(), code,
            [](const auto& entry, char32_t value) { return entry.code < value; });
        if (found != CaseFoldExpansions.end() && found->code == code) result += QString::fromUcs4(found->replacement);
        else { const auto folded = QChar::toCaseFolded(code); result += QString::fromUcs4(&folded, 1); }
    }
    return result.simplified();
}
namespace {
// >>> DEFENSE-NOTE | validId = 校验稳定标识
// 限制长度并拒绝空白、空字符和控制字符；不是按名字猜测人物身份。
// <<< DEFENSE-NOTE | 本段讲解结束
bool validId(const QString& text) {
    if (text.isEmpty() || text.size() > MaxIdentifierUnits) return false;
    for (const auto ch : text) if (ch.isSpace() || ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
// >>> DEFENSE-NOTE | sameRef = 比较作品及精确来源
// 同时比较 workId、warehouseId、itemId；同作品不同来源不自动视作同一个来源位置。
// <<< DEFENSE-NOTE | 本段讲解结束
bool sameRef(const ResourceRef& a, const ResourceRef& b) {
    return a.workId == b.workId && a.source.warehouseId == b.source.warehouseId && a.source.itemId == b.source.itemId;
}
// >>> DEFENSE-NOTE | sameWork = 比较完整作品快照
// 检查媒体字段及人物列表；同来源发生冲突不能随意选一份静默覆盖。
// <<< DEFENSE-NOTE | 本段讲解结束
bool sameWork(const ResourceSummary& a, const ResourceSummary& b) {
    const auto& x = a.media; const auto& y = b.media;
    if (!sameRef(a.resource, b.resource) || x.id != y.id || x.fileName != y.fileName || x.category != y.category ||
        x.fileType != y.fileType || x.fileSize != y.fileSize || x.viewCount != y.viewCount || x.director != y.director ||
        x.actors != y.actors || x.awards != y.awards || x.releaseDate != y.releaseDate || a.people.size() != b.people.size()) return false;
    for (qsizetype i = 0; i < a.people.size(); ++i) {
        const auto& p = a.people[i]; const auto& q = b.people[i];
        if (p.personId != q.personId || p.name != q.name || p.roles != q.roles || p.followed != q.followed) return false;
    }
    return true;
}
// >>> DEFENSE-NOTE | checkWork = 校验作品快照
// 检查资源标识、可选标题约束和人物身份角色。requireTitle=false 允许仓库已接受的无标题情形进入特定适配验证。
// <<< DEFENSE-NOTE | 本段讲解结束
Status checkWork(const ResourceSummary& work, bool requireTitle) {
    if (!validate(work.resource).ok() || (requireTitle && work.media.fileName.trimmed().isEmpty()))
        return serverError(QStringLiteral("资源快照包含无效标识或标题。"));
    for (const auto& person : work.people) {
        if (!validId(person.personId) || person.name.trimmed().isEmpty()) return serverError(QStringLiteral("人物快照包含无效身份。"));
        for (const auto role : person.roles)
            if (role != PersonRole::Actor && role != PersonRole::Director && role != PersonRole::Musician)
                return serverError(QStringLiteral("人物角色无效。"));
    }
    return {};
}
// >>> DEFENSE-NOTE | sameEvent = 判定重复事件是否内容一致
// 相同事件编号只允许是同一动作、时间和资源；否则是冲突数据。
// <<< DEFENSE-NOTE | 本段讲解结束
bool sameEvent(const ObservedBehavior& a, const ObservedBehavior& b) {
    const auto& x = a.record.event; const auto& y = b.record.event;
    return x.eventId == y.eventId && x.action == y.action && x.query == y.query &&
        x.resource.has_value() == y.resource.has_value() && (!x.resource || sameRef(*x.resource, *y.resource)) &&
        a.record.occurredAtUtc == b.record.occurredAtUtc;
}
// >>> DEFENSE-NOTE | collect = 按身份和事件ID清洗行为
// QMap以(userKey,eventId)去重；个人路径排除其他用户，公共路径验证publicEligible和精确来源。
// 未来事件跳过，损坏时间/冲突内容报错；相同事件合并模拟标记和可用快照，最后按时间/用户/事件ID排序。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<QList<ObservedBehavior>> collect(const QList<ObservedBehavior>& input, qint64 now, const QString* user,
                                      const QMap<QPair<qint64, qint64>, QString>* publicSources) {
    QMap<QPair<QString, QString>, ObservedBehavior> unique;
    for (const auto& row : input) {
        if (user && row.userKey != *user) continue;
        if (publicSources && !row.publicEligible) continue;
        if (row.record.occurredAtUtc < 0 || row.record.occurredAtUtc >= MaxUtcExclusive)
            return {serverError(QStringLiteral("行为时间无效。")), {}};
        if (row.record.occurredAtUtc > now) continue;
        if (!validId(row.userKey) || !validate(row.record.event).ok())
            return {serverError(QStringLiteral("行为快照包含无效身份或字段。")), {}};
        if (publicSources && row.record.event.resource) {
            const auto& ref = *row.record.event.resource;
            if (publicSources->value({ref.source.warehouseId, ref.source.itemId}) != ref.workId) continue;
        }
        if (row.workSnapshot) {
            if (!row.record.event.resource || !sameRef(*row.record.event.resource, row.workSnapshot->resource))
                return {serverError(QStringLiteral("历史作品快照与行为资源不一致。")), {}};
            const auto status = checkWork(*row.workSnapshot, false);
            if (!status.ok()) return {status, {}};
        }
        const auto key = qMakePair(row.userKey, row.record.event.eventId);
        auto found = unique.find(key);
        if (found == unique.end()) { unique.insert(key, row); continue; }
        if (!sameEvent(found.value(), row) || (found->workSnapshot && row.workSnapshot && !sameWork(*found->workSnapshot, *row.workSnapshot)))
            return {serverError(QStringLiteral("相同事件编号包含冲突数据。")), {}};
        found->record.synthetic = found->record.synthetic || row.record.synthetic;
        if (!found->workSnapshot && row.workSnapshot) found->workSnapshot = row.workSnapshot;
    }
    auto events = unique.values();
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        if (a.record.occurredAtUtc != b.record.occurredAtUtc) return a.record.occurredAtUtc < b.record.occurredAtUtc;
        if (a.userKey != b.userKey) return a.userKey < b.userKey;
        return a.record.event.eventId < b.record.event.eventId;
    });
    return {{}, events};
}
}
// >>> DEFENSE-NOTE | canonicalWorks = 生成规范作品候选
// 检查同一仓库条目是否有冲突内容，然后按workId去重并稳定选择来源位置较小的副本。
// 同名不等于同作品；去重使用已确认workId。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<QList<ResourceSummary>> canonicalWorks(const QList<ResourceSummary>& works, bool requireTitle) {
    QMap<QString, ResourceSummary> canonical;
    QMap<QPair<qint64, qint64>, ResourceSummary> sources;
    for (const auto& work : works) {
        const auto status = checkWork(work, requireTitle); if (!status.ok()) return {status, {}};
        const auto source = qMakePair(work.resource.source.warehouseId, work.resource.source.itemId);
        if (sources.contains(source) && !sameWork(sources.value(source), work))
            return {serverError(QStringLiteral("同一来源位置包含冲突作品。")), {}};
        sources.insert(source, work);
        auto found = canonical.find(work.resource.workId);
        if (found == canonical.end() || source < qMakePair(found->resource.source.warehouseId, found->resource.source.itemId))
            canonical.insert(work.resource.workId, work);
    }
    return {{}, canonical.values()};
}
// >>> DEFENSE-NOTE | personalEvents = 提取个人有效历史
// 检查当前时间、historyAvailable与身份，再调用collect限制为该用户。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<QList<ObservedBehavior>> personalEvents(const PersonalSnapshot& snapshot) {
    const auto status = validateNow(snapshot.nowUtc); if (!status.ok()) return {status, {}};
    if (!snapshot.historyAvailable || !validId(snapshot.userKey)) return {serverError(QStringLiteral("个人历史数据不可用。")), {}};
    return collect(snapshot.events, snapshot.nowUtc, &snapshot.userKey, nullptr);
}
// >>> DEFENSE-NOTE | publicEvents = 提取公共合格事件
// 从当前公开作品建立(仓库,条目)→workId映射，再让collect检查事件来源。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<QList<ObservedBehavior>> publicEvents(const PublicSnapshot& snapshot) {
    const auto status = validateNow(snapshot.nowUtc); if (!status.ok()) return {status, {}};
    if (!snapshot.dataAvailable) return {serverError(QStringLiteral("公共统计数据不可用。")), {}};
    const auto works = canonicalWorks(snapshot.publicWorks); if (!works.status.ok()) return {works.status, {}};
    QMap<QPair<qint64, qint64>, QString> sources;
    for (const auto& work : snapshot.publicWorks)
        sources.insert({work.resource.source.warehouseId, work.resource.source.itemId}, work.resource.workId);
    return collect(snapshot.events, snapshot.nowUtc, nullptr, &sources);
}
} // namespace care_algorithms::detail
