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
