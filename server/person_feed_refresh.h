#pragma once

#include "person_source_config.h"

class QNetworkAccessManager;
namespace person_store { class Store; }

namespace person_feed_refresh {
// Fetches only an already configured source. Store and network must outlive the callback.
void refresh(person_store::Store& store, QNetworkAccessManager& network,
             const person_source_config::Source& source, care_contract::StatusCallback done);
} // namespace person_feed_refresh
