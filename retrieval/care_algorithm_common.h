#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms::detail {
care_contract::Status serverError(const QString& message);
care_contract::Status validateNow(qint64 nowUtc);
QString normalizeText(const QString& text); // NFKC, casefold, whitespace collapse; preserves punctuation.
care_contract::Result<QList<care_contract::ResourceSummary>> canonicalWorks(
    const QList<care_contract::ResourceSummary>& works);
care_contract::Result<QList<ObservedBehavior>> personalEvents(const PersonalSnapshot& snapshot);
care_contract::Result<QList<ObservedBehavior>> publicEvents(const PublicSnapshot& snapshot);
} // namespace care_algorithms::detail
