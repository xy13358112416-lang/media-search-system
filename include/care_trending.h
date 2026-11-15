#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms {
care_contract::Result<care_contract::TrendingResult> buildTrending(
    const care_contract::TrendingRequest& request, const PublicSnapshot& snapshot);
} // namespace care_algorithms
