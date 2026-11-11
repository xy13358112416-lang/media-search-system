#include "media_click_store.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

MediaClickStore::MediaClickStore(const QString& path)
    : database_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QUuid::createUuid().toString()))
{
    database_.setDatabaseName(path);
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
}

MediaClickStore::~MediaClickStore()
{
    const QString connection = database_.connectionName();
    database_ = QSqlDatabase();
    if (!connection.isEmpty())
        QSqlDatabase::removeDatabase(connection);
}

bool MediaClickStore::open(QString& error)
{
    if (ready_)
        return true;
    if (!database_.isOpen() && !database_.open()) {
        error = database_.lastError().text();
        return false;
    }
    QSqlQuery query(database_);
    ready_ = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS media_clicks ("
        "user_id TEXT NOT NULL, media_id TEXT NOT NULL, clicked_at INTEGER NOT NULL)"))
        && query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS media_clicks_time ON media_clicks(clicked_at)"));
    if (!ready_)
        error = query.lastError().text();
    return ready_;
}

bool MediaClickStore::record(const QString& userId, const QString& mediaId, QString& error)
{
    error.clear();
    if (!open(error))
        return false;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO media_clicks (user_id, media_id, clicked_at) VALUES (?, ?, ?)"));
    query.addBindValue(userId);
    query.addBindValue(mediaId);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (query.exec())
        return true;
    error = query.lastError().text();
    return false;
}

bool MediaClickStore::ranking(QList<MediaClickCount>& results, QString& error)
{
    results.clear();
    error.clear();
    if (!open(error))
        return false;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    constexpr qint64 weekSeconds = 7 * 24 * 60 * 60;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT media_id, COUNT(DISTINCT user_id) AS users, COUNT(*) AS clicks "
        "FROM media_clicks WHERE clicked_at BETWEEN ? AND ? GROUP BY media_id "
        "ORDER BY users DESC, clicks DESC, media_id ASC"));
    query.addBindValue(now - weekSeconds);
    query.addBindValue(now);
    if (!query.exec()) {
        error = query.lastError().text();
        return false;
    }
    while (query.next()) {
        results.append({query.value(0).toString(), query.value(1).toLongLong(), query.value(2).toLongLong()});
    }
    if (query.lastError().isValid()) {
        error = query.lastError().text();
        results.clear();
        return false;
    }
    return true;
}
