#pragma once

#include "mediarecord.h"
#include <QList>
#include <QString>

// 操作结果：失败时 error 包含可展示的原因。
struct RepositoryResult {
    bool success = false;  // 操作是否成功
    QString error;         // 失败原因；成功时为空
};

// 数据持久化接口；具体 CSV 实现将在后续提交中提供。
class MediaRepository {
public:
    virtual ~MediaRepository() = default;

    // 加载完整记录：成功时写入 records；失败时 records 保持不变。
    virtual RepositoryResult load(QList<MediaRecord>& records) = 0;

    // 新增并持久化：ID 必须非空且不重复；成功后主程序通知检索模块。
    virtual RepositoryResult add(const MediaRecord& record) = 0;

    // 按 ID 删除并持久化：不存在时失败；成功后主程序通知检索模块。
    virtual RepositoryResult remove(const QString& id) = 0;
};
