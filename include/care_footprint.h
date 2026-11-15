#pragma once

#include "care_algorithm_types.h"

namespace care_algorithms {
// Pure computation over an authenticated, server-created snapshot. No I/O.
// Days follow Asia/Shanghai; each requested period is capped at MaxReportDays.
care_contract::Result<care_contract::FootprintReport> buildFootprint(
    const care_contract::FootprintRequest& request, const PersonalSnapshot& snapshot);
} // namespace care_algorithms
