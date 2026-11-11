#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct MediaClickCount {
    QString mediaId;
    qint64 users;
    qint64 clicks;
};

class MediaClickStore {
public:
    explicit MediaClickStore(const QString& path);
    ~MediaClickStore();
    MediaClickStore(const MediaClickStore&) = delete;
    MediaClickStore& operator=(const MediaClickStore&) = delete;

    bool record(const QString& userId, const QString& mediaId, QString& error);
    bool ranking(QList<MediaClickCount>& results, QString& error);

private:
    bool open(QString& error);
    QSqlDatabase database_;
    bool ready_ = false;
};
