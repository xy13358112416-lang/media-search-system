#pragma once
#include "csvmediarepository.h"
#include "mediasearch.h"

// 主逻辑协调器：调用成功后，界面可刷新；错误通过 RepositoryResult 返回。
class MediaLibrary {
public:
    // password 必须由调用方配置；空口令始终不能通过验证。
    MediaLibrary(QString csvPath, QString password);
    RepositoryResult load(); // 加载 CSV 并重建索引；返回成功状态或错误。
    RepositoryResult add(const MediaRecord& record); // 添加 record；ID 由调用方提供且必须唯一。
    RepositoryResult remove(const QString& id); // 按 id 删除，返回成功状态或错误。
    bool authenticate(const QString& password) const; // 验证输入口令，匹配时返回 true。
    QStringList fileTypes() const; // 返回现有扩展名列表。
    QList<MediaRecord> records(const QString& fileType = {}) const; // 按类型筛选，空字符串代表全部；日期降序。
    bool find(const QString& id, MediaRecord& result) const; // 查找 id，存在时写入 result 并返回 true。
    QStringList search(const SearchQuery& query) const; // 组合查询，返回匹配记录的 ID。
private:
    RepositoryResult verifyUnchanged(); // 防止外部修改后使用过期索引写入
    CsvMediaRepository repository_; // 持久化存储
    MediaSearch search_;           // 检索索引
    QList<MediaRecord> records_;   // 最近成功加载的记录
    QString password_;             // 调用方配置的演示口令
    bool loaded_ = false;          // 记录和索引是否可供查询
};
