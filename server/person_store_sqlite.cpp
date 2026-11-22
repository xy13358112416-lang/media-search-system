// >>> MAIN-DEFENSE | 主逻辑文件导读
// 人物SQLite仓储：当前用户可见人物上下文、本人关注、资讯缓存与刷新代次。关注不扩大资源权限，取关仍可处理已失去来源的旧关注。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "person_store_sqlite.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QMetaType>
#include <QSqlError>
#include <QSqlQuery>
#include <limits>

namespace person_store {
using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
// >>> MAIN-DEFENSE | 函数入口｜error
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status error(const QString& text) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status error(const QString& text) { return {ErrorCode::ServerError, text}; }
// >>> MAIN-DEFENSE | 函数入口｜invalid
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status invalid(const QString& text) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status invalid(const QString& text) { return {ErrorCode::InvalidArgument, text}; }
// >>> MAIN-DEFENSE | 函数入口｜denied
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status denied() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status denied() { return {ErrorCode::PermissionDenied, QStringLiteral("没有访问该人物或资源的权限。")}; }
// >>> MAIN-DEFENSE | 函数入口｜validPersonId
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool validPersonId(const QString& id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool validPersonId(const QString& id) { return validate(PersonUpdatesRequest{id,{0,1},{}}).ok(); }
// >>> MAIN-DEFENSE | 函数入口｜validUrl
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool validUrl(const QUrl& url) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool validUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty() && url.toString().size() <= 2048;
}
// >>> MAIN-DEFENSE | 函数入口｜validTimestamp
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool validTimestamp(qint64 time) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool validTimestamp(qint64 time) { return time >= 0 && time < MaxUtcExclusive; }
// >>> MAIN-DEFENSE | 函数入口｜readDatabaseInteger
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readDatabaseInteger(const QVariant& value, qint64& result) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readDatabaseInteger(const QVariant& value, qint64& result) {
    if (value.isNull()) return false;
    const auto type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::UInt &&
        type != QMetaType::LongLong && type != QMetaType::ULongLong)
        // >>> MAIN-DEFENSE | readDatabaseInteger｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    bool ok = false;
    result = value.toLongLong(&ok);
    return ok;
}
// >>> MAIN-DEFENSE | 函数入口｜readOptionalTimestamp
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readOptionalTimestamp(const QVariant& value, std::optional<qint64>& result) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readOptionalTimestamp(const QVariant& value, std::optional<qint64>& result) {
    if (value.isNull()) { result.reset(); return true; }
    qint64 parsed = 0;
    if (!readDatabaseInteger(value, parsed) || !validTimestamp(parsed)) return false;
    result = parsed;
    // >>> MAIN-DEFENSE | readOptionalTimestamp｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜validName
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool validName(const QString& name) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool validName(const QString& name) {
    if (name.simplified().isEmpty() || name.size() > MaxQueryUnits) return false;
    for (auto ch : name) if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜roleFor
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：std::optional<PersonRole> roleFor(const QString& role) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<PersonRole> roleFor(const QString& role) {
    if (role == "actor") return PersonRole::Actor;
    if (role == "director") return PersonRole::Director;
    if (role == "musician") return PersonRole::Musician;
    // >>> MAIN-DEFENSE | roleFor｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜roleName
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QString roleName(PersonRole role) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString roleName(PersonRole role) {
    switch (role) {
    case PersonRole::Actor: return "actor";
    case PersonRole::Director: return "director";
    case PersonRole::Musician: return "musician";
    }
    // >>> MAIN-DEFENSE | roleName｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜checkUser
// 核对当前会话与目标用户身份，阻止读取或写入其他用户个人历史。
// 签名提示：Status checkUser(QSqlDatabase& db, qint64 id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status checkUser(QSqlDatabase& db, qint64 id) {
    if (id <= 0) return invalid(QStringLiteral("用户标识无效。"));
    if (!db.isOpen() || db.driverName() != "QSQLITE") return error(QStringLiteral("人物数据库不可用。"));
    QSqlQuery q(db); q.prepare("SELECT 1 FROM network_users WHERE id=?"); q.addBindValue(id);
    // >>> MAIN-DEFENSE | checkUser｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec()) return error(QStringLiteral("无法查询用户。"));
    if (!q.next()) return denied();
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜checkReadablePerson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status checkReadablePerson(QSqlDatabase& db, qint64 userId, const QString& personId) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status checkReadablePerson(QSqlDatabase& db, qint64 userId, const QString& personId) {
    auto status = checkUser(db, userId); if (!status.ok()) return status;
    if (!validPersonId(personId)) return invalid(QStringLiteral("人物标识无效。"));
    QSqlQuery q(db);
    // >>> MAIN-DEFENSE | checkReadablePerson｜处理阶段 01
    // 准备参数化SQL，涉及 person_credits、work_sources、warehouse_items、warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    q.prepare("SELECT 1 FROM person_credits c "
              "JOIN work_sources s ON s.work_id=c.work_id "
              "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
              "JOIN warehouses w ON w.id=s.warehouse_id "
              "WHERE c.person_id=? AND (w.owner_id=? OR w.is_public=1) LIMIT 1");
    q.addBindValue(personId);
    q.addBindValue(userId);
    // >>> MAIN-DEFENSE | checkReadablePerson｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec()) return error(QStringLiteral("无法检查人物权限。"));
    if (!q.next()) return denied();
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜validateUpdate
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status validateUpdate(const PersonUpdate& update, const QString& personId, qint64 completedAtUtc) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status validateUpdate(const PersonUpdate& update, const QString& personId, qint64 completedAtUtc) {
    if (!validPersonId(update.updateId) || update.personId != personId ||
        !validName(update.title) || !validUrl(update.sourceUrl) ||
        (update.kind != UpdateKind::News && update.kind != UpdateKind::Release) ||
        !validTimestamp(update.fetchedAtUtc) || update.fetchedAtUtc > completedAtUtc)
        // >>> MAIN-DEFENSE | validateUpdate｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return error(QStringLiteral("人物资讯包含无效编号、来源、内容或日期。"));
    if (update.publishedAtUtc &&
        (!validTimestamp(*update.publishedAtUtc) || *update.publishedAtUtc > completedAtUtc))
        return error(QStringLiteral("人物资讯发布时间无效。"));
    // >>> MAIN-DEFENSE | validateUpdate｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (update.eventAtUtc && !validTimestamp(*update.eventAtUtc))
        return error(QStringLiteral("人物资讯事件时间无效。"));
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜sameUpdate
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool sameUpdate(const PersonUpdate& a, const PersonUpdate& b) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool sameUpdate(const PersonUpdate& a, const PersonUpdate& b) {
    return a.updateId == b.updateId && a.personId == b.personId && a.title == b.title &&
           a.kind == b.kind && a.sourceUrl == b.sourceUrl &&
           a.publishedAtUtc == b.publishedAtUtc && a.eventAtUtc == b.eventAtUtc &&
           a.fetchedAtUtc == b.fetchedAtUtc && a.stale == b.stale;
}
// >>> MAIN-DEFENSE | 函数入口｜decodeRoles
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Result<QList<PersonRole>> decodeRoles(const QByteArray& bytes) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<QList<PersonRole>> decodeRoles(const QByteArray& bytes) {
    if (bytes.size() > 256) return {error(QStringLiteral("关注角色数据损坏。")), {}};
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isArray()) return {error(QStringLiteral("关注角色数据损坏。")), {}};
    QList<PersonRole> roles;
    // >>> MAIN-DEFENSE | decodeRoles｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& value : document.array()) {
        if (!value.isString()) return {error(QStringLiteral("关注角色数据损坏。")), {}};
        const auto role = roleFor(value.toString());
        if (!role || roles.contains(*role)) return {error(QStringLiteral("关注角色数据损坏。")), {}};
        roles.append(*role);
    }
    // >>> MAIN-DEFENSE | decodeRoles｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (roles.isEmpty()) return {error(QStringLiteral("关注角色数据损坏。")), {}};
    return {{}, roles};
}
}
// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::SqliteStore
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：SqliteStore::SqliteStore(QSqlDatabase database) : db_(database) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
SqliteStore::SqliteStore(QSqlDatabase database) : db_(database) {}

// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::contextFor
// 按当前认证用户和精确资源引用查询已确认人物关联与关注，构造PersonContext供算法适配。
// 签名提示：Result<care_algorithms::PersonContext> SqliteStore::contextFor(qint64 userId, const QList<ResourceRef>& resources) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<care_algorithms::PersonContext> SqliteStore::contextFor(qint64 userId, const QList<ResourceRef>& resources) {
    auto status = checkUser(db_, userId); if (!status.ok()) return {status, {}};
    if (resources.size() > 1000) return {invalid(QStringLiteral("资源数量超过上限。")), {}};
    care_algorithms::PersonContext context;
    // >>> MAIN-DEFENSE | contextFor｜处理阶段 01
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    QSet<QString> visiblePeople;
    for (const auto& resource : resources) {
        status = validate(resource); if (!status.ok()) return {status, {}};
        QSqlQuery permission(db_);
        // >>> MAIN-DEFENSE | contextFor｜处理阶段 02
        // 准备参数化SQL，涉及 work_sources、warehouse_items、warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        permission.prepare("SELECT 1 FROM work_sources s JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                           "JOIN warehouses w ON w.id=s.warehouse_id WHERE s.work_id=? AND s.warehouse_id=? AND s.item_id=? "
                           "AND (w.owner_id=? OR w.is_public=1)");
        permission.addBindValue(resource.workId); permission.addBindValue(resource.source.warehouseId);
        permission.addBindValue(resource.source.itemId); permission.addBindValue(userId);
        // >>> MAIN-DEFENSE | contextFor｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!permission.exec()) return {error(QStringLiteral("无法检查资源权限。")), {}};
        if (!permission.next()) return {denied(), {}};
        QSqlQuery rows(db_);
        rows.prepare("SELECT p.person_id,p.display_name,c.role FROM person_credits c JOIN person_catalog p ON p.person_id=c.person_id "
                     "WHERE c.work_id=? ORDER BY p.person_id,c.role");
        rows.addBindValue(resource.workId);
        // >>> MAIN-DEFENSE | contextFor｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!rows.exec()) return {error(QStringLiteral("无法读取人物关联。")), {}};
        QMap<QString,PersonSummary> people;
        while (rows.next()) {
            const auto id = rows.value(0).toString(); const auto name = rows.value(1).toString(); const auto role = roleFor(rows.value(2).toString());
            // >>> MAIN-DEFENSE | contextFor｜处理阶段 05
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!validPersonId(id) || !validName(name) || !role) return {error(QStringLiteral("人物关联数据无效。")), {}};
            auto it = people.find(id);
            if (it == people.end()) it = people.insert(id, {id,name,{},false});
            if (!it->roles.contains(*role)) it->roles.append(*role);
            // >>> MAIN-DEFENSE | contextFor｜处理阶段 06
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (people.size() > care_algorithms::MaxConfirmedPeoplePerWork) return {error(QStringLiteral("作品人物数量超过上限。")), {}};
        }
        if (rows.lastError().isValid()) return {error(QStringLiteral("读取人物关联失败。")), {}};
        for (const auto& person : people) visiblePeople.insert(person.personId);
        context.credits.append({resource,people.values()});
    }
    // >>> MAIN-DEFENSE | contextFor｜处理阶段 07
    // 准备参数化SQL，涉及 person_follows。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    QSqlQuery follows(db_); follows.prepare("SELECT person_id FROM person_follows WHERE user_id=? ORDER BY person_id LIMIT 10001"); follows.addBindValue(userId);
    if (!follows.exec()) return {error(QStringLiteral("无法读取关注。")), {}};
    int count = 0;
    while (follows.next()) {
        const auto id = follows.value(0).toString();
        // >>> MAIN-DEFENSE | contextFor｜处理阶段 08
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (++count > 10000 || !validPersonId(id)) return {error(QStringLiteral("关注数据无效或超过上限。")), {}};
        if (visiblePeople.contains(id)) context.followedPeople.insert(id);
    }
    if (follows.lastError().isValid()) return {error(QStringLiteral("读取关注失败。")), {}};
    // >>> MAIN-DEFENSE | contextFor｜处理阶段 09
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{},context};
}

// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::followsFor
// 只读本人关注列表，用LIMIT多取一项判断是否还有下一页，保留已知姓名角色快照。
// 签名提示：Result<Page<PersonSummary>> SqliteStore::followsFor(qint64 userId, const PageRequest& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Page<PersonSummary>> SqliteStore::followsFor(qint64 userId, const PageRequest& page) {
    auto status = checkUser(db_, userId); if (!status.ok()) return {status, {}};
    status = validate(page); if (!status.ok()) return {status, {}};
    QSqlQuery q(db_); q.prepare("SELECT person_id,known_name,known_roles_json FROM person_follows WHERE user_id=? ORDER BY person_id LIMIT ? OFFSET ?");
    q.addBindValue(userId); q.addBindValue(page.limit+1); q.addBindValue(page.offset);
    // >>> MAIN-DEFENSE | followsFor｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec()) return {error(QStringLiteral("无法读取关注列表。")), {}};
    Page<PersonSummary> result;
    while (q.next()) {
        if (result.items.size() == page.limit) { result.nextOffset = page.offset + page.limit; break; }
        const auto id=q.value(0).toString(); const auto name=q.value(1).toString(); const auto roles=decodeRoles(q.value(2).toByteArray());
        // >>> MAIN-DEFENSE | followsFor｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!validPersonId(id) || !validName(name) || !roles.status.ok()) return {error(QStringLiteral("关注列表数据无效。")), {}};
        result.items.append({id,name,*roles.value,true});
    }
    if (q.lastError().isValid()) return {error(QStringLiteral("读取关注列表失败。")), {}};
    // >>> MAIN-DEFENSE | followsFor｜处理阶段 03
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{},result};
}

// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::setFollow
// 关注先核验人物可见性；取关按本人user_id+person_id删除，撤权后的旧关注仍能取消。
// 签名提示：Status SqliteStore::setFollow(qint64 userId, const QString& personId, bool followed) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status SqliteStore::setFollow(qint64 userId, const QString& personId, bool followed) {
    auto status = checkUser(db_, userId); if (!status.ok()) return status;
    if (!validPersonId(personId)) return invalid(QStringLiteral("人物标识无效。"));
    if (!db_.transaction()) return error(QStringLiteral("无法开始关注事务。"));
    // >>> MAIN-DEFENSE | setFollow｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!followed) {
        QSqlQuery q(db_); q.prepare("DELETE FROM person_follows WHERE user_id=? AND person_id=?"); q.addBindValue(userId); q.addBindValue(personId);
        if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法取消关注。")); }
    } else {
        QSqlQuery q(db_);
        // >>> MAIN-DEFENSE | setFollow｜处理阶段 02
        // 准备参数化SQL，涉及 person_catalog、person_credits、work_sources、warehouse_items、warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        q.prepare("SELECT DISTINCT p.display_name,c.role FROM person_catalog p JOIN person_credits c ON c.person_id=p.person_id "
                  "JOIN work_sources s ON s.work_id=c.work_id JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                  "JOIN warehouses w ON w.id=s.warehouse_id WHERE p.person_id=? AND (w.owner_id=? OR w.is_public=1) ORDER BY c.role");
        q.addBindValue(personId); q.addBindValue(userId);
        // >>> MAIN-DEFENSE | setFollow｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法检查人物权限。")); }
        QString name; QJsonArray roles;
        while (q.next()) {
            const auto nextName=q.value(0).toString(); const auto role=roleFor(q.value(1).toString());
            // >>> MAIN-DEFENSE | setFollow｜处理阶段 04
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!validName(nextName) || !role || (!name.isEmpty() && name != nextName)) { db_.rollback(); return error(QStringLiteral("人物数据无效。")); }
            name=nextName; roles.append(roleName(*role));
        }
        if (q.lastError().isValid()) { db_.rollback(); return error(QStringLiteral("无法读取人物权限。")); }
        // >>> MAIN-DEFENSE | setFollow｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (name.isEmpty()) { db_.rollback(); return denied(); }
        q.finish(); q.prepare("INSERT INTO person_follows(user_id,person_id,known_name,known_roles_json) VALUES(?,?,?,?) ON CONFLICT(user_id,person_id) DO NOTHING");
        q.addBindValue(userId); q.addBindValue(personId); q.addBindValue(name); q.addBindValue(QJsonDocument(roles).toJson(QJsonDocument::Compact));
        if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法保存关注。")); }
    }
    // >>> MAIN-DEFENSE | setFollow｜处理阶段 06
    // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
    // <<< MAIN-DEFENSE
    if (!db_.commit()) { db_.rollback(); return error(QStringLiteral("无法提交关注事务。")); }
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::updatesFor
// 先核验人物读权限，再读来源状态和文章缓存；来源过期与文章过期都必须反映。
// 签名提示：Result<QList<PersonUpdate>> SqliteStore::updatesFor(qint64 userId, const QString& personId) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<QList<PersonUpdate>> SqliteStore::updatesFor(qint64 userId, const QString& personId) {
    auto status = checkReadablePerson(db_, userId, personId); if (!status.ok()) return {status, {}};
    QSqlQuery sources(db_);
    sources.prepare("SELECT source_url,last_success_utc,stale,refresh_generation "
                    "FROM person_sources WHERE person_id=? ORDER BY source_url");
    sources.addBindValue(personId);
    // >>> MAIN-DEFENSE | updatesFor｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!sources.exec()) return {error(QStringLiteral("无法读取人物资讯源。")), {}};
    QMap<QString, bool> sourceStale;
    QMap<QString, bool> sourceSucceeded;
    while (sources.next()) {
        const auto sourceUrl = sources.value(0).toString();
        std::optional<qint64> lastSuccess;
        qint64 stale = 0, generation = 0;
        // >>> MAIN-DEFENSE | updatesFor｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!validUrl(QUrl(sourceUrl)) ||
            !readOptionalTimestamp(sources.value(1), lastSuccess) ||
            !readDatabaseInteger(sources.value(2), stale) || (stale != 0 && stale != 1) ||
            !readDatabaseInteger(sources.value(3), generation) || generation < 0)
            // >>> MAIN-DEFENSE | updatesFor｜处理阶段 03
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return {error(QStringLiteral("人物资讯源状态损坏。")), {}};
        sourceStale.insert(sourceUrl, stale == 1);
        sourceSucceeded.insert(sourceUrl, lastSuccess.has_value());
    }
    // >>> MAIN-DEFENSE | updatesFor｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (sources.lastError().isValid()) return {error(QStringLiteral("读取人物资讯源失败。")), {}};
    if (sourceStale.isEmpty()) return {error(QStringLiteral("人物资讯源尚未配置。")), {}};

    QSqlQuery rows(db_);
    // >>> MAIN-DEFENSE | updatesFor｜处理阶段 05
    // 准备参数化SQL，涉及 person_updates。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    rows.prepare("SELECT update_id,feed_url,source_url,title,kind,published_at_utc,event_at_utc,"
                 "fetched_at_utc,stale FROM person_updates WHERE person_id=? ORDER BY update_id,feed_url");
    rows.addBindValue(personId);
    if (!rows.exec()) return {error(QStringLiteral("无法读取人物资讯缓存。")), {}};
    QList<PersonUpdate> result;
    // >>> MAIN-DEFENSE | updatesFor｜处理阶段 06
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (rows.next()) {
        const auto updateId = rows.value(0).toString();
        const auto feedUrl = rows.value(1).toString();
        const auto sourceUrl = rows.value(2).toString();
        const auto title = rows.value(3).toString();
        const auto kindText = rows.value(4).toString();
        std::optional<qint64> publishedAtUtc, eventAtUtc;
        qint64 fetchedAtUtc = 0, staleValue = 0;
        // >>> MAIN-DEFENSE | updatesFor｜处理阶段 07
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!validPersonId(updateId) || !sourceStale.contains(feedUrl) ||
            !validUrl(QUrl(feedUrl)) || !validUrl(QUrl(sourceUrl)) || !validName(title) ||
            (kindText != "news" && kindText != "release") ||
            !readOptionalTimestamp(rows.value(5), publishedAtUtc) ||
            !readOptionalTimestamp(rows.value(6), eventAtUtc) ||
            !readDatabaseInteger(rows.value(7), fetchedAtUtc) || !validTimestamp(fetchedAtUtc) ||
            !readDatabaseInteger(rows.value(8), staleValue) ||
            (staleValue != 0 && staleValue != 1))
            // >>> MAIN-DEFENSE | updatesFor｜处理阶段 08
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return {error(QStringLiteral("人物资讯缓存数据损坏。")), {}};
        if (!sourceSucceeded.value(feedUrl))
            return {error(QStringLiteral("人物资讯缓存缺少成功刷新时间。")), {}};
        const bool stale = sourceStale.value(feedUrl) || staleValue == 1;
        result.append({updateId, personId, title,
            kindText == "news" ? UpdateKind::News : UpdateKind::Release,
            QUrl(sourceUrl), publishedAtUtc, eventAtUtc,
            fetchedAtUtc, stale});
    }
    // >>> MAIN-DEFENSE | updatesFor｜处理阶段 09
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (rows.lastError().isValid()) return {error(QStringLiteral("读取人物资讯缓存失败。")), {}};
    if (result.isEmpty()) {
        for (auto it = sourceSucceeded.cbegin(); it != sourceSucceeded.cend(); ++it)
            if (!it.value()) return {error(QStringLiteral("人物资讯刷新失败，且没有可用的旧缓存。")), {}};
    }
    // >>> MAIN-DEFENSE | updatesFor｜处理阶段 10
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{}, result};
}

// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::beginUpdatesRefresh
// 为指定人物和来源增加刷新代次并标记过期，返回本轮generation作为后续写入条件。
// 签名提示：Result<qint64> SqliteStore::beginUpdatesRefresh(const QString& personId, const QUrl& feedUrl) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<qint64> SqliteStore::beginUpdatesRefresh(const QString& personId, const QUrl& feedUrl) {
    if (!validPersonId(personId)) return {invalid(QStringLiteral("人物标识无效。")), {}};
    if (!validUrl(feedUrl)) return {invalid(QStringLiteral("人物资讯 feed 地址无效。")), {}};
    if (!db_.isOpen() || db_.driverName() != "QSQLITE")
        // >>> MAIN-DEFENSE | beginUpdatesRefresh｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {error(QStringLiteral("人物数据库不可用。")), {}};
    if (!db_.transaction()) return {error(QStringLiteral("无法开始资讯刷新事务。")), {}};
    QSqlQuery q(db_);
    q.prepare("SELECT refresh_generation FROM person_sources WHERE person_id=? AND source_url=?");
    q.addBindValue(personId);
    q.addBindValue(feedUrl.toString());
    // >>> MAIN-DEFENSE | beginUpdatesRefresh｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec()) { db_.rollback(); return {error(QStringLiteral("无法读取资讯刷新代次。")), {}}; }
    if (!q.next()) { db_.rollback(); return {{ErrorCode::NotFound, QStringLiteral("人物资讯源未配置。")}, {}}; }
    qint64 generation = 0;
    if (!readDatabaseInteger(q.value(0), generation) || generation < 0 ||
        generation == std::numeric_limits<qint64>::max()) {
        db_.rollback(); return {error(QStringLiteral("资讯刷新代次损坏。")), {}};
    }
    const qint64 next = generation + 1;
    q.finish();
    // >>> MAIN-DEFENSE | beginUpdatesRefresh｜处理阶段 03
    // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    q.prepare("UPDATE person_sources SET refresh_generation=?,stale=1 "
              "WHERE person_id=? AND source_url=?");
    q.addBindValue(next);
    q.addBindValue(personId);
    q.addBindValue(feedUrl.toString());
    // >>> MAIN-DEFENSE | beginUpdatesRefresh｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec() || q.numRowsAffected() != 1) {
        db_.rollback(); return {error(QStringLiteral("无法推进资讯刷新代次。")), {}};
    }
    if (!db_.commit()) { db_.rollback(); return {error(QStringLiteral("无法提交资讯刷新代次。")), {}}; }
    // >>> MAIN-DEFENSE | beginUpdatesRefresh｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{}, next};
}

// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::failUpdatesRefresh
// 仅处理本轮generation的失败状态，保留旧缓存供过期展示。
// 签名提示：Status SqliteStore::failUpdatesRefresh(const QString& personId, const QUrl& feedUrl,
//                                          qint64 generation)
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status SqliteStore::failUpdatesRefresh(const QString& personId, const QUrl& feedUrl,
                                       qint64 generation) {
    if (!validPersonId(personId)) return invalid(QStringLiteral("人物标识无效。"));
    if (!validUrl(feedUrl)) return invalid(QStringLiteral("人物资讯 feed 地址无效。"));
    // >>> MAIN-DEFENSE | failUpdatesRefresh｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (generation <= 0) return invalid(QStringLiteral("资讯刷新代次无效。"));
    if (!db_.isOpen() || db_.driverName() != "QSQLITE")
        return error(QStringLiteral("人物数据库不可用。"));
    if (!db_.transaction()) return error(QStringLiteral("无法开始资讯刷新失败事务。"));
    QSqlQuery q(db_);
    // >>> MAIN-DEFENSE | failUpdatesRefresh｜处理阶段 02
    // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    q.prepare("UPDATE person_sources SET stale=1 "
              "WHERE person_id=? AND source_url=? AND refresh_generation=?");
    q.addBindValue(personId);
    q.addBindValue(feedUrl.toString());
    q.addBindValue(generation);
    // >>> MAIN-DEFENSE | failUpdatesRefresh｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法标记资讯刷新失败。")); }
    if (q.numRowsAffected() != 1) {
        db_.rollback(); return invalid(QStringLiteral("资讯刷新代次已过期。"));
    }
    q.finish();
    // >>> MAIN-DEFENSE | failUpdatesRefresh｜处理阶段 04
    // 准备参数化SQL，涉及 person_updates。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    q.prepare("UPDATE person_updates SET stale=1 WHERE person_id=? AND feed_url=?");
    q.addBindValue(personId);
    q.addBindValue(feedUrl.toString());
    if (!q.exec()) { db_.rollback(); return error(QStringLiteral("无法标记旧资讯缓存。")); }
    // >>> MAIN-DEFENSE | failUpdatesRefresh｜处理阶段 05
    // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
    // <<< MAIN-DEFENSE
    if (!db_.commit()) { db_.rollback(); return error(QStringLiteral("无法提交资讯刷新失败状态。")); }
    return {};
}

