// >>> DEFENSE-NOTE | 文件导读｜热榜AI摘要的请求生命周期
// 阅读路线：解析模式 → 当前公共聚合 → 保存证据 → 模型摘要 → 再聚合复核 → 只替换文字
// 联调关系：规则热榜和模型文字分层；同会话仅一个摘要任务。页面普通刷新不应触发模型。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "warehouse_server.h"
#include "care_trending.h"
#include "media_json.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>

using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
// >>> DEFENSE-NOTE | databaseFailure = 统一数据库错误
// 读取公开数据失败必须报错，不能生成看似正常的空榜。
// <<< DEFENSE-NOTE | 本段讲解结束
Status databaseFailure() { return {ErrorCode::ServerError, QStringLiteral("仓库数据库操作失败。")}; }
// >>> DEFENSE-NOTE | utcValue = 严格读取十进制UTC秒字符串
// 大整数以字符串传输，避免JSON浮点精度问题；拒绝负数和非规范格式。
// <<< DEFENSE-NOTE | 本段讲解结束
bool utcValue(const QJsonValue& value, qint64& number) {
    if (!value.isString()) return false;
    bool ok=false;number=value.toString().toLongLong(&ok);
    return ok && number>=0 && QString::number(number)==value.toString();
}
// >>> DEFENSE-NOTE | publicContents = 可比较的公共榜单证据
// 去掉生成时间、摘要文字和模型标志，只比较实际公开条目、元数据与精确计数。
// <<< DEFENSE-NOTE | 本段讲解结束
QJsonObject publicContents(QJsonObject result) {
    // Comparison includes complete public rows/metadata and exact counts, but not
    // generated time or derived wording which can change without new public evidence.
    result.remove("generated_at_utc");result.remove("summary");result.remove("summary_from_model");
    return result;
}
}

// >>> DEFENSE-NOTE | parseTrendingRequest = 解析榜单请求
// 校验作品/搜索词、时间窗口与数量；summary_mode缺省rules保持旧请求兼容，model需显式选择。
// <<< DEFENSE-NOTE | 本段讲解结束
Status WarehouseServer::parseTrendingRequest(const QJsonObject& payload, TrendingRequest& output) {
    const auto invalid=[] {return Status{ErrorCode::InvalidArgument, QStringLiteral("请求参数无效。")};};
    TrendingRequest input;
    const auto kind=payload.value("kind");
    if(kind=="works") input.kind=TrendingKind::Works;
    else if(kind=="search_terms") input.kind=TrendingKind::SearchTerms;
    else return invalid();
    const auto range=payload.value("range");
    if(!range.isObject() || !utcValue(range.toObject().value("begin_utc"),input.range.beginUtc) ||
       !utcValue(range.toObject().value("end_utc"),input.range.endUtc)) return invalid();
    const auto limit=payload.value("limit");const auto number=limit.toDouble(-1);
    if(!limit.isDouble() || number<1 || number>MaxPageSize || number!=static_cast<int>(number)) return invalid();
    input.limit=static_cast<int>(number);
    const auto mode=payload.value("summary_mode");
    if(mode.isUndefined() || mode=="rules") input.summaryMode=TrendingSummaryMode::Rules;
    else if(mode=="model") input.summaryMode=TrendingSummaryMode::Model;
    else return invalid();
    const auto valid=validate(input);if(!valid.ok()) return valid;
    output=input;return {};
}

