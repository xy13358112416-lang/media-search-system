#include "warehouse_server.h"
#include "media_json.h"
#include <QJsonArray>
#include <QPointer>
#include <QTcpSocket>

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
    session->scenes.insert(id,{0,session->token,session->userId});
    QPointer<QTcpSocket> guarded(socket);
    const auto modelId=sceneRecommender_.recommend(mode,candidates,limit,
        [this,guarded,session,id,poolRequest,original,limit](llm_scene::Result selection) {
        const auto it=session->scenes.find(id);if(it==session->scenes.end()) return;
        const auto pending=it.value();session->scenes.erase(it);
        if(!guarded || guarded->state()!=QAbstractSocket::ConnectedState || session->token!=pending.token || session->userId!=pending.userId) return;
        // Re-read authorized candidates before delivery; never resurrect a removed/private resource.
        auto current=dispatch(*session,poolRequest);
        if(current.value("type")=="error_resp") {sendResponse(guarded,current);return;}
        auto payload=current.value("payload").toObject(),result=payload.value("result").toObject();
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
