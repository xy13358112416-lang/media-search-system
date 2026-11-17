#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms {
// R00-v1: each view contributes separately. Category/person distributions are
// normalized independently by their total weight. A person is credited once per
// view by personId (roles do not multiply interest); matching also uses personId.
// Any/Explicit category disables historical category completion. Person interest
// remains independent. File-type Auto has no historical term in the R00 formula;
// only Explicit file types add a soft bonus, and unknown types never match.
// Weights/bonuses must lie in [0,1], categoryWeight+peopleWeight <= 1, and
// half-lives must be finite positive values; invalid experiments return errors.
struct TemporalScoringConfig {
    double longHalfLifeDays = 90.0;
    double recentHalfLifeDays = 7.0;
    double freshnessHalfLifeDays = 30.0;
    double categoryWeight = 0.60;
    double peopleWeight = 0.40;
    double longWeight = 0.35;
    double recentWeight = 0.40;
    double heatWeight = 0.15;
    double freshnessWeight = 0.10;
    double explicitCategoryBonus = 0.20;
    double explicitTypeBonus = 0.10;
    double followBonus = 0.05;
};

// Pure scoring over an already authorized/recalled snapshot; performs no I/O.
// This pure rule engine validates scene without inventing scene tags. The server
// applies optional live DeepSeek web-search selection after rule candidate recall.
// Date freshness uses the Asia/Shanghai date at snapshot.nowUtc. With no usable
// personal features, available heat/freshness weights are normalized to sum 1.
// Positive-score candidates bound relevance: zero-score works do not fill an
// otherwise positive result. If every eligible work scores zero, stable IDs win.
// Diversity moves a positive alternative category ahead of a third same-category
// item. All errors (including unavailable history) are returned without fallback.
care_contract::Result<care_contract::TemporalRecommendationResult> recommendTemporal(
    const care_contract::TemporalRecommendationRequest& request, const PersonalSnapshot& snapshot);

// Optional experiments must carry their configuration/version outside the result;
// the two-argument service API always uses the R00-v1 defaults above.
care_contract::Result<care_contract::TemporalRecommendationResult> recommendTemporal(
    const care_contract::TemporalRecommendationRequest& request, const PersonalSnapshot& snapshot,
    const TemporalScoringConfig& config);
} // namespace care_algorithms