// >>> DEFENSE-NOTE | currentTrending = 从数据库重新生成当前公共热榜
// SQL限制当前公开来源、用户允许公共贡献、事件当时public_eligible及时间范围；再调用纯算法buildTrending。
// 候选数、字节数与事件数都有限额，不能把数据库异常当成无数据。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<TrendingResult> WarehouseServer::currentTrending(const TrendingRequest& input) {
    care_algorithms::PublicSnapshot snapshot;
    snapshot.nowUtc = QDateTime::currentSecsSinceEpoch();
    QSqlQuery sources(db_);
    sources.prepare("SELECT s.work_id,s.warehouse_id,s.item_id,i.media FROM work_sources s "
                    "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                    "JOIN warehouses w ON w.id=s.warehouse_id WHERE w.is_public=1 AND EXISTS ("
                    "SELECT 1 FROM behavior_events e JOIN behavior_settings p ON p.user_id=e.user_id "
                    "WHERE e.public_eligible=1 AND p.contribute_public_trends=1 AND e.action='view' "
                    "AND e.work_id=s.work_id AND e.warehouse_id=s.warehouse_id AND e.item_id=s.item_id "
                    "AND e.occurred_at_utc>=? AND e.occurred_at_utc<? AND e.occurred_at_utc<=?) ORDER BY s.warehouse_id,s.item_id");
    sources.addBindValue(input.range.beginUtc); sources.addBindValue(input.range.endUtc); sources.addBindValue(snapshot.nowUtc);
    if (input.kind == care_contract::TrendingKind::Works && !sources.exec()) return {databaseFailure(), {}};
    qint64 bytes = 0;
    while (input.kind == care_contract::TrendingKind::Works && sources.next()) {
        if (snapshot.publicWorks.size() >= 10000) return {databaseFailure(), {}};
        const auto blob = sources.value(3).toByteArray(); bytes += blob.size();
        if (bytes > 16 * 1024 * 1024) return {databaseFailure(), {}};
        MediaRecord media; QString error;
        if (!media_json::fromJson(QJsonDocument::fromJson(blob).object(), media, error)) return {databaseFailure(), {}};
        snapshot.publicWorks.append({{sources.value(0).toString(), {sources.value(1).toLongLong(), sources.value(2).toLongLong()}}, media, {}});
    }
    if (sources.lastError().isValid()) return {databaseFailure(), {}};
    QSqlQuery events(db_);
    QString eventSql = QStringLiteral("SELECT e.user_id,e.event_id,e.action,e.query,e.occurred_at_utc,e.synthetic,e.work_id,e.warehouse_id,e.item_id "
                   "FROM behavior_events e JOIN behavior_settings s ON s.user_id=e.user_id "
                   "WHERE e.public_eligible=1 AND s.contribute_public_trends=1 AND e.action=? "
                   "AND e.occurred_at_utc>=? AND e.occurred_at_utc<? AND e.occurred_at_utc<=?");
    if (input.kind == care_contract::TrendingKind::Works)
        eventSql += QStringLiteral(" AND EXISTS (SELECT 1 FROM work_sources ws "
            "JOIN warehouse_items i ON i.id=ws.item_id AND i.warehouse_id=ws.warehouse_id "
            "JOIN warehouses w ON w.id=ws.warehouse_id WHERE w.is_public=1 "
            "AND ws.work_id=e.work_id AND ws.warehouse_id=e.warehouse_id AND ws.item_id=e.item_id)");
    events.prepare(eventSql);
    events.addBindValue(input.kind == care_contract::TrendingKind::Works ? "view" : "search");
    events.addBindValue(input.range.beginUtc); events.addBindValue(input.range.endUtc); events.addBindValue(snapshot.nowUtc);
    if (!events.exec()) return {databaseFailure(), {}};
    while (events.next()) {
        if (snapshot.events.size() >= 100000) return {databaseFailure(), {}};
        care_algorithms::ObservedBehavior event;
        event.userKey = QString::number(events.value(0).toLongLong());
        event.record.event.eventId = events.value(1).toString();
        if (input.kind == care_contract::TrendingKind::Works) {
            event.record.event.action = care_contract::BehaviorAction::View;
            event.record.event.resource = care_contract::ResourceRef{events.value(6).toString(),
                {events.value(7).toLongLong(), events.value(8).toLongLong()}};
        } else {
            event.record.event.action = care_contract::BehaviorAction::Search;
            event.record.event.query = events.value(3).toString();
        }
        event.record.occurredAtUtc = events.value(4).toLongLong();
        event.record.synthetic = events.value(5).toBool(); event.publicEligible = true;
        snapshot.events.append(event);
    }
    if (events.lastError().isValid()) return {databaseFailure(), {}};
    return care_algorithms::buildTrending(input,snapshot);
}

