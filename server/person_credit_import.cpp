// >>> DEFENSE-NOTE | 文件导读｜可信人物与作品关联导入
// 阅读路线：严格解析JSON → 来源/ID/角色验证 → 保存点 → 幂等写入 → 提交或回滚
// 联调关系：人工或可追溯来源提供Person与Credit；写入主逻辑迁移确定的表结构，不按同名猜人物、不创造未登记作品来源。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "person_credit_import.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <limits>

namespace person_credit_import {
using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
constexpr qsizetype MaxJsonBytes = 8 * 1024 * 1024;
constexpr qsizetype MaxEntries = 10000;
Status invalid(const QString& message) { return {ErrorCode::InvalidArgument, message}; }
Status databaseError() { return {ErrorCode::ServerError, QStringLiteral("Person import database operation failed.")}; }
// >>> DEFENSE-NOTE | validText = 检查展示文本
// 限制空文本、长度和控制字符，避免坏别名/姓名进入数据库。
// <<< DEFENSE-NOTE | 本段讲解结束
bool validText(const QString& text) {
    if (text.simplified().isEmpty() || text.size() > MaxQueryUnits) return false;
    for (auto ch : text)
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
// >>> DEFENSE-NOTE | validSource = 检查证据URL
// 要求HTTPS、有效域名且无用户信息；真实批次拒绝.invalid假来源，合成夹具必须明确synthetic。
// <<< DEFENSE-NOTE | 本段讲解结束
bool validSource(const QUrl& url, bool synthetic) {
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.toString().size() > 2048) return false;
    auto host = url.host().toLower();
    if (host.endsWith('.')) host.chop(1);
    return synthetic || (host != "invalid" && !host.endsWith(".invalid"));
}
// >>> DEFENSE-NOTE | roleText = 角色枚举转数据库字符串
// Actor/Director/Musician分别写actor/director/musician；未知枚举返回空并由验证拒绝。
// <<< DEFENSE-NOTE | 本段讲解结束
QString roleText(PersonRole role) {
    switch (role) {
    case PersonRole::Actor: return QStringLiteral("actor");
    case PersonRole::Director: return QStringLiteral("director");
    case PersonRole::Musician: return QStringLiteral("musician");
    }
    return {};
}
// >>> DEFENSE-NOTE | validateBatch = 验证整个导入批次
// 检查来源说明、条数、人物ID/姓名/别名和证据URL，以及每项作品关联的角色和精确资源引用。
// <<< DEFENSE-NOTE | 本段讲解结束
Status validateBatch(const Batch& batch) {
    if (!validText(batch.provenance) || batch.people.size() > MaxEntries || batch.credits.size() > MaxEntries)
        return invalid(QStringLiteral("Import provenance or entry count is invalid."));
    for (const auto& person : batch.people) {
        if (!validatePersonId(person.personId).ok() || !validText(person.name) ||
            !validSource(person.identitySource,batch.synthetic))
            return invalid(QStringLiteral("Person identity, name or evidence URL is invalid."));
        for (const auto& alias : person.aliases)
            if (!validText(alias)) return invalid(QStringLiteral("Person alias is invalid."));
    }
    for (const auto& credit : batch.credits)
        if (!validatePersonId(credit.personId).ok() || !validate(credit.resource).ok() ||
            roleText(credit.role).isEmpty() || !validSource(credit.creditSource,batch.synthetic))
            return invalid(QStringLiteral("Credit person, resource, role or evidence URL is invalid."));
    return {};
}
// >>> DEFENSE-NOTE | decimalId = 无溢出解析正整数ID
// JSON使用十进制字符串保存64位编号；逐位累计前检查溢出，不接受前导零或非数字。
// <<< DEFENSE-NOTE | 本段讲解结束
bool decimalId(const QJsonValue& value, qint64& result) {
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.front() < '1' || text.front() > '9') return false;
    result = 0;
    for (auto ch : text) {
        if (ch < '0' || ch > '9') return false;
        const int digit = ch.unicode() - '0';
        if (result > (std::numeric_limits<qint64>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    return true;
}
// >>> DEFENSE-NOTE | sourceUrl = 解析并验证来源地址
// 只接受字符串且限制长度，再用StrictMode解析URL，复用真实/合成来源规则。
// <<< DEFENSE-NOTE | 本段讲解结束
bool sourceUrl(const QJsonValue& value, QUrl& url, bool synthetic) {
    if (!value.isString() || value.toString().size() > 2048) return false;
    url = QUrl(value.toString(),QUrl::StrictMode);
    return validSource(url,synthetic);
}
// >>> DEFENSE-NOTE | execute = 参数化SQL执行
// 先finish旧结果再prepare，逐一绑定values，避免把外部文本拼进SQL。
// <<< DEFENSE-NOTE | 本段讲解结束
bool execute(QSqlQuery& query, const QString& sql, const QList<QVariant>& values = {}) {
    query.finish();
    if (!query.prepare(sql)) return false;
    for (const auto& value : values) query.addBindValue(value);
    return query.exec();
}
// Releasing this savepoint commits only when no transaction was already active.
// Failure paths preserve the caller's transaction and roll back every import write.
// >>> DEFENSE-NOTE | Savepoint = 导入批次的作用域回滚保护
// 使用独立SAVEPOINT；成功release，否则析构ROLLBACK TO并RELEASE。
// 可嵌套于调用者现有事务，不擅自提交或破坏外层事务；这是数据库保存点，不是C++索引临时对象。
// <<< DEFENSE-NOTE | 本段讲解结束
class Savepoint {
public:
    explicit Savepoint(QSqlDatabase& db) : db_(db), name_("person_import_" + QUuid::createUuid().toString(QUuid::Id128)) {}
    bool start() {
        QSqlQuery query(db_); active_ = query.exec("SAVEPOINT " + name_); return active_;
    }
    bool release() {
        QSqlQuery query(db_);
        if (!query.exec("RELEASE SAVEPOINT " + name_)) return false;
        active_ = false; return true;
    }
    ~Savepoint() {
        if (active_) {
            QSqlQuery query(db_);
            query.exec("ROLLBACK TO SAVEPOINT " + name_);
            query.exec("RELEASE SAVEPOINT " + name_);
        }
    }
private:
    QSqlDatabase& db_;
    QString name_;
    bool active_ = false;
};
}

// >>> DEFENSE-NOTE | parse = 将JSON解码为导入批次
// 限制8MiB及条数，验证结构和字段，保留synthetic/provenance和可核验证据；不写数据库。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<Batch> parse(const QByteArray& bytes) {
    if (bytes.size() > MaxJsonBytes)
        return {invalid(QStringLiteral("Person import JSON exceeds 8 MiB.")), {}};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes,&error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {invalid(QStringLiteral("Person import requires a JSON object.")), {}};
    const auto root = document.object();
    if (root.value("schema") != QJsonValue("care-person-import-v1") ||
        !root.value("synthetic").isBool() || !root.value("provenance").isString() ||
        !root.value("people").isArray() || !root.value("credits").isArray())
        return {invalid(QStringLiteral("Person import schema or field types are invalid.")), {}};
    Batch batch;
    batch.synthetic = root.value("synthetic").toBool(); batch.provenance = root.value("provenance").toString();
    const auto people = root.value("people").toArray(); const auto credits = root.value("credits").toArray();
    if (people.size() > MaxEntries || credits.size() > MaxEntries)
        return {invalid(QStringLiteral("Person import has too many entries.")), {}};
    for (const auto& value : people) {
        if (!value.isObject()) return {invalid(QStringLiteral("Person entry must be an object.")), {}};
        const auto object = value.toObject(); Person person;
        if (!object.value("person_id").isString() || !object.value("name").isString() ||
            !object.value("aliases").isArray() || !sourceUrl(object.value("identity_source"),person.identitySource,batch.synthetic))
            return {invalid(QStringLiteral("Person entry fields or identity evidence are invalid.")), {}};
        person.personId = object.value("person_id").toString(); person.name = object.value("name").toString();
        for (const auto& alias : object.value("aliases").toArray()) {
            if (!alias.isString()) return {invalid(QStringLiteral("Person aliases must be strings.")), {}};
            person.aliases.append(alias.toString());
        }
        batch.people.append(person);
    }
    for (const auto& value : credits) {
        if (!value.isObject()) return {invalid(QStringLiteral("Credit entry must be an object.")), {}};
        const auto object = value.toObject(); Credit credit;
        if (!object.value("person_id").isString() || !object.value("role").isString() ||
            !object.value("resource").isObject() || !sourceUrl(object.value("credit_source"),credit.creditSource,batch.synthetic))
            return {invalid(QStringLiteral("Credit entry fields or evidence are invalid.")), {}};
        credit.personId = object.value("person_id").toString();
        const auto role = object.value("role").toString();
        if (role == "actor") credit.role = PersonRole::Actor;
        else if (role == "director") credit.role = PersonRole::Director;
        else if (role == "musician") credit.role = PersonRole::Musician;
        else return {invalid(QStringLiteral("Credit role is invalid.")), {}};
        const auto resource = object.value("resource").toObject();
        if (!resource.value("work_id").isString() || !resource.value("source").isObject())
            return {invalid(QStringLiteral("Credit resource fields are invalid.")), {}};
        credit.resource.workId = resource.value("work_id").toString();
        const auto source = resource.value("source").toObject();
        if (!decimalId(source.value("warehouse_id"),credit.resource.source.warehouseId) ||
            !decimalId(source.value("item_id"),credit.resource.source.itemId))
            return {invalid(QStringLiteral("Warehouse and item IDs must be positive decimal strings within signed 64-bit range.")), {}};
        batch.credits.append(credit);
    }
    const auto status = validateBatch(batch);
    if (!status.ok()) return {status,{}};
    for (auto& person : batch.people) {
        person.name = person.name.simplified();
        for (auto& alias : person.aliases) alias = alias.simplified();
    }
    return {{},batch};
}

// >>> DEFENSE-NOTE | importBatch = 将已验证批次写入目录与credits
// 先校验数据与显式合成许可，再检查现有目录、登记来源三元组及关联冲突。
// 保存点保护全批次，重复导入不重复记插入；返回人物/别名/关联实际新增数，失败不能留下半份结果。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<Report> importBatch(QSqlDatabase& db, const Batch& batch, const Options& options) {
    if (batch.synthetic && !options.allowSynthetic)
        return {invalid(QStringLiteral("Synthetic person import requires explicit opt-in.")), {}};
    const auto status = validateBatch(batch);
    if (!status.ok()) return {status,{}};
    if (!db.isOpen() || db.driverName() != "QSQLITE") return {databaseError(),{}};
    QSqlQuery query(db);
    if (!query.exec("PRAGMA foreign_keys") || !query.next() || query.value(0).toInt() != 1)
        return {invalid(QStringLiteral("Person import requires SQLite foreign keys enabled.")),{}};
    query.finish();
    if (!query.exec("SELECT MAX(version) FROM server_schema_migrations") || !query.next())
        return {databaseError(),{}};
    bool validVersion = false;
    const auto versionType = query.value(0).metaType().id();
    const auto version = query.value(0).toLongLong(&validVersion);
    query.finish();
    if (!validVersion || (versionType != QMetaType::Int && versionType != QMetaType::LongLong) || version != 3)
        return {invalid(QStringLiteral("Person import requires server schema version 3.")),{}};
    Savepoint savepoint(db);
    if (!savepoint.start()) return {databaseError(),{}};
    Report report;
    for (const auto& person : batch.people) {
        const auto name = person.name.simplified(); const auto evidence = person.identitySource.toString();
        if (!execute(query,"SELECT display_name,identity_source FROM person_catalog WHERE person_id=?",{person.personId}))
            return {databaseError(),{}};
        const bool exists = query.next();
        if (query.lastError().isValid()) return {databaseError(),{}};
        if (exists) {
            if (query.value(0).toString() != name || query.value(1).toString() != evidence)
                return {invalid(QStringLiteral("Existing person ID has conflicting name or identity evidence.")),{}};
        } else if (!execute(query,"INSERT INTO person_catalog(person_id,display_name,identity_source) VALUES(?,?,?)",
                            {person.personId,name,evidence})) return {databaseError(),{}};
        else ++report.peopleInserted;
        query.finish();
        for (const auto& alias : person.aliases) {
            const auto normalized = alias.simplified();
            if (!execute(query,"SELECT 1 FROM person_aliases WHERE person_id=? AND alias=?",{person.personId,normalized}))
                return {databaseError(),{}};
            const bool aliasExists = query.next();
            if (query.lastError().isValid()) return {databaseError(),{}};
            query.finish();
            if (!aliasExists) {
                if (!execute(query,"INSERT INTO person_aliases(person_id,alias) VALUES(?,?)",{person.personId,normalized}))
                    return {databaseError(),{}};
                ++report.aliasesInserted;
            }
        }
    }
    QSet<QString> touchedWorks;
    for (const auto& credit : batch.credits) {
        if (!execute(query,"SELECT 1 FROM work_sources s JOIN warehouse_items i "
                           "ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                           "WHERE s.work_id=? AND s.warehouse_id=? AND s.item_id=? LIMIT 1",
                     {credit.resource.workId,credit.resource.source.warehouseId,credit.resource.source.itemId}))
            return {databaseError(),{}};
        if (!query.next()) return {invalid(QStringLiteral("Credit requires an existing exact work, warehouse and item source.")),{}};
        if (!execute(query,"SELECT 1 FROM person_catalog WHERE person_id=?",{credit.personId})) return {databaseError(),{}};
        if (!query.next()) return {invalid(QStringLiteral("Credit person does not exist.")),{}};
        const auto role = roleText(credit.role); const auto evidence = credit.creditSource.toString();
        if (!execute(query,"SELECT credit_source FROM person_credits WHERE person_id=? AND work_id=? AND role=?",
                     {credit.personId,credit.resource.workId,role})) return {databaseError(),{}};
        const bool exists = query.next();
        if (query.lastError().isValid()) return {databaseError(),{}};
        if (exists) {
            if (query.value(0).toString() != evidence)
                return {invalid(QStringLiteral("Existing credit has conflicting evidence.")),{}};
        } else if (!execute(query,"INSERT INTO person_credits(person_id,work_id,role,credit_source) VALUES(?,?,?,?)",
                            {credit.personId,credit.resource.workId,role,evidence})) return {databaseError(),{}};
        else ++report.creditsInserted;
        query.finish(); touchedWorks.insert(credit.resource.workId);
    }
    for (const auto& work : touchedWorks) {
        if (!execute(query,"SELECT COUNT(DISTINCT person_id) FROM person_credits WHERE work_id=?",{work}) || !query.next())
            return {databaseError(),{}};
        if (query.value(0).toLongLong() > 256)
            return {invalid(QStringLiteral("A work cannot contain more than 256 distinct credited people.")),{}};
        query.finish();
    }
    if (!savepoint.release()) return {databaseError(),{}};
    return {{},report};
}
}
