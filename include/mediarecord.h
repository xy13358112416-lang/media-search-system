#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// 一条媒体文件信息；ID 在记录生命周期内唯一且不变。
struct MediaRecord {
    QString id;             // 唯一编号
    QString fileName;       // 文件名
    QString fileType;       // 大写扩展名，不含点，例如 MP4
    quint64 fileSize = 0;   // 文件大小，单位为字节
    QString director;       // 导演
    QStringList actors;     // 主要演员，逐个保存姓名
    quint64 viewCount = 0;  // 浏览次数
    QDate releaseDate;      // 发行日期
    QStringList awards;     // 所获奖项，可为空
    QString category;       // 内容类别，例如纪录片，可为空
    QString releasePeriod;  // 只有年份或年月时保留 YYYY 或 YYYY-MM。
};

inline QString mediaReleaseText(const MediaRecord& record)
{
    return record.releaseDate.isValid() ? record.releaseDate.toString(Qt::ISODate) : record.releasePeriod;
}

inline bool validReleasePeriod(const QString& value)
{
    if (value.size() != 4 && value.size() != 7)
        return false;
    const auto date = QDate::fromString(value + (value.size() == 4 ? "-01-01" : "-01"), Qt::ISODate);
    return date.isValid() && date.toString(Qt::ISODate).left(value.size()) == value;
}

inline bool parseMediaRelease(const QString& value, MediaRecord& record)
{
    if (value.isEmpty() || validReleasePeriod(value)) {
        record.releaseDate = {};
        record.releasePeriod = value;
        return true;
    }
    const auto date = QDate::fromString(value, Qt::ISODate);
    if (value.size() != 10 || !date.isValid() || date.toString(Qt::ISODate) != value)
        return false;
    record.releaseDate = date;
    record.releasePeriod.clear();
    return true;
}
