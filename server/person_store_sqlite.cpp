#include "person_store_sqlite.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <limits>

namespace person_store {
using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
Status error(const QString& text) { return {ErrorCode::ServerError, text}; }
Status invalid(const QString& text) { return {ErrorCode::InvalidArgument, text}; }
Status denied() { return {ErrorCode::PermissionDenied, QStringLiteral("没有访问该人物或资源的权限。")}; }
bool validPersonId(const QString& id) { return validate(PersonUpdatesRequest{id,{0,1},{}}).ok(); }
bool validUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty() && url.toString().size() <= 2048;
}
bool validTimestamp(qint64 time) { return time >= 0 && time < MaxUtcExclusive; }
bool validName(const QString& name) {
    if (name.simplified().isEmpty() || name.size() > MaxQueryUnits) return false;
    for (auto ch : name) if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
std::optional<PersonRole> roleFor(const QString& role) {
    if (role == "actor") return PersonRole::Actor;
    if (role == "director") return PersonRole::Director;
    if (role == "musician") return PersonRole::Musician;
    return {};
}
QString roleName(PersonRole role) {
    switch (role) {
    case PersonRole::Actor: return "actor";
    case PersonRole::Director: return "director";
    case PersonRole::Musician: return "musician";
    }
    return {};
}
Status checkUser(QSqlDatabase& db, qint64 id) {
    if (id <= 0) return invalid(QStringLiteral("用户标识无效。"));
    if (!db.isOpen() || db.driverName() != "QSQLITE") return error(QStringLiteral("人物数据库不可用。"));
    QSqlQuery q(db); q.prepare("SELECT 1 FROM network_users WHERE id=?"); q.addBindValue(id);
    if (!q.exec()) return error(QStringLiteral("无法查询用户。"));
    if (!q.next()) return denied();
    return {};
}
Result<QList<PersonRole>> decodeRoles(const QByteArray& bytes) {
    if (bytes.size() > 256) return {error(QStringLiteral("关注角色数据损坏。")), {}};
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isArray()) return {error(QStringLiteral("关注角色数据损坏。")), {}};
    QList<PersonRole> roles;
    for (const auto& value : document.array()) {
        if (!value.isString()) return {error(QStringLiteral("关注角色数据损坏。")), {}};
        const auto role = roleFor(value.toString());
        if (!role || roles.contains(*role)) return {error(QStringLiteral("关注角色数据损坏。")), {}};
        roles.append(*role);
    }
    if (roles.isEmpty()) return {error(QStringLiteral("关注角色数据损坏。")), {}};
    return {{}, roles};
}
}
SqliteStore::SqliteStore(QSqlDatabase database) : db_(database) {}

Result<care_algorithms::PersonContext> SqliteStore::contextFor(qint64 userId, const QList<ResourceRef>& resources) {
    auto status = checkUser(db_, userId); if (!status.ok()) return {status, {}};
    if (resources.size() > 1000) return {invalid(QStringLiteral("资源数量超过上限。")), {}};
    care_algorithms::PersonContext context;
    QSet<QString> visiblePeople;
    for (const auto& resource : resources) {
        status = validate(resource); if (!status.ok()) return {status, {}};
        QSqlQuery permission(db_);
        permission.prepare("SELECT 1 FROM work_sources s JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                           "JOIN warehouses w ON w.id=s.warehouse_id WHERE s.work_id=? AND s.warehouse_id=? AND s.item_id=? "
                           "AND (w.owner_id=? OR w.is_public=1)");
        permission.addBindValue(resource.workId); permission.addBindValue(resource.source.warehouseId);
        permission.addBindValue(resource.source.itemId); permission.addBindValue(userId);
        if (!permission.exec()) return {error(QStringLiteral("无法检查资源权限。")), {}};
        if (!permission.next()) return {denied(), {}};
        QSqlQuery rows(db_);
        rows.prepare("SELECT p.person_id,p.display_name,c.role FROM person_credits c JOIN person_catalog p ON p.person_id=c.person_id "
                     "WHERE c.work_id=? ORDER BY p.person_id,c.role");
        rows.addBindValue(resource.workId);
        if (!rows.exec()) return {error(QStringLiteral("无法读取人物关联。")), {}};
        QMap<QString,PersonSummary> people;
        while (rows.next()) {
            const auto id = rows.value(0).toString(); const auto name = rows.value(1).toString(); const auto role = roleFor(rows.value(2).toString());
            if (!validPersonId(id) || !validName(name) || !role) return {error(QStringLiteral("人物关联数据无效。")), {}};
            auto it = people.find(id);
            if (it == people.end()) it = people.insert(id, {id,name,{},false});
            if (!it->roles.contains(*role)) it->roles.append(*role);
            if (people.size() > care_algorithms::MaxConfirmedPeoplePerWork) return {error(QStringLiteral("作品人物数量超过上限。")), {}};
        }
        if (rows.lastError().isValid()) return {error(QStringLiteral("读取人物关联失败。")), {}};
        for (const auto& person : people) visiblePeople.insert(person.personId);
        context.credits.append({resource,people.values()});
    }
    QSqlQuery follows(db_); follows.prepare("SELECT person_id FROM person_follows WHERE user_id=? ORDER BY person_id LIMIT 10001"); follows.addBindValue(userId);
    if (!follows.exec()) return {error(QStringLiteral("无法读取关注。")), {}};
    int count = 0;
    while (follows.next()) {
        const auto id = follows.value(0).toString();
        if (++count > 10000 || !validPersonId(id)) return {error(QStringLiteral("关注数据无效或超过上限。")), {}};
        if (visiblePeople.contains(id)) context.followedPeople.insert(id);
    }
    if (follows.lastError().isValid()) return {error(QStringLiteral("读取关注失败。")), {}};
    return {{},context};
}

