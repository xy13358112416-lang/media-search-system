#pragma once

#include "care_contract.h"
#include <QSqlDatabase>
#include <QUrl>

namespace person_source_config {
struct Entry {
    QString personId;
    QUrl feedUrl;
};
struct Batch {
    QString provenance;
    QList<Entry> entries;
};
struct Report {
    int inserted = 0;
    int unchanged = 0;
};
struct Source {
    QString personId;
    QUrl feedUrl;
    std::optional<qint64> lastSuccessUtc;
    bool stale = false;
    qint64 refreshGeneration = 0;
};

// Parses a server-local, operator-authored JSON configuration. It performs no file
// or network I/O and never derives feed URLs from person identities or articles.
care_contract::Result<Batch> parse(const QByteArray& json);

// Additive and idempotent. The batch is transactional; missing people, malformed
// URLs or database failures roll back the entire batch. Existing rows and their
// cached refresh state are never modified or deleted.
care_contract::Result<Report> registerBatch(QSqlDatabase& db, const Batch& batch);
care_contract::Result<Report> registerFile(QSqlDatabase& db, const QString& path);

// Server-internal read interfaces for the provider. Neither function exposes
// person sources directly to a network client.
care_contract::Result<care_contract::Page<Source>> listSources(
    QSqlDatabase& db, const care_contract::PageRequest& page);
care_contract::Result<care_contract::Page<Source>> listSourcesForPerson(
    QSqlDatabase& db, const QString& personId, const care_contract::PageRequest& page);
} // namespace person_source_config
