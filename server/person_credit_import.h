#pragma once

#include "care_contract.h"
#include <QByteArray>
#include <QSqlDatabase>
#include <QStringList>

namespace person_credit_import {
struct Person {
    QString personId;
    QString name;
    QUrl identitySource;
    QStringList aliases;
};
struct Credit {
    care_contract::ResourceRef resource;
    QString personId;
    care_contract::PersonRole role = care_contract::PersonRole::Actor;
    QUrl creditSource;
};
struct Batch {
    bool synthetic = false;
    QString provenance;
    QList<Person> people;
    QList<Credit> credits;
};
struct Options { bool allowSynthetic = false; };
struct Report { int peopleInserted = 0; int aliasesInserted = 0; int creditsInserted = 0; };

care_contract::Result<Batch> parse(const QByteArray& json);
care_contract::Result<Report> importBatch(QSqlDatabase& db, const Batch& batch,
                                          const Options& options = {});
} // namespace person_credit_import