Result<Page<PersonSummary>> SqliteStore::followsFor(qint64 userId, const PageRequest& page) {
    auto status = checkUser(db_, userId); if (!status.ok()) return {status, {}};
    status = validate(page); if (!status.ok()) return {status, {}};
    QSqlQuery q(db_); q.prepare("SELECT person_id,known_name,known_roles_json FROM person_follows WHERE user_id=? ORDER BY person_id LIMIT ? OFFSET ?");
    q.addBindValue(userId); q.addBindValue(page.limit+1); q.addBindValue(page.offset);
    if (!q.exec()) return {error(QStringLiteral("无法读取关注列表。")), {}};
    Page<PersonSummary> result;
    while (q.next()) {
        if (result.items.size() == page.limit) { result.nextOffset = page.offset + page.limit; break; }
        const auto id=q.value(0).toString(); const auto name=q.value(1).toString(); const auto roles=decodeRoles(q.value(2).toByteArray());
        if (!validPersonId(id) || !validName(name) || !roles.status.ok()) return {error(QStringLiteral("关注列表数据无效。")), {}};
        result.items.append({id,name,*roles.value,true});
    }
    if (q.lastError().isValid()) return {error(QStringLiteral("读取关注列表失败。")), {}};
    return {{},result};
}

Status SqliteStore::setFollow(qint64 userId, const QString& personId, bool followed) {
    auto status = checkUser(db_, userId); if (!status.ok()) return status;
    if (!validPersonId(personId)) return invalid(QStringLiteral("人物标识无效。"));
    if (!db_.transaction()) return error(QStringLiteral("无法开始关注事务。"));
    if (!followed) {
        QSqlQuery q(db_); q.prepare("DELETE FROM person_follows WHERE user_id=? AND person_id=?"); q.addBindValue(userId); q.addBindValue(personId);
        if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法取消关注。")); }
    } else {
        QSqlQuery q(db_);
        q.prepare("SELECT DISTINCT p.display_name,c.role FROM person_catalog p JOIN person_credits c ON c.person_id=p.person_id "
                  "JOIN work_sources s ON s.work_id=c.work_id JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                  "JOIN warehouses w ON w.id=s.warehouse_id WHERE p.person_id=? AND (w.owner_id=? OR w.is_public=1) ORDER BY c.role");
        q.addBindValue(personId); q.addBindValue(userId);
        if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法检查人物权限。")); }
        QString name; QJsonArray roles;
        while (q.next()) {
            const auto nextName=q.value(0).toString(); const auto role=roleFor(q.value(1).toString());
            if (!validName(nextName) || !role || (!name.isEmpty() && name != nextName)) { db_.rollback(); return error(QStringLiteral("人物数据无效。")); }
            name=nextName; roles.append(roleName(*role));
        }
        if (q.lastError().isValid()) { db_.rollback(); return error(QStringLiteral("无法读取人物权限。")); }
        if (name.isEmpty()) { db_.rollback(); return denied(); }
        q.finish(); q.prepare("INSERT INTO person_follows(user_id,person_id,known_name,known_roles_json) VALUES(?,?,?,?) ON CONFLICT(user_id,person_id) DO NOTHING");
        q.addBindValue(userId); q.addBindValue(personId); q.addBindValue(name); q.addBindValue(QJsonDocument(roles).toJson(QJsonDocument::Compact));
        if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法保存关注。")); }
    }
    if (!db_.commit()) { db_.rollback(); return error(QStringLiteral("无法提交关注事务。")); }
    return {};
}
Result<QList<PersonUpdate>> SqliteStore::updatesFor(qint64, const QString&) { return {error(QStringLiteral("资讯缓存尚未实现。")), {}}; }
Status SqliteStore::mergeUpdates(const QString&, const QList<PersonUpdate>&) { return error(QStringLiteral("资讯缓存尚未实现。")); }
}
