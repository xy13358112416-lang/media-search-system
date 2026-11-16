#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms::detail {
care_contract::Status serverError(const QString& message);
care_contract::Status validateNow(qint64 nowUtc);
QString normalizeText(const QString& text); // NFKC, casefold, whitespace collapse; preserves punctuation.
// Algorithm candidates require a display title by default. Server adapters may
// validate identity/people/source consistency without adding a title constraint
// to media already accepted by the warehouse contract.
care_contract::Result<QList<care_contract::ResourceSummary>> canonicalWorks(
    const QList<care_contract::ResourceSummary>& works, bool requireTitle = true);
care_contract::Result<QList<ObservedBehavior>> personalEvents(const PersonalSnapshot& snapshot);
care_contract::Result<QList<ObservedBehavior>> publicEvents(const PublicSnapshot& snapshot);
} // namespace care_algorithms::detail
