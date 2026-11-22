// >>> DEFENSE-NOTE | 文件导读｜实时场景查询与规则结果的服务端组合
// 阅读路线：规则校验 → 30候选 → 模型查询 → 原会话核对 → 当前作品复核 → 回退或选中结果
// 联调关系：场景算法不拥有数据库；本路由通过dispatch复用已有规则和权限。保留正在进行的功能改动，只追加说明。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "warehouse_server.h"
#include "media_json.h"
#include <QJsonArray>
#include <QPointer>
#include <QTcpSocket>

// >>> DEFENSE-NOTE | startSceneRecommendation = 发起实时场景推荐
// 先用原请求dispatch做参数/鉴权，再将候选数量设30获取规则基线；同会话只允许一个场景请求进行中。
// <<< DEFENSE-NOTE | 本段讲解结束
void WarehouseServer::startSceneRecommendation(QTcpSocket* socket,const std::shared_ptr<Session>& session,
                                               const QJsonObject& request) {
    // dispatch validates authentication, preferences, scene and limit and builds authorized rule candidates.
    auto baseline=dispatch(*session,request);
    if(baseline.value("type")=="error_resp") {sendResponse(socket,baseline);return;}
    const int limit=request.value("payload").toObject().value("limit").toInt();
    auto poolRequest=request;
    auto poolPayload=request.value("payload").toObject();poolPayload.insert("limit",30);poolRequest.insert("payload",poolPayload);
    baseline=dispatch(*session,poolRequest);
    if(baseline.value("type")=="error_resp") {sendResponse(socket,baseline);return;}
    const auto id=request.value("req_id").toString();
    if(!session->scenes.isEmpty()) {
        sendResponse(socket,{{"type","error_resp"},{"req_id",id},{"code","ServerError"},
            {"message",QStringLiteral("正在查询场景推荐，请稍后再试。")}});return;
    }
    // >>> DEFENSE-NOTE | 场景路由 01｜从规则结果构造模型候选
    // 保存original的key→作品快照，模型只接收已授权候选；不得引入网上新搜到但库里不存在的作品。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const auto rows=baseline.value("payload").toObject().value("result").toObject().value("items").toArray();
    QList<llm_recommendation::Candidate> candidates;
    QHash<QString,QJsonObject> original;
    for(const auto& v:rows) {
        const auto row=v.toObject(),work=row.value("work").toObject();
        const auto key=work.value("resource").toObject().value("work_id").toString();
        MediaRecord media;QString error;
        if(!media_json::fromJson(work.value("media").toObject(),media,error)) continue;
        candidates.append({key,media});original.insert(key,work);
    }
    const auto scene=request.value("payload").toObject().value("scene").toString();
    const auto mode=scene=="relax"?care_contract::BrowseScene::Relax:
        scene=="focus"?care_contract::BrowseScene::Focus:care_contract::BrowseScene::Explore;
    // >>> DEFENSE-NOTE | 场景路由 02｜绑定当前用户、token和请求号
    // QPointer管理socket存活；scenes记录异步任务，以便退出和取消时清理。
    // <<< DEFENSE-NOTE | 本段讲解结束
    session->scenes.insert(id,{0,session->token,session->userId});
    QPointer<QTcpSocket> guarded(socket);
    const auto modelId=sceneRecommender_.recommend(mode,candidates,limit,
        [this,guarded,session,id,poolRequest,original,limit](llm_scene::Result selection) {
        const auto it=session->scenes.find(id);if(it==session->scenes.end()) return;
        const auto pending=it.value();session->scenes.erase(it);
        if(!guarded || guarded->state()!=QAbstractSocket::ConnectedState || session->token!=pending.token || session->userId!=pending.userId) return;
        // Re-read authorized candidates before delivery; never resurrect a removed/private resource.
        // >>> DEFENSE-NOTE | 场景路由 03｜交付前重新跑规则与权限
        // 使用当前数据，而不是发送模型前的旧目录；已删除/变私有的作品不能被模型结果重新带回来。
        // <<< DEFENSE-NOTE | 本段讲解结束
        auto current=dispatch(*session,poolRequest);
        if(current.value("type")=="error_resp") {sendResponse(guarded,current);return;}
        auto payload=current.value("payload").toObject(),result=payload.value("result").toObject();
        // >>> DEFENSE-NOTE | 场景路由 04｜模型成功与回退走不同输出
        // 成功时按模型ID顺序选取，并要求原快照与当前作品一致；合法空数组保持空，不强行凑数。
        // fallback=true时使用当前规则结果；最后裁到原limit并传message/fallback给页面。
        // <<< DEFENSE-NOTE | 本段讲解结束
        if(!selection.fallback) {
            QHash<QString,QJsonObject> readable;
            for(const auto& v:result.value("items").toArray()) {
                auto row=v.toObject();const auto work=row.value("work").toObject();
                const auto key=work.value("resource").toObject().value("work_id").toString();
                if(original.contains(key) && original.value(key)==work) {row.insert("reason",QString{});readable.insert(key,row);}
            }
            QJsonArray selected;
            for(const auto& key:selection.ids) if(readable.contains(key)) selected.append(readable.value(key));
            result.insert("items",selected);
            if(selected.isEmpty() && !selection.ids.isEmpty()) selection.message=QStringLiteral("候选已变化，请重新加载场景推荐。");
        }
        auto delivered=result.value("items").toArray();
        while(delivered.size()>limit) delivered.removeLast();
        result.insert("items",delivered);
        result.insert("fallback",selection.fallback);result.insert("message",selection.message);
        payload.insert("result",result);current.insert("payload",payload);sendResponse(guarded,current);
    });
    if(auto it=session->scenes.find(id);it!=session->scenes.end()) it->modelId=modelId;
}
