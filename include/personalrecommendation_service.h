// >>> DEFENSE-NOTE | 头文件导读｜单机历史推荐服务实现声明
// 账户服务与媒体提供函数由主程序注入；引用不转移所有权，两者必须比本服务活得久。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "account_history_contract.h"
#include "personalrecommendation_contract.h"
#include <functional>

namespace recommendation_contract {

// Does not own either dependency. The account service and captured media library
// must outlive this object; call synchronously on their owning application thread.
class PersonalRecommendationServiceImpl final : public PersonalRecommendationService {
public:
    using MediaProvider = std::function<QList<MediaRecord>()>;
    PersonalRecommendationServiceImpl(account_contract::AccountHistoryService& accounts,
                                      MediaProvider currentMedia);
    RecommendationResult recommend(const RecommendationRequest& request) override;
private:
    account_contract::AccountHistoryService& accounts_;
    MediaProvider currentMedia_;
};

} // namespace recommendation_contract
