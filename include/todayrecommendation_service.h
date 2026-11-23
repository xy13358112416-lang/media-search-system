// >>> DEFENSE-NOTE | 头文件导读｜单机今日服务的依赖组合
// base_生成偏好候选，currentMedia_用于成功空候选的热度回退，dailyPick完成洗牌。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
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
