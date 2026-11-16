#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms {
constexpr int MaxConfirmedPeoplePerWork = 256; // Normalized distinct IDs, shared with historical codecs.
// Trusted server-owned directory records. Never construct this context from
// client JSON, CSV names, or a local MediaRecord::id.
struct ConfirmedWorkPeople {
    care_contract::ResourceRef resource;
    QList<care_contract::PersonSummary> people;
};
struct PersonContext {
    bool dataAvailable = true;
    QList<ConfirmedWorkPeople> credits;
    QSet<QString> followedPeople;
};

// Pure attachment to CURRENT authorized resources, using the exact stable
// (workId, warehouseId, itemId). Missing confirmation means empty people.
// Output preserves every input source and list order. Duplicate person roles
// are sorted/merged; conflicting identities fail rather than guess.
// Follows are limited to visible confirmed identities and flags are rebuilt.
// Personal historical workSnapshot values are preserved in their entirety,
// including snapshots that deliberately recorded no known people.
care_contract::Result<CatalogSnapshot> attachConfirmedPeople(
    const CatalogSnapshot& snapshot, const PersonContext& context);
care_contract::Result<PersonalSnapshot> attachConfirmedPeople(
    const PersonalSnapshot& snapshot, const PersonContext& context);
} // namespace care_algorithms
