// >>> DEFENSE-NOTE | 文件导读｜仓库模型推荐的服务端接线
// 阅读路线：核对会话 → 授权候选 → 登记请求 → 异步模型 → 会话/权限复核 → 返回
// 联调关系：Qt使用网络业务请求；模型任务编号与客户端req_id独立映射。主逻辑的readableCandidates提供权限边界。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "warehouse_server.h"
#include "llm_warehouse_adapter.h"
#include "media_json.h"

#include <QJsonArray>
#include <QPointer>
#include <QTcpSocket>

using namespace warehouse_contract;

// >>> DEFENSE-NOTE | cancelPending = 取消会话中所有模型类任务
// 摘要、推荐及当前版本新增的场景任务分别保存并取消；断线/退出时调用。
// notify决定是否向仍在线客户端发送取消错误，不是自动再次请求模型。
// <<< DEFENSE-NOTE | 本段讲解结束
void WarehouseServer::cancelPending(Session& session, QTcpSocket* socket, bool notify)
{
    const auto scenes = std::exchange(session.scenes, {});
    for (auto it = scenes.cbegin(); it != scenes.cend(); ++it) {
        sceneRecommender_.cancel(it->modelId);
        if (notify) sendResponse(socket, {{"type","error_resp"},{"req_id",it.key()},
            {"code","Cancelled"},{"message",QStringLiteral("场景查询已取消。")}});
    }
    const auto summaries = std::exchange(session.summaries, {});
    for (auto it = summaries.cbegin(); it != summaries.cend(); ++it) {
        trendSummarizer_.cancel(it->modelId);
        if (notify)
            sendResponse(socket, {{"type", "error_resp"}, {"req_id", it.key()},
                                  {"code", "Cancelled"}, {"message", QStringLiteral("摘要已取消。")}});
    }
    const auto pending = std::exchange(session.pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        recommender_.cancel(it->modelId);
        if (notify)
            sendResponse(socket, {{"type", "error_resp"}, {"req_id", it.key()},
                                  {"code", "Cancelled"}, {"message", QStringLiteral("推荐已取消。")}});
    }
}

