// >>> MAIN-DEFENSE | 主逻辑文件导读
// 人物在线查询接线：先按用户可读作品组装目录，交给人物算法，结果编码给Qt；人物检索、作品、资讯、关注不能泄露私人仓库。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "warehouse_server.h"

#include "care_person.h"
#include "deepseek_person_news.h"
#include "media_json.h"
#include "person_json.h"
#include "person_store_sqlite.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>

using namespace care_contract;
using warehouse_contract::ErrorCode;

namespace {
// >>> MAIN-DEFENSE | 函数入口｜unavailable
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status unavailable()  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status unavailable()
{
    return {ErrorCode::ServerError, QStringLiteral("人物目录暂时无法读取。")};
}

// A name printed in old media metadata is not a confirmed real-world identity.
// Expose it as a read-only field grouping, so existing works remain discoverable
// without silently inserting a guessed person into the trusted person tables.
// >>> MAIN-DEFENSE | 函数入口｜mediaFieldCatalog
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Result<care_algorithms::CatalogSnapshot> mediaFieldCatalog(QSqlDatabase& db, qint64 userId)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<care_algorithms::CatalogSnapshot> mediaFieldCatalog(QSqlDatabase& db, qint64 userId)
{
    QSqlQuery query(db);
    query.prepare("SELECT s.work_id,s.warehouse_id,s.item_id,i.media FROM work_sources s "
                  "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                  "JOIN warehouses w ON w.id=s.warehouse_id WHERE w.owner_id=? OR w.is_public=1 "
                  "ORDER BY s.work_id,s.warehouse_id,s.item_id LIMIT 1001");
    query.addBindValue(userId);
    // >>> MAIN-DEFENSE | mediaFieldCatalog｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!query.exec()) return {unavailable(), {}};
    care_algorithms::CatalogSnapshot catalog;
    qsizetype bytesRead=0;
    while(query.next()) {
        const auto bytes=query.value(3).toByteArray(); bytesRead+=bytes.size();
        // >>> MAIN-DEFENSE | mediaFieldCatalog｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if(catalog.readableWorks.size()>=1000 || bytesRead>16*1024*1024) return {unavailable(), {}};
        MediaRecord media; QString error;
        if(!media_json::fromJson(QJsonDocument::fromJson(bytes).object(),media,error)) return {unavailable(), {}};
        ResourceSummary work{{query.value(0).toString(),{query.value(1).toLongLong(),query.value(2).toLongLong()}},media,{}};
        auto add=[&](QString name,PersonRole role) {
            name=name.simplified();
            // >>> MAIN-DEFENSE | mediaFieldCatalog｜处理阶段 03
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if(name.isEmpty() || name.startsWith(QStringLiteral("演示导演")) || name.startsWith(QStringLiteral("演示演员"))) return;
            const auto id=QStringLiteral("field:")+QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(),QCryptographicHash::Sha256).toHex());
            for(auto& p:work.people) if(p.personId==id) {if(!p.roles.contains(role)) p.roles.append(role);return;}
            work.people.append({id,name,{role},false});
        };
        const bool music=media.category==QStringLiteral("音乐");
        add(media.director,music?PersonRole::Musician:PersonRole::Director);
        // >>> MAIN-DEFENSE | mediaFieldCatalog｜处理阶段 04
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for(const auto& actor:media.actors) add(actor,music?PersonRole::Musician:PersonRole::Actor);
        catalog.readableWorks.append(work);
    }
    if(query.lastError().isValid()) return {unavailable(), {}};
    // >>> MAIN-DEFENSE | mediaFieldCatalog｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{},catalog};
}

