#include "todayrecommendation_service.h"
#include "daily_pick.h"
#include "mediarecommendation.h"
#include <QDate>
#include <utility>

namespace recommendation_contract {
namespace {
constexpr int kPoolSize = 30; // 先取一个较大的候选池，再做每日洗牌。
}

TodayRecommendationServiceImpl::TodayRecommendationServiceImpl(
    PersonalRecommendationService& base, MediaProvider currentMedia)
    : base_(base), currentMedia_(std::move(currentMedia)) {}

TodayRecommendationResult TodayRecommendationServiceImpl::recommendToday(
    const TodayRecommendationRequest& request) {
    const QString dateKey = QDate::currentDate().toString(Qt::ISODate);
    if (request.limit <= 0) {
        return {true, {}, {}, QStringLiteral("推荐数量为零。"), dateKey};
    }

    RecommendationRequest poolRequest;
    poolRequest.userId = request.userId;
    poolRequest.category = request.category;
    poolRequest.fileType = request.fileType;
    poolRequest.limit = kPoolSize;

    const RecommendationResult base = base_.recommend(poolRequest);
    if (!base.success) {
        return {false, base.error, {}, {}, dateKey};
    }

    QStringList pool = base.mediaIds;
    if (pool.isEmpty() && currentMedia_) {
        // 候选池为空（例如冷启动或库为空）时，回退为全库热度排序。
        pool = recommendMedia(currentMedia_(), RecommendationOptions{{}, {}, kPoolSize});
    }

    const quint64 seed = request.seed != 0
        ? request.seed
        : static_cast<quint64>(QDate::currentDate().toJulianDay());
    const QStringList picked = dailyPick(pool, seed, request.limit);

    QString reason = QStringLiteral("今日推荐 · %1 · 每日刷新。").arg(dateKey);
    if (!base.reason.isEmpty()) {
        reason += QStringLiteral(" 依据：") + base.reason;
    }
    return {true, {}, picked, reason, dateKey};
}
} // namespace recommendation_contract