// >>> DEFENSE-NOTE | trendingJson = 榜单转网络JSON
// 64位人数和次数用十进制字符串，保留条目顺序、模拟标记和summary_from_model供客户端验证和展示。
// <<< DEFENSE-NOTE | 本段讲解结束
QJsonObject WarehouseServer::trendingJson(const TrendingResult& result) {
    QJsonArray items;
    for(const auto& item:result.items) {
        QJsonObject row{{"term",item.term},{"distinct_users",QString::number(item.distinctUsers)},
                        {"effective_events",QString::number(item.effectiveEvents)}};
        if(item.work) {
            const auto& work=*item.work;
            row.insert("work",QJsonObject{{"resource",QJsonObject{{"work_id",work.resource.workId},
                {"source",QJsonObject{{"warehouse_id",QString::number(work.resource.source.warehouseId)},
                                     {"item_id",QString::number(work.resource.source.itemId)}}}}},
                {"media",media_json::toJson(work.media)},{"people",QJsonArray{}}});
        }
        items.append(row);
    }
    return {{"kind",result.kind==TrendingKind::Works?"works":"search_terms"},
        {"range",QJsonObject{{"begin_utc",QString::number(result.range.beginUtc)},{"end_utc",QString::number(result.range.endUtc)}}},
        {"items",items},{"generated_at_utc",QString::number(result.generatedAtUtc)},
        {"summary",result.summary},{"summary_from_model",result.summaryFromModel},
        {"contains_synthetic_data",result.containsSyntheticData}};
}

// >>> DEFENSE-NOTE | startTrendingSummary = 用户显式请求AI摘要
// 先核对当前连接登录身份、请求模式和同会话并发限制，生成规则结果与公共证据快照。
// <<< DEFENSE-NOTE | 本段讲解结束
void WarehouseServer::startTrendingSummary(QTcpSocket* socket,const std::shared_ptr<Session>& session,
                                          const QJsonObject& request) {
    const auto id=request.value("req_id").toString();
    const auto fail=[&](const char* code,const QString& message) {
        sendResponse(socket,{{"type","error_resp"},{"req_id",id},{"code",code},{"message",message}});
    };
    if(!request.value("payload").isObject()) {fail("InvalidArgument",QStringLiteral("请求参数无效。"));return;}
    if(!session->userId || !request.value("token").isString() || request.value("token").toString()!=session->token) {
        fail("NotAuthenticated",QStringLiteral("请在当前连接登录。"));return;
    }
    TrendingRequest input;
    const auto valid=parseTrendingRequest(request.value("payload").toObject(),input);
    if(!valid.ok()) {fail("InvalidArgument",valid.message);return;}
    if (!session->summaries.isEmpty()) {
        fail("ServerError", QStringLiteral("当前会话已有一个热榜 AI 摘要请求，请稍后再试。"));
        return;
    }
    const auto original=currentTrending(input);
    if(!original.status.ok() || !original.value) {fail("ServerError",original.status.message);return;}
    // >>> DEFENSE-NOTE | 生命周期 01｜记录发出时证据和会话
    // 保存榜单公共内容、token和用户ID；模型调用耗时期间可能退出、撤权或出现新行为。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const auto evidence=publicContents(trendingJson(*original.value));
    session->summaries.insert(id,{0,session->token,session->userId});
    QPointer<QTcpSocket> guarded(socket);
    const auto modelId=trendSummarizer_.summarize(*original.value,
        [this,guarded,session,id,input,evidence](TrendingResult summarized) {
            auto it=session->summaries.find(id);
            if(it==session->summaries.end()) return;
            const auto pending=it.value();session->summaries.erase(it);
            if(!guarded || guarded->state()!=QAbstractSocket::ConnectedState ||
               session->token!=pending.token || session->userId!=pending.userId) return;
            // >>> DEFENSE-NOTE | 生命周期 02｜模型回来后重新聚合
            // 先比对连接/用户/token，再从数据库得到当前规则结果。
            // 只有当前公共证据与发出时完全一致，才允许采用模型摘要；变化时返回当前规则结果，不自动再付费调用。
            // <<< DEFENSE-NOTE | 本段讲解结束
            auto current=currentTrending(input);
            if(!current.status.ok() || !current.value) {
                sendResponse(guarded,{{"type","error_resp"},{"req_id",id},{"code","ServerError"},
                                     {"message",current.status.message}});return;
            }
            if(summarized.summaryFromModel && publicContents(trendingJson(*current.value))==evidence) {
                current.value->summary=summarized.summary;current.value->summaryFromModel=true;
            }
            sendResponse(guarded,{{"type","get_trending_resp"},{"req_id",id},
                                 {"payload",QJsonObject{{"result",trendingJson(*current.value)}}}});
        });
    if (auto pending = session->summaries.find(id); pending != session->summaries.end())
        pending->modelId = modelId;
}
