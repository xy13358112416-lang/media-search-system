#pragma once
#include "mediarepository.h"

// CSV 持久化实现。路径由调用者决定，必须指向已有的有效 CSV。
class CsvMediaRepository final : public MediaRepository {
public:
    explicit CsvMediaRepository(QString path);
    static QStringList supportedFileTypes(); // 新增和 CSV 加载共用；界面可用作选项。
    static RepositoryResult validateNewRecord(const MediaRecord& record); // 新增前校验；忽略待生成的 ID。
    RepositoryResult ensureSequence(const QList<MediaRecord>& records); // 初始化或更新编号高水位。
    RepositoryResult reserveNextId(QString& id); // 先持久化预留编号，再返回 ID。
    RepositoryResult load(QList<MediaRecord>& records) override;
    RepositoryResult add(const MediaRecord& record) override;
    RepositoryResult remove(const QString& id) override;
private:
    RepositoryResult updateSequence(const QList<MediaRecord>& records, bool reserve, QString* id);
    RepositoryResult save(const QList<MediaRecord>& records);
    QString path_;  // CSV 文件路径
};
