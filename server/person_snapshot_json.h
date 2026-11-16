#pragma once
#include "care_person_snapshot.h"
#include <QJsonArray>
#include <QJsonObject>

// Internal media_snapshot extension only. Not accepted from record_behavior payloads.
// Missing field is a legacy/unknown snapshot; malformed present fields fail the read.
namespace person_snapshot_json {
inline constexpr auto Field = "_care_people_v1";
QJsonArray encode(const QList<care_contract::PersonSummary>& people);
std::optional<QList<care_contract::PersonSummary>> decode(
    const QJsonObject& snapshot, const care_contract::ResourceRef& ref, const MediaRecord& media);
}
