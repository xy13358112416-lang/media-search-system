#pragma once

#include <QString>
class QSqlDatabase;

namespace person_schema {
// Caller owns an active SQLite transaction and commits or rolls it back.
// Does not open connections or create a second database.
bool migrate(QSqlDatabase& db, QString& error);
}
