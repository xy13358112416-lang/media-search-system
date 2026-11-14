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
// Server MUST authenticate the session and authorize the target before calling.
// readable contains only warehouse snapshots readable by that network user.
Request fromWarehouses(const warehouse_contract::WarehouseSummary& target,
                       const QList<warehouse_contract::WarehouseDetail>& readable, int limit = 6);
// Call with freshly authorized snapshots after completion. Deleted/inaccessible
// entries are omitted; session and target authorization remain the server's job.
WarehouseResult toWarehouses(const Result& result,
                            const QList<warehouse_contract::WarehouseDetail>& currentlyReadable);
} // namespace llm_recommendation