// >>> DEFENSE-NOTE | startRecommendation = 处理仓库推荐请求
// 验证payload、当前socket会话token、query长度和limit；可选目标仓库也必须在当前可读集合里。
// <<< DEFENSE-NOTE | 本段讲解结束
void WarehouseServer::startRecommendation(QTcpSocket* socket, const std::shared_ptr<Session>& session,
                                          const QJsonObject& request)
{
    const QString id = request.value("req_id").toString();
    const QString type = request.value("type").toString();
    auto fail = [&](const char* code, const QString& message) {
        sendResponse(socket, {{"type", "error_resp"}, {"req_id", id}, {"code", code}, {"message", message}});
    };
    if (!request.value("payload").isObject()) {
        fail("InvalidArgument", QStringLiteral("请求参数无效。")); return;
    }
    if (!session->userId || !request.value("token").isString() || request.value("token").toString() != session->token) {
        fail("NotAuthenticated", QStringLiteral("请在当前连接登录。")); return;
    }
    const auto payload = request.value("payload").toObject();
    if (type == "cancel_recommendation") {
        const auto targetValue = payload.value("target_req_id");
        if (!targetValue.isString() || targetValue.toString().isEmpty() || targetValue.toString().size() > 128) {
            fail("InvalidArgument", QStringLiteral("取消编号无效。")); return;
        }
        const QString target = targetValue.toString();
        auto scene = session->scenes.find(target);
        if (scene != session->scenes.end()) {
            const auto modelId = scene->modelId;
            session->scenes.erase(scene);
            sceneRecommender_.cancel(modelId);
            if (!sendResponse(socket, {{"type","error_resp"},{"req_id",target},{"code","Cancelled"},
                {"message",QStringLiteral("场景查询已取消。")}})) return;
        }
        auto summary = session->summaries.find(target);
        if (summary != session->summaries.end()) {
            const auto modelId = summary->modelId;
            session->summaries.erase(summary);
            trendSummarizer_.cancel(modelId);
            if (!sendResponse(socket, {{"type", "error_resp"}, {"req_id", target}, {"code", "Cancelled"},
                                       {"message", QStringLiteral("摘要已取消。")}})) return;
        }
        auto it = session->pending.find(target);
        if (it != session->pending.end()) {
            const auto modelId = it->modelId;
            session->pending.erase(it);
            recommender_.cancel(modelId);
            if (!sendResponse(socket, {{"type", "error_resp"}, {"req_id", target}, {"code", "Cancelled"},
                                       {"message", QStringLiteral("推荐已取消。")}})) return;
        }
        sendResponse(socket, {{"type", "cancel_recommendation_resp"}, {"req_id", id}, {"payload", QJsonObject{}}});
        return;
    }
    const auto query = payload.value("query");
    const auto limit = payload.value("limit");
    const double limitNumber = limit.toDouble(-1);
    if (!query.isString() || query.toString().trimmed().isEmpty() || query.toString().size() > 1000 ||
        !limit.isDouble() || limitNumber < 1 || limitNumber > 50 || limitNumber != static_cast<int>(limitNumber)) {
        fail("InvalidArgument", QStringLiteral("推荐条件无效。")); return;
    }
    std::optional<qint64> target;
    if (payload.contains("warehouse_id")) {
        const auto value = payload.value("warehouse_id");
        const auto text = value.toString();
        bool ok = false;
        const auto number = text.toLongLong(&ok);
        if (!value.isString() || !ok || number <= 0 || QString::number(number) != text) {
            fail("InvalidArgument", QStringLiteral("仓库编号无效。")); return;
        }
        target = number;
    }
    // >>> DEFENSE-NOTE | 接线步骤 01｜服务器决定候选权限
    // 使用session里的userId，而不是让客户端自报任意身份。读取失败与候选过大均报错。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const auto readable = readableCandidates(session->userId);
    if (!readable) { fail("ServerError", QStringLiteral("读取候选失败，或候选超过数量或大小上限。")); return; }
    if (target) {
        bool found = false;
        for (const auto& warehouse : *readable)
            if (warehouse.warehouse.warehouseId == *target) found = true;
        if (!found) { fail("PermissionDenied", QStringLiteral("没有目标仓库的访问权限。")); return; }
    }
    const WarehouseRecommendationRequest spec{query.toString(), target, static_cast<int>(limitNumber)};
    auto modelRequest = llm_recommendation::fromWarehouses(spec, *readable);
    // >>> DEFENSE-NOTE | 接线步骤 02｜保存请求的用户、会话、目标和候选键
    // 模型异步执行期间数据与权限可能变化；保存这些字段是为了回来时核对，不是隔离数据库事务。
    // <<< DEFENSE-NOTE | 本段讲解结束
    Session::Pending pending;
    pending.userId = session->userId;
    pending.token = session->token;
    pending.target = target;
    for (const auto& candidate : modelRequest.candidates) pending.candidateKeys.insert(candidate.key);
    session->pending.insert(id, pending);
    QPointer<QTcpSocket> guardedSocket(socket);
    // >>> DEFENSE-NOTE | 接线步骤 03｜异步模型完成后再查当前权限
    // QPointer避免向已销毁连接写入；比对用户/token，确认还是原会话；重新读当前可读目录。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const quint64 modelId = recommender_.recommend(std::move(modelRequest), [this, guardedSocket, session, id](llm_recommendation::Result result) {
        auto it = session->pending.find(id);
        if (it == session->pending.end()) return;
        const auto pending = it.value();
        session->pending.erase(it);
        if (!guardedSocket || guardedSocket->state() != QAbstractSocket::ConnectedState ||
            session->userId != pending.userId || session->token != pending.token) return;
        const auto current = readableCandidates(session->userId);
        if (!current) {
            sendResponse(guardedSocket, {{"type", "error_resp"}, {"req_id", id}, {"code", "ServerError"},
                                        {"message", QStringLiteral("重新检查候选失败，或候选超过数量或大小上限。")}}); return;
        }
        if (pending.target) {
            bool found = false;
            for (const auto& warehouse : *current)
                if (warehouse.warehouse.warehouseId == *pending.target) found = true;
            if (!found) {
                sendResponse(guardedSocket, {{"type", "error_resp"}, {"req_id", id}, {"code", "PermissionDenied"},
                                            {"message", QStringLiteral("目标仓库已不可访问。")}}); return;
            }
        }
        // >>> DEFENSE-NOTE | 接线步骤 04｜原候选和当前可读集合双重约束
        // 先剔除不在发出时候选中的key，再通过adapter过滤当前已撤权/删除条目；转换为客户端作品结果。
        // <<< DEFENSE-NOTE | 本段讲解结束
        for (auto item = result.items.begin(); item != result.items.end(); )
            if (!pending.candidateKeys.contains(item->key)) item = result.items.erase(item); else ++item;
        const auto converted = llm_recommendation::toRecommendationResult(result, *current);
        QJsonArray items;
        for (const auto& item : converted.items)
            items.append(QJsonObject{{"warehouse_id", QString::number(item.warehouseId)},
                         {"item_id", QString::number(item.itemId)}, {"media", media_json::toJson(item.media)},
                         {"reason", item.reason}});
        sendResponse(guardedSocket, {{"type", "recommend_resources_resp"}, {"req_id", id},
                     {"payload", QJsonObject{{"items", items}, {"fallback", converted.fallback}, {"message", converted.message}}}});
    });
    session->pending[id].modelId = modelId;
}
