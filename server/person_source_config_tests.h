#pragma once

#include <QObject>

class QSqlDatabase;

class PersonSourceConfigTests : public QObject {
    Q_OBJECT
private:
    static bool fixture(QSqlDatabase& db);
private slots:
    void registrationIsIdempotentAndDoesNotFakeRefreshSuccess();
    void invalidEntryRollsBackWholeRegistration();
    void feedFragmentsAreRejectedWithoutPartialRegistration();
    void rejectsInsecureOrMalformedConfiguration();
    void readsPagedSourcesWithoutChangingRefreshState();
    void additiveRegistrationPreservesCachedSourceState();
};
