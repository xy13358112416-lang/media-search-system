#include "llm_warehouse_adapter.h"
#include <QSet>

namespace llm_recommendation {
namespace {
QString key(qint64 warehouse, qint64 item) {
    return QString::number(warehouse) + QLatin1Char(':') + QString::number(item);
}
}
Request fromWarehouses(const warehouse_contract::WarehouseSummary& target,
                       const QList<warehouse_contract::WarehouseDetail>& readable, int limit) {
    Request request;
    request.context = target.name;
    request.limit = limit;
    QSet<QString> seen;
    for (const auto& warehouse : readable) {
        if (warehouse.warehouse.warehouseId <= 0) continue;
        for (const auto& item : warehouse.items) {
            if (item.itemId <= 0) continue;
            const auto id = key(warehouse.warehouse.warehouseId, item.itemId);
            if (seen.contains(id)) continue;
            seen.insert(id);
            request.candidates.append(Candidate{id, item.media});
        }
    }
    return request;
}
WarehouseResult toWarehouses(const Result& result,
                            const QList<warehouse_contract::WarehouseDetail>& currentlyReadable) {
    QHash<QString, WarehouseSelection> allowed;
    for (const auto& warehouse : currentlyReadable) {
        if (warehouse.warehouse.warehouseId <= 0) continue;
        for (const auto& item : warehouse.items) {
            if (item.itemId <= 0) continue;
            const auto id = key(warehouse.warehouse.warehouseId, item.itemId);
            if (!allowed.contains(id)) allowed.insert(id, {warehouse.warehouse.warehouseId, item, {}});
        }
    }
    WarehouseResult output{result.fallback, {}, result.message};
    QSet<QString> seen;
    for (const auto& item : result.items) {
        if (!allowed.contains(item.key) || seen.contains(item.key)) continue;
        auto selected = allowed.value(item.key);
        selected.reason = item.reason;
        output.items.append(selected);
        seen.insert(item.key);
    }
    return output;
}
} // namespace llm_recommendation
