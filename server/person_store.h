#pragma once

#include "care_person_snapshot.h"
#include <QUrl>

// N00 internal SERVER boundary. Not a new RPC, client database or concrete SQLite store.
// N03 implements this using the server's existing connection, migration and transactions.
// Calls stay on that connection's owning thread; the caller supplies an authenticated user.
namespace person_store {
class Store {
public:
    virtual ~Store() = default;

    // Only exact server-registered work/source triples may be decorated. The caller has
    // checked current readability; the implementation must recheck it before returning.
    // Unknown credits mean empty people, database/permission failure means an error.
    // No merging by MediaRecord.id, equal names, or guessed actor/director strings.
    virtual care_contract::Result<care_algorithms::PersonContext> contextFor(
        qint64 authenticatedUserId,
        const QList<care_contract::ResourceRef>& authorizedResources) = 0;

    // Own follows ordered by opaque personId. A departed person's already-known minimal
    // identity may remain for unfollow; private sources must not add new information.
    virtual care_contract::Result<care_contract::Page<care_contract::PersonSummary>> followsFor(
        qint64 authenticatedUserId, const care_contract::PageRequest& page) = 0;

    // Idempotent transaction. Following requires a currently readable confirmed credit;
    // unfollowing one's existing entry is allowed after loss/deletion of its last source.
    virtual care_contract::Status setFollow(qint64 authenticatedUserId,
        const QString& personId, bool followed) = 0;

    // Caller authorizes the person via current readable credits; failures are not empty caches.
    virtual care_contract::Result<QList<care_contract::PersonUpdate>> updatesFor(
        qint64 authenticatedUserId, const QString& personId) = 0;

    // Trusted server provider only. feedUrl is the exact configured
    // person_sources.source_url, never a PersonUpdate article URL. begin returns a
    // persistent generation, invalidating any older in-flight refresh for that feed.
    virtual care_contract::Result<qint64> beginUpdatesRefresh(
        const QString& personId, const QUrl& feedUrl) = 0;

    // Marks the current generation stale without discarding the last successful cache.
    virtual care_contract::Status failUpdatesRefresh(
        const QString& personId, const QUrl& feedUrl, qint64 generation) = 0;

    // Trusted provider only. Validates every row and feed generation, then atomically
    // replaces this feed's cache. Invalid rows or write failure leave old rows stale.
    // Empty successful updates still record last_success so an empty result is valid.
    virtual care_contract::Status mergeUpdates(const QString& personId, const QUrl& feedUrl,
        qint64 generation, qint64 completedAtUtc,
        const QList<care_contract::PersonUpdate>& updates) = 0;
};
} // namespace person_store
