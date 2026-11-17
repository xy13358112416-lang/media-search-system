#include "warehouse_server.h"

#include "care_person.h"
#include "deepseek_person_news.h"
#include "media_json.h"
#include "person_json.h"
#include "person_store_sqlite.h"

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
Status unavailable()
{
    return {ErrorCode::ServerError, QStringLiteral("人物目录暂时无法读取。")};
}

template<class Item>
Result<QJsonObject> pageResponse(Result<Page<Item>> result)
{
    if (!result.status.ok())
        return {result.status, {}};
    if (!result.value)
        return {unavailable(), {}};
    QJsonObject response{{"page", person_json::encode(*result.value)}};
    // Leave room for the response envelope, including the string request ID.
    if (QJsonDocument(response).toJson(QJsonDocument::Compact).size() > warehouse_protocol::MaxFrameSize - 2048)
        return {{ErrorCode::ServerError, QStringLiteral("人物查询结果过大，请缩小每页数量。")}, {}};
    return {{}, std::move(response)};
}
} // namespace

void WarehouseServer::startPersonLookup(QTcpSocket* socket, const std::shared_ptr<Session>& session,
                                        const QJsonObject& request)
{
    const auto id = request.value("req_id").toString();
    const auto fail = [this, socket, id](const char* code, const QString& message) {
        sendResponse(socket, {{"type", "error_resp"}, {"req_id", id}, {"code", code}, {"message", message}});
    };
    const auto token = request.value("token").toString();
    if (!session->userId || token != session->token) {
        fail("NotAuthenticated", QStringLiteral("请在当前连接登录。"));
        return;
    }
    const auto name = request.value("payload").toObject().value("name");
    if (!name.isString() || name.toString().size() > 128) {
        fail("InvalidArgument", QStringLiteral("请输入人物姓名。"));
        return;
    }
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
            if (!guarded || guarded->state() != QAbstractSocket::ConnectedState || token != session->token)
                return;
            if (!result.status.ok() || !result.value) {
                const auto code = result.status.code == ErrorCode::InvalidArgument ? "InvalidArgument" : "ServerError";
                sendResponse(guarded, {{"type", "error_resp"}, {"req_id", id}, {"code", code},
                                       {"message", result.status.message}});
                return;
            }
            QJsonArray sources;
            for (const auto& source : *result.value)
                sources.append(QJsonObject{{"title", source.title}, {"url", source.url.toString(QUrl::FullyEncoded)}});
            sendResponse(guarded, {{"type", "search_person_web_resp"}, {"req_id", id},
                                   {"payload", QJsonObject{{"sources", sources}}}});
        });
}

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
    if (!personId.isEmpty())
        sql += QStringLiteral(" AND c.person_id=?");
    sql += QStringLiteral(") ORDER BY s.work_id,s.warehouse_id,s.item_id LIMIT 1001");
    rows.prepare(sql);
    rows.addBindValue(userId);
    if (!personId.isEmpty())
        rows.addBindValue(personId);
    if (!rows.exec())
        return {unavailable(), {}};

    QList<ResourceSummary> works;
    qsizetype bytesRead = 0;
    while (rows.next()) {
        const auto bytes = rows.value(3).toByteArray();
        bytesRead += bytes.size();
        ResourceRef ref{rows.value(0).toString(), {rows.value(1).toLongLong(), rows.value(2).toLongLong()}};
        MediaRecord record;
        QString error;
        if (works.size() >= 1000 || bytesRead > 16 * 1024 * 1024 || !validate(ref).ok() ||
            !media_json::fromJson(QJsonDocument::fromJson(bytes).object(), record, error))
            return {unavailable(), {}};
        works.append({std::move(ref), std::move(record), {}});
    }
    if (rows.lastError().isValid())
        return {unavailable(), {}};
    rows.finish();

    auto catalog = personCatalog(userId, works);
    if (!catalog.status.ok() || !catalog.value || !includeAliases)
        return catalog;
    QSet<QString> visible;
    for (const auto& work : catalog.value->readableWorks)
        for (const auto& person : work.people)
            visible.insert(person.personId);

    QSqlQuery aliases(db_);
    aliases.prepare("SELECT alias FROM person_aliases WHERE person_id=? ORDER BY alias");
    for (const auto& id : visible) {
        aliases.bindValue(0, id);
        if (!aliases.exec())
            return {unavailable(), {}};
        while (aliases.next()) {
            const auto alias = aliases.value(0).toString();
            bytesRead += alias.size() * sizeof(QChar);
            if (alias.trimmed().isEmpty() || alias.size() > MaxQueryUnits || bytesRead > 16 * 1024 * 1024)
                return {unavailable(), {}};
            catalog.value->aliasesByPerson[id].append(alias);
        }
        if (aliases.lastError().isValid())
            return {unavailable(), {}};
    }
    return catalog;
}

std::optional<Result<QJsonObject>> WarehouseServer::dispatchPersonQuery(
    qint64 userId, const QString& type, const QJsonObject& payload)
{
    if (type != "search_people" && type != "list_follows" && type != "set_follow" &&
        type != "get_person_works" && type != "get_person_updates")
        return std::nullopt;
    if (!personStore_)
        return Result<QJsonObject>{unavailable(), {}};
    const auto invalid = Result<QJsonObject>{{ErrorCode::InvalidArgument, QStringLiteral("人物请求参数无效。")}, {}};
    const auto person = payload.value("person_id");
    const auto personId = person.toString();
    if (type == "set_follow") {
        const auto followed = payload.value("followed");
        if (!person.isString() || !validatePersonId(personId).ok() || !followed.isBool())
            return invalid;
        const auto status = personStore_->setFollow(userId, personId, followed.toBool());
        return status.ok() ? Result<QJsonObject>{{}, QJsonObject{}} : Result<QJsonObject>{status, {}};
    }
    PageRequest page;
    if (!person_json::decodePageRequest(payload, page))
        return invalid;
    if (type == "list_follows")
        return pageResponse(personStore_->followsFor(userId, page));
    if (type == "search_people") {
        const auto query = payload.value("query");
        const PersonSearchRequest input{query.toString(), page};
        if (!query.isString() || !validate(input).ok())
            return invalid;
        const auto catalog = readablePersonCatalog(userId, {}, true);
        if (!catalog.status.ok() || !catalog.value)
            return Result<QJsonObject>{unavailable(), {}};
        return pageResponse(care_algorithms::searchPeople(input, *catalog.value));
    }
    if (!person.isString() || !validatePersonId(personId).ok())
        return invalid;
    if (type == "get_person_works") {
        const auto catalog = readablePersonCatalog(userId, personId, false);
        if (!catalog.status.ok() || !catalog.value)
            return Result<QJsonObject>{unavailable(), {}};
        return pageResponse(care_algorithms::worksForPerson(personId, page, *catalog.value));
    }
    TimeRange range;
    if (!person_json::decodeTimeRange(payload.value("range"), range))
        return invalid;
    const auto cache = personStore_->updatesFor(userId, personId);
    if (!cache.status.ok())
        return Result<QJsonObject>{cache.status, {}};
    if (!cache.value)
        return Result<QJsonObject>{unavailable(), {}};
    const auto catalog = readablePersonCatalog(userId, personId, false);
    if (!catalog.status.ok() || !catalog.value)
        return Result<QJsonObject>{unavailable(), {}};
    return pageResponse(care_algorithms::personUpdates({personId, range, page}, *catalog.value, *cache.value,
                                                      QDateTime::currentSecsSinceEpoch()));
}
