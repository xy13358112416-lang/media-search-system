// >>> DEFENSE-NOTE | 文件导读｜仓库数据与模型候选之间的适配
// 阅读路线：授权仓库→不歧义key→模型请求；模型key→当前可读条目→客户端结果
// 联调关系：路由提供已鉴权快照；模型不能生成新仓库或扩大读权限；完成后必须用重新授权的数据转换。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "llm_warehouse_adapter.h"
#include <QSet>

namespace llm_recommendation {
namespace {
// >>> DEFENSE-NOTE | key = 将仓库和条目组合为稳定候选键
// 这里使用“仓库:条目”，与在线检索使用“仓库/条目”是不同模块的内部约定；都由调用层生成，模型只原样引用。
// <<< DEFENSE-NOTE | 本段讲解结束
QString key(qint64 warehouse, qint64 item) {
    return QString::number(warehouse) + QLatin1Char(':') + QString::number(item);
}
}
// >>> DEFENSE-NOTE | fromWarehouses = 授权仓库转推荐请求
// 保留用户query和limit，遍历可读条目，以组合key去重；此函数本身不做登录与目标仓库权限校验。
// <<< DEFENSE-NOTE | 本段讲解结束
Request fromWarehouses(const warehouse_contract::WarehouseRecommendationRequest& query,
                       const QList<warehouse_contract::WarehouseDetail>& readable) {
    Request request;
    request.context = query.query;
    request.limit = query.limit;
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
// >>> DEFENSE-NOTE | 兼容入口｜使用仓库名作为推荐主题
// 将旧的目标仓库摘要转换成统一请求，再复用同一候选整理流程。
// <<< DEFENSE-NOTE | 本段讲解结束
Request fromWarehouses(const warehouse_contract::WarehouseSummary& target,
                       const QList<warehouse_contract::WarehouseDetail>& readable, int limit) {
    return fromWarehouses(warehouse_contract::WarehouseRecommendationRequest{target.name, target.warehouseId, limit}, readable);
}
// >>> DEFENSE-NOTE | toWarehouses = 模型结果映射回当前资源
// 从currentlyReadable重建key白名单；删除、撤权或未知key跳过；seen去重并保留模型顺序。
// <<< DEFENSE-NOTE | 本段讲解结束
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
// >>> DEFENSE-NOTE | toRecommendationResult = 转成网络业务返回结构
// 把仓库ID、条目ID、当前媒体和理由组合返回；fallback与message继续传递给Qt。
// <<< DEFENSE-NOTE | 本段讲解结束
warehouse_contract::WarehouseRecommendationResult toRecommendationResult(
    const Result& result, const QList<warehouse_contract::WarehouseDetail>& currentlyReadable) {
    const auto mapped = toWarehouses(result, currentlyReadable);
    warehouse_contract::WarehouseRecommendationResult output;
    output.fallback = mapped.fallback; output.message = mapped.message;
    for (const auto& selection : mapped.items)
        output.items.append({selection.warehouseId, selection.item.itemId, selection.item.media, selection.reason});
    return output;
}
} // namespace llm_recommendation
