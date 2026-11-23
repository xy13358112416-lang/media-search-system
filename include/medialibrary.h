// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 单机业务协调：仓储负责磁盘，records_是内存记录，search_是检索索引。先完成保存，再维护内存与索引；若后续失败，必须区分已持久化和未保存。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "csvmediarepository.h"
#include "mediasearch.h"

// 主逻辑协调器：调用成功后，界面可刷新；错误通过 RepositoryResult 返回。
// >>> MAIN-DEFENSE | 接口结构｜MediaLibrary
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class MediaLibrary {
public:
    // password 必须由调用方配置；空口令始终不能通过验证。
    // >>> MAIN-DEFENSE | 方法声明｜MediaLibrary
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    MediaLibrary(QString csvPath, QString password);
    RepositoryResult load(); // 加载 CSV 并重建索引；返回成功状态或错误。
    RepositoryResult add(const MediaRecord& record); // 兼容旧调用；自动分配 ID。新代码应使用 addNew。
    RepositoryResult addNew(MediaRecord& record); // 为新记录分配编号后保存；record.id 接收分配结果。
    RepositoryResult remove(const QString& id); // 按 id 删除，返回成功状态或错误。
    bool authenticate(const QString& password) const; // 验证输入口令，匹配时返回 true。
    QStringList fileTypes() const; // 返回现有扩展名列表。
    static QStringList supportedFileTypes(); // 返回可新增和导入的扩展名。
    QList<MediaRecord> records(const QString& fileType = {}) const; // 按类型筛选，空字符串代表全部；日期降序。
    bool find(const QString& id, MediaRecord& result) const; // 查找 id，存在时写入 result 并返回 true。
    QStringList search(const SearchQuery& query) const; // 组合查询，返回匹配记录的 ID。
    QStringList search(const SearchQuery& query, SearchTrace& trace) const; // 同时返回实际索引步骤。
private:
    RepositoryResult persistRecord(const MediaRecord& record); // 保存已预留 ID 的新记录。
    RepositoryResult verifyUnchanged(); // 防止外部修改后使用过期索引写入
    CsvMediaRepository repository_; // 持久化存储
    MediaSearch search_;           // 检索索引
    QList<MediaRecord> records_;   // 最近成功加载的记录
    QString password_;             // 调用方配置的演示口令
    bool loaded_ = false;          // 记录和索引是否可供查询
};