template<class Item>
// >>> MAIN-DEFENSE | 函数入口｜pageResponse
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Result<QJsonObject> pageResponse(Result<Page<Item>> result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<QJsonObject> pageResponse(Result<Page<Item>> result)
{
    if (!result.status.ok())
        return {result.status, {}};
    // >>> MAIN-DEFENSE | pageResponse｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.value)
        return {unavailable(), {}};
    QJsonObject response{{"page", person_json::encode(*result.value)}};
    // Leave room for the response envelope, including the string request ID.
    // >>> MAIN-DEFENSE | pageResponse｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (QJsonDocument(response).toJson(QJsonDocument::Compact).size() > warehouse_protocol::MaxFrameSize - 2048)
        return {{ErrorCode::ServerError, QStringLiteral("人物查询结果过大，请缩小每页数量。")}, {}};
    return {{}, std::move(response)};
}
} // namespace

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::startPersonLookup
// 发起人物资料异步查询，绑定当前用户/连接，结束后不能向失效会话交付。
// 签名提示：void WarehouseServer::startPersonLookup(QTcpSocket* socket, const std::shared_ptr<Session>& session,                                          const QJsonObject& request)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseServer::startPersonLookup(QTcpSocket* socket, const std::shared_ptr<Session>& session,
                                        const QJsonObject& request)
{
    const auto id = request.value("req_id").toString();
    const auto fail = [this, socket, id](const char* code, const QString& message) {
        sendResponse(socket, {{"type", "error_resp"}, {"req_id", id}, {"code", code}, {"message", message}});
    };
    const auto token = request.value("token").toString();
    // >>> MAIN-DEFENSE | startPersonLookup｜处理阶段 01
    // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
    // <<< MAIN-DEFENSE
    if (!session->userId || token != session->token) {
        fail("NotAuthenticated", QStringLiteral("请在当前连接登录。"));
        return;
    }
    const auto name = request.value("payload").toObject().value("name");
    // >>> MAIN-DEFENSE | startPersonLookup｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!name.isString() || name.toString().size() > 128) {
        fail("InvalidArgument", QStringLiteral("请输入人物姓名。"));
        return;
    }
    // >>> MAIN-DEFENSE | startPersonLookup｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!session->personLookup.isEmpty() || personLookupsInFlight_ >= 3) {
        fail("ServerError", QStringLiteral("联网人物查询正在处理，请稍后再试。"));
        return;
    }
    session->personLookup = id;
    ++personLookupsInFlight_;
    deepseek_person_news::findPeople(*personLookupNetwork_, name.toString(),
        qgetenv("MEDIA_SERVER_DEEPSEEK_API_KEY"),
        [this, guarded = QPointer<QTcpSocket>(socket), session, id, token](Result<QList<PersonWebSource>> result) {
            --personLookupsInFlight_;
            session->personLookup.clear();
            // >>> MAIN-DEFENSE | startPersonLookup｜处理阶段 04
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!guarded || guarded->state() != QAbstractSocket::ConnectedState || token != session->token)
                return;
            if (!result.status.ok() || !result.value) {
                const auto code = result.status.code == ErrorCode::InvalidArgument ? "InvalidArgument" : "ServerError";
                sendResponse(guarded, {{"type", "error_resp"}, {"req_id", id}, {"code", code},
                                       {"message", result.status.message}});
                return;
            }
            QJsonArray sources;
            // >>> MAIN-DEFENSE | startPersonLookup｜处理阶段 05
            // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
            // <<< MAIN-DEFENSE
            for (const auto& source : *result.value) {
                QJsonArray works;
                for (const auto& work : source.works)
                    works.append(QJsonObject{{"title", work.title}, {"year", work.year}, {"role", work.role},
                                             {"source_url", work.sourceUrl.toString(QUrl::FullyEncoded)}});
                sources.append(QJsonObject{{"title", source.title}, {"url", source.url.toString(QUrl::FullyEncoded)},
                                           {"biography", source.biography}, {"works", works}});
            }
            sendResponse(guarded, {{"type", "search_person_web_resp"}, {"req_id", id},
                                   {"payload", QJsonObject{{"sources", sources}}}});
        });
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::readablePersonCatalog
// 从当前用户可读来源构建人物目录，并附加可信关联/别名与本人关注。
// 签名提示：Result<care_algorithms::CatalogSnapshot> WarehouseServer::readablePersonCatalog(      qint64 userId, const QString& personId, bool includeAliases)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<care_algorithms::CatalogSnapshot> WarehouseServer::readablePersonCatalog(
    qint64 userId, const QString& personId, bool includeAliases)
{
    // Confirmed credits only exist for registered works. Keep discovery read-only.
    QSqlQuery rows(db_);
    QString sql = QStringLiteral(
        "SELECT s.work_id,s.warehouse_id,s.item_id,i.media FROM work_sources s "
        "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
        "JOIN warehouses w ON w.id=s.warehouse_id WHERE (w.owner_id=? OR w.is_public=1) "
        "AND EXISTS (SELECT 1 FROM person_credits c WHERE c.work_id=s.work_id");
    // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!personId.isEmpty())
        sql += QStringLiteral(" AND c.person_id=?");
    sql += QStringLiteral(") ORDER BY s.work_id,s.warehouse_id,s.item_id LIMIT 1001");
    rows.prepare(sql);
    rows.addBindValue(userId);
    // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!personId.isEmpty())
        rows.addBindValue(personId);
    if (!rows.exec())
        return {unavailable(), {}};

    QList<ResourceSummary> works;
    qsizetype bytesRead = 0;
    // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 03
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (rows.next()) {
        const auto bytes = rows.value(3).toByteArray();
        bytesRead += bytes.size();
        ResourceRef ref{rows.value(0).toString(), {rows.value(1).toLongLong(), rows.value(2).toLongLong()}};
        MediaRecord record;
        QString error;
        // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (works.size() >= 1000 || bytesRead > 16 * 1024 * 1024 || !validate(ref).ok() ||
            !media_json::fromJson(QJsonDocument::fromJson(bytes).object(), record, error))
            return {unavailable(), {}};
        works.append({std::move(ref), std::move(record), {}});
    }
    // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (rows.lastError().isValid())
        return {unavailable(), {}};
    rows.finish();

    auto catalog = personCatalog(userId, works);
    // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 06
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!catalog.status.ok() || !catalog.value || !includeAliases)
        return catalog;
    QSet<QString> visible;
    for (const auto& work : catalog.value->readableWorks)
        // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 07
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& person : work.people)
            visible.insert(person.personId);

    QSqlQuery aliases(db_);
    // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 08
    // 准备参数化SQL，涉及 person_aliases。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    aliases.prepare("SELECT alias FROM person_aliases WHERE person_id=? ORDER BY alias");
    for (const auto& id : visible) {
        aliases.bindValue(0, id);
        if (!aliases.exec())
            // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 09
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return {unavailable(), {}};
        while (aliases.next()) {
            const auto alias = aliases.value(0).toString();
            bytesRead += alias.size() * sizeof(QChar);
            // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 10
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (alias.trimmed().isEmpty() || alias.size() > MaxQueryUnits || bytesRead > 16 * 1024 * 1024)
                return {unavailable(), {}};
            catalog.value->aliasesByPerson[id].append(alias);
        }
        // >>> MAIN-DEFENSE | readablePersonCatalog｜处理阶段 11
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (aliases.lastError().isValid())
            return {unavailable(), {}};
    }
    return catalog;
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::dispatchPersonQuery
// 路由人物搜索、关注、作品与资讯请求；返回可选结果表示是否命中此人物分支。
// 签名提示：std::optional<Result<QJsonObject>> WarehouseServer::dispatchPersonQuery(      qint64 userId, const QString& type, const QJsonObject& payload)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<Result<QJsonObject>> WarehouseServer::dispatchPersonQuery(
    qint64 userId, const QString& type, const QJsonObject& payload)
{
    if (type != "search_people" && type != "list_follows" && type != "set_follow" &&
        type != "get_person_works" && type != "get_person_updates")
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return std::nullopt;
    if (!personStore_)
        return Result<QJsonObject>{unavailable(), {}};
    const auto invalid = Result<QJsonObject>{{ErrorCode::InvalidArgument, QStringLiteral("人物请求参数无效。")}, {}};
    const auto person = payload.value("person_id");
    const auto personId = person.toString();
    // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 02
    // 请求分支：set_follow。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "set_follow") {
        const auto followed = payload.value("followed");
        if (!person.isString() || !validatePersonId(personId).ok() || !followed.isBool())
            return invalid;
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (personId.startsWith("field:"))
            return Result<QJsonObject>{{ErrorCode::InvalidArgument, QStringLiteral("媒体姓名尚未核实身份，请先查看作品或联网资料。")}, {}};
        const auto status = personStore_->setFollow(userId, personId, followed.toBool());
        return status.ok() ? Result<QJsonObject>{{}, QJsonObject{}} : Result<QJsonObject>{status, {}};
    }
    PageRequest page;
    // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!person_json::decodePageRequest(payload, page))
        return invalid;
    if (type == "list_follows")
        return pageResponse(personStore_->followsFor(userId, page));
    // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 05
    // 请求分支：search_people。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "search_people") {
        const auto query = payload.value("query");
        const PersonSearchRequest input{query.toString(), page};
        if (!query.isString() || !validate(input).ok())
            // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 06
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return invalid;
        const auto catalog = readablePersonCatalog(userId, {}, true);
        if (!catalog.status.ok() || !catalog.value)
            return Result<QJsonObject>{unavailable(), {}};
        PersonSearchRequest all=input; all.page={0,MaxPageSize};
        QList<PersonSummary> combined;
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 07
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (;;) {
            auto confirmed=care_algorithms::searchPeople(all,*catalog.value);
            if(!confirmed.status.ok() || !confirmed.value) return pageResponse(confirmed);
            combined.append(confirmed.value->items);
            // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 08
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if(!confirmed.value->nextOffset) break;
            if(combined.size()>=1000) return Result<QJsonObject>{unavailable(),{}};
            all.page.offset=*confirmed.value->nextOffset;
        }
        all.page.offset=0;
        auto fields=mediaFieldCatalog(db_,userId);
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 09
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if(!fields.status.ok() || !fields.value) return Result<QJsonObject>{unavailable(),{}};
        auto fallback=care_algorithms::searchPeople(all,*fields.value);
        if(!fallback.status.ok() || !fallback.value) return pageResponse(fallback);
        QSet<QString> names;
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 10
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for(const auto& item:combined) {
            names.insert(item.name.toCaseFolded());
            for(const auto& alias:catalog.value->aliasesByPerson.value(item.personId)) names.insert(alias.toCaseFolded());
        }
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 11
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (;;) {
            for(const auto& item:fallback.value->items)
                if(!names.contains(item.name.toCaseFolded())) {combined.append(item);names.insert(item.name.toCaseFolded());}
            if(!fallback.value->nextOffset) break;
            // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 12
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if(combined.size()>=2000) return Result<QJsonObject>{unavailable(),{}};
            all.page.offset=*fallback.value->nextOffset;
            fallback=care_algorithms::searchPeople(all,*fields.value);
            if(!fallback.status.ok() || !fallback.value) return pageResponse(fallback);
        }
        Page<PersonSummary> output;
        output.items=combined.mid(page.offset,page.limit);
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 13
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if(page.offset+output.items.size()<combined.size()) output.nextOffset=page.offset+output.items.size();
        return pageResponse(Result<Page<PersonSummary>>{{},output});
    }
    if (!person.isString() || !validatePersonId(personId).ok())
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 14
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return invalid;
    if (type == "get_person_works") {
        if(personId.startsWith("field:")) {
            const auto fields=mediaFieldCatalog(db_,userId);
            // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 15
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if(!fields.status.ok() || !fields.value) return Result<QJsonObject>{unavailable(),{}};
            return pageResponse(care_algorithms::worksForPerson(personId,page,*fields.value));
        }
        const auto catalog = readablePersonCatalog(userId, personId, false);
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 16
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!catalog.status.ok() || !catalog.value)
            return Result<QJsonObject>{unavailable(), {}};
        return pageResponse(care_algorithms::worksForPerson(personId, page, *catalog.value));
    }
    TimeRange range;
    // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 17
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!person_json::decodeTimeRange(payload.value("range"), range))
        return invalid;
    const auto cache = personStore_->updatesFor(userId, personId);
    if (!cache.status.ok())
        // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 18
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return Result<QJsonObject>{cache.status, {}};
    if (!cache.value)
        return Result<QJsonObject>{unavailable(), {}};
    const auto catalog = readablePersonCatalog(userId, personId, false);
    // >>> MAIN-DEFENSE | dispatchPersonQuery｜处理阶段 19
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!catalog.status.ok() || !catalog.value)
        return Result<QJsonObject>{unavailable(), {}};
    return pageResponse(care_algorithms::personUpdates({personId, range, page}, *catalog.value, *cache.value,
                                                      QDateTime::currentSecsSinceEpoch()));
}
