#pragma once

#include "person_store.h"
#include <QSqlDatabase>

namespace person_store {
// Uses the server's existing SQLite connection on its owning thread.
class SqliteStore final : public Store {
public:
    explicit SqliteStore(QSqlDatabase database);
    care_contract::Result<care_algorithms::PersonContext> contextFor(
        qint64 userId, const QList<care_contract::ResourceRef>& resources) override;
    care_contract::Result<care_contract::Page<care_contract::PersonSummary>> followsFor(
        qint64 userId, const care_contract::PageRequest& page) override;
    care_contract::Status setFollow(qint64 userId, const QString& personId, bool followed) override;
    care_contract::Result<QList<care_contract::PersonUpdate>> updatesFor(qint64 userId, const QString& personId) override;
    care_contract::Status mergeUpdates(const QString& personId, const QUrl& feedUrl, qint64 fetchedAtUtc,
        const QList<care_contract::PersonUpdate>& updates) override;
    care_contract::Status markSourceFailure(const QString& personId, const QUrl& feedUrl) override;
private:
    QSqlDatabase db_;
};
}
