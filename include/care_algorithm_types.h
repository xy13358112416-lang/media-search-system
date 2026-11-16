#pragma once
#include "care_contract.h"
#include <QHash>
#include <QSet>

// Server-side inputs only, never accepted as client-authored JSON. The network
// service must authenticate and construct the permitted snapshot before calling.
namespace care_algorithms {
struct ObservedBehavior {
    QString userKey; // Stable server identity, not a caller-supplied client identity.
    care_contract::BehaviorRecord record;
    std::optional<care_contract::ResourceSummary> workSnapshot; // Minimal historical credit/category snapshot.
    bool publicEligible = false; // Server-established opt-in/public-search eligibility at ingestion and now.
};
struct PersonalSnapshot {
    QString userKey;
    qint64 nowUtc = 0;
    bool historyAvailable = true;
    care_contract::ParticipationSettings settings;
    QList<ObservedBehavior> events; // Must contain this user's history; helpers also exclude foreign rows.
    QList<care_contract::ResourceSummary> readableWorks; // Current authorized candidates only.
    QSet<QString> followedPeople;
    QSet<QString> notInterestedWorkIds;
    QHash<QString, qint64> publicHeat; // Canonical work -> trusted public effective event count.
    bool publicHeatContainsSyntheticData = false; // Set from the public statistic's provenance, not client JSON.
};
struct PublicSnapshot {
    qint64 nowUtc = 0;
    bool dataAvailable = true;
    QList<ObservedBehavior> events;
    QList<care_contract::ResourceSummary> publicWorks; // Currently PUBLIC sources only, not all user-readable works.
};
struct CatalogSnapshot {
    bool dataAvailable = true;
    QList<care_contract::ResourceSummary> readableWorks;
    QSet<QString> followedPeople;
};
constexpr int MaxReportDays = 3660; // Resource cap for zero-filled reports, including custom ranges.
} // namespace care_algorithms
