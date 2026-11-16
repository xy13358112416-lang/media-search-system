#pragma once

#include "care_contract.h"

#include <QJsonObject>
#include <QJsonValue>

namespace person_json {

// Requests use offset and limit at the payload level. Successful lists carry a page object.
QJsonObject encodePageRequest(const care_contract::PageRequest& page);
bool decodePageRequest(const QJsonObject& payload, care_contract::PageRequest& page);

QJsonObject encode(const care_contract::Page<care_contract::PersonSummary>& page);
QJsonObject encode(const care_contract::Page<care_contract::ResourceSummary>& page);
QJsonObject encode(const care_contract::Page<care_contract::PersonUpdate>& page);

bool decode(const QJsonValue& work, care_contract::ResourceSummary& result);
bool decode(const QJsonValue& page, const care_contract::PageRequest& request,
            care_contract::Page<care_contract::PersonSummary>& result);
bool decode(const QJsonValue& page, const care_contract::PageRequest& request,
            care_contract::Page<care_contract::ResourceSummary>& result);
bool decode(const QJsonValue& page, const care_contract::PageRequest& request,
            care_contract::Page<care_contract::PersonUpdate>& result);

} // namespace person_json
