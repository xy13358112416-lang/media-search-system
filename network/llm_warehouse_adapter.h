// >>> DEFENSE-NOTE | 头文件导读｜仓库与模型ID适配接口
// fromWarehouses将可读资源转模型候选，toWarehouses在完成时按当前可读资源映射，权限检查由服务器负责。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "llm_recommendation.h"
#include "warehouse_contract.h"

namespace llm_recommendation {
struct WarehouseSelection {
    qint64 warehouseId = 0;
    warehouse_contract::WarehouseItem item;
    QString reason;
};
struct WarehouseResult {
    bool fallback = false;
    QList<WarehouseSelection> items;
    QString message;
};
// Server MUST authenticate, validate query/limit/optional target and authorize any target before calling.
// readable contains only warehouse snapshots readable by that network user.
Request fromWarehouses(const warehouse_contract::WarehouseRecommendationRequest& query,
                       const QList<warehouse_contract::WarehouseDetail>& readable);
// Compatibility entry point for existing consumers that use the warehouse name as a theme.
Request fromWarehouses(const warehouse_contract::WarehouseSummary& target,
                       const QList<warehouse_contract::WarehouseDetail>& readable, int limit = 6);
// Call with freshly authorized snapshots after completion. Deleted/inaccessible
// entries are omitted; session and target authorization remain the server's job.
WarehouseResult toWarehouses(const Result& result,
                            const QList<warehouse_contract::WarehouseDetail>& currentlyReadable);
warehouse_contract::WarehouseRecommendationResult toRecommendationResult(
    const Result& result, const QList<warehouse_contract::WarehouseDetail>& currentlyReadable);
} // namespace llm_recommendation
