#pragma once

#include "warehouse_contract.h"
#include <optional>

// Online-only DTOs. Authentication, database writes and algorithms are separate implementations.
namespace care_contract {
using Status = warehouse_contract::Status;
template<class T> using Result = warehouse_contract::Result<T>;
template<class T> using Callback = warehouse_contract::Callback<T>;
using StatusCallback = warehouse_contract::StatusCallback;
constexpr int MaxIdentifierUnits = 128;
constexpr int MaxQueryUnits = 1000;
constexpr int MaxPageSize = 50;
constexpr int MaxRecommendationLimit = 30;
constexpr qint64 MaxUtcExclusive = 253402300800LL;

struct WarehouseItemRef { qint64 warehouseId = 0; qint64 itemId = 0; };
struct ResourceRef {
    QString workId; // Stable canonical/source-namespaced work identity, NOT a local CSV row ID.
    WarehouseItemRef source; // Current permission is checked at this actual warehouse location.
};
enum class BehaviorAction { View, Search };
struct BehaviorEvent {
    QString eventId; // Unique per authenticated user. Retries retain the same ID and body.
    BehaviorAction action = BehaviorAction::View;
    std::optional<ResourceRef> resource; // Required for View, absent for Search.
    QString query; // Required for Search, empty for View; only submitted online searches.
    // No userId, timestamp or synthetic flag: normal clients cannot forge these fields.
};
struct BehaviorRecord {
    BehaviorEvent event;
    qint64 occurredAtUtc = 0; // Server receipt time, Unix SECONDS. Historical import is a separate tool.
    bool synthetic = false; // Set only by the server's demo importer, never by record_behavior.
};
struct TimeRange { qint64 beginUtc = 0; qint64 endUtc = 0; }; // [begin,end), UTC Unix seconds.
struct PageRequest { qint64 offset = 0; int limit = 20; };
template<class T> struct Page {
    QList<T> items;
    std::optional<qint64> nextOffset; // Absent means no more items, even if this page is empty.
};
struct ParticipationSettings {
    bool historyEnabled = true;
    bool personalizationEnabled = true;
    bool contributePublicTrends = false; // Explicit opt-in for public aggregate contribution.
};

// Structural checks only. Success never replaces authentication, authorization or ID lookup.
Status validate(const ResourceRef& value);
Status validate(const WarehouseItemRef& value);
Status validate(const BehaviorEvent& value);
Status validate(const TimeRange& value);
Status validate(const PageRequest& value);
} // namespace care_contract
