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
};
