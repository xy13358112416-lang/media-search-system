// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 单机CSV持久化：状态机解析CSV，演员/奖项字段用JSON数组编码；ID序号单独维护，保存采用QSaveFile。此模块不控制在线仓库权限。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "mediarepository.h"

// CSV 持久化实现。路径由调用者决定，必须指向已有的有效 CSV。
// >>> MAIN-DEFENSE | 接口结构｜CsvMediaRepository
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class CsvMediaRepository final : public MediaRepository {
public:
    // >>> MAIN-DEFENSE | 方法声明｜CsvMediaRepository
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    explicit CsvMediaRepository(QString path);
    static QStringList supportedFileTypes(); // 新增和 CSV 加载共用；界面可用作选项。
    static RepositoryResult validateNewRecord(const MediaRecord& record); // 新增前校验；忽略待生成的 ID。
    RepositoryResult ensureSequence(const QList<MediaRecord>& records); // 初始化或更新编号高水位。
    RepositoryResult reserveNextId(QString& id); // 先持久化预留编号，再返回 ID。
    // >>> MAIN-DEFENSE | 方法声明｜load
    // 从仓储读取并验证数据；上层在全部加载成功后才采用新快照，避免半份结果。
    // <<< MAIN-DEFENSE
    RepositoryResult load(QList<MediaRecord>& records) override;
    // >>> MAIN-DEFENSE | 方法声明｜add
    // 在现有记录中添加已校验记录；上层addNew负责分配ID。
    // <<< MAIN-DEFENSE
    RepositoryResult add(const MediaRecord& record) override;
    // >>> MAIN-DEFENSE | 方法声明｜remove
    // 按ID删除媒体；业务路径先持久化，再同步内存和检索索引。
    // <<< MAIN-DEFENSE
    RepositoryResult remove(const QString& id) override;
private:
    // >>> MAIN-DEFENSE | 方法声明｜updateSequence
    // 读取并锁定序号状态，结合已有编号更新高水位；预留与写入序列有明确失败返回。
    // <<< MAIN-DEFENSE
    RepositoryResult updateSequence(const QList<MediaRecord>& records, bool reserve, QString* id);
    // >>> MAIN-DEFENSE | 方法声明｜save
    // 序列化媒体列表并保存；QSaveFile先写临时文件，commit成功才替换目标。
    // <<< MAIN-DEFENSE
    RepositoryResult save(const QList<MediaRecord>& records);
    QString path_;  // CSV 文件路径
};
