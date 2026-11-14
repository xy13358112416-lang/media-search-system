#pragma once

#include "personalrecommendation_service.h"
#include "todayrecommendation_contract.h"
#include <functional>

namespace recommendation_contract {

// 今日推荐实现：组合现有 PersonalRecommendationService（取偏好排序候选池）
// 与 dailyPick（按日期种子每日洗牌）。不拥有 base / currentMedia，两者需比本对象长寿。
class TodayRecommendationServiceImpl final : public TodayRecommendationService {
public:
    using MediaProvider = std::function<QList<MediaRecord>()>;
    TodayRecommendationServiceImpl(PersonalRecommendationService& base, MediaProvider currentMedia);
    TodayRecommendationResult recommendToday(const TodayRecommendationRequest& request) override;

private:
    PersonalRecommendationService& base_;
    MediaProvider currentMedia_;
};

} // namespace recommendation_contract