// >>> MAIN-DEFENSE | 函数入口｜SqliteStore::mergeUpdates
// 校验人物、来源和资讯，事务内合并；提交时检查generation，防旧请求覆盖新刷新结果。
// 签名提示：Status SqliteStore::mergeUpdates(const QString& personId, const QUrl& feedUrl,
//                                    qint64 generation, qint64 completedAtUtc,
//                                    const QList<PersonUpdate>& up
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status SqliteStore::mergeUpdates(const QString& personId, const QUrl& feedUrl,
                                 qint64 generation, qint64 completedAtUtc,
                                 const QList<PersonUpdate>& updates) {
    if (!validPersonId(personId)) return invalid(QStringLiteral("人物标识无效。"));
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!validUrl(feedUrl)) return invalid(QStringLiteral("人物资讯 feed 地址无效。"));
    if (generation <= 0 || !validTimestamp(completedAtUtc))
        return invalid(QStringLiteral("资讯刷新代次或完成时间无效。"));
    QMap<QString, PersonUpdate> unique;
    Status contentStatus;
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (updates.size() > 10000) {
        contentStatus = invalid(QStringLiteral("人物资讯数量超过上限。"));
    } else {
        for (auto update : updates) {
            update.stale = false;
            const auto content = validateUpdate(update, personId, completedAtUtc);
            // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 03
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!content.ok()) { contentStatus = content; break; }
            const auto found = unique.constFind(update.updateId);
            if (found != unique.cend() && !sameUpdate(*found, update)) {
                contentStatus = error(QStringLiteral("相同资讯编号包含冲突内容。"));
                break;
            }
            unique.insert(update.updateId, update);
        }
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!db_.isOpen() || db_.driverName() != "QSQLITE")
        return error(QStringLiteral("人物数据库不可用。"));
    if (!db_.transaction()) return error(QStringLiteral("无法开始资讯缓存事务。"));

    const auto failCurrent = [&] {
        db_.rollback();
        failUpdatesRefresh(personId, feedUrl, generation);
    };
    QSqlQuery source(db_);
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 05
    // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    source.prepare("SELECT refresh_generation,last_success_utc FROM person_sources "
                   "WHERE person_id=? AND source_url=?");
    source.addBindValue(personId);
    source.addBindValue(feedUrl.toString());
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 06
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!source.exec()) {
        db_.rollback();
        return error(QStringLiteral("无法校验资讯刷新代次。"));
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 07
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!source.next()) {
        db_.rollback();
        return {ErrorCode::NotFound, QStringLiteral("人物资讯源未配置。")};
    }
    qint64 current = 0;
    std::optional<qint64> previousSuccess;
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 08
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!readDatabaseInteger(source.value(0), current) ||
        !readOptionalTimestamp(source.value(1), previousSuccess)) {
        db_.rollback();
        return error(QStringLiteral("资讯刷新状态损坏。"));
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 09
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (current != generation) {
        db_.rollback();
        return invalid(QStringLiteral("资讯刷新代次已过期。"));
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 10
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!contentStatus.ok()) {
        failCurrent();
        return contentStatus;
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 11
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (previousSuccess && completedAtUtc < *previousSuccess) {
        failCurrent();
        return invalid(QStringLiteral("资讯刷新完成时间早于现有缓存。"));
    }
    source.finish();

    QSqlQuery remove(db_);
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 12
    // 准备参数化SQL，涉及 person_updates。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    remove.prepare("DELETE FROM person_updates WHERE person_id=? AND feed_url=?");
    remove.addBindValue(personId);
    remove.addBindValue(feedUrl.toString());
    if (!remove.exec()) {
        failCurrent();
        // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 13
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return error(QStringLiteral("无法替换旧资讯缓存。"));
    }

    QSqlQuery insert(db_);
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 14
    // 准备参数化SQL，涉及 person_updates。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    insert.prepare("INSERT INTO person_updates(person_id,update_id,feed_url,source_url,title,kind,"
                   "published_at_utc,event_at_utc,fetched_at_utc,stale) "
                   "VALUES(?,?,?,?,?,?,?,?,?,0)");
    for (const auto& update : unique) {
        insert.addBindValue(personId);
        insert.addBindValue(update.updateId);
        insert.addBindValue(feedUrl.toString());
        insert.addBindValue(update.sourceUrl.toString());
        insert.addBindValue(update.title);
        insert.addBindValue(update.kind == UpdateKind::News ? "news" : "release");
        insert.addBindValue(update.publishedAtUtc ? QVariant(*update.publishedAtUtc) : QVariant());
        insert.addBindValue(update.eventAtUtc ? QVariant(*update.eventAtUtc) : QVariant());
        insert.addBindValue(update.fetchedAtUtc);
        // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 15
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!insert.exec()) {
            failCurrent();
            return error(QStringLiteral("无法写入人物资讯缓存。"));
        }
        insert.finish();
    }

    QSqlQuery commitState(db_);
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 16
    // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    commitState.prepare("UPDATE person_sources SET last_success_utc=?,stale=0 "
                        "WHERE person_id=? AND source_url=? AND refresh_generation=?");
    commitState.addBindValue(completedAtUtc);
    commitState.addBindValue(personId);
    commitState.addBindValue(feedUrl.toString());
    commitState.addBindValue(generation);
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 17
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!commitState.exec() || commitState.numRowsAffected() != 1) {
        failCurrent();
        return error(QStringLiteral("无法提交资讯刷新状态。"));
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 18
    // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
    // <<< MAIN-DEFENSE
    if (!db_.commit()) {
        failCurrent();
        return error(QStringLiteral("无法提交人物资讯缓存。"));
    }
    // >>> MAIN-DEFENSE | mergeUpdates｜处理阶段 19
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}
}
