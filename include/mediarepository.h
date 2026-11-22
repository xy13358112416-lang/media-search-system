// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 共享接口/数据契约：上层通过这里的类型和方法与主逻辑对接。声明不等于执行，实际实现见对应cpp。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "mediarecord.h"
#include <QList>
#include <QString>

// 操作结果：失败时 error 包含可展示的原因。
// >>> MAIN-DEFENSE | 接口结构｜RepositoryResult
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct RepositoryResult {
    bool success = false;  // 操作是否成功
    QString error;         // 失败原因；成功时为空
    bool persisted = false; // 此操作是否已经改变 CSV。
};

// 数据持久化接口；具体 CSV 实现将在后续提交中提供。
// >>> MAIN-DEFENSE | 接口结构｜MediaRepository
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class MediaRepository {
public:
    // >>> MAIN-DEFENSE | 方法声明｜MediaRepository
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    virtual ~MediaRepository() = default;

    // 加载完整记录：成功时写入 records；失败时 records 保持不变。
    // >>> MAIN-DEFENSE | 方法声明｜load
    // 从仓储读取并验证数据；上层在全部加载成功后才采用新快照，避免半份结果。
    // <<< MAIN-DEFENSE
    virtual RepositoryResult load(QList<MediaRecord>& records) = 0;

    // 新增并持久化：ID 必须非空且不重复；成功后主程序通知检索模块。
    // >>> MAIN-DEFENSE | 方法声明｜add
    // 在现有记录中添加已校验记录；上层addNew负责分配ID。
    // <<< MAIN-DEFENSE
    virtual RepositoryResult add(const MediaRecord& record) = 0;

    // 按 ID 删除并持久化：不存在时失败；成功后主程序通知检索模块。
    // >>> MAIN-DEFENSE | 方法声明｜remove
    // 按ID删除媒体；业务路径先持久化，再同步内存和检索索引。
    // <<< MAIN-DEFENSE
    virtual RepositoryResult remove(const QString& id) = 0;
};
