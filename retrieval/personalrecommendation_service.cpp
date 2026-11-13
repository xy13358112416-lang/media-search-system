#include "personalrecommendation_service.h"
#include "mediarecommendation.h"
#include <QHash>
#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace recommendation_contract {
namespace {
RecommendationResult failure(const QString& error) { return {false, error, {}, {}}; }
bool valid(const PreferenceSelection& choice) {
    switch (choice.mode) {
    case PreferenceMode::Auto:
    case PreferenceMode::Any: return true;
    case PreferenceMode::Explicit: return !choice.value.trimmed().isEmpty();
    }
    return false;
}
struct Interest { quint64 clicks = 0; qint64 lastViewedAt = 0; };
using Interests = QHash<QString, Interest>;
bool accumulate(Interests& interests, const QString& value, const account_contract::MediaHistoryEntry& entry) {
    if (value.isEmpty()) return true;
    auto& interest = interests[value];
    const auto count = static_cast<quint64>(entry.viewCount);
    if (count > std::numeric_limits<quint64>::max() - interest.clicks) return false;
    interest.clicks += count;
    interest.lastViewedAt = std::max(interest.lastViewedAt, entry.lastViewedAt);
    return true;
}
QString favorite(const Interests& interests) {
    QString best;
    Interest score;
    for (auto it = interests.cbegin(); it != interests.cend(); ++it) {
        const auto& candidate = it.value();
        if (best.isEmpty() || candidate.clicks > score.clicks ||
            (candidate.clicks == score.clicks && (candidate.lastViewedAt > score.lastViewedAt ||
             (candidate.lastViewedAt == score.lastViewedAt && it.key() < best)))) {
            best = it.key(); score = candidate;
        }
    }
    return best;
}
QString explanation(const QString& field, const PreferenceSelection& choice, const QString& resolved) {
    if (choice.mode == PreferenceMode::Any) return field + QStringLiteral("：不限");
    if (choice.mode == PreferenceMode::Explicit) return field + QStringLiteral("：按本次选择“") + resolved + QStringLiteral("”");
    if (resolved.isEmpty()) return field + QStringLiteral("：暂无可用历史偏好");
    return field + QStringLiteral("：根据历史浏览偏好选择“") + resolved + QStringLiteral("”");
}
}

PersonalRecommendationServiceImpl::PersonalRecommendationServiceImpl(
    account_contract::AccountHistoryService& accounts, MediaProvider currentMedia)
    : accounts_(accounts), currentMedia_(std::move(currentMedia)) {}

RecommendationResult PersonalRecommendationServiceImpl::recommend(const RecommendationRequest& request) {
    const auto user = accounts_.currentUser();
    if (!user || request.userId <= 0 || user->userId != request.userId)
        return failure(QStringLiteral("请使用当前登录用户请求推荐。"));
    if (!valid(request.category) || !valid(request.fileType))
        return failure(QStringLiteral("推荐偏好模式无效，或指定值为空。"));
    if (request.limit <= 0) return {true, {}, {}, QStringLiteral("推荐数量为零。")};
    if (!currentMedia_) return failure(QStringLiteral("当前媒体数据服务不可用。"));

    QList<MediaRecord> snapshot;
    try { snapshot = currentMedia_(); }
    catch (const std::exception&) { return failure(QStringLiteral("读取当前媒体数据失败。")); }
    const QList<MediaRecord> records = std::move(snapshot);
    if (records.isEmpty()) return {true, {}, {}, QStringLiteral("媒体库暂无可推荐记录。")};
    QHash<QString, const MediaRecord*> byId;
    byId.reserve(records.size());
    for (const auto& record : records) {
        if (record.id.isEmpty() || byId.contains(record.id))
            return failure(QStringLiteral("媒体数据包含无效或重复的编号。"));
        byId.insert(record.id, &record);
    }

    Interests categories, types;
    if (request.category.mode == PreferenceMode::Auto || request.fileType.mode == PreferenceMode::Auto) {
        const auto history = accounts_.loadMediaHistory(request.userId);
        if (!history.status.ok() || !history.value)
            return failure(history.status.message.isEmpty() ? QStringLiteral("读取用户浏览历史失败。") : history.status.message);
        for (const auto& entry : *history.value) {
            const auto found = byId.constFind(entry.mediaId);
            if (found == byId.cend()) continue; // Deleted media no longer contributes to interests.
            if (entry.viewCount < 0) return failure(QStringLiteral("浏览历史包含无效次数。"));
            if (entry.viewCount == 0) continue;
            const auto& record = **found;
            if ((request.category.mode == PreferenceMode::Auto &&
                 !accumulate(categories, record.category.trimmed(), entry)) ||
                (request.fileType.mode == PreferenceMode::Auto &&
                 !accumulate(types, record.fileType.trimmed().toUpper(), entry)))
                return failure(QStringLiteral("累计浏览次数超出支持范围。"));
        }
    }

    const auto resolve = [](const PreferenceSelection& choice, const Interests& interests) {
        if (choice.mode == PreferenceMode::Explicit) return choice.value.trimmed();
        return choice.mode == PreferenceMode::Auto ? favorite(interests) : QString();
    };
    RecommendationOptions options{resolve(request.category, categories),
                                  resolve(request.fileType, types).toUpper(), request.limit};
    const QString reason = explanation(QStringLiteral("类别"), request.category, options.preferredCategory)
        + QStringLiteral("；") + explanation(QStringLiteral("文件类型"), request.fileType, options.preferredFileType)
        + QStringLiteral("；结合媒体库热度排序。");
    return {true, {}, recommendMedia(records, options), reason};
}
} // namespace recommendation_contract
