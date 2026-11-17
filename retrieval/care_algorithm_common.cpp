#include "care_algorithm_common.h"
#include "care_casefold_expansions.h"
#include <QMap>
#include <algorithm>

namespace care_algorithms::detail {
using namespace care_contract;
Status serverError(const QString& message) { return {warehouse_contract::ErrorCode::ServerError, message}; }
Status validateNow(qint64 nowUtc) {
    return nowUtc >= 0 && nowUtc < MaxUtcExclusive ? Status{} : serverError(QStringLiteral("服务器时间无效。"));
}
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
bool validId(const QString& text) {
    if (text.isEmpty() || text.size() > MaxIdentifierUnits) return false;
    for (const auto ch : text) if (ch.isSpace() || ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
bool sameRef(const ResourceRef& a, const ResourceRef& b) {
    return a.workId == b.workId && a.source.warehouseId == b.source.warehouseId && a.source.itemId == b.source.itemId;
}
bool sameWork(const ResourceSummary& a, const ResourceSummary& b) {
    const auto& x = a.media; const auto& y = b.media;
    if (!sameRef(a.resource, b.resource) || x.id != y.id || x.fileName != y.fileName || x.category != y.category ||
        x.fileType != y.fileType || x.fileSize != y.fileSize || x.viewCount != y.viewCount || x.creator != y.creator ||
        x.actors != y.actors || x.awards != y.awards || x.releaseDate != y.releaseDate || a.people.size() != b.people.size()) return false;
    for (qsizetype i = 0; i < a.people.size(); ++i) {
        const auto& p = a.people[i]; const auto& q = b.people[i];
        if (p.personId != q.personId || p.name != q.name || p.roles != q.roles || p.followed != q.followed) return false;
    }
    return true;
}
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
bool sameEvent(const ObservedBehavior& a, const ObservedBehavior& b) {
    const auto& x = a.record.event; const auto& y = b.record.event;
    return x.eventId == y.eventId && x.action == y.action && x.query == y.query &&
        x.resource.has_value() == y.resource.has_value() && (!x.resource || sameRef(*x.resource, *y.resource)) &&
        a.record.occurredAtUtc == b.record.occurredAtUtc;
}
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
Result<QList<ObservedBehavior>> personalEvents(const PersonalSnapshot& snapshot) {
    const auto status = validateNow(snapshot.nowUtc); if (!status.ok()) return {status, {}};
    if (!snapshot.historyAvailable || !validId(snapshot.userKey)) return {serverError(QStringLiteral("个人历史数据不可用。")), {}};
    return collect(snapshot.events, snapshot.nowUtc, &snapshot.userKey, nullptr);
}
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
