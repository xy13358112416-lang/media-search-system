// >>> DEFENSE-NOTE | 文件导读｜单机今日推荐服务
// 阅读路线：请求偏好候选30条 → 空候选热度回退 → 日期种子 → 每日洗牌
// 联调关系：组合 PersonalRecommendationService 和 dailyPick；在线今日推荐在 frontend/online_window.cpp 调用服务器时序候选后复用 dailyPick。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | recommendToday = 生成单机今日推荐
// 限制数量后构造候选请求，传递用户与偏好。候选获取失败直接报错，不能把失败当无历史。
// <<< DEFENSE-NOTE | 本段讲解结束
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

    // >>> DEFENSE-NOTE | 步骤 01｜获取候选；成功但为空时才考虑热度回退
    // 只有 base 成功后才能走此分支；currentMedia_ 提供当前库快照，热度回退仍限制为30。
    // <<< DEFENSE-NOTE | 本段讲解结束
    QStringList pool = base.mediaIds;
    if (pool.isEmpty() && currentMedia_) {
        // 候选池为空（例如冷启动或库为空）时，回退为全库热度排序。
        pool = recommendMedia(currentMedia_(), RecommendationOptions{{}, {}, kPoolSize});
    }

    // >>> DEFENSE-NOTE | 步骤 02｜选择种子并复用洗牌
    // 非零 request.seed 用于可复现实验；否则按本机当天日期转JulianDay。
    // 相同输入与种子输出一致；不同日期不保证所有作品都换新。在线路径使用北京时间日期。
    // <<< DEFENSE-NOTE | 本段讲解结束
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
