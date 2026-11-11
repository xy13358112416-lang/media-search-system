#pragma once
#include "mediarepository.h"

// CSV 持久化实现。路径由调用者决定，必须指向已有的有效 CSV。
class CsvMediaRepository final : public MediaRepository {
public:
    explicit CsvMediaRepository(QString path);
    RepositoryResult load(QList<MediaRecord>& records) override;
    RepositoryResult add(const MediaRecord& record) override;
    RepositoryResult remove(const QString& id) override;
private:
    RepositoryResult save(const QList<MediaRecord>& records);
    QString path_;  // CSV 文件路径
};
