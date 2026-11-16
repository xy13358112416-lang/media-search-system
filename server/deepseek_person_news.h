#pragma once

#include "care_contract.h"

class QNetworkAccessManager;
namespace person_store { class Store; }

namespace deepseek_person_news {
struct Request {
    QString personId;
    QString displayName;
    QString approvedSite; // Exact verified publisher hostname, without scheme or path.
    QString identityClue; // A public work title or other human-confirmed disambiguator.
};

// Namespaces this source in the existing cache independently of RSS/Atom.
care_contract::Result<QUrl> cacheSource(const Request& request);
bool isSearchSource(const QUrl& source);

// Operates on an already registered source on the store's thread.
void refresh(person_store::Store& store, QNetworkAccessManager& network, const Request& request,
             const QByteArray& apiKey, care_contract::StatusCallback done);
} // namespace deepseek_person_news
