#include "care_contract.h"
#include <limits>

namespace care_contract {
namespace {
Status invalid(const char* reason) {
    return {warehouse_contract::ErrorCode::InvalidArgument, QString::fromUtf8(reason)};
}
bool identifier(const QString& value) {
    if (value.isEmpty() || value.size() > MaxIdentifierUnits) return false;
    for (const auto character : value)
        if (character.isSpace() || character.isNull() || character.category() == QChar::Other_Control) return false;
    return true;
}
bool preference(const recommendation_contract::PreferenceSelection& selection) {
    using M = recommendation_contract::PreferenceMode;
    switch (selection.mode) {
    case M::Auto: case M::Any: return true; // Value is intentionally ignored in these modes.
    case M::Explicit: return !selection.value.trimmed().isEmpty() && selection.value.size() <= MaxIdentifierUnits;
    }
    return false;
}
}
Status validate(const ResourceRef& value) {
    if (!identifier(value.workId)) return invalid("作品标识无效。");
    return validate(value.source);
}
Status validate(const WarehouseItemRef& value) {
    if (value.warehouseId <= 0 || value.itemId <= 0) return invalid("来源仓库条目标识无效。");
    return {};
}
Status validate(const BehaviorEvent& value) {
    if (!identifier(value.eventId)) return invalid("行为事件标识无效。");
    switch (value.action) {
    case BehaviorAction::View:
        if (!value.resource || !value.query.isEmpty()) return invalid("浏览事件需要资源引用，且不能包含搜索词。");
        return validate(*value.resource);
    case BehaviorAction::Search:
        if (value.resource || value.query.trimmed().isEmpty() || value.query.size() > MaxQueryUnits)
            return invalid("搜索事件需要非空搜索词，且不能包含资源引用。");
        return {};
    }
    return invalid("行为动作无效。");
}
Status validate(const TimeRange& value) {
    if (value.beginUtc < 0 || value.endUtc <= value.beginUtc || value.endUtc > MaxUtcExclusive)
        return invalid("时间范围必须为有效UTC秒级左闭右开区间。");
    return {};
}
Status validate(const PageRequest& value) {
    if (value.offset < 0 || value.offset > std::numeric_limits<qint64>::max() - MaxPageSize ||
        value.limit <= 0 || value.limit > MaxPageSize) return invalid("分页范围无效。");
    return {};
}
Status validatePersonId(const QString& personId) {
    return identifier(personId) ? Status{} : invalid("人物标识无效。");
}
Status validate(const PersonUpdate& value) {
    auto status = validatePersonId(value.personId);
    if (!status.ok()) return status;
    const auto timestamp = [](qint64 seconds) { return seconds >= 0 && seconds < MaxUtcExclusive; };
    if (!identifier(value.updateId) || value.title.trimmed().isEmpty() ||
        (value.kind != UpdateKind::News && value.kind != UpdateKind::Release) ||
        !value.sourceUrl.isValid() || value.sourceUrl.scheme() != "https" ||
        value.sourceUrl.host().isEmpty() || !value.sourceUrl.userInfo().isEmpty() ||
        !timestamp(value.fetchedAtUtc) ||
        (value.publishedAtUtc && !timestamp(*value.publishedAtUtc)) ||
        (value.eventAtUtc && !timestamp(*value.eventAtUtc)))
        return invalid("人物资讯标识、来源或日期无效。");
    return {};
}
Status validate(const PersonUpdatesRequest& value) {
    auto status = validatePersonId(value.personId);
    if (!status.ok()) return status;
    status = validate(value.range); if (!status.ok()) return status;
    return validate(value.page);
}
Status validate(const TrendingRequest& value) {
    if ((value.kind != TrendingKind::Works && value.kind != TrendingKind::SearchTerms) ||
        value.limit <= 0 || value.limit > MaxPageSize) return invalid("热榜类型或数量无效。");
    return validate(value.range);
}
Status validate(const FootprintRequest& value) {
    auto status = validate(value.range); if (!status.ok()) return status;
    if (value.comparison) {
        status = validate(*value.comparison); if (!status.ok()) return status;
        if (value.comparison->endUtc > value.range.beginUtc) return invalid("比较区间必须在当前区间之前，且不能重叠。");
    }
    return {};
}
Status validate(const TemporalRecommendationRequest& value) {
    if (value.limit <= 0 || value.limit > MaxRecommendationLimit || !preference(value.category) || !preference(value.fileType))
        return invalid("推荐数量或偏好无效。");
    switch (value.scene) {
    case BrowseScene::Neutral: case BrowseScene::Relax: case BrowseScene::Focus: case BrowseScene::Explore: return {};
    }
    return invalid("浏览场景无效。");
}
Status validate(const WorkFeedbackRequest& value) {
    if (value.feedback != WorkFeedbackKind::Neutral && value.feedback != WorkFeedbackKind::NotInterested)
        return invalid("作品反馈类型无效。");
    return validate(value.resource);
}
Status validate(const PersonSearchRequest& value) {
    if (value.query.trimmed().isEmpty() || value.query.size() > MaxQueryUnits)
        return invalid("人物搜索词无效。");
    return validate(value.page);
}
} // namespace care_contract
