#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms {
care_contract::Result<care_contract::Page<care_contract::PersonSummary>> searchPeople(
    const care_contract::PersonSearchRequest& request, const CatalogSnapshot& snapshot);
care_contract::Result<care_contract::Page<care_contract::ResourceSummary>> worksForPerson(
    const QString& personId, const care_contract::PageRequest& page, const CatalogSnapshot& snapshot);
// Suggestions never change follows; identity and current readable-work scope are mandatory.
care_contract::Result<QList<care_contract::PersonSummary>> suggestPeople(int limit, const PersonalSnapshot& snapshot);
// Pure selection over a server-managed source cache. Stale records remain usable;
// cacheAvailable=false denotes failure to read the cache, not failure of an external refresh.
care_contract::Result<care_contract::Page<care_contract::PersonUpdate>> personUpdates(
    const care_contract::PersonUpdatesRequest& request, const CatalogSnapshot& snapshot,
    const QList<care_contract::PersonUpdate>& cache, qint64 nowUtc, bool cacheAvailable = true);
} // namespace care_algorithms
